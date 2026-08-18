// Copyright (C) 2026 GM GLOBAL TECHNOLOGY OPERATIONS LLC ALL RIGHTS RESERVED.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../../../implementation/e2e_protection/include/crc/crc.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/protect_result.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile_interface/checker.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile_interface/protector.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile01/checker.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile01/protector.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile04/checker.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile04/protector.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile05/checker.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile05/protector.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile07/checker.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile07/protector.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile_custom/checker.hpp"
#include "../../../implementation/e2e_protection/include/e2e/profile/profile_custom/protector.hpp"

namespace {

using vsomeip_v3::buffer_view;
using vsomeip_v3::e2e_buffer;
using vsomeip_v3::e2e::protect_result;
using vsomeip_v3::e2e::profile_interface::generic_check_status;

constexpr vsomeip_v3::instance_t k_instance = 0x0001;

e2e_buffer make_app_payload(std::size_t _n, std::uint8_t _seed = 0x10) {
    e2e_buffer payload(_n);
    for (std::size_t i = 0; i < _n; ++i) {
        payload[i] = static_cast<std::uint8_t>(_seed + i);
    }
    return payload;
}

e2e_buffer flatten(const protect_result& _parts) {
    e2e_buffer out;
    auto append = [&](const std::shared_ptr<e2e_buffer>& _buf) {
        if (_buf) {
            out.insert(out.end(), _buf->begin(), _buf->end());
        }
    };
    append(_parts.e2e_header);
    append(_parts.app_payload);
    append(_parts.e2e_footer);
    return out;
}

void expect_scatter(const protect_result& _parts, std::size_t _header_size, const e2e_buffer& _app, std::size_t _footer_size = 0) {
    ASSERT_TRUE(_parts.valid);
    ASSERT_NE(_parts.e2e_header, nullptr);
    ASSERT_NE(_parts.app_payload, nullptr);
    EXPECT_EQ(_parts.e2e_header->size(), _header_size);
    EXPECT_EQ(*_parts.app_payload, _app);
    if (_footer_size == 0) {
        EXPECT_TRUE(!_parts.e2e_footer || _parts.e2e_footer->empty());
    } else {
        ASSERT_NE(_parts.e2e_footer, nullptr);
        EXPECT_EQ(_parts.e2e_footer->size(), _footer_size);
    }
    EXPECT_EQ(_parts.size(), _header_size + _app.size() + _footer_size);
}

template<typename Checker>
void expect_check_ok(Checker& _checker, const e2e_buffer& _protected, vsomeip_v3::instance_t _instance = k_instance) {
    vsomeip_v3::e2e::profile_interface::check_status_t status = generic_check_status::E2E_ERROR;
    _checker.check(_protected, _instance, status);
    EXPECT_EQ(status, generic_check_status::E2E_OK);
}

TEST(protect_result_test, size_sums_header_payload_and_footer) {
    protect_result parts;
    parts.e2e_header = std::make_shared<e2e_buffer>(12, 0);
    parts.app_payload = std::make_shared<e2e_buffer>(8, 0xAB);
    parts.e2e_footer = std::make_shared<e2e_buffer>(16, 0xCD);
    EXPECT_EQ(parts.size(), 36U);
}

TEST(protect_parts_profile04, offset_zero_scatters_header_and_payload) {
    vsomeip_v3::e2e::profile04::profile_config config(/*data_id=*/0x2d, /*offset=*/0, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile04::protector protector(config);
    vsomeip_v3::e2e::profile04::profile_04_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, 12, app);
    expect_check_ok(checker, flatten(parts));
}

TEST(protect_parts_profile04, nonzero_offset_padding_lives_in_header) {
    constexpr std::size_t offset = 8;
    vsomeip_v3::e2e::profile04::profile_config config(/*data_id=*/0x2d, offset, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile04::protector protector(config);
    vsomeip_v3::e2e::profile04::profile_04_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, offset + 12, app);
    for (std::size_t i = 0; i < offset; ++i) {
        EXPECT_EQ((*parts.e2e_header)[i], 0);
    }
    expect_check_ok(checker, flatten(parts));
}

TEST(protect_parts_profile05, offset_zero_scatters_header_and_payload) {
    // data_length is bits; (56/8)+1=8 must be <= protected size (3+payload).
    vsomeip_v3::e2e::profile05::profile_config config(/*data_id=*/0x2d, /*data_length=*/56, /*offset=*/0,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile05::protector protector(config);
    vsomeip_v3::e2e::profile05::profile_05_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, 3, app);
    expect_check_ok(checker, flatten(parts));
}

TEST(protect_parts_profile05, nonzero_offset_padding_lives_in_header) {
    constexpr std::size_t offset = 4;
    vsomeip_v3::e2e::profile05::profile_config config(/*data_id=*/0x2d, /*data_length=*/56, offset,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile05::protector protector(config);
    vsomeip_v3::e2e::profile05::profile_05_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, offset + 3, app);
    expect_check_ok(checker, flatten(parts));
}

TEST(protect_parts_profile07, offset_zero_scatters_header_and_payload) {
    vsomeip_v3::e2e::profile07::profile_config config(/*data_id=*/0x2d, /*offset=*/0, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffffffffu);
    vsomeip_v3::e2e::profile07::protector protector(config);
    vsomeip_v3::e2e::profile07::profile_07_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, 20, app);
    expect_check_ok(checker, flatten(parts));
}

