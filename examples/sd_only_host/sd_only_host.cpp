// Copyright (C) 2026 GM Global Technology Operations LLC.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Standalone Service-Discovery host.
//
// Links against `libvsomeip3-core.so` (the wire-level subset of vsomeip) and
// `libvsomeip3-sd.so.3` (the SD plugin). Does NOT link against
// `libvsomeip3.so`: no routing_manager_impl, no application_impl, no
// local-UDS endpoints, no netlink_connector. The process should open exactly
// one UDP socket (the SD port), no AF_NETLINK socket, and no AF_UNIX socket.
//
// Verify with:
//   strace -f -e trace=socket,bind,connect ./sd_only_host
//   ss -anp | grep $(pgrep sd_only_host)
//
// Notes on the linkage trick that this example demonstrates:
//
//   `service_discovery_impl::init()` calls
//   `plugin_manager::get()->get_plugin(SD_RUNTIME_PLUGIN, "libvsomeip3-sd.so.3")`
//   which ultimately ends in `dlopen("libvsomeip3-sd.so.3", RTLD_LAZY|RTLD_LOCAL)`.
//   Because this binary lists `:vsomeip-sd` as a link-time dep, the
//   loader has already mapped `libvsomeip3-sd.so.3` at process start (the
//   binary's DT_NEEDED). The dlopen() call therefore just bumps the
//   refcount on the already-loaded library — no filesystem search, no
//   second mapping, and the SD `runtime` factory's singletons remain
//   process-global.

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/udp.hpp>

// vsomeip's public headers must come before the cfg::* structs: client.hpp
// and friends use ANY_SERVICE / ANY_INSTANCE from constants.hpp in their
// in-class initialisers but don't pull constants.hpp themselves (upstream
// gets away with this because everything is reached transitively via
// configuration_impl.hpp's larger include graph).
#include <vsomeip/constants.hpp>
#include <vsomeip/internal/serializable.hpp>
#include <vsomeip/vsomeip_sec.h>

#include <vsomeip/implementation/configuration/include/client.hpp>
#include <vsomeip/implementation/configuration/include/configuration_impl.hpp>
#include <vsomeip/implementation/configuration/include/eventgroup.hpp>
#include <vsomeip/implementation/configuration/include/service.hpp>
#include <vsomeip/implementation/endpoints/include/endpoint_host.hpp>
#include <vsomeip/implementation/endpoints/include/udp_server_endpoint_impl.hpp>
#include <vsomeip/implementation/message/include/serializer.hpp>
#include <vsomeip/implementation/routing/include/eventgroupinfo.hpp>
#include <vsomeip/implementation/routing/include/routing_host.hpp>
#include <vsomeip/implementation/routing/include/serviceinfo.hpp>
#include <vsomeip/implementation/service_discovery/include/defines.hpp>
#include <vsomeip/implementation/service_discovery/include/runtime.hpp>
#include <vsomeip/implementation/service_discovery/include/service_discovery_host.hpp>
#include <vsomeip/implementation/service_discovery/include/service_discovery_impl.hpp>

namespace v3 = vsomeip_v3;

