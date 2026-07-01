// Copyright (C) 2026 GM Global Technology Operations LLC.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_ABSTRACT_NETLINK_FACTORY_HPP_
#define VSOMEIP_V3_ABSTRACT_NETLINK_FACTORY_HPP_

#if defined(__linux__)

#include <memory>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>

#include "abstract_netlink_connector.hpp"

namespace vsomeip_v3 {

// Companion injector for `abstract_socket_factory`, split out because the
// concrete asio implementation needs to construct `netlink_connector`,
// whose vtable lives in the routing layer (libvsomeip3.so) — pulling that
// reference into core would force every core-only consumer (e.g. an
// SD-only host) to ship a stub vtable. Both the singleton storage and
// the default install live next to `asio_netlink_factory` in the
// routing layer (see asio_netlink_factory.cpp); core never references
// netlink symbols.
class abstract_netlink_factory {
public:
    virtual ~abstract_netlink_factory() = default;

    // Lazily installs the production `asio_netlink_factory` on first
    // call, mirroring `abstract_socket_factory::get()`. Never returns
    // null. Tests must install fakes via `set_abstract_netlink_factory()`
    // before any thread reaches `get()`.
    static abstract_netlink_factory* get();

    virtual std::shared_ptr<abstract_netlink_connector>
    create_netlink_connector(boost::asio::io_context& _io,
                             const boost::asio::ip::address& _address,
                             const boost::asio::ip::address& _multicast_address,
                             bool _is_requiring_link = true) = 0;
};

// Same threading caveats as `set_abstract_factory` (see
// abstract_socket_factory.cpp): tests install before `SetUpTestSuite`
// returns; production code does not call this. No mutex by design.
void set_abstract_netlink_factory(std::shared_ptr<abstract_netlink_factory> _factory);

} // namespace vsomeip_v3

#endif // defined(__linux__)
#endif // VSOMEIP_V3_ABSTRACT_NETLINK_FACTORY_HPP_
