// Copyright (C) 2026 GM Global Technology Operations LLC.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "../include/asio_netlink_factory.hpp"

#if defined(__linux__)

#include "../include/netlink_connector.hpp"

namespace vsomeip_v3 {

// Singleton storage for `abstract_netlink_factory` lives here, not in
// `abstract_netlink_factory.cpp` (which would have to live in core),
// because the lazy default below references `asio_netlink_factory` —
// whose ctor pulls in `netlink_connector`'s vtable. Keeping both the
// storage and the default install in this TU means libvsomeip3-core.so
// never sees the netlink_connector symbol; SD-only hosts that link
// only core do not need a stub vtable.
//
// Same threading contract as `abstract_socket_factory` (see
// abstract_socket_factory.cpp): production initialises once on the
// first `get()` call, tests install fakes via the setter before any
// other thread reaches `get()`. No mutex by design.
static std::shared_ptr<abstract_netlink_factory> _factory;
static std::shared_ptr<abstract_netlink_factory> init() {
    if (!_factory) {
        _factory = std::make_shared<asio_netlink_factory>();
    }
    return _factory;
}

void set_abstract_netlink_factory(std::shared_ptr<abstract_netlink_factory> _f) {
    _factory = std::move(_f);
}

abstract_netlink_factory* abstract_netlink_factory::get() {
    static auto const factory = init();
    return factory.get();
}

std::shared_ptr<abstract_netlink_connector>
asio_netlink_factory::create_netlink_connector(boost::asio::io_context& _io,
                                               const boost::asio::ip::address& _address,
                                               const boost::asio::ip::address& _multicast_address,
                                               bool _is_requiring_link) {
    return std::make_shared<netlink_connector>(_io, _address, _multicast_address, _is_requiring_link);
}

} // namespace vsomeip_v3

#endif // defined(__linux__)