namespace {

// -----------------------------------------------------------------------------
// endpoint_host_stub
//
// Three methods on `endpoint_host` are actually reachable from
// `udp_server_endpoint_impl` (and its `endpoint_impl<udp>` base):
//
//   * `add_multicast_option()` — called from `join_unlocked()` /
//     `leave_unlocked()` / `restart()` (udp_server_endpoint_impl.cpp:253,
//     505, 533). Upstream records the join so `endpoint_manager_impl`'s
//     netlink_connector can re-issue IP_ADD_MEMBERSHIP when the interface
//     bounces. The actual `setsockopt(IP_ADD_MEMBERSHIP)` is done directly
//     via boost::asio at line ~967, so a no-op here is fine — we just lose
//     auto-rejoin on link flap.
//
//   * `on_error()` — udp_server_endpoint_impl.cpp:746, fired when an
//     inbound UDP packet has a malformed SOME/IP header and the service
//     id is *not* the SD one. For an SD-only host this is purely a
//     diagnostic;
//
//   * `find_instance()` — `endpoint_impl<>::get_instance()` calls it from
//     the SOME/IP-TP segmented-message receive path
//     (udp_server_endpoint_impl.cpp:696). SD never uses TP, so this is
//     unreachable; we still have to provide a return value, so we hand
//     back ANY_INSTANCE.
//
// The remaining overrides (`on_connect`/`on_disconnect`, `on_bind_error`,
// `release_port`, `get_client`, `get_client_host`) are pure-virtual on the
// base interface but only ever called from the *client* endpoint impls
// (tcp/udp_client_endpoint_impl, local_uds_client_endpoint_impl) and the
// local-UDS server — never from `udp_server_endpoint_impl`. They exist
// here solely to make the class concrete.
// -----------------------------------------------------------------------------
class endpoint_host_stub : public v3::endpoint_host {
public:
    void on_connect(std::shared_ptr<v3::endpoint>) override {}
    void on_disconnect(std::shared_ptr<v3::endpoint>) override {}
    bool on_bind_error(std::shared_ptr<v3::endpoint>,
                       const boost::asio::ip::address&, uint16_t,
                       uint16_t&) override { return false; }
    void on_error(const v3::byte_t*, v3::length_t, v3::endpoint*,
                  const boost::asio::ip::address&, std::uint16_t) override {
                    VSOMEIP_ERROR << "[endpoint_host_stub] on_error: malformed SOME/IP packet received";
                  }
    void release_port(uint16_t, bool) override {}
    v3::client_t get_client() const override { return 0; }
    std::string get_client_host() const override { return "sd_only_host"; }
    v3::instance_t find_instance(v3::service_t,
                                 v3::endpoint* const) const override {
        return vsomeip_v3::ANY_INSTANCE;
    }
    void add_multicast_option(const v3::multicast_option_t&) override {}
};

// -----------------------------------------------------------------------------
// routing_host_stub
//
// `udp_server_endpoint_impl::on_message_received_unlocked` forwards every
// inbound SOME/IP message via `routing_host::on_message`. For an SD-only
// host we only care about SD PDUs (service id 0xFFFF, method 0x8100). Those
// are handed to `service_discovery_impl::on_message` directly by the
// endpoint (not via this stub), so here we just log everything else as a
// sanity check that no application traffic is reaching this process.
// -----------------------------------------------------------------------------
class routing_host_stub : public v3::routing_host {
public:
    void on_message(const v3::byte_t* /*data*/, v3::length_t length,
                    v3::endpoint* /*receiver*/, bool is_multicast,
                    v3::client_t /*bound_client*/,
                    const vsomeip_sec_client_t* /*sec_client*/,
                    const boost::asio::ip::address& remote_address,
                    std::uint16_t remote_port) override {
        std::cout << "[routing_host_stub] on_message: " << length
                  << " bytes from " << remote_address.to_string() << ':'
                  << remote_port
                  << (is_multicast ? " (mcast)" : " (ucast)") << '\n';
    }
    v3::client_t get_client() const override { return 0; }
    void add_known_client(v3::client_t, const std::string&) override {}
    v3::client_t get_guest_by_address(const boost::asio::ip::address&,
                                      v3::port_t) const override {
        return VSOMEIP_CLIENT_UNSET;
    }
    void add_guest(v3::client_t, const boost::asio::ip::address&,
                   v3::port_t) override {}
    void remove_local(v3::client_t, bool, bool) override {}
    std::string get_env(v3::client_t) const override { return {}; }
    void remove_subscriptions(v3::port_t,
                              const boost::asio::ip::address&,
                              v3::port_t) override {}
};

// -----------------------------------------------------------------------------
// sd_log_host
//
// Implements `service_discovery_host`. Owns the io_context, the two
// no-op host stubs, the configuration, and the SD UDP endpoint. Every
// SD callback into here is logged; nothing routes anywhere.
//
// The only SD callback that does real work is
// `create_service_discovery_endpoint`, which is invoked once from
// `service_discovery_impl::start()`. It mirrors what
// `routing_manager_impl::create_service_discovery_endpoint` does in
// upstream vsomeip — construct the real `udp_server_endpoint_impl`,
// init+start it, register the per-multicast callbacks, and join the
// SD multicast group.
// -----------------------------------------------------------------------------
class sd_log_host : public v3::sd::service_discovery_host {
public:
    sd_log_host(std::shared_ptr<v3::cfg::configuration_impl> configuration,
                boost::asio::io_context& io)
        : configuration_(std::move(configuration)),
          io_(io),
          endpoint_host_(std::make_shared<endpoint_host_stub>()),
          routing_host_(std::make_shared<routing_host_stub>()) {}

