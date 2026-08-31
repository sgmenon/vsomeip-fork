// Copyright (C) 2026 GM GLOBAL TECHNOLOGY OPERATIONS LLC ALL RIGHTS RESERVED.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <vsomeip/defines.hpp>
#include <vsomeip/enumeration_types.hpp>
#include <vsomeip/primitive_types.hpp>

#include "../../../implementation/e2e_protection/include/buffer/buffer.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile04/checker.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile04/protector.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile_interface/profile_interface.hpp"
#include "../../../implementation/endpoints/include/buffer.hpp"
#include "../../../implementation/message/include/payload_impl.hpp"
#include "../../../implementation/routing/include/e2e_receive_helpers.hpp"
#include "../../../implementation/utility/include/bithelper.hpp"

namespace {

using vsomeip_v3::bithelper;
using vsomeip_v3::buffer_view;
using vsomeip_v3::byte_t;
using vsomeip_v3::e2e_buffer;
using vsomeip_v3::message_buffer_t;
using vsomeip_v3::owned_buffer_slice;
using vsomeip_v3::payload_impl;
using vsomeip_v3::e2e::profile_interface::generic_check_status;

constexpr vsomeip_v3::instance_t k_instance = 0x0001;
constexpr vsomeip_v3::service_t k_service = 0x1234;
constexpr vsomeip_v3::method_t k_method = 0x5678;

e2e_buffer make_app_payload(std::size_t _n, std::uint8_t _seed = 0x10) {
    e2e_buffer payload(_n);
    for (std::size_t i = 0; i < _n; ++i) {
        payload[i] = static_cast<std::uint8_t>(_seed + i);
    }
    return payload;
}

e2e_buffer flatten_protect(const vsomeip_v3::e2e::protect_result& _parts) {
    e2e_buffer out;
    if (_parts.e2e_header) {
        out.insert(out.end(), _parts.e2e_header->begin(), _parts.e2e_header->end());
    }
    if (_parts.owned_app_payload) {
        out.insert(out.end(), _parts.owned_app_payload->begin(), _parts.owned_app_payload->end());
    } else {
        out.insert(out.end(), _parts.app_payload.begin(), _parts.app_payload.end());
    }
    if (_parts.e2e_footer) {
        out.insert(out.end(), _parts.e2e_footer->begin(), _parts.e2e_footer->end());
    }
    return out;
}

owned_buffer_slice make_someip_frame(const e2e_buffer& _protected_payload) {
    auto frame = std::make_shared<message_buffer_t>(VSOMEIP_FULL_HEADER_SIZE + _protected_payload.size());
    bithelper::write_uint16_be(k_service, frame->data() + VSOMEIP_SERVICE_POS_MIN);
    bithelper::write_uint16_be(k_method, frame->data() + VSOMEIP_METHOD_POS_MIN);
    const uint32_t its_total = static_cast<uint32_t>(frame->size());
    bithelper::write_uint32_be(its_total - 8U, frame->data() + VSOMEIP_LENGTH_POS_MIN);
    bithelper::write_uint16_be(0x0001, frame->data() + VSOMEIP_CLIENT_POS_MIN);
    bithelper::write_uint16_be(0x0001, frame->data() + VSOMEIP_SESSION_POS_MIN);
    (*frame)[VSOMEIP_PROTOCOL_VERSION_POS] = 0x01;
    (*frame)[VSOMEIP_INTERFACE_VERSION_POS] = 0x01;
    (*frame)[VSOMEIP_MESSAGE_TYPE_POS] = static_cast<byte_t>(vsomeip_v3::message_type_e::MT_REQUEST);
    (*frame)[VSOMEIP_RETURN_CODE_POS] = static_cast<byte_t>(vsomeip_v3::return_code_e::E_OK);
    std::copy(_protected_payload.begin(), _protected_payload.end(), frame->begin() + VSOMEIP_FULL_HEADER_SIZE);
    return owned_buffer_slice::whole(frame);
}

bool span_in_buffer(vsomeip_v3::span<const uint8_t> _span, const vsomeip_v3::message_buffer_ptr_t& _buffer) {
    if (!_buffer || _span.empty()) {
        return _span.empty();
    }
    return _span.data() >= _buffer->data() && (_span.data() + _span.size()) <= (_buffer->data() + _buffer->size());
}

} // namespace

