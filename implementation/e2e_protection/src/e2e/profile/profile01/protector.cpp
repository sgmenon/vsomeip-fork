// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <iomanip>
#include <algorithm>
#include <memory>
#include <mutex>

#include <vsomeip/internal/logger.hpp>
#include "../../../../include/e2e/profile/profile01/protector.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile01 {

/** @req [SWS_E2E_00195] */
void protector::protect(e2e_buffer& _buffer, instance_t _instance) {

    (void)_instance;

    std::lock_guard<std::mutex> lock(protect_mutex_);

    if (profile_01::is_buffer_length_valid(config_, _buffer)) {
        // write the current Counter value in Data
        write_counter(_buffer);

        // write DataID nibble in Data (E2E_P01_DATAID_NIBBLE) in Data
        write_data_id(_buffer);

        // compute the CRC over DataID and Data
        uint8_t computed_crc = profile_01::compute_crc(config_, _buffer);
        // write CRC in Data
        write_crc(_buffer, computed_crc);

        // increment the Counter (new value will be used in the next invocation of
        // E2E_P01Protect()),
        increment_counter();
    }
}

protect_result protector::protect_parts(buffer_view _app_payload, instance_t _instance) {
    (void)_instance;

    // AUTOSAR data_length is in bits; is_buffer_length_valid requires (data_length/8)+1 bytes.
    const std::size_t total = static_cast<std::size_t>(config_.data_length_ / 8) + 1U;
    if (_app_payload.data_length() > total) {
        return protect_result{};
    }

    e2e_buffer its_buffer(total, 0);
    std::copy(_app_payload.begin(), _app_payload.end(), its_buffer.begin() + (total - _app_payload.data_length()));
    protect(its_buffer, _instance);

    protect_result its_result;
    its_result.contiguous = std::make_shared<e2e_buffer>(std::move(its_buffer));
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