    boost::asio::io_context& get_io() override { return io_; }

    std::shared_ptr<v3::endpoint>
    create_service_discovery_endpoint(const std::string& address,
                                      uint16_t port, bool reliable) override {
        if (reliable) {
            std::cerr << "[sd_log_host] SD over TCP not supported in this demo\n";
            return nullptr;
        }

        auto ep = std::make_shared<v3::udp_server_endpoint_impl>(
                endpoint_host_, routing_host_, io_, configuration_);

        const auto& unicast = configuration_->get_unicast_address();
        boost::asio::ip::udp::endpoint local{unicast, port};

        boost::system::error_code ec;
        ep->init(local, ec);
        if (ec) {
            std::cerr << "[sd_log_host] udp_server_endpoint_impl::init failed: "
                      << ec.message() << '\n';
            return nullptr;
        }

        ep->start();
        ep->add_default_target(VSOMEIP_SD_SERVICE, address, port);
        ep->set_receive_own_multicast_messages(true);
        ep->join(address);

        std::cout << "[sd_log_host] SD endpoint bound to "
                  << unicast.to_string() << ':' << port << ", joined "
                  << address << '\n';

        sd_endpoint_ = ep;
        return ep;
    }

    // --- everything below is a logging / no-op stub ---

    v3::services_t get_offered_services() const override { return {}; }

    std::shared_ptr<v3::eventgroupinfo>
    find_eventgroup(v3::service_t, v3::instance_t,
                    v3::eventgroup_t) const override {
        return nullptr;
    }

    bool send(v3::client_t, std::shared_ptr<v3::message> _message, bool) override {
        // Called by service_discovery_impl::send() for outbound SD PDUs.
        // Mirrors routing_manager_base::send() + routing_manager_impl::send():
        // serialize the message and hand the bytes to the SD UDP endpoint,
        // which routes them to its default target (the SD multicast group).
        if (!sd_endpoint_) {
            return false;
        }
        auto* serializable_message =
                dynamic_cast<const v3::serializable*>(_message.get());
        if (!serializable_message) {
            return false;
        }
        v3::serializer ser{/*buffer_shrink_threshold=*/0};
        if (!ser.serialize(serializable_message)) {
            std::cerr << "[sd_log_host] SD message serialize failed\n";
            return false;
        }
        return sd_endpoint_->send(ser.get_data(), ser.get_size());
    }

    bool send_via_sd(const std::shared_ptr<v3::endpoint_definition>& _target,
                     const v3::byte_t* _data, uint32_t _size,
                     uint16_t /*sd_port*/) override {
        // Called for unicast OfferService replies to a remote FindService.
        // Mirrors routing_manager_impl::send_via_sd().
        if (!sd_endpoint_) {
            return false;
        }
        return sd_endpoint_->send_to(_target, _data, _size);
    }

    void add_routing_info(v3::service_t service, v3::instance_t instance,
                          v3::major_version_t, v3::minor_version_t,
                          v3::ttl_t,
                          const boost::asio::ip::address& reliable_addr,
                          uint16_t reliable_port,
                          const boost::asio::ip::address& unreliable_addr,
                          uint16_t unreliable_port) override {
        std::cout << "[sd_log_host] add_routing_info svc=" << std::hex
                  << service << " inst=" << instance << std::dec << " r="
                  << reliable_addr.to_string() << ':' << reliable_port
                  << " u=" << unreliable_addr.to_string() << ':'
                  << unreliable_port << '\n';
    }

    void del_routing_info(v3::service_t, v3::instance_t, bool, bool,
                          bool) override {}
    void update_routing_info(std::chrono::milliseconds) override {}

    void on_remote_unsubscribe(
            std::shared_ptr<v3::remote_subscription>&) override {}
    void on_subscribe_ack(v3::client_t, v3::service_t, v3::instance_t,
                          v3::eventgroup_t, v3::event_t,
                          v3::remote_subscription_id_t) override {}
    void on_subscribe_ack_with_multicast(v3::service_t, v3::instance_t,
                                         const boost::asio::ip::address&,
                                         const boost::asio::ip::address&,
                                         uint16_t) override {}

    std::shared_ptr<v3::endpoint>
    find_or_create_remote_client(v3::service_t, v3::instance_t,
                                 bool) override {
        return nullptr;
    }