TEST(protect_parts_profile07, nonzero_offset_padding_lives_in_header) {
    constexpr std::size_t offset = 8;
    vsomeip_v3::e2e::profile07::profile_config config(/*data_id=*/0x2d, offset, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffffffffu);
    vsomeip_v3::e2e::profile07::protector protector(config);
    vsomeip_v3::e2e::profile07::profile_07_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, offset + 20, app);
    expect_check_ok(checker, flatten(parts));
}

TEST(protect_parts_profile_custom, crc_offset_zero_scatters_header_and_payload) {
    vsomeip_v3::e2e::profile_custom::profile_config config(/*crc_offset=*/0);
    vsomeip_v3::e2e::profile_custom::protector protector(config);
    vsomeip_v3::e2e::profile_custom::profile_custom_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, 4, app);
    expect_check_ok(checker, flatten(parts));
}

TEST(protect_parts_profile_custom, nonzero_crc_offset_padding_lives_in_header) {
    constexpr std::uint16_t crc_offset = 8;
    vsomeip_v3::e2e::profile_custom::profile_config config(crc_offset);
    vsomeip_v3::e2e::profile_custom::protector protector(config);
    vsomeip_v3::e2e::profile_custom::profile_custom_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, crc_offset + 4, app);
    expect_check_ok(checker, flatten(parts));
}

TEST(protect_parts_profile01, packed_data_lives_in_payload) {
    // Matches docker e2e_crc CRC8 sample: 56-bit data length, DATAID_NIBBLE.
    using vsomeip_v3::e2e::profile01::p01_data_id_mode;
    vsomeip_v3::e2e::profile01::profile_config config(/*crc_offset=*/0, /*data_id=*/0xA73, p01_data_id_mode::E2E_P01_DATAID_NIBBLE,
                                                      /*data_length=*/56, /*counter_offset=*/8, /*data_id_nibble_offset=*/12);
    vsomeip_v3::e2e::profile01::protector protector(config);
    vsomeip_v3::e2e::profile01::profile_01_checker checker(config);

    const auto app = make_app_payload(6);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    ASSERT_TRUE(parts.valid);
    EXPECT_TRUE(!parts.e2e_header || parts.e2e_header->empty());
    EXPECT_TRUE(!parts.e2e_footer || parts.e2e_footer->empty());
    ASSERT_NE(parts.app_payload, nullptr);
    EXPECT_EQ(parts.app_payload->size(), 8U);
    expect_check_ok(checker, *parts.app_payload);
}

