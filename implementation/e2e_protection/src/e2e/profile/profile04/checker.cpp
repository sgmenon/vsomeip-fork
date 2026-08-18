// Copyright (C) 2020-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <iomanip>

#include <vsomeip/internal/logger.hpp>

#include "../../../../include/e2e/profile/profile04/checker.hpp"
#include "../../../../../utility/include/bithelper.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile04 {

check_result profile_04_checker::check(buffer_view _buffer, instance_t _instance) {

    std::lock_guard<std::mutex> lock(check_mutex_);
    auto status = e2e::profile_interface::generic_check_status::E2E_ERROR;

    if (_instance > VSOMEIP_E2E_PROFILE04_MAX_INSTANCE) {
        VSOMEIP_ERROR << "E2E Profile 4 can only be used for instances [1-255]";
        return check_result{};
    }

    if (verify_input(_buffer)) {
        uint16_t its_received_length;
        if (read_16(_buffer, its_received_length, 0)) {
            uint16_t its_received_counter;
            if (read_16(_buffer, its_received_counter, 2)) {
                uint32_t its_received_data_id;
                if (read_32(_buffer, its_received_data_id, 4)) {
                    uint32_t its_received_crc;
                    if (read_32(_buffer, its_received_crc, 8)) {
                        uint32_t its_crc = profile_04::compute_crc(config_, _buffer);
                        if (its_received_crc != its_crc) {
                            status = e2e::profile_interface::generic_check_status::E2E_WRONG_CRC;
                            VSOMEIP_ERROR << std::hex << "E2E P04 protection: CRC32 does not match: calculated CRC: " << its_crc
                                          << " received CRC: " << its_received_crc;
                        } else {
                            uint32_t its_data_id(uint32_t(_instance) << 24 | config_.data_id_);
                            if (its_received_data_id == its_data_id && static_cast<size_t>(its_received_length) == _buffer.size()
                                && verify_counter(_instance, its_received_counter)) {
                                status = e2e::profile_interface::generic_check_status::E2E_OK;
                            }
                        }
                    }
                }
            }
        }
    }
    return make_check_result(status, _buffer, config_.offset_ + 12U);
}

bool profile_04_checker::verify_input(buffer_view _buffer) const {

    auto its_length = _buffer.size();
    return (its_length >= config_.min_data_length_ && its_length <= config_.max_data_length_);
}

bool profile_04_checker::verify_counter(instance_t _instance, uint16_t _received_counter) {

    uint16_t its_delta(0);

    auto find_counter = counter_.find(_instance);
    if (find_counter != counter_.end()) {
        uint16_t its_counter = find_counter->second;
        if (its_counter < _received_counter)
            its_delta = uint16_t(_received_counter - its_counter);
        else
            its_delta = uint16_t(uint16_t(0xffff) - its_counter + _received_counter);

        find_counter->second = _received_counter;
    } else {
        counter_[_instance] = _received_counter;
    }

    return (its_delta <= config_.max_delta_counter_);
}

bool profile_04_checker::read_16(buffer_view _buffer, uint16_t& _data, size_t _index) const {

    _data = bithelper::read_uint16_be(&_buffer[config_.offset_ + _index]);
    return true;
}

bool profile_04_checker::read_32(buffer_view _buffer, uint32_t& _data, size_t _index) const {

    _data = bithelper::read_uint32_be(&_buffer[config_.offset_ + _index]);
    return true;
}

} // namespace profile04
} // namespace e2e
} // namespace vsomeip_v3