    void expire_subscriptions(const boost::asio::ip::address&) override {}
    void expire_subscriptions(const boost::asio::ip::address&, std::uint16_t,
                              bool) override {}
    void expire_services(const boost::asio::ip::address&) override {}
    void expire_services(const boost::asio::ip::address&, std::uint16_t,
                         bool) override {}

    void on_remote_subscribe(
            std::shared_ptr<v3::remote_subscription>&,
            const v3::remote_subscription_callback_t&) override {}

    void on_subscribe_nack(v3::client_t, v3::service_t, v3::instance_t,
                           v3::eventgroup_t, bool,
                           v3::remote_subscription_id_t) override {}

    std::chrono::steady_clock::time_point
    expire_subscriptions(bool /*force*/) override {
        // Push the next subscription-expiry check far enough out that the
        // SD impl's TTL timer effectively becomes a no-op for this demo.
        return std::chrono::steady_clock::now() + std::chrono::hours{24};
    }

    std::shared_ptr<v3::serviceinfo>
    get_offered_service(v3::service_t, v3::instance_t) const override {
        return nullptr;
    }

    std::map<v3::instance_t, std::shared_ptr<v3::serviceinfo>>
    get_offered_service_instances(v3::service_t) const override {
        return {};
    }

    std::set<v3::eventgroup_t>
    get_subscribed_eventgroups(v3::service_t, v3::instance_t) override {
        return {};
    }

    // -------------------------------------------------------------------
    // Helpers used by main() to drive offer/request/subscribe.
    //
    // create_unreliable_service_endpoint: brings up a real UDP socket bound
    // to the host's unicast address on `port`. The SD layer copies that
    // address+port into the IPv4 endpoint option of the OfferService PDU
    // (see service_discovery_impl::insert_option), so the offers we publish
    // are well-formed wire-wise even though the data plane is inert.
    //
    // The shared_ptrs are stashed in `retained_*_` so the SD layer's
    // shared_ptr<serviceinfo> / shared_ptr<eventgroupinfo> can rely on a
    // stable owner for the lifetime of the process.
    // -------------------------------------------------------------------
    std::shared_ptr<v3::endpoint>
    create_unreliable_service_endpoint(uint16_t port) {
        auto ep = std::make_shared<v3::udp_server_endpoint_impl>(
                endpoint_host_, routing_host_, io_, configuration_);
        const auto& unicast = configuration_->get_unicast_address();
        boost::asio::ip::udp::endpoint local{unicast, port};
        boost::system::error_code ec;
        ep->init(local, ec);
        if (ec) {
            std::cerr << "[sd_log_host] data endpoint init failed on port "
                      << port << ": " << ec.message() << '\n';
            return nullptr;
        }
        ep->start();
        std::cout << "[sd_log_host] data endpoint bound to "
                  << unicast.to_string() << ':' << port << '\n';
        retained_endpoints_.push_back(ep);
        return ep;
    }

    void retain(const std::shared_ptr<v3::serviceinfo>& info) {
        retained_services_.push_back(info);
    }
    void retain(const std::shared_ptr<v3::eventgroupinfo>& info) {
        retained_eventgroups_.push_back(info);
    }

private:
    std::shared_ptr<v3::cfg::configuration_impl> configuration_;
    boost::asio::io_context& io_;
    std::shared_ptr<endpoint_host_stub> endpoint_host_;
    std::shared_ptr<routing_host_stub> routing_host_;
    std::shared_ptr<v3::udp_server_endpoint_impl> sd_endpoint_;

