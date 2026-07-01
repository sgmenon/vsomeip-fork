// Copyright (C) 2026 GM Global Technology Operations LLC.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_ASIO_NETLINK_FACTORY_HPP_
#define VSOMEIP_V3_ASIO_NETLINK_FACTORY_HPP_

#if defined(__linux__)

#include "abstract_netlink_factory.hpp"

namespace vsomeip_v3 {

// Production implementation of `abstract_netlink_factory`. The
// translation unit deliberately lives in the routing layer
// (libvsomeip3.so), so the `netlink_connector` vtable reference pulled
// in by `std::make_shared<netlink_connector>(...)` stays out of
// libvsomeip3-core.so.
class asio_netlink_factory final : public abstract_netlink_factory {
public:
    ~asio_netlink_factory() override = default;

    std::shared_ptr<abstract_netlink_connector>
    create_netlink_connector(boost::asio::io_context& _io,
                             const boost::asio::ip::address& _address,
                             const boost::asio::ip::address& _multicast_address,
                             bool _is_requiring_link) override;
};

} // namespace vsomeip_v3

#endif // defined(__linux__)
#endif // VSOMEIP_V3_ASIO_NETLINK_FACTORY_HPP_
