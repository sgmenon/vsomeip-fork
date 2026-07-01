# Using vsomeip's SD without the rest of vsomeip

**TL;DR.** Today the GM active SD plugin runs inside the UDS Broker via
vsomeip's `protocol=external` path — i.e. we spin up a full `application_impl`
just to reach the SD stack. That works but pulls in `routing_manager_impl`,
the netlink connector, the local Unix-socket endpoints, and the whole routing
infrastructure that the broker does not need. We're moving to a thin
**SD-host library** (`sd_host_lib`) that re-uses, **unchanged**:

- A new, smaller **`libvsomeip3-core.so`** — carved out of `libvsomeip3.so`, containing exactly what the broker needs (`plugin_manager`, logger, serializer, message classes, endpoint base classes, `udp_server_endpoint_impl`, and `configuration_impl` folded in the same way upstream does when `ENABLE_MULTIPLE_ROUTING_MANAGERS` is set — see `CMakeLists.txt` line 320). Linked via `cc_import` so the singletons stay singleton across `sd_host_lib` and `libvsomeip3-sd.so`. See §3.4 for the exact source carve-out.
- `libvsomeip3-sd.so` for the SD wire state machine. **Linked into `gm_sd_active.so` via `dynamic_deps` (`DT_NEEDED`) — not dlopened from disk at runtime.** `service_discovery_impl::init()` still calls `plugin_manager::get()->get_plugin(SD_RUNTIME_PLUGIN, ...)`, but the `dlopen` it ultimately does finds the library already loaded by `ld.so` at process start, bumps the refcount, and returns a handle to it. Same singleton coherence as before, no second mapping, no runtime filesystem dependency. This mirrors what we already do for cfg via `MULTIPLE_ROUTING_MANAGERS`.
- `vsomeip_v3::udp_server_endpoint_impl` for the SD socket — `service_discovery_impl` `dynamic_pointer_cast`s to this class (see §2.1) so we have to use the real one. It lives inside `libvsomeip3-core.so`.

…and stops short of `application_impl` / `routing_manager_impl`. The broker's
existing `BrokerSDHost` is passed in as the `service_discovery_host`
unchanged. Netlink lives in `routing_manager_impl::init()`; we never
instantiate it, so the netlink code path is dormant in the broker process —
no fork, no patch.

