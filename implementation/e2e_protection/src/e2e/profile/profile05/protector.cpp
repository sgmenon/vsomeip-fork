// Copyright (C) 2020-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <iomanip>
#include <algorithm>
#include <memory>
#include <mutex>

#include <vsomeip/internal/logger.hpp>
#include "../../../../include/crc/crc.hpp"
#include "../../../../include/e2e/profile/profile05/protector.hpp"
#include "../../../../../utility/include/bithelper.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile05 {

void protector::protect(e2e_buffer& _buffer, instance_t _instance) {

    (void)_instance;

    std::lock_guard<std::mutex> lock(protect_mutex_);

    if (_instance > VSOMEIP_E2E_PROFILE05_MAX_INSTANCE) {
        VSOMEIP_ERROR << "E2E Profile 5 can only be used for instances [1-255]";
        return;
    }

    if (profile_05::is_buffer_length_valid(config_, _buffer)) {
        // write the current Counter value in Data
        write_counter(_buffer, get_counter(_instance), 2);

        // compute the CRC
        uint16_t its_crc = profile_05::compute_crc(config_, _buffer);
        bithelper::write_uint16_be(its_crc, &_buffer[config_.offset_]);

        // increment the Counter (new value will be used in the next invocation of
        // E2E_P05Protect()),
        increment_counter(_instance);
    }
}

protect_result protector::protect_parts(buffer_view _app_payload, instance_t _instance) {
    std::unique_lock<std::mutex> lock(protect_mutex_);

    if (_instance > VSOMEIP_E2E_PROFILE05_MAX_INSTANCE) {
        VSOMEIP_ERROR << "E2E Profile 5 can only be used for instances [1-255]";
        return protect_result{};
    }

    const std::size_t protected_size = 3 + _app_payload.data_length();

    if (config_.offset_ != 0) {
        e2e_buffer its_buffer(config_.offset_ + protected_size, 0);
        std::copy(_app_payload.begin(), _app_payload.end(), its_buffer.begin() + config_.offset_ + 3);
        lock.unlock();
        protect(its_buffer, _instance);

        protect_result its_result;
        its_result.contiguous = std::make_shared<e2e_buffer>(std::move(its_buffer));
        its_result.valid = true;
        return its_result;
    }

    e2e_buffer its_length_check(protected_size);
    if (!profile_05::is_buffer_length_valid(config_, its_length_check)) {
        return protect_result{};
    }

    const uint8_t its_counter = get_counter(_instance);
    auto its_header = std::make_shared<e2e_buffer>(3, 0);
    (*its_header)[2] = its_counter;

    e2e_buffer its_after;
    its_after.push_back(its_counter);
    its_after.insert(its_after.end(), _app_payload.begin(), _app_payload.end());

    // Same as profile_05::compute_crc with offset_ == 0: empty prefix, then counter||payload, then data_id.
    uint16_t its_crc = e2e_crc::calculate_profile_05(buffer_view(its_after));

    uint8_t data_id[2];
    data_id[0] = static_cast<uint8_t>((config_.data_id_ >> 0) & 0xFF);
    data_id[1] = static_cast<uint8_t>((config_.data_id_ >> 8) & 0xFF);
    its_crc = e2e_crc::calculate_profile_05(buffer_view(data_id, sizeof(data_id)), its_crc);

    bithelper::write_uint16_be(its_crc, &(*its_header)[0]);

    increment_counter(_instance);

    protect_result its_result;
    its_result.e2e_header = its_header;
    its_result.app_payload = std::make_shared<e2e_buffer>(_app_payload.begin(), _app_payload.end());
    its_result.valid = true;
    return its_result;
}

void protector::write_counter(e2e_buffer& _buffer, uint8_t _data, size_t _index) {

    _buffer[config_.offset_ + _index] = _data;
}

uint8_t protector::get_counter(instance_t _instance) const {

    uint8_t its_counter(0);

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

} // namespace profile05
} // namespace e2e
} // namespace vsomeip_v3
