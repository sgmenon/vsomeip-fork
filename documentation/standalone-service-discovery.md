# Using vsomeip's SD without vsomeip — what's actually feasible

**TL;DR.** The `vsomeip3::sd::service_discovery` interface looks reusable but the
implementation is not. The shipped `libvsomeip3-sd.so` is a vsomeip plugin, not a
standalone library, and `service_discovery_impl` is bound to internal vsomeip types
that you cannot avoid by swapping out the host adapter. If you want SD without the
rest of vsomeip you have three realistic options, listed cheapest to most expensive:

1. **Keep using vsomeip + `protocol=external`** (what you are doing today).
2. **Re-implement SOME/IP-SD on top of the vsomeip wire-format classes only**
   (`message_impl`, `entry_impl`, `option_impl`, `serializer`, `deserializer`).
3. **Hard-fork the `service_discovery_impl` state machine** into your own tree.

The rest of this document explains _why_ option 0 ("just instantiate
`service_discovery_impl` against my own host") is not viable, and what shape each
of the three options takes.

---

## 1. The interface, and what it hides

The public-looking surface is
[implementation/service_discovery/include/service_discovery.hpp](implementation/service_discovery/include/service_discovery.hpp):

```cpp
class service_discovery {
public:
    virtual void init() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void request_service(...);
    virtual void release_service(...);
    virtual void subscribe(...);
    virtual void unsubscribe(...);
    virtual bool send(bool _is_announcing) = 0;
    virtual void on_message(const byte_t* _data, length_t _length,
                            const boost::asio::ip::address& _sender,
                            bool _is_multicast) = 0;
    virtual void offer_service(const std::shared_ptr<serviceinfo>& _info) = 0;
    virtual bool stop_offer_service(...);
    ...
};
```

That looks promising: you feed it inbound multicast bytes via `on_message`, you
call `offer_service` / `request_service`, and you get to drive `send`. But the
constructor of the only implementation,
[implementation/service_discovery/include/service_discovery_impl.hpp](implementation/service_discovery/include/service_discovery_impl.hpp#L57):

```cpp
service_discovery_impl(service_discovery_host* _host,
                      const std::shared_ptr<configuration>& _configuration);
```

requires you to provide two interfaces that are **not** vsomeip-free:

| Interface | Pure virtuals | Where |
|---|---|---|
| `vsomeip_v3::configuration` | 121 | [implementation/configuration/include/configuration.hpp](implementation/configuration/include/configuration.hpp) |
| `vsomeip_v3::sd::service_discovery_host` | 16 | [implementation/service_discovery/include/service_discovery_host.hpp](implementation/service_discovery/include/service_discovery_host.hpp) |

Of those, only a small fraction are actually reached from the SD code path. But
the ones that _are_ reached force you to drag the rest of vsomeip in anyway. See
sections 2 and 3.

---

## 2. Why `service_discovery_impl` is not host-pluggable

Three things in [implementation/service_discovery/src/service_discovery_impl.cpp](implementation/service_discovery/src/service_discovery_impl.cpp)
make a "just implement the host" plan fail:

### 2.1 The endpoint downcast

`start()` calls back into the host to create the multicast endpoint, then
**downcasts the result to an internal vsomeip type**:

```cpp
// service_discovery_impl.cpp:146
endpoint_ = host_->create_service_discovery_endpoint(sd_multicast_, port_, reliable_);
...
// service_discovery_impl.cpp:173 (and 3057)
auto its_server_endpoint = std::dynamic_pointer_cast<udp_server_endpoint_impl>(endpoint_);
if (its_server_endpoint && !its_server_endpoint->is_joined(sd_multicast_)) {
    its_server_endpoint->join(sd_multicast_);
}
```

`udp_server_endpoint_impl` is an internal class in
[implementation/endpoints/include/udp_server_endpoint_impl.hpp](implementation/endpoints/include/udp_server_endpoint_impl.hpp).
You cannot satisfy this call from a thin Boost ASIO multicast wrapper — the
`dynamic_pointer_cast` will return null, the join/leave on multicast group changes
will silently no-op, and reliability of the receive path degrades (no
re-join-on-silence behaviour from `on_last_msg_received_timer_expired`).

You can implement `endpoint` (~30 pure virtuals, see
[implementation/endpoints/include/endpoint.hpp](implementation/endpoints/include/endpoint.hpp))
yourself, but to satisfy the cast you must also inherit from
`udp_server_endpoint_impl`, which transitively pulls in `udp_server_endpoint_base_impl`,
`server_endpoint_impl`, the routing-host typedefs, and so on. At that point you
have re-implemented half of vsomeip's endpoint layer.

### 2.2 The plugin manager call in `init()`

```cpp
// service_discovery_impl.cpp:78
void service_discovery_impl::init() {
    const char *its_sd_module = getenv(VSOMEIP_ENV_SD_MODULE);
    std::string plugin_name = its_sd_module != nullptr ? its_sd_module : VSOMEIP_SD_LIBRARY;

    runtime_ = std::dynamic_pointer_cast<sd::runtime>(
        plugin_manager::get()->get_plugin(plugin_type_e::SD_RUNTIME_PLUGIN, plugin_name));
    ...
}
```

This walks the vsomeip plugin manager and `dlopen`s `libvsomeip3-sd.so` to obtain
an `sd::runtime` factory used to allocate SD `message_impl` instances. So even
with your own host, `service_discovery_impl::init()` requires
`libvsomeip3-sd.so` on the filesystem and `libvsomeip3.so` already loaded in
the process. You cannot statically link against `service_discovery_impl` and
have an empty `dlopen` table — `runtime_` will be null and message construction
fails.

### 2.3 The host's callback surface

[implementation/service_discovery/include/service_discovery_host.hpp](implementation/service_discovery/include/service_discovery_host.hpp)
declares 16 pure virtuals. The ones the SD impl actually calls (verified by
grepping `host_->` in
[implementation/service_discovery/src/service_discovery_impl.cpp](implementation/service_discovery/src/service_discovery_impl.cpp))
are:

- `get_io()` — easy, hand it a `boost::asio::io_context&`
- `create_service_discovery_endpoint(addr, port, reliable)` — see 2.1
- `get_offered_services()` / `get_offered_service()` / `get_offered_service_instances()`
- `find_eventgroup(service, instance, eventgroup)`
- `find_or_create_remote_client(service, instance, reliable)` — must return
  a vsomeip `endpoint` for the matched remote
- `add_routing_info(...)` / `del_routing_info(...)` / `update_routing_info(...)`
- `on_remote_subscribe(...)` / `on_remote_unsubscribe(...)`
- `on_subscribe_ack(...)` / `on_subscribe_ack_with_multicast(...)` / `on_subscribe_nack(...)`
- `expire_subscriptions(...)` (overloaded) / `expire_services(...)` (overloaded)
- `get_subscribed_eventgroups(service, instance)`
- `send(client, message, force)` / `send_via_sd(target, data, size, port)`

The signatures use `serviceinfo`, `eventgroupinfo`, `endpoint`,
`endpoint_definition`, `remote_subscription`, `remote_subscription_callback_t`,
`message`, `vsomeip_sec_client_t`, and friends — every one of which lives inside
`libvsomeip3.so`. You cannot stub these without copying their headers and
implementations.

### 2.4 The 20-odd `configuration_->get_sd_*()` calls

[implementation/service_discovery/src/service_discovery_impl.cpp](implementation/service_discovery/src/service_discovery_impl.cpp#L80-L138)
reads from `configuration` for: unicast address, SD multicast address, SD port,
SD protocol, SD TTL, initial-delay min/max, repetitions base delay, repetitions
max, cyclic offer delay, find initial-debounce reps & time, offer debounce time,
find debounce time, TTL factor for offers, TTL factor for subscribes, stop-offer
watchdog time, buffer shrink threshold, netmask, reliability-type lookup.

That subset _is_ small enough to stub with a hand-rolled `configuration` derived
class that returns hard-coded values. **But** because `configuration` has 121
pure virtuals, you have to provide 100+ stubs that throw or return defaults — and
some of them are reached from message paths you don't expect (`get_reliability_type`,
the security clients, the routing read-set). Doable, but tedious and brittle
across vsomeip upgrades.

### 2.5 Logging and other globals

The SD impl uses `VSOMEIP_INFO`/`VSOMEIP_ERROR`/`VSOMEIP_WARNING` macros which
expand into calls on the vsomeip logger singleton. The logger is initialised by
`vsomeip_v3::logger::logger_impl` which itself reads from `configuration`. If
you do nothing, the first log call from the SD impl will crash. You either need
to call `vsomeip_v3::logger::logger_impl::init(configuration)` (which means you
need most of the runtime anyway) or patch the macros out of every SD source file.

---

## 3. The three realistic options

### Option A — Keep vsomeip + `protocol=external`

This is the cow-comes-with-the-farm path you described. The runtime cost is
that you have an `application_impl` instance, a routing manager, the plugin
manager, the logger thread, and `libvsomeip3-cfg.so` parsing your JSON config —
all to ferry SD entries you care about.

Pros: zero new code, AS_OFFERED/AS_AVAILABLE plumb through the existing
availability handler API, eventgroup subscription book-keeping is done for you.

Cons: process footprint, JSON config surface, you are stuck with vsomeip's
configuration knobs and threading.

If you choose this, the only thing you may want is the AS_OFFERED-on-first-offer
patch that is already in
[implementation/routing/src/routing_manager_impl.cpp](implementation/routing/src/routing_manager_impl.cpp)
in this fork (see the earlier session).

### Option B — Wire-format reuse only (recommended for medium effort)

The SOME/IP-SD message classes _are_ relatively self-contained:

- [implementation/service_discovery/include/message_impl.hpp](implementation/service_discovery/include/message_impl.hpp)
  / [.cpp](implementation/service_discovery/src/message_impl.cpp)
- [implementation/service_discovery/include/entry_impl.hpp](implementation/service_discovery/include/entry_impl.hpp)
  /  `serviceentry_impl`, `eventgroupentry_impl` `.cpp`s
- [implementation/service_discovery/include/option_impl.hpp](implementation/service_discovery/include/option_impl.hpp)
  and the `ipv4_option_impl`, `ipv6_option_impl`, `configuration_option_impl`,
  `load_balancing_option_impl`, `protection_option_impl`, `selective_option_impl`,
  `unknown_option_impl` files
- [implementation/service_discovery/include/deserializer.hpp](implementation/service_discovery/include/deserializer.hpp)
  / [.cpp](implementation/service_discovery/src/deserializer.cpp)
- [implementation/message/include/serializer.hpp](implementation/message/include/serializer.hpp)

Their hard dependencies are:

- `vsomeip/primitive_types.hpp` — fine, it is in the public include tree
- `endpoint_definition` — small data class
- `message_base_impl` — small base, but it transitively touches `vsomeip_sec_client_t`,
  which you can stub as an empty struct
- The `serviceinfo` / `eventgroupinfo` types — but you only need them if you
  go up the call chain into `service_discovery_impl`. The message classes
  themselves don't.

**What you build yourself in this option:**

1. A Boost ASIO UDP socket joined to the SD multicast address with the standard
   options:

   ```cpp
   namespace ba = boost::asio;
   using udp = ba::ip::udp;

   ba::io_context io;
   udp::socket sock(io);
   sock.open(udp::v4());
   sock.set_option(ba::socket_base::reuse_address(true));
   sock.bind(udp::endpoint(ba::ip::address_v4::any(), 30490));
   sock.set_option(ba::ip::multicast::join_group(
       ba::ip::address_v4::from_string("224.224.224.245"),
       ba::ip::address_v4::from_string(local_iface_ip)));
   sock.set_option(ba::ip::multicast::enable_loopback(false));
   sock.set_option(ba::ip::multicast::hops(/*ttl*/ 16));
   ```

2. The SD state machine — initial wait → repetition phase (with exponential
   delays) → main phase (cyclic offer delay) → TTL expiry. Read
   [implementation/service_discovery/src/service_discovery_impl.cpp](implementation/service_discovery/src/service_discovery_impl.cpp)
   and pick out the `start_main_phase_timer`, `start_offer_debounce_timer`,
   `start_find_debounce_timer`, `start_ttl_timer`,
   `start_last_msg_received_timer` flows. This is ~600 lines of
   `boost::asio::steady_timer` orchestration, no exotic state.

3. The entries you actually need to emit and parse:
   - `FindService` (entry type 0x00)
   - `OfferService` / `StopOfferService` (entry type 0x01, TTL=0 for stop)
   - `SubscribeEventgroup` / `StopSubscribeEventgroup` (entry type 0x06)
   - `SubscribeEventgroupAck` / `Nack` (entry type 0x07)

4. Options that matter on the wire: IPv4 endpoint option (0x04), IPv6 endpoint
   option (0x06), IPv4 multicast option (0x14), IPv6 multicast option (0x16),
   configuration option (0x01). You can ignore load-balancing (0x02) and
   protection (0x03) at first — they are rarely emitted in practice.

5. Re-use the existing `serializer.cpp` / `deserializer.cpp` and the
   `*_option_impl.cpp` files. They only need:
   - `vsomeip_v3` primitive types
   - `endpoint_definition` (copyable struct of address+port+reliable)
   - `bithelper.hpp` from
     [implementation/utility/include/bithelper.hpp](implementation/utility/include/bithelper.hpp)
   - A stripped `logger` shim that turns `VSOMEIP_INFO << ...` into either a
     no-op or a `std::cerr <<` — easiest is a header
     [implementation/logger/include/logger.hpp](implementation/logger/include/logger.hpp)
     replacement.

6. **Throw away** `service_discovery_impl` itself, `service_discovery_host`,
   `runtime_impl`, and the configuration interface. Replace with your own state
   machine + a plain struct for SD parameters.

Rough budget: ~1,500–2,000 lines of your own glue + the copied
message/option/serializer translation units.

### Option C — Hard-fork `service_discovery_impl`

If you want the full SD state machine without re-writing the timer logic, copy
the entire `implementation/service_discovery/` directory into your tree and
mechanically replace its dependencies:

| Original dep | Replace with |
|---|---|
| `vsomeip_v3::configuration*` | `struct sd_params {...}` flat config |
| `service_discovery_host*` | Your own callback interface, kept to the ~10 host methods actually invoked |
| `udp_server_endpoint_impl` cast | A thin `multicast_socket` class that owns the Boost ASIO socket directly; replace `dynamic_pointer_cast` with direct call |
| `plugin_manager` lookup in `init()` | Direct construction: `runtime_ = std::make_shared<runtime_impl>();` |
| `endpoint`, `endpoint_definition` | Keep `endpoint_definition` (it is just data); replace `endpoint` with a Boost ASIO sender abstraction |
| `serviceinfo`, `eventgroupinfo` | Keep these — they are mostly POD with a mutex |
| `remote_subscription`, `subscription` | Keep these (already SD-local) |
| `VSOMEIP_INFO`/etc. macros | Redirect to your logger of choice |
| `vsomeip_sec_client_t` | Empty struct |

Doable, but you now own a fork and every upstream change has to be cherry-picked.
The patches in this GM fork (e.g. AS_OFFERED-on-first-offer, the `gm_option_size_increase`
glue) show that the SD layer does get touched.

---

## 4. What "the multicast socket" actually has to do

Independent of which option you pick, the socket setup vsomeip does is
straightforward and you can absolutely reproduce it in plain Boost ASIO. From
[implementation/endpoints/src/udp_server_endpoint_impl.cpp](implementation/endpoints/src/udp_server_endpoint_impl.cpp)
the relevant operations on the SD socket are:

1. Open a UDP v4 (or v6) socket.
2. Set `SO_REUSEADDR` (and `SO_REUSEPORT` on Linux/QNX) so multiple processes on
   the same host can co-bind to the SD port.
3. Bind to `0.0.0.0:30490` (or whatever your configured SD port is).
4. Set the outbound multicast interface explicitly to your local NIC's address
   (`IP_MULTICAST_IF`).
5. Set TTL/hops (default 16) and disable loopback if you don't want to receive
   your own announcements.
6. `IP_ADD_MEMBERSHIP` for `224.224.224.245` (the AUTOSAR default) on the local
   interface.
7. Optionally `setsockopt(SO_PRIORITY)` / DSCP if the deployment cares.
8. On rebind / network event, leave and re-join the group — this is what
   `on_last_msg_received_timer_expired` does after a silence period.

That's the entire socket layer. The "magic" vsomeip does on top of that is:
SOME/IP header detection (service id `0xFFFF`, method id `0x8100`, message type
`0x02`), session-id and reboot-flag tracking per remote peer, and re-join on
silence — all of which are 50–150 lines of code each.

---

## 5. Recommendation

Given the goal is "use vsomeip's SD with Boost ASIO and nothing else":

- If you can live with the cow-and-farm cost: **Option A**. No code to write.
- If you want SD as a library inside your own process but you don't need full
  fidelity (e.g. you don't run eventgroup subscriptions through it, you only
  want OfferService/FindService): **Option B** is the clean answer. The wire
  classes from vsomeip's SD source tree are worth keeping; the state machine is
  worth re-writing because yours will be simpler.
- If you want the production-tested SD state machine but with your own
  transport: **Option C**, accept the fork cost.

The thing the public `service_discovery.hpp` interface _suggests_ — pass us a
host and a config, drive us with raw bytes — is what the implementation does
**not** actually support, and the gap is mostly hidden under one
`dynamic_pointer_cast` and one `plugin_manager` lookup. Worth knowing before
you sink time into a shim.