The rest of this document explains _why_ the naïve approach ("just
instantiate `service_discovery_impl` against my own host with my own
everything") does not work, what `sd_host_lib`'s shape is, and what we
need to verify before prototyping.

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

| Interface                                | Pure virtuals | Where                                                                                                                                      |
| ---------------------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `vsomeip_v3::configuration`              | 121           | [implementation/configuration/include/configuration.hpp](implementation/configuration/include/configuration.hpp)                           |
| `vsomeip_v3::sd::service_discovery_host` | 16            | [implementation/service_discovery/include/service_discovery_host.hpp](implementation/service_discovery/include/service_discovery_host.hpp) |

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

## 3. The slim host library on top of vsomeip-sd + vsomeip-core

The active plugin keeps its current shape — `BrokerSDHost` implements
`service_discovery_host`, the broker drives `clear_learning_table()` from the
RID 0x0654 handler — but its construction path changes. Instead of going
through `application_impl`/`routing_manager_impl` to reach the SD impl, the
broker constructs a small `SDHostRuntime` from the new library, hands it
`BrokerSDHost`, and lets it run.

### What we reuse, unchanged

| Component                                               | Source                                                                        | Linkage                                                                                                                                 | Why we keep it                                                                                                                                                                                                                                                   |
| ------------------------------------------------------- | ----------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `vsomeip_v3::sd::service_discovery_impl`                | `libvsomeip3-sd.so`                                                           | Shared-library dep (`DT_NEEDED`) of `gm_sd_active.so`; `plugin_manager`'s dlopen call later returns a handle to the already-loaded copy | Wire state machine in lockstep with upstream; this is the cow we actually want                                                                                                                                                                                   |
| `vsomeip_v3::cfg::configuration_impl`                   | `libvsomeip3-core.so` (folded in à la upstream `MULTIPLE_ROUTING_MANAGERS=1`) | Already inside core                                                                                                                     | JSON schema stays compatible with upstream vsomeip; one less thing to maintain. No separate `-cfg.so` needed because we never want to dlopen-swap it.                                                                                                            |
| `vsomeip_v3::udp_server_endpoint_impl`                  | `libvsomeip3-core.so`                                                         | Already inside core                                                                                                                     | `service_discovery_impl::start()` `dynamic_pointer_cast`s to this concrete type (§2.1). We have to use the real class — but doing so is _also_ what gives us the upstream multicast-join, reuse-port, IP_MULTICAST_IF, and rejoin-on-silence behaviour for free. |
| `plugin_manager`, `logger`, serializer, message classes | `libvsomeip3-core.so`                                                         | `cc_import` into `sd_host_lib`; shared dep of `libvsomeip3-sd.so`                                                                       | Singletons in process scope; need ONE process-wide copy. Static linking would duplicate them.                                                                                                                                                                    |

### What `sd_host_lib` adds

1. **`vsomeip_v3::endpoint_host` stub** — returns sensible defaults
   (client id 0, no host name), never relays to a routing manager. The
   broker is not a vsomeip application.
2. **`vsomeip_v3::routing_host` stub** — no-ops every "on*message_received"
   call from the endpoint that is _not_ an SD PDU. SD PDUs are recognised
   by service id `0xFFFF` / method id `0x8100` and forwarded to
   `service_discovery_impl::on_message`. Everything else is logged and
   dropped (the broker has no business routing application traffic).
3. **`SDHostRuntime` factory** that:
   - Loads JSON via `vsomeip_v3::cfg::configuration_impl(path)`.
   - Owns a `boost::asio::io_context` and one thread running it.
   - Constructs `udp_server_endpoint_impl` with the two stubs + the
     configuration + io_context, bound to the SD multicast address and
     port from config.
   - Looks up the SD runtime via
     `plugin_manager::get()->get_plugin(SD_RUNTIME_PLUGIN, VSOMEIP_SD_LIBRARY)`
     — exactly what `service_discovery_impl::init()` does today.
   - Constructs `service_discovery_impl(user_host, configuration)` and
     wires its `on_message` to the endpoint's receive path.
   - Exposes `start()` / `stop()` that just forward to the SD impl.
4. **Lifecycle ownership** — the broker calls
   `SDHostRuntime::set_host(my_broker_sd_host)`, then `start()`. On
   `clear_learning_table()` it can `stop()` + `start()` to re-arm.

### What we explicitly do not instantiate

- `application_impl` — no fake client id registration, no protocol negotiation, no Unix-socket IPC.
- `routing_manager_impl` — and therefore `netlink_connector`. The netlink
  code is excluded from `libvsomeip3-core.so` by the §3.4 carve-out, and
  even in the fallback ("ship the full `libvsomeip3.so`") it would be
  dead at runtime: it is constructed only inside
  `routing_manager_impl::init()` (lines 220–225 of
  `implementation/routing/src/routing_manager_impl.cpp`), which the broker
  never calls. See §3.3.
- `local_uds_server_endpoint` and the rest of the IPC plumbing.
- `e2e_provider`.
- The security manager (lightweight stub may be needed depending on
  whether `udp_server_endpoint_impl` calls into it on receive; see §5).

### 3.1 Dependency graph

```
//cruise/someip/gm_sd:gm_sd_active_plugin_lib  (cc_library, existing)
  └── deps:
       └── //cruise/someip/gm_sd:sd_host_lib              (cc_library, NEW)
             ├── @vsomeip//:vsomeip-core-import           cc_import → libvsomeip3-core.so
             │                                            + hdrs (public `interface/` +
             │                                              the internal headers below):
             │      service_discovery/include/service_discovery_impl.hpp
             │      service_discovery/include/service_discovery_host.hpp
             │      endpoints/include/udp_server_endpoint_impl.hpp
             │      endpoints/include/endpoint_host.hpp
             │      routing/include/routing_host.hpp
             │      plugin/include/plugin_manager.hpp
             │      configuration/include/configuration.hpp
             └── @boost.asio

//cruise/someip/gm_sd:gm_sd_active                        (cc_shared_library, existing)
  └── dynamic_deps = [
        "@vsomeip//:vsomeip3-core",   # libvsomeip3-core.so  — same .so sd_host_lib's cc_import points at
        "@vsomeip//:vsomeip3-sd",     # libvsomeip3-sd.so    — DT_NEEDED, loaded at process start
      ]
  └── data = []                       # NOTHING dlopened from disk at runtime
```

New vsomeip targets we need to add (in an overlay BUILD if not upstreamed):

```
@vsomeip//:vsomeip-core           cc_library, srcs = §3.4 carve-out
@vsomeip//:vsomeip3-core          cc_shared_library wrapping :vsomeip-core   → libvsomeip3-core.so
@vsomeip//:vsomeip-core-import    cc_import for libvsomeip3-core.so, with
                                  hdrs = (public interface headers + the
                                  internal SD/endpoint/plugin/cfg headers
                                  the broker needs to name types) and
                                  includes = ["interface", "implementation/..."].
                                  One target ships both the .so to link
                                  against and the headers to compile against,
                                  so downstream depends on a single label.
@vsomeip//:vsomeip3-sd            cc_shared_library wrapping :vsomeip-sd,
                                  with dynamic_deps = [":vsomeip3-core"]
```

Three non-obvious choices encoded here:

- **`cc_import` (not `cc_library`) on `vsomeip-core`.** We want exactly one
  process-wide copy of `plugin_manager::get()`, the logger state, and any
  other statics inside `libvsomeip3-core.so`. Static linking would duplicate
  them — `libvsomeip3-sd.so` links against `libvsomeip3-core.so`
  dynamically, so it would see a _different_ `plugin_manager` instance than
  the broker if we statically linked the core sources into `sd_host_lib`.
  `cc_import` against the same shared library `libvsomeip3-sd.so` links
  against keeps the singleton coherent.

- **Internal headers ride along on the same `cc_import` target.** We don't
  link the .cpp sources for `service_discovery_impl` or
  `udp_server_endpoint_impl` — those live in `libvsomeip3-sd.so` /
  `libvsomeip3-core.so`. We do need the internal headers
  (`implementation/*/include/*.hpp`) so the broker code can name the types
  and call the constructors, and the natural home for them is the
  `vsomeip-core-import` target's `hdrs` attribute (with `includes` set to
  expose the right `-I` paths). Consumers depend on one label and get both
  the link-time `.so` and the compile-time headers.

