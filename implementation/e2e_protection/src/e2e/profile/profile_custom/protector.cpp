// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <memory>
#include <mutex>

#include "../../../../include/crc/crc.hpp"
#include "../../../../include/e2e/profile/profile_custom/protector.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile_custom {

void protector::protect(e2e_buffer& _buffer, instance_t _instance) {

    (void)_instance;

    std::lock_guard<std::mutex> lock(protect_mutex_);

    if (profile_custom::is_buffer_length_valid(config_, _buffer)) {
        // compute the CRC over DataID and Data
        uint32_t computed_crc = profile_custom::compute_crc(config_, _buffer);
        // write CRC in Data
        write_crc(_buffer, computed_crc);
    }
}

protect_result protector::protect_parts(buffer_view _app_payload, instance_t _instance) {
    (void)_instance;

    std::unique_lock<std::mutex> lock(protect_mutex_);

    if (config_.crc_offset_ != 0) {
        e2e_buffer its_buffer(config_.crc_offset_ + 4 + _app_payload.data_length(), 0);
        std::copy(_app_payload.begin(), _app_payload.end(), its_buffer.begin() + config_.crc_offset_ + 4);
        lock.unlock();
        protect(its_buffer, _instance);

        protect_result its_result;
        its_result.contiguous = std::make_shared<e2e_buffer>(std::move(its_buffer));
        its_result.valid = true;
        return its_result;
    }

    auto its_payload = std::make_shared<e2e_buffer>(_app_payload.begin(), _app_payload.end());
    const uint32_t its_crc = e2e_crc::calculate_profile_custom(buffer_view(*its_payload));

    auto its_header = std::make_shared<e2e_buffer>(4, 0);
    (*its_header)[0] = static_cast<uint8_t>(its_crc >> 24U);
    (*its_header)[1] = static_cast<uint8_t>(its_crc >> 16U);
    (*its_header)[2] = static_cast<uint8_t>(its_crc >> 8U);
    (*its_header)[3] = static_cast<uint8_t>(its_crc);

    protect_result its_result;
    its_result.e2e_header = its_header;
    its_result.app_payload = its_payload;
    its_result.valid = true;
    return its_result;
}

void protector::write_crc(e2e_buffer& _buffer, uint32_t _computed_crc) {
    _buffer[config_.crc_offset_] = static_cast<uint8_t>(_computed_crc >> 24U);
    _buffer[config_.crc_offset_ + 1U] = static_cast<uint8_t>(_computed_crc >> 16U);
    _buffer[config_.crc_offset_ + 2U] = static_cast<uint8_t>(_computed_crc >> 8U);
    _buffer[config_.crc_offset_ + 3U] = static_cast<uint8_t>(_computed_crc);
}

} // namespace profile_custom
} // namespace e2e
} // namespace vsomeip_v3
