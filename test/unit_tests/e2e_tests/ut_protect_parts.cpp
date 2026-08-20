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
#include "../../../implementation/message/include/payload_impl.hpp"
#include "../../../implementation/endpoints/include/buffer.hpp"
#include "../../../implementation/routing/include/payload_ownership.hpp"

namespace {

using vsomeip_v3::buffer_view;
using vsomeip_v3::e2e_buffer;
using vsomeip_v3::e2e::check_result;
using vsomeip_v3::e2e::make_check_result;
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
    auto append_owned = [&](const std::shared_ptr<e2e_buffer>& _buf) {
        if (_buf) {
            out.insert(out.end(), _buf->begin(), _buf->end());
        }
    };
    append_owned(_parts.e2e_header);
    if (_parts.owned_app_payload) {
        append_owned(_parts.owned_app_payload);
    } else {
        out.insert(out.end(), _parts.app_payload.begin(), _parts.app_payload.end());
    }
    append_owned(_parts.e2e_footer);
    return out;
}

void expect_scatter(const protect_result& _parts, std::size_t _header_size, const e2e_buffer& _app, std::size_t _footer_size = 0) {
    ASSERT_TRUE(_parts.valid);
    ASSERT_NE(_parts.e2e_header, nullptr);
    EXPECT_EQ(_parts.e2e_header->size(), _header_size);
    EXPECT_EQ(_parts.app_payload.size(), _app.size());
    EXPECT_TRUE(std::equal(_parts.app_payload.begin(), _parts.app_payload.end(), _app.begin()));
    EXPECT_TRUE(!_parts.owned_app_payload);
    if (_footer_size == 0) {
        EXPECT_TRUE(!_parts.e2e_footer || _parts.e2e_footer->empty());
    } else {
        ASSERT_NE(_parts.e2e_footer, nullptr);
        EXPECT_EQ(_parts.e2e_footer->size(), _footer_size);
    }
    EXPECT_EQ(_parts.size(), _header_size + _app.size() + _footer_size);
}

void expect_check_sections(const check_result& _checked, const e2e_buffer& _protected, std::size_t _header_size, const e2e_buffer& _app,
                           std::size_t _footer_size = 0) {
    EXPECT_EQ(_checked.e2e_header.size(), _header_size);
    EXPECT_EQ(_checked.app_payload.size(), _app.size());
    EXPECT_EQ(_checked.e2e_footer.size(), _footer_size);
    EXPECT_TRUE(std::equal(_checked.app_payload.begin(), _checked.app_payload.end(), _app.begin()));

    if (!_checked.e2e_header.empty()) {
        EXPECT_EQ(_checked.e2e_header.data(), _protected.data());
    } else if (!_checked.app_payload.empty()) {
        EXPECT_EQ(_checked.app_payload.data(), _protected.data());
    }
    if (!_checked.e2e_footer.empty()) {
        EXPECT_EQ(_checked.e2e_footer.data(), _protected.data() + _protected.size() - _footer_size);
    }
}

template<typename Checker>
void expect_check_ok(Checker& _checker, const e2e_buffer& _protected, std::size_t _header_size, const e2e_buffer& _app,
                     std::size_t _footer_size = 0, vsomeip_v3::instance_t _instance = k_instance) {
    const auto checked = _checker.check(buffer_view(_protected), _instance);
    EXPECT_EQ(checked.status, generic_check_status::E2E_OK);
    expect_check_sections(checked, _protected, _header_size, _app, _footer_size);
}

TEST(protect_result_test, size_sums_header_payload_and_footer) {
    const std::vector<uint8_t> app_bytes(8, 0xAB);
    protect_result parts;
    parts.e2e_header = std::make_shared<e2e_buffer>(12, 0);
    parts.app_payload = vsomeip_v3::span<const uint8_t>(app_bytes);
    parts.e2e_footer = std::make_shared<e2e_buffer>(16, 0xCD);
    EXPECT_EQ(parts.size(), 36U);
}

TEST(protect_profile04, offset_zero_scatters_header_and_payload) {
    vsomeip_v3::e2e::profile04::profile_config config(/*data_id=*/0x2d, /*offset=*/0, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile04::protector protector(config);
    vsomeip_v3::e2e::profile04::profile_04_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, 12, app);
    expect_check_ok(checker, flatten(parts), 12, app);
}