- **`libvsomeip3-sd.so` is `DT_NEEDED`, not a `dlopen`-at-runtime data dep.**
  Even though `service_discovery_impl::init()` still calls
  `plugin_manager::get()->get_plugin(SD_RUNTIME_PLUGIN, "vsomeip-sd")` which
  ends in a `dlopen("libvsomeip3-sd.so", RTLD_LAZY|RTLD_LOCAL)`
  (`plugin_manager_impl.cpp` line 63 + `load_library`), that dlopen finds
  the library already in the process image — because `gm_sd_active.so`
  declared it as a `dynamic_deps` / `DT_NEEDED` dep at link time — and just
  bumps the refcount. No filesystem search, no second load. We keep the
  benefit of plugin_manager's bookkeeping without the runtime surprise of
  disk-driven discovery. This mirrors what the user's existing build
  already does for cfg via `ENABLE_MULTIPLE_ROUTING_MANAGERS`: upstream
  folds `configuration_impl.cpp` directly into `libvsomeip3.so` in that
  mode (see `CMakeLists.txt` line 320), so there is no `-cfg.so` to dlopen.

### 3.2 Why the §2 blockers stop being blockers

| §2 blocker                                            | How `sd_host_lib` handles it                                                                                                                                                                                                                                                                                                                                            |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| §2.1 `dynamic_pointer_cast<udp_server_endpoint_impl>` | We _are_ using the real `udp_server_endpoint_impl` from `libvsomeip3-core.so`. Cast succeeds. The `join` / `is_joined` calls and the `on_last_msg_received_timer_expired` re-join behaviour work as upstream.                                                                                                                                                           |
| §2.2 `plugin_manager` lookup in `init()`              | `plugin_manager::get()` lives in `libvsomeip3-core.so` (cc_import, shared singleton). The `dlopen("libvsomeip3-sd.so")` inside `load_plugin` finds the library already mapped — because `gm_sd_active.so` declared it as a `dynamic_deps` / `DT_NEEDED` dep at link time — so it just bumps the refcount and returns a handle. No filesystem lookup; no second mapping. |
| §2.3 16-method host surface                           | `BrokerSDHost` already implements this in the GM tree. `sd_host_lib` does not change that contract.                                                                                                                                                                                                                                                                     |
| §2.4 121-method `configuration` surface               | We use `cfg::configuration_impl`, folded into `libvsomeip3-core.so` à la upstream's `MULTIPLE_ROUTING_MANAGERS=1`. No hand-rolled config, no stubs to maintain, no separate `-cfg.so` to ship.                                                                                                                                                                          |
| §2.5 Logger globals                                   | `logger` lives in `libvsomeip3-core.so` and reads from `configuration`. We initialise it the same way upstream does: hand it the `configuration_impl` instance we already own.                                                                                                                                                                                          |