TEST(e2e_check_strip_pin, profile04_check_and_strip_pin_recv_frame) {
    vsomeip_v3::e2e::profile04::profile_config config(/*data_id=*/0x2d, /*offset=*/0, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile04::protector protector(config);
    vsomeip_v3::e2e::profile04::profile_04_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);
    ASSERT_TRUE(parts.valid);

    const auto protected_payload = flatten_protect(parts);
    ASSERT_EQ(protected_payload.size(), 12U + app.size());

    const auto frame = make_someip_frame(protected_payload);
    ASSERT_TRUE(frame.valid());
    ASSERT_EQ(frame.length, VSOMEIP_FULL_HEADER_SIZE + protected_payload.size());

    // Match e2e_provider_impl::check: checker sees bytes after SOME/IP base.
    const buffer_view protected_view(frame.data() + VSOMEIP_FULL_HEADER_SIZE, frame.length - VSOMEIP_FULL_HEADER_SIZE);
    const auto checked = checker.check(protected_view, k_instance);

    EXPECT_EQ(checked.status, generic_check_status::E2E_OK);
    EXPECT_TRUE(span_in_buffer(checked.e2e_header, frame.buffer));
    EXPECT_TRUE(span_in_buffer(checked.app_payload, frame.buffer));
    EXPECT_EQ(checked.app_payload.size(), app.size());
    EXPECT_TRUE(std::equal(checked.app_payload.begin(), checked.app_payload.end(), app.begin()));

    auto its_payload = vsomeip_v3::make_payload_from_span(checked.app_payload, frame);
    ASSERT_NE(its_payload, nullptr);
    auto its_impl = std::dynamic_pointer_cast<payload_impl>(its_payload);
    ASSERT_NE(its_impl, nullptr);
    EXPECT_EQ(its_impl->get_buffer().get(), frame.buffer.get());
    EXPECT_EQ(its_impl->get_length(), app.size());
    EXPECT_EQ(its_impl->get_buffer_offset(),
              static_cast<std::size_t>(checked.app_payload.data() - frame.buffer->data()));
    EXPECT_EQ(0, std::memcmp(its_impl->get_data(), app.data(), app.size()));

    auto its_sequence = vsomeip_v3::compose_e2e_stripped_sequence(frame, checked);
    ASSERT_NE(its_sequence, nullptr);
    ASSERT_EQ(its_sequence->segments().size(), 2U);

    const auto& hdr_seg = its_sequence->segments()[0];
    const auto& app_seg = its_sequence->segments()[1];
    EXPECT_EQ(hdr_seg.size(), VSOMEIP_FULL_HEADER_SIZE);
    EXPECT_NE(hdr_seg.buffer.get(), frame.buffer.get()); // owned patched header copy
    EXPECT_EQ(app_seg.buffer.get(), frame.buffer.get()); // app pin
    EXPECT_EQ(app_seg.size(), app.size());
    EXPECT_EQ(0, std::memcmp(app_seg.data(), app.data(), app.size()));

    const uint32_t expected_length_field = static_cast<uint32_t>(VSOMEIP_FULL_HEADER_SIZE + app.size() - 8U);
    EXPECT_EQ(bithelper::read_uint32_be(hdr_seg.data() + VSOMEIP_LENGTH_POS_MIN), expected_length_field);

    auto its_message = vsomeip_v3::build_message_from_check_result(frame, checked);
    ASSERT_NE(its_message, nullptr);
    EXPECT_EQ(its_message->get_service(), k_service);
    EXPECT_EQ(its_message->get_method(), k_method);
    auto msg_payload = std::dynamic_pointer_cast<payload_impl>(its_message->get_payload());
    ASSERT_NE(msg_payload, nullptr);
    EXPECT_EQ(msg_payload->get_buffer().get(), frame.buffer.get());
}
