// Copyright (C) 2023 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <memory>
#include <mutex>

#include "../../../../include/crc/crc.hpp"
#include "../../../../include/e2e/profile/profile07/protector.hpp"
#include "../../../../../utility/include/bithelper.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile07 {

protect_result protector::protect_parts(buffer_view _app_payload, instance_t _instance) {
    std::lock_guard<std::mutex> lock(protect_mutex_);

    const std::size_t header_size = config_.offset_ + 20;
    const std::size_t protected_size = header_size + _app_payload.data_length();
    if (protected_size < config_.min_data_length_ || protected_size > config_.max_data_length_) {
        return protect_result{};
    }

    auto its_header = std::make_shared<e2e_buffer>(header_size, 0);
    bithelper::write_uint32_be(static_cast<uint16_t>(protected_size), &(*its_header)[config_.offset_ + PROFILE_07_SIZE_OFFSET]);
    bithelper::write_uint32_be(get_counter(_instance), &(*its_header)[config_.offset_ + PROFILE_07_COUNTER_OFFSET]);
    bithelper::write_uint32_be(config_.data_id_, &(*its_header)[config_.offset_ + PROFILE_07_DATAID_OFFSET]);

    uint64_t its_crc = e2e_crc::calculate_profile_07(buffer_view(*its_header, config_.offset_));
    e2e_buffer its_after;
    its_after.insert(its_after.end(), its_header->begin() + config_.offset_ + PROFILE_07_SIZE_OFFSET, its_header->end());
    its_after.insert(its_after.end(), _app_payload.begin(), _app_payload.end());
    its_crc = e2e_crc::calculate_profile_07(buffer_view(its_after), its_crc);
    bithelper::write_uint64_be(its_crc, &(*its_header)[config_.offset_ + PROFILE_07_CRC_OFFSET]);

    increment_counter(_instance);

    protect_result its_result;
    its_result.e2e_header = std::move(its_header);
    its_result.app_payload = std::make_shared<e2e_buffer>(_app_payload.begin(), _app_payload.end());
    its_result.valid = true;
    return its_result;
}

uint32_t protector::get_counter(instance_t _instance) const {

    uint32_t its_counter(0);

    auto find_counter = counter_.find(_instance);
    if (find_counter != counter_.end())
        its_counter = find_counter->second;

    return its_counter;
}

void protector::increment_counter(instance_t _instance) {

    auto find_counter = counter_.find(_instance);
    if (find_counter != counter_.end())
        find_counter->second++;
    else
        counter_[_instance] = 1;
}

} // namespace profile07
} // namespace e2e
} // namespace vsomeip_v3