### 3.3 Why netlink stops being a concern

`netlink_connector` is constructed exactly once — in
`routing_manager_impl::init()`
(`implementation/routing/src/routing_manager_impl.cpp` lines 220–225).
`sd_host_lib` never instantiates `routing_manager_impl`, so
`netlink_connector` is never constructed, no `AF_NETLINK` socket is
opened, and none of the `RTMGRP_*` subscriptions happen. With the
§3.4 carve-out the source is also not linked into `libvsomeip3-core.so`
at all (belt-and-braces); even without that, it would be dead code at
runtime for the broker process. We don't have to fork or patch anything.

(Aside: even when netlink _is_ exercised by the routing manager, the
multicast-route portion of its gate is functionally redundant on
single-VLAN IGMP-snooped automotive networks — the upstream
`sd_wait_route_netlink_notification` config knob exists precisely to opt
out of it. Here the question is moot because the whole connector is
gone.)

### 3.4 What goes into `libvsomeip3-core.so`

Defined as `libvsomeip3.so` minus the things the broker provably doesn't
need. Starting hypothesis:

**Include** (current `:vsomeip-lib` srcs glob minus the excludes below):

- `implementation/configuration/src/configuration_impl.cpp` and companions —
  folded in the same way `ENABLE_MULTIPLE_ROUTING_MANAGERS=1` does (see
  `CMakeLists.txt` lines 320–322).
- `implementation/endpoints/src/*` **except** the ones below.
- `implementation/logger/src/*`
- `implementation/message/src/*`
- `implementation/plugin/src/*`
- `implementation/protocol/src/*`
- `implementation/security/src/*` — SD code paths touch `vsomeip_sec_client_t`;
  safer to keep until verified.
- `implementation/tracing/src/*` — small, no transitive cost.
- `implementation/utility/src/*`
- `implementation/runtime/src/runtime_impl.cpp` only — the factory the
  message classes need. `application_impl.cpp` is excluded.

**Exclude:**

- `implementation/routing/src/*` — `routing_manager_impl`, `routing_manager_proxy`,
  `routing_manager_stub`, `routing_host`. All unused; their removal also
  guarantees `netlink_connector` is never constructed (see §3.3).
- `implementation/endpoints/src/netlink_connector.cpp` — belt-and-braces
  exclusion so we don't even ship the AF_NETLINK code in `libvsomeip3-core.so`.
- `implementation/endpoints/src/local_*_endpoint.cpp` — UDS IPC plumbing
  used only by `application_impl` ↔ `routing_manager_impl`.
- `implementation/endpoints/src/credentials.cpp` — peer-credential check for UDS.
- `implementation/runtime/src/application_impl.cpp` — the entry point we are
  explicitly avoiding.
- `implementation/e2e_protection/src/*` — out of scope for the broker.

**Open question.** Several `endpoint_*.cpp` files cross-reference each
other and may pull in routing internals. We will likely hit a few
undefined-symbol errors at link time and need to either pull a file back in
or stub a symbol. The exclude list above is the starting point, not the
final answer — §5 item 7 covers this.

**Fallback.** If the carve-out turns out to be too invasive, the safe
fallback is `libvsomeip3-core.so == libvsomeip3.so` (i.e. just rename the
current full `:vsomeip3` target). We lose the size win but keep every
other property of the design, including "netlink is dead code at
runtime" (`netlink_connector` is never constructed because we never
instantiate `routing_manager_impl`).

---

## 4. Alternative considered and rejected

### Keep vsomeip + `protocol=external` (the current path)

Spin up an `application_impl`, configure `protocol=external`, ride the
existing SD-plugin contract through `routing_manager_impl`.

**Works today.** Zero new code. AS_OFFERED/AS_AVAILABLE plumb through the
existing availability handler API.

**Why we're moving off it.** The broker has to register a fake application
name, suppress unwanted offer/find behaviour from the routing manager,
deal with the local Unix-socket endpoints the RM creates regardless,
deal with security gates, deal with the netlink connector firing for an
interface it doesn't care about, and live with the JSON config schema's
expectation that the process is a real vsomeip app. Each item is a small
papercut; collectively they are the "cow comes with the farm" cost we
want to eliminate.

