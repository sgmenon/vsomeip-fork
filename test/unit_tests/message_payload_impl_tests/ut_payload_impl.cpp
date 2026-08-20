// Copyright (C) 2024 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include "../../../implementation/message/include/deserializer.hpp"
#include "../../../implementation/message/include/payload_impl.hpp"
#include "../../../implementation/message/include/serializer.hpp"

namespace {
const std::uint8_t array_size = 4;
const std::uint32_t buffer_shrink_threshold = 1;
const vsomeip_v3::byte_t byte1 = 1;
const vsomeip_v3::byte_t byte2 = 2;
const vsomeip_v3::byte_t byte3 = 3;
const vsomeip_v3::byte_t byte4 = 4;
}

TEST(payload_impl_test, equalequal_operator) {
    // Create test data.
    std::vector<std::uint8_t> data_vector_{byte1, byte2, byte3, byte4};

    vsomeip_v3::payload_impl its_payload_impl(data_vector_);

    std::vector<std::uint8_t> data_vector2_{byte1, byte2, byte3, byte4};

    vsomeip_v3::payload_impl its_similar_payload_impl(data_vector2_);

    std::vector<std::uint8_t> data_vector3_{byte4, byte3, byte2, byte1};

    vsomeip_v3::payload_impl its_different_payload_impl(data_vector3_);

    // Checks.
    ASSERT_TRUE(its_payload_impl.operator==(its_similar_payload_impl));
    ASSERT_FALSE(its_payload_impl.operator==(its_different_payload_impl));
}

TEST(payload_impl_test, set_data) {
    // Create test data.
    std::vector<vsomeip_v3::byte_t> data_vector_{byte1, byte2, byte3, byte4};

    std::array<std::uint8_t, array_size> data_array_{byte1, byte2, byte3, byte4};

    vsomeip_v3::payload_impl its_payload_impl1;
    vsomeip_v3::payload_impl its_payload_impl2;
    vsomeip_v3::payload_impl its_payload_impl3;

    // Test methods.
    its_payload_impl1.set_data(data_vector_);
    its_payload_impl2.set_data(data_array_.data(), data_array_.size());
    its_payload_impl3.set_data(std::move(data_vector_));

    // Checks.
    ASSERT_TRUE(its_payload_impl1.operator==(its_payload_impl2));
    ASSERT_TRUE(its_payload_impl1.operator==(its_payload_impl3));
}

TEST(payload_impl_test, constructors) {
    // Create test data.
    std::vector<std::uint8_t> data_vector_{byte1, byte2, byte3, byte4};

    std::array<std::uint8_t, array_size> data_array_{byte1, byte2, byte3, byte4};

    // Test Overloaded constructors.
    vsomeip_v3::payload_impl its_payload_impl1;
    // Add data to the empty data.
    its_payload_impl1.set_data(data_vector_);

    vsomeip_v3::payload_impl its_payload_impl2(data_vector_);
    vsomeip_v3::payload_impl its_payload_impl3(data_array_.data(), data_array_.size());
    vsomeip_v3::payload_impl its_payload_impl4(its_payload_impl1);

    // Checks.
    ASSERT_TRUE(its_payload_impl1.operator==(its_payload_impl2));
    ASSERT_TRUE(its_payload_impl1.operator==(its_payload_impl3));
    ASSERT_TRUE(its_payload_impl1.operator==(its_payload_impl4));
}

TEST(payload_impl_test, get_length) {
    // Create test data.
    std::array<std::uint8_t, array_size> data_array_{byte1, byte2, byte3, byte4};

    std::unique_ptr<vsomeip_v3::payload_impl> its_payload_impl(new vsomeip_v3::payload_impl(data_array_.data(), data_array_.size()));

    // Test method.
    ASSERT_EQ(its_payload_impl->get_length(), array_size);
}

TEST(payload_impl_test, serialize) {
    // Create test data.
    std::array<std::uint8_t, array_size> data_array_{byte1, byte2, byte3, byte4};

    std::unique_ptr<vsomeip_v3::payload_impl> its_payload_impl(new vsomeip_v3::payload_impl(data_array_.data(), data_array_.size()));
    vsomeip_v3::serializer its_serializer(buffer_shrink_threshold);

    // Test method.
    ASSERT_TRUE(its_payload_impl->serialize(&its_serializer));

    // Checks.
    ASSERT_EQ(its_payload_impl->get_data()[0], its_serializer.get_data()[0]);
    ASSERT_EQ(its_payload_impl->get_data()[1], its_serializer.get_data()[1]);
    ASSERT_EQ(its_payload_impl->get_data()[2], its_serializer.get_data()[2]);
    ASSERT_EQ(its_payload_impl->get_data()[3], its_serializer.get_data()[3]);
}

TEST(payload_impl_test, pin_slice) {
    auto its_buffer = std::make_shared<std::vector<vsomeip_v3::byte_t>>(std::vector<vsomeip_v3::byte_t>{0xAA, byte1, byte2, byte3, byte4, 0xBB});
    const std::size_t its_offset = 1;
    const std::size_t its_length = 4;

    vsomeip_v3::payload_impl its_pinned(its_buffer, its_offset, its_length);

    ASSERT_EQ(its_pinned.get_length(), its_length);
    ASSERT_EQ(its_pinned.get_data(), its_buffer->data() + its_offset);
    ASSERT_EQ(its_pinned.get_data()[0], byte1);
    ASSERT_EQ(its_pinned.get_data()[3], byte4);

    // Copy constructor materializes independent storage.
    vsomeip_v3::payload_impl its_copy(its_pinned);
    ASSERT_TRUE(its_pinned.operator==(its_copy));
    ASSERT_NE(its_copy.get_data(), its_pinned.get_data());

    vsomeip_v3::serializer its_serializer(buffer_shrink_threshold);
    ASSERT_TRUE(its_pinned.serialize(&its_serializer));
    ASSERT_EQ(its_serializer.get_data()[0], byte1);
    ASSERT_EQ(its_serializer.get_data()[3], byte4);

    // set_data replaces the backing store with a fresh owned buffer.
    its_pinned.set_data(std::vector<vsomeip_v3::byte_t>{byte4, byte3});
    ASSERT_EQ(its_pinned.get_length(), 2u);
    ASSERT_EQ(its_pinned.get_data()[0], byte4);
    ASSERT_NE(its_pinned.get_data(), its_buffer->data() + its_offset);
}
