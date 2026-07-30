// Copyright (C) 2025 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_UDP_SOCKET_HPP_
#define VSOMEIP_V3_UDP_SOCKET_HPP_

#include <functional>
#include <vector>

#include <boost/asio/ip/udp.hpp>

namespace vsomeip_v3 {

/**
 * Thin abstraction over boost::asio::ip::udp::socket so send paths can use
 * ConstBufferSequence scatter-gather consistently with tcp_socket.
 */
class udp_socket {
public:
    using rw_handler = std::function<void(boost::system::error_code const&, std::size_t)>;
    using endpoint_type = boost::asio::ip::udp::endpoint;

    virtual ~udp_socket() = default;

    [[nodiscard]] virtual bool is_open() const = 0;
    virtual void open(boost::asio::ip::udp::endpoint::protocol_type, boost::system::error_code&) = 0;
    virtual void bind(const endpoint_type&, boost::system::error_code&) = 0;
    virtual void close(boost::system::error_code&) = 0;
    virtual void cancel(boost::system::error_code&) = 0;

    virtual endpoint_type local_endpoint(boost::system::error_code&) const = 0;

    virtual void set_option(boost::asio::socket_base::reuse_address, boost::system::error_code&) = 0;
    virtual void set_option(boost::asio::socket_base::broadcast, boost::system::error_code&) = 0;

    virtual void async_send(const std::vector<boost::asio::const_buffer>&, rw_handler) = 0;
    virtual void async_send_to(const std::vector<boost::asio::const_buffer>&, const endpoint_type&, rw_handler) = 0;
    virtual void async_receive_from(boost::asio::mutable_buffer, endpoint_type&, rw_handler) = 0;

    // Expose native socket for multicast / advanced options used by server endpoint.
    virtual boost::asio::ip::udp::socket& native() = 0;
};

} // namespace vsomeip_v3

#endif
