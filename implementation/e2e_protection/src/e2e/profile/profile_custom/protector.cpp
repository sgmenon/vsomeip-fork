// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <memory>
#include <mutex>

#include "../../../../include/crc/crc.hpp"
#include "../../../../include/e2e/profile/profile_custom/protector.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile_custom {

protect_result protector::protect(buffer_view _app_payload, instance_t _instance) {
    (void)_instance;

    std::lock_guard<std::mutex> lock(protect_mutex_);

    const uint32_t its_crc = e2e_crc::calculate_profile_custom(_app_payload);

    auto its_header = std::make_shared<e2e_buffer>(static_cast<std::size_t>(config_.crc_offset_) + 4U, 0);
    (*its_header)[config_.crc_offset_] = static_cast<uint8_t>(its_crc >> 24U);
    (*its_header)[config_.crc_offset_ + 1U] = static_cast<uint8_t>(its_crc >> 16U);
    (*its_header)[config_.crc_offset_ + 2U] = static_cast<uint8_t>(its_crc >> 8U);
    (*its_header)[config_.crc_offset_ + 3U] = static_cast<uint8_t>(its_crc);

    protect_result its_result;
    its_result.e2e_header = std::move(its_header);
    its_result.app_payload = _app_payload;
    its_result.valid = true;
    return its_result;
}

} // namespace profile_custom
} // namespace e2e
} // namespace vsomeip_v3
