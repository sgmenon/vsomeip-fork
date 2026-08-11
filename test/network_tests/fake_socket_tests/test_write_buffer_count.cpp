// Copyright (C) 2025 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "helpers/fake_tcp_socket_handle.hpp"

#include <boost/asio.hpp>
#include <gtest/gtest.h>

namespace vsomeip_v3::testing {

TEST(fake_tcp_socket_handle, write_tracks_multi_buffer_count) {
    boost::asio::io_context io;
    auto sender = std::make_shared<fake_tcp_socket_handle>(io);
    auto receiver = std::make_shared<fake_tcp_socket_handle>(io);

    ASSERT_TRUE(receiver->add_connection(*sender));

    unsigned char data_a[] = {0x01, 0x02};
    unsigned char data_b[] = {0x03, 0x04, 0x05};
    std::vector<boost::asio::const_buffer> buffers{
            boost::asio::buffer(data_a),
            boost::asio::buffer(data_b),
    };

    bool write_done = false;
    sender->write(buffers, [&](boost::system::error_code const& ec, size_t bytes) {
        EXPECT_FALSE(ec);
        EXPECT_EQ(bytes, 5U);
        write_done = true;
    });

    io.run();
    ASSERT_TRUE(write_done);
    EXPECT_GE(sender->last_write_buffer_count(), 2U);
}

} // namespace vsomeip_v3::testing