    std::vector<std::shared_ptr<v3::endpoint>> retained_endpoints_;
    std::vector<std::shared_ptr<v3::serviceinfo>> retained_services_;
    std::vector<std::shared_ptr<v3::eventgroupinfo>> retained_eventgroups_;
};

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

// Drive the SD layer from the parsed vsomeip configuration:
//   * every `services[]` entry → offered service (OfferService PDU)
//   * every `clients[]`  entry → requested service (FindService PDU)
//   * for every clients[] entry that *also* has a matching `services[]`
//     entry (same service/instance), its eventgroups become Subscribe
//     entries (they fire once a peer's OfferService for that id arrives).
//
// All wire fields come straight from `configuration_impl` — port from
// `cfg::service::unreliable_`, eventgroup ids from `cfg::service::eventgroups_`,
// requested ids from `cfg::client::service_/instance_` — so this binary
// shares the schema with every other vsomeip app.
void drive_sd_from_config(v3::cfg::configuration_impl& cfg,
                          v3::sd::service_discovery& sd,
                          sd_log_host& host,
                          v3::ttl_t sd_ttl) {
    // We don't store major/minor in cfg::service (upstream takes them from
    // the application API at runtime). For an example binary, a fixed pair
    // is fine and keeps the wire trace readable.
    constexpr v3::major_version_t k_major = 1;
    constexpr v3::minor_version_t k_minor = 0;

    // Build a (service, instance) -> cfg::service index from the snapshot
    // so the clients[] pass below can look up eventgroup ids without
    // touching configuration internals (find_service is private).
    std::map<std::pair<v3::service_t, v3::instance_t>,
             std::shared_ptr<v3::cfg::service>> services_by_id;

    for (const auto& svc : cfg.get_services()) {
        services_by_id[{svc->service_, svc->instance_}] = svc;

        auto info = std::make_shared<v3::serviceinfo>(
                svc->service_, svc->instance_, k_major, k_minor, sd_ttl,
                /*is_local=*/true);
        if (svc->unreliable_ != v3::ILLEGAL_PORT) {
            if (auto ep = host.create_unreliable_service_endpoint(svc->unreliable_)) {
                info->set_endpoint(ep, /*reliable=*/false);
            }
        }
        sd.offer_service(info);
        host.retain(info);

        std::cout << "[main] offering svc=0x" << std::hex << svc->service_
                  << " inst=0x" << svc->instance_ << std::dec
                  << " port=" << svc->unreliable_ << '\n';
    }

    for (const auto& cli : cfg.get_clients()) {
        sd.request_service(cli->service_, cli->instance_, k_major, k_minor,
                           v3::DEFAULT_TTL);
        std::cout << "[main] requesting svc=0x" << std::hex << cli->service_
                  << " inst=0x" << cli->instance_ << std::dec << '\n';

        // If the requested service is also declared in `services[]`, use
        // its eventgroup ids to fire Subscribe entries. SD only puts them
        // on the wire once a matching OfferService comes back — so the
        // subscribe call is just a queued intent at this point.
        auto it = services_by_id.find({cli->service_, cli->instance_});
        if (it == services_by_id.end()) {
            continue;
        }
        for (const auto& eg_entry : it->second->eventgroups_) {
            const auto eg_id = eg_entry.first;
            auto eg_info = std::make_shared<v3::eventgroupinfo>(
                    cli->service_, cli->instance_, eg_id, k_major, sd_ttl,
                    /*max_remote_subscribers=*/0);
            sd.subscribe(cli->service_, cli->instance_, eg_id, k_major,
                         sd_ttl, /*VSOMEIP_ROUTING_CLIENT=*/0, eg_info);
            host.retain(eg_info);
            std::cout << "[main] subscribing svc=0x" << std::hex
                      << cli->service_ << " inst=0x" << cli->instance_
                      << " eg=0x" << eg_id << std::dec << '\n';
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string config_path =
            argc > 1 ? argv[1] : "examples/sd_only_host/sd_only_host.json";

    // configuration_impl::load() initialises the logger (`logger_impl::init`)
    // as a side effect when the configuration parses the "logging" section,
    // so we don't have to do it explicitly.
    auto configuration =
            std::make_shared<v3::cfg::configuration_impl>(config_path);
    if (!configuration->load("sd_only_host")) {
        std::cerr << "Failed to load configuration: " << config_path << '\n';
        return 1;
    }

    boost::asio::io_context io;
    auto host = std::make_shared<sd_log_host>(configuration, io);

    // service_discovery_impl takes a raw pointer to the host; the host
    // owns the SD impl below via shared_ptr, so the lifetime is correct.
    auto sd = std::make_shared<v3::sd::service_discovery_impl>(host.get(),
                                                                configuration);
    sd->init();
    sd->start();

    // Drive the SD layer with whatever the JSON's `services[]` / `clients[]`
    // sections asked for. Must run after sd->start() so the debounce
    // timers picking up these calls are already armed.
    drive_sd_from_config(*configuration, *sd, *host, configuration->get_sd_ttl());

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
            work{io.get_executor()};
    std::thread io_thread{[&io]() { io.run(); }};

    const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (!g_stop.load(std::memory_order_relaxed)
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }

    sd->stop();
    work.reset();
    io.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    return 0;
}
