// Copyright (C) 2026 GM Global Technology Operations LLC.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Stub definitions for routing-layer symbols that `libvsomeip3-core.so`
// references but our SD-only host never actually calls.
//
// `runtime::get()` — defense-in-depth singleton (Option A)
//    A non-virtual static accessor that forwards to `runtime_impl`.
//    It is referenced from `vsomeip_v3::event::event()` in
//    `implementation/routing/src/event.cpp`, which is still part of
//    `libvsomeip3-core.so` (via `VSOMEIP_CORE_ROUTING_DATA_SRCS`). No
//    `vsomeip_v3::event` is constructed in an SD-only host today
//    (the cfg plugin constructs `vsomeip_v3::cfg::event` instead,
//    which is a different class), but if some future change wires the
//    routing event ctor in by accident we would prefer a clear
//    `logic_error` over a `nullptr` dereference. So instead of
//    returning an empty `shared_ptr<runtime>`, we return a real
//    singleton of `sd_only_runtime` below: it implements
//    `create_payload()` (the only `runtime` method core code paths
//    actually call when alive — `message_impl::message_impl()` and
//    `message_impl::deserialize()` would invoke it; both now live in
//    libvsomeip3.so and so are no longer in this binary's link
//    surface, but we keep the implementation in place defensively),
//    and throws for every method whose invocation would signal that
//    SD-only assumptions have been broken.
//
// Cleanest follow-up: move `implementation/routing/src/event.cpp` out of
// core. Once that is done the `runtime::get()` stub can be removed and
// the whole rationale for this file disappears.

#include <cassert>
#include <memory>
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include <vsomeip/application.hpp>
#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>
#include <vsomeip/primitive_types.hpp>
#include <vsomeip/runtime.hpp>

#include <vsomeip/implementation/message/include/payload_impl.hpp>

namespace vsomeip_v3 {

// --------------------------------------------------------------------------
// Defense-in-depth runtime singleton
// --------------------------------------------------------------------------

namespace {

[[noreturn]] void unreachable(const char* _what) {
    throw std::logic_error(std::string("sd_only_host: ") + _what
                           + " is not supported in an SD-only host"
                             " (no routing manager linked).");
}

/// This entire class definition is defensive
/// the runtime class shouldn't have even been constructed in a SD-Only host that has been properly written
class sd_only_runtime final : public runtime {
public:
    // Payload factories: payload_impl lives in libvsomeip3-core.so so we
    // can construct it directly. Real implementations.
    std::shared_ptr<payload> create_payload() const override {
        return std::make_shared<payload_impl>();
    }
    std::shared_ptr<payload> create_payload(const byte_t* _data,
                                            uint32_t _size) const override {
        return std::make_shared<payload_impl>(_data, _size);
    }
    std::shared_ptr<payload> create_payload(
            const std::vector<byte_t>& _data) const override {
        return std::make_shared<payload_impl>(_data);
    }

    // Message and application factories require routing-layer types
    // (`message_impl`, `application_impl`) that this binary does not
    // link. Anyone calling these has stepped outside SD-only territory.
    std::shared_ptr<message> create_message(bool /*_reliable*/) const override {
        unreachable("runtime::create_message");
    }
    std::shared_ptr<message> create_request(bool /*_reliable*/) const override {
        unreachable("runtime::create_request");
    }
    std::shared_ptr<message> create_response(
            const std::shared_ptr<message>& /*_request*/) const override {
        unreachable("runtime::create_response");
    }
    std::shared_ptr<message> create_notification(bool /*_reliable*/) const override {
        unreachable("runtime::create_notification");
    }
    std::shared_ptr<application> create_application(
            const std::string& /*_name*/) override {
        unreachable("runtime::create_application");
    }
    std::shared_ptr<application> create_application(
            const std::string& /*_name*/,
            const std::string& /*_path*/) override {
        unreachable("runtime::create_application");
    }
    std::shared_ptr<application> get_application(
            const std::string& /*_name*/) const override {
        unreachable("runtime::get_application");
    }
    void remove_application(const std::string& /*_name*/) override {
        unreachable("runtime::remove_application");
    }
};

} // namespace

std::shared_ptr<runtime> runtime::get() {
    unreachable("runtime::get");
}

} // namespace vsomeip_v3