---

## 5. Things to verify before prototyping

The dependency graph in §3.1 is the plan; the call-surface assumptions
need to be checked against the source before we commit to it. In rough
priority order:

1. **What does `udp_server_endpoint_impl` actually call on
   `endpoint_host` / `routing_host` during construction and during
   inbound packet handling?**
   - If the set is small ("notify on new client", "deliver bytes") we can
     stub. If it expects a real security context or e2e provider, we
     need to wire those.
   - Likely call sites to walk:
     `udp_server_endpoint_impl::receive_cbk`,
     `server_endpoint_impl::on_message_received`,
     `endpoint_impl::*`.

2. **Does `cfg::configuration_impl` work standalone?** In normal use it
   is owned by `application_impl::init()`, which calls `load(...)` and
   `set_*` helpers on it. We need to confirm that constructing it
   directly and calling `load(path)` plus whatever logger init dance
   upstream does is enough to get sane return values from
   `get_sd_*()`, `get_unicast_address()`, etc.

3. **Logger initialisation order.** Does
   `vsomeip_v3::logger::logger_impl::init(configuration)` need to be
   called before the first `VSOMEIP_INFO`, or does it default-init on
   first use? If we skip it, does the SD plugin crash on its first log
   line?

4. **`service_discovery_impl::init()`'s `plugin_manager` lookup.** Will
   `plugin_manager::get()->get_plugin(SD_RUNTIME_PLUGIN, "vsomeip-sd")`
   succeed when called from a process that did _not_ go through
   `application_impl::init()`? Expected answer: yes, because
   `libvsomeip3-sd.so` is a `DT_NEEDED` dep of `gm_sd_active.so` — so
   it is already mapped before `plugin_manager` runs, and the
   subsequent `dlopen` is a no-op refcount bump. Verify by walking the
   `load_library` → `load_symbol` path in
   `implementation/plugin/src/plugin_manager_impl.cpp` and confirming
   that `VSOMEIP_PLUGIN_PATH` / `LD_LIBRARY_PATH` env vars are _not_
   consulted on the no-op path. (If they are, set them in the test
   harness to keep things deterministic.)

5. **Which `host_->` calls from `service_discovery_impl` does
   `BrokerSDHost` not yet implement correctly?** §2.3 lists 16 methods;
   the broker today implements them in its `BrokerSDHost`. Spot-check
   `find_or_create_remote_client` — in broker mode the natural answer
   is "return a stub endpoint that never sends anything", which is
   slightly different from RM mode.

6. **Does `udp_server_endpoint_impl`'s constructor require the
   netmask/prefix bits from configuration?** It uses them for joining the
   multicast group on the right interface. The configuration JSON we
   already use specifies these, so this should be fine, but worth a
   skim of the constructor.

7. **`libvsomeip3-core.so` carve-out actually links.** Apply the §3.4
   include/exclude list to a candidate `:vsomeip-core` cc_library and
   try to build the cc_shared_library. Resolve undefined symbols by
   either pulling the offending .cpp back in or, if the symbol is only
   referenced from excluded code, leaving it alone. Cross-check that
   `nm -D libvsomeip3-core.so | grep -E 'netlink|routing_manager_impl|local_uds|application_impl'`
   is empty. If symbol-graph cleanup turns out to be more than a few
   files of work, take the §3.4 fallback (use the existing
   `libvsomeip3.so` as `libvsomeip3-core.so`) and revisit later.

---

## 6. Prototype scaffold (planned)

A single small Bazel target that:

1. Links the new `sd_host_lib`.
2. Loads a stripped-down JSON config — just the SD section (multicast
   address, port, unicast interface) and one offered service. No
   routing manager config, no applications block.
3. Constructs `SDHostRuntime` + a hand-written `BrokerSDHost`-style
   host that logs every callback.
4. Calls `start()`, runs the io_context for 30 seconds, exits.

Pass criteria: process opens exactly one UDP socket (the SD port),
emits `OfferService` cyclically per the config, parses inbound SD PDUs
from a remote test peer, and does NOT open any netlink socket, any
Unix socket, or any other endpoint. Verify with `strace -e
trace=socket,bind,connect` and `ss -anp`.

Once that works we replace the hand-written log-only host with
`BrokerSDHost` and graft the result into the broker binary.
