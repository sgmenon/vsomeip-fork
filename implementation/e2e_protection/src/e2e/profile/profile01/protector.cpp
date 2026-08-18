// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <mutex>

#include "../../../../include/e2e/profile/profile01/protector.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile01 {

protect_result protector::protect_parts(buffer_view _app_payload, instance_t _instance) {
    (void)_instance;
    std::lock_guard<std::mutex> lock(protect_mutex_);

    const std::size_t total = static_cast<std::size_t>(config_.data_length_ / 8) + 1U;
    if (_app_payload.data_length() > total) {
        return protect_result{};
    }

    e2e_buffer packed(total, 0);
    if (!profile_01::is_buffer_length_valid(config_, packed)) {
        return protect_result{};
    }
    std::copy(_app_payload.begin(), _app_payload.end(), packed.begin() + (total - _app_payload.data_length()));
    write_counter(packed);
    write_data_id(packed);
    write_crc(packed, profile_01::compute_crc(config_, packed));
    increment_counter();

    protect_result its_result;
    its_result.app_payload = std::make_shared<e2e_buffer>(std::move(packed));
    its_result.valid = true;
    return its_result;
}

/** @req [SRS_E2E_08528] */
void protector::write_counter(e2e_buffer& _buffer) {
    if (config_.counter_offset_ % 8 == 0) {
        // write write counter value into low nibble
        _buffer[config_.counter_offset_ / 8] = static_cast<uint8_t>((_buffer[config_.counter_offset_ / 8] & 0xF0) | (counter_ & 0x0F));
    } else {
        // write counter into high nibble
        _buffer[config_.counter_offset_ / 8] =
                static_cast<uint8_t>((_buffer[config_.counter_offset_ / 8] & 0x0F) | ((counter_ << 4) & 0xF0));
    }
}

/** @req [SRS_E2E_08528] */
void protector::write_data_id(e2e_buffer& _buffer) {
    if (config_.data_id_mode_ == p01_data_id_mode::E2E_P01_DATAID_NIBBLE) {
        if (config_.data_id_nibble_offset_ % 8 == 0) {
            // write low nibble of high byte of Data ID
            _buffer[config_.data_id_nibble_offset_ / 8] =
                    static_cast<uint8_t>((_buffer[config_.data_id_nibble_offset_ / 8] & 0xF0) | ((config_.data_id_ >> 8) & 0x0F));
        } else {
            // write low nibble of high byte of Data ID
            _buffer[config_.data_id_nibble_offset_ / 8] =
                    static_cast<uint8_t>((_buffer[config_.data_id_nibble_offset_ / 8] & 0x0F) | ((config_.data_id_ >> 4) & 0xF0));
        }
    }
}

/** @req [SRS_E2E_08528] */
void protector::write_crc(e2e_buffer& _buffer, uint8_t _computed_crc) {
    _buffer[config_.crc_offset_] = _computed_crc;
}

/** @req [SWS_E2E_00075] */
void protector::increment_counter(void) {
    counter_ = static_cast<uint8_t>((counter_ + 1U) % 15);
}

} // namespace profile01
} // namespace e2e
} // namespace vsomeip_v3