TEST(protect_profile04, nonzero_offset_padding_lives_in_header) {
    constexpr std::size_t offset = 8;
    vsomeip_v3::e2e::profile04::profile_config config(/*data_id=*/0x2d, offset, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile04::protector protector(config);
    vsomeip_v3::e2e::profile04::profile_04_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, offset + 12, app);
    for (std::size_t i = 0; i < offset; ++i) {
        EXPECT_EQ((*parts.e2e_header)[i], 0);
    }
    expect_check_ok(checker, flatten(parts), offset + 12, app);
}

TEST(protect_profile05, offset_zero_scatters_header_and_payload) {
    vsomeip_v3::e2e::profile05::profile_config config(/*data_id=*/0x2d, /*data_length=*/56, /*offset=*/0,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile05::protector protector(config);
    vsomeip_v3::e2e::profile05::profile_05_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, 3, app);
    expect_check_ok(checker, flatten(parts), 3, app);
}

TEST(protect_profile05, nonzero_offset_padding_lives_in_header) {
    constexpr std::size_t offset = 4;
    vsomeip_v3::e2e::profile05::profile_config config(/*data_id=*/0x2d, /*data_length=*/56, offset,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile05::protector protector(config);
    vsomeip_v3::e2e::profile05::profile_05_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, offset + 3, app);
    expect_check_ok(checker, flatten(parts), offset + 3, app);
}

TEST(protect_profile07, offset_zero_scatters_header_and_payload) {
    vsomeip_v3::e2e::profile07::profile_config config(/*data_id=*/0x2d, /*offset=*/0, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffffffffu);
    vsomeip_v3::e2e::profile07::protector protector(config);
    vsomeip_v3::e2e::profile07::profile_07_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, 20, app);
    expect_check_ok(checker, flatten(parts), 20, app);
}

TEST(protect_profile07, nonzero_offset_padding_lives_in_header) {
    constexpr std::size_t offset = 8;
    vsomeip_v3::e2e::profile07::profile_config config(/*data_id=*/0x2d, offset, /*min=*/0, /*max=*/0xffff,
                                                      /*max_delta=*/0xffffffffu);
    vsomeip_v3::e2e::profile07::protector protector(config);
    vsomeip_v3::e2e::profile07::profile_07_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, offset + 20, app);
    expect_check_ok(checker, flatten(parts), offset + 20, app);
}

TEST(protect_profile_custom, crc_offset_zero_scatters_header_and_payload) {
    vsomeip_v3::e2e::profile_custom::profile_config config(/*crc_offset=*/0);
    vsomeip_v3::e2e::profile_custom::protector protector(config);
    vsomeip_v3::e2e::profile_custom::profile_custom_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, 4, app);
    expect_check_ok(checker, flatten(parts), 4, app);
}

TEST(protect_profile_custom, nonzero_crc_offset_padding_lives_in_header) {
    constexpr std::uint16_t crc_offset = 8;
    vsomeip_v3::e2e::profile_custom::profile_config config(crc_offset);
    vsomeip_v3::e2e::profile_custom::protector protector(config);
    vsomeip_v3::e2e::profile_custom::profile_custom_checker checker(config);

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, crc_offset + 4, app);
    expect_check_ok(checker, flatten(parts), crc_offset + 4, app);
}

TEST(protect_profile01, packed_data_lives_in_payload) {
    using vsomeip_v3::e2e::profile01::p01_data_id_mode;
    vsomeip_v3::e2e::profile01::profile_config config(/*crc_offset=*/0, /*data_id=*/0xA73, p01_data_id_mode::E2E_P01_DATAID_NIBBLE,
                                                      /*data_length=*/56, /*counter_offset=*/8, /*data_id_nibble_offset=*/12);
    vsomeip_v3::e2e::profile01::protector protector(config);
    vsomeip_v3::e2e::profile01::profile_01_checker checker(config);

    const auto app = make_app_payload(6);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    ASSERT_TRUE(parts.valid);
    EXPECT_TRUE(!parts.e2e_header || parts.e2e_header->empty());
    EXPECT_TRUE(!parts.e2e_footer || parts.e2e_footer->empty());
    ASSERT_NE(parts.owned_app_payload, nullptr);
    EXPECT_EQ(parts.owned_app_payload->size(), 8U);
    EXPECT_EQ(parts.app_payload.size(), 8U);

    constexpr std::size_t p01_header = 2;
    expect_check_ok(checker, *parts.owned_app_payload, p01_header, app);
}