TEST(protect_parts_profile04, invalid_when_payload_exceeds_max_length) {
    vsomeip_v3::e2e::profile04::profile_config config(/*data_id=*/0x2d, /*offset=*/0, /*min=*/0, /*max=*/16,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile04::protector protector(config);

    const auto app = make_app_payload(20); // 12 + 20 > max 16
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);
    EXPECT_FALSE(parts.valid);
}

std::shared_ptr<e2e_buffer> crc32_be(buffer_view _data) {
    const uint32_t crc = vsomeip_v3::e2e_crc::calculate_profile_custom(_data);
    auto out = std::make_shared<e2e_buffer>(4);
    (*out)[0] = static_cast<uint8_t>(crc >> 24U);
    (*out)[1] = static_cast<uint8_t>(crc >> 16U);
    (*out)[2] = static_cast<uint8_t>(crc >> 8U);
    (*out)[3] = static_cast<uint8_t>(crc);
    return out;
}

uint32_t read_crc32_be(const e2e_buffer& _buf, std::size_t _off) {
    return (static_cast<uint32_t>(_buf[_off]) << 24U) | (static_cast<uint32_t>(_buf[_off + 1U]) << 16U)
            | (static_cast<uint32_t>(_buf[_off + 2U]) << 8U) | static_cast<uint32_t>(_buf[_off + 3U]);
}

// Example profile: optional 1-byte header (counter) + payload + 4-byte CRC footer.
class crc_footer_protector : public vsomeip_v3::e2e::profile_interface::protector {
public:
    protect_result protect_parts(buffer_view _app_payload, vsomeip_v3::instance_t) override {
        protect_result parts;
        parts.e2e_header = std::make_shared<e2e_buffer>(1, counter_++);
        parts.app_payload = std::make_shared<e2e_buffer>(_app_payload.begin(), _app_payload.end());
        parts.e2e_footer = crc32_be(buffer_view(*parts.app_payload));
        parts.valid = true;
        return parts;
    }

private:
    uint8_t counter_{0};
};

class crc_footer_checker : public vsomeip_v3::e2e::profile_interface::checker {
public:
    void check(const e2e_buffer& _buffer, vsomeip_v3::instance_t,
               vsomeip_v3::e2e::profile_interface::check_status_t& _status) override {
        _status = generic_check_status::E2E_ERROR;
        if (_buffer.size() < 5) {
            return;
        }
        const uint32_t received = read_crc32_be(_buffer, _buffer.size() - 4);
        const uint32_t calculated = vsomeip_v3::e2e_crc::calculate_profile_custom(buffer_view(_buffer, 1, _buffer.size() - 4));
        _status = (received == calculated) ? generic_check_status::E2E_OK : generic_check_status::E2E_WRONG_CRC;
    }
};

TEST(protect_parts_crc_footer, scatters_header_payload_and_footer) {
    crc_footer_protector protector;
    crc_footer_checker checker;

    const auto app = make_app_payload(8);
    const auto parts = protector.protect_parts(buffer_view(app), k_instance);

    expect_scatter(parts, 1, app, 4);
    ASSERT_EQ((*parts.e2e_header)[0], 0);

    const auto wire = flatten(parts);
    expect_check_ok(checker, wire);

    ASSERT_GE(wire.size(), 5U);
    EXPECT_TRUE(std::equal(wire.begin() + 1, wire.end() - 4, app.begin()));
}

TEST(protect_parts_crc_footer, check_rejects_tampered_footer) {
    crc_footer_protector protector;
    crc_footer_checker checker;

    const auto app = make_app_payload(8);
    auto wire = flatten(protector.protect_parts(buffer_view(app), k_instance));
    ASSERT_FALSE(wire.empty());
    wire.back() ^= 0xFF;

    vsomeip_v3::e2e::profile_interface::check_status_t status = generic_check_status::E2E_ERROR;
    checker.check(wire, k_instance, status);
    EXPECT_EQ(status, generic_check_status::E2E_WRONG_CRC);
}

} // namespace
