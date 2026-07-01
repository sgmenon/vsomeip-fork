// Copyright (C) 2025 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_ASIO_SOCKET_FACTORY_HPP_
#define VSOMEIP_V3_ASIO_SOCKET_FACTORY_HPP_

#include "abstract_socket_factory.hpp"

namespace vsomeip_v3 {

class asio_socket_factory final : public abstract_socket_factory {
public:
    ~asio_socket_factory() override = default;

    std::unique_ptr<tcp_socket> create_tcp_socket(boost::asio::io_context& _io) override;
    std::unique_ptr<tcp_acceptor> create_tcp_acceptor(boost::asio::io_context& _io) override;
    std::unique_ptr<abstract_timer> create_timer(boost::asio::io_context& _io) override;
};

}
#endif