TEST(protect_profile04, invalid_when_payload_exceeds_max_length) {
    vsomeip_v3::e2e::profile04::profile_config config(/*data_id=*/0x2d, /*offset=*/0, /*min=*/0, /*max=*/16,
                                                      /*max_delta=*/0xffff);
    vsomeip_v3::e2e::profile04::protector protector(config);

    const auto app = make_app_payload(20);
    const auto parts = protector.protect(buffer_view(app), k_instance);
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

uint32_t read_crc32_be(buffer_view _buf, std::size_t _off) {
    return (static_cast<uint32_t>(_buf[_off]) << 24U) | (static_cast<uint32_t>(_buf[_off + 1U]) << 16U)
            | (static_cast<uint32_t>(_buf[_off + 2U]) << 8U) | static_cast<uint32_t>(_buf[_off + 3U]);
}

class crc_footer_protector : public vsomeip_v3::e2e::profile_interface::protector {
public:
    protect_result protect(buffer_view _app_payload, vsomeip_v3::instance_t) override {
        protect_result parts;
        parts.e2e_header = std::make_shared<e2e_buffer>(1, counter_++);
        parts.app_payload = _app_payload;
        parts.e2e_footer = crc32_be(_app_payload);
        parts.valid = true;
        return parts;
    }

private:
    uint8_t counter_{0};
};

class crc_footer_checker : public vsomeip_v3::e2e::profile_interface::checker {
public:
    check_result check(buffer_view _buffer, vsomeip_v3::instance_t) override {
        auto status = generic_check_status::E2E_ERROR;
        if (_buffer.size() >= 5) {
            const uint32_t received = read_crc32_be(_buffer, _buffer.size() - 4);
            const uint32_t calculated = vsomeip_v3::e2e_crc::calculate_profile_custom(_buffer.subspan(1, _buffer.size() - 5));
            status = (received == calculated) ? generic_check_status::E2E_OK : generic_check_status::E2E_WRONG_CRC;
        }
        return make_check_result(status, _buffer, 1, 4);
    }
};

TEST(protect_crc_footer, scatters_header_payload_and_footer) {
    crc_footer_protector protector;
    crc_footer_checker checker;

    const auto app = make_app_payload(8);
    const auto parts = protector.protect(buffer_view(app), k_instance);

    expect_scatter(parts, 1, app, 4);
    ASSERT_EQ((*parts.e2e_header)[0], 0);

    const auto wire = flatten(parts);
    expect_check_ok(checker, wire, 1, app, 4);

    ASSERT_GE(wire.size(), 5U);
    EXPECT_TRUE(std::equal(wire.begin() + 1, wire.end() - 4, app.begin()));
}

TEST(protect_crc_footer, check_rejects_tampered_footer) {
    crc_footer_protector protector;
    crc_footer_checker checker;

    const auto app = make_app_payload(8);
    auto wire = flatten(protector.protect(buffer_view(app), k_instance));
    ASSERT_FALSE(wire.empty());
    wire.back() ^= 0xFF;

    const auto checked = checker.check(buffer_view(wire), k_instance);
    EXPECT_EQ(checked.status, generic_check_status::E2E_WRONG_CRC);
    expect_check_sections(checked, wire, 1, app, 4);
}

TEST(buffer_sequence_test, append_payload_pins_without_copy) {
    vsomeip_v3::buffer_sequence seq;
    auto owned = std::make_shared<vsomeip_v3::message_buffer_t>(std::vector<vsomeip_v3::byte_t>{1, 2, 3});
    seq.append(std::move(owned));

    auto payload = std::make_shared<vsomeip_v3::payload_impl>(std::vector<vsomeip_v3::byte_t>{0x10, 0x11, 0x12});
    const auto* payload_bytes = payload->get_data();
    vsomeip_v3::append_message_payload(seq, payload);

    EXPECT_EQ(seq.size(), 6U);
    EXPECT_EQ(seq.segments().size(), 2U);
    EXPECT_EQ(seq.segments()[1].data(), payload_bytes);
    EXPECT_EQ(seq.flatten()->size(), 6U);
}

} // namespace
