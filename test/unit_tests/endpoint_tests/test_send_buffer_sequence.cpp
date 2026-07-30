// Copyright (C) 2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <cstdint>

#include "../../../implementation/endpoints/include/buffer.hpp"
#include <vsomeip/span.hpp>

#if defined(__has_include)
#if __has_include(<boost/core/span.hpp>)
#include <boost/core/span.hpp>
#include <type_traits>
static_assert(std::is_same<vsomeip_v3::span<const std::uint8_t>, boost::span<const std::uint8_t>>::value,
              "vsomeip span should map to boost::span when available");
#endif
#endif

namespace {

TEST(send_buffer_sequence_test, flatten_single_buffer_reuses_storage) {
    auto payload = std::make_shared<vsomeip_v3::message_buffer_t>();
    payload->push_back(0x11);
    payload->push_back(0x22);

    vsomeip_v3::send_buffer_sequence sequence(payload);
    auto flat = sequence.flatten();

    EXPECT_EQ(flat.get(), payload.get());
    ASSERT_EQ(flat->size(), 2U);
    EXPECT_EQ((*flat)[0], 0x11);
    EXPECT_EQ((*flat)[1], 0x22);
}

TEST(send_buffer_sequence_test, flatten_multi_buffer_concatenates) {
    auto a = std::make_shared<vsomeip_v3::message_buffer_t>();
    a->push_back(0xAA);
    a->push_back(0xBB);
    auto b = std::make_shared<vsomeip_v3::message_buffer_t>();
    b->push_back(0xCC);

    vsomeip_v3::send_buffer_sequence sequence;
    sequence.append(a);
    sequence.append(b);

    auto flat = sequence.flatten();
    ASSERT_NE(flat, nullptr);
    EXPECT_NE(flat.get(), a.get());
    ASSERT_EQ(flat->size(), 3U);
    EXPECT_EQ((*flat)[0], 0xAA);
    EXPECT_EQ((*flat)[1], 0xBB);
    EXPECT_EQ((*flat)[2], 0xCC);
}

TEST(send_buffer_sequence_test, read_uint16_be_handles_cross_buffer) {
    auto a = std::make_shared<vsomeip_v3::message_buffer_t>();
    a->push_back(0x12);
    auto b = std::make_shared<vsomeip_v3::message_buffer_t>();
    b->push_back(0x34);
    b->push_back(0x56);

    vsomeip_v3::send_buffer_sequence sequence;
    sequence.append(a);
    sequence.append(b);

    std::uint16_t value = 0;
    ASSERT_TRUE(sequence.read_uint16_be(0U, value));
    EXPECT_EQ(value, 0x1234);

    ASSERT_TRUE(sequence.read_uint16_be(1U, value));
    EXPECT_EQ(value, 0x3456);
}

} // namespace
