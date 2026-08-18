// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_PROFILE01_CHECKER_HPP
#define VSOMEIP_V3_E2E_PROFILE01_CHECKER_HPP

#include <algorithm>

#include "../profile01/profile_01.hpp"
#include "../profile_interface/checker.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile01 {

class profile_01_checker final : public e2e::profile_interface::checker {

public:
    profile_01_checker(void) = delete;

    explicit profile_01_checker(const profile_config& _config) : config_(_config) { }

    check_result check(buffer_view _buffer, instance_t _instance) override final;

private:
    static std::size_t header_size(const profile_config& _config) {
        return std::max({static_cast<std::size_t>(_config.crc_offset_) + 1U, static_cast<std::size_t>(_config.counter_offset_ / 8) + 1U,
                         static_cast<std::size_t>(_config.data_id_nibble_offset_ / 8) + 1U});
    }

    profile_config config_;
    std::mutex check_mutex_;
};

} // namespace profile01
} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROFILE01_CHECKER_HPP
