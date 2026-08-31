// Copyright (C) 2026 GM GLOBAL TECHNOLOGY OPERATIONS LLC ALL RIGHTS RESERVED.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <vsomeip/defines.hpp>
#include <vsomeip/enumeration_types.hpp>
#include <vsomeip/primitive_types.hpp>

#include "../../../implementation/endpoints/include/buffer.hpp"
#include "../../../implementation/message/include/payload_impl.hpp"
#include "../../../implementation/protocol/include/protocol.hpp"
#include "../../../implementation/protocol/include/send_command.hpp"
#include "../../../implementation/routing/include/e2e_receive_helpers.hpp"
#include "../../../implementation/utility/include/bithelper.hpp"

namespace {

using vsomeip_v3::bithelper;
using vsomeip_v3::byte_t;
using vsomeip_v3::message_buffer_t;
using vsomeip_v3::owned_buffer_slice;
using vsomeip_v3::payload_impl;
using vsomeip_v3::protocol::error_e;
using vsomeip_v3::protocol::id_e;
using vsomeip_v3::protocol::send_command;
using vsomeip_v3::protocol::SEND_COMMAND_HEADER_SIZE;

constexpr vsomeip_v3::service_t k_service = 0x1234;
constexpr vsomeip_v3::method_t k_method = 0x5678;
constexpr vsomeip_v3::instance_t k_instance = 0x0009;
constexpr vsomeip_v3::client_t k_client = 0x00AB;
constexpr vsomeip_v3::client_t k_target = 0x00CD;

std::vector<byte_t> make_send_command_ipc(const std::vector<byte_t>& _app) {
    std::vector<byte_t> someip(VSOMEIP_FULL_HEADER_SIZE + _app.size());
    bithelper::write_uint16_be(k_service, someip.data() + VSOMEIP_SERVICE_POS_MIN);
    bithelper::write_uint16_be(k_method, someip.data() + VSOMEIP_METHOD_POS_MIN);
    bithelper::write_uint32_be(static_cast<uint32_t>(someip.size() - 8U), someip.data() + VSOMEIP_LENGTH_POS_MIN);
    bithelper::write_uint16_be(k_client, someip.data() + VSOMEIP_CLIENT_POS_MIN);
    bithelper::write_uint16_be(0x0001, someip.data() + VSOMEIP_SESSION_POS_MIN);
    someip[VSOMEIP_PROTOCOL_VERSION_POS] = 0x01;
    someip[VSOMEIP_INTERFACE_VERSION_POS] = 0x01;
    someip[VSOMEIP_MESSAGE_TYPE_POS] = static_cast<byte_t>(vsomeip_v3::message_type_e::MT_REQUEST);
    someip[VSOMEIP_RETURN_CODE_POS] = static_cast<byte_t>(vsomeip_v3::return_code_e::E_OK);
    std::memcpy(someip.data() + VSOMEIP_FULL_HEADER_SIZE, _app.data(), _app.size());

    send_command cmd(id_e::SEND_ID);
    cmd.set_client(k_client);
    cmd.set_instance(k_instance);
    cmd.set_reliable(true);
    cmd.set_status(0);
    cmd.set_target(k_target);
    cmd.set_message(someip);

    std::vector<byte_t> ipc;
    error_e err = error_e::ERROR_OK;
    cmd.serialize(ipc, err);
    EXPECT_EQ(err, error_e::ERROR_OK);
    return ipc;
}

} // namespace

TEST(send_command_test, deserialize_header_does_not_copy_body) {
    const std::vector<byte_t> app{0x10, 0x11, 0x12, 0x13};
    const auto ipc = make_send_command_ipc(app);

    send_command header_only(id_e::SEND_ID);
    error_e err = error_e::ERROR_OK;
    header_only.deserialize_header(ipc, err);
    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(header_only.get_instance(), k_instance);
    EXPECT_TRUE(header_only.is_reliable());
    EXPECT_EQ(header_only.get_target(), k_target);
    EXPECT_TRUE(header_only.get_message().empty());

    send_command full(id_e::SEND_ID);
    full.deserialize(ipc, err);
    ASSERT_EQ(err, error_e::ERROR_OK);
    EXPECT_EQ(full.get_message().size(), VSOMEIP_FULL_HEADER_SIZE + app.size());
}

TEST(proxy_send_pin_test, build_message_pins_ipc_buffer) {
    const std::vector<byte_t> app{0x20, 0x21, 0x22, 0x23, 0x24};
    auto ipc = std::make_shared<message_buffer_t>(make_send_command_ipc(app));

    send_command header_only(id_e::SEND_ID);
    error_e err = error_e::ERROR_OK;
    header_only.deserialize_header(*ipc, err);
    ASSERT_EQ(err, error_e::ERROR_OK);

    const std::size_t someip_off = SEND_COMMAND_HEADER_SIZE;
    const std::size_t someip_len = ipc->size() - someip_off;
    auto its_message = vsomeip_v3::build_message_from_buffer(owned_buffer_slice::slice(ipc, someip_off, someip_len));
    ASSERT_NE(its_message, nullptr);
    its_message->set_instance(header_only.get_instance());
    its_message->set_reliable(header_only.is_reliable());

    EXPECT_EQ(its_message->get_service(), k_service);
    EXPECT_EQ(its_message->get_method(), k_method);
    EXPECT_EQ(its_message->get_instance(), k_instance);
    EXPECT_TRUE(its_message->is_reliable());

    auto its_payload = std::dynamic_pointer_cast<payload_impl>(its_message->get_payload());
    ASSERT_NE(its_payload, nullptr);
    EXPECT_EQ(its_payload->get_buffer().get(), ipc.get());
    EXPECT_EQ(its_payload->get_length(), app.size());
    EXPECT_EQ(0, std::memcmp(its_payload->get_data(), app.data(), app.size()));
}
