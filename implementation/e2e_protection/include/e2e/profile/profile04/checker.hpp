// Copyright (C) 2020-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_PROFILE04_CHECKER_HPP
#define VSOMEIP_V3_E2E_PROFILE04_CHECKER_HPP

#include <map>

#include "../profile04/profile_04.hpp"
#include "../profile_interface/checker.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile04 {

class profile_04_checker final : public e2e::profile_interface::checker {

public:
    profile_04_checker(void) = delete;

    explicit profile_04_checker(const profile_config& _config) : config_(_config) { }

    check_result check(buffer_view _buffer, instance_t _instance) override final;

private:
    bool verify_input(buffer_view _buffer) const;
    bool verify_counter(instance_t _instance, uint16_t _received_counter);

    bool read_16(buffer_view _buffer, uint16_t& _data, size_t _index) const;
    bool read_32(buffer_view _buffer, uint32_t& _data, size_t _index) const;

    std::mutex check_mutex_;

    profile_config config_;
    std::map<instance_t, uint16_t> counter_;
};

} // namespace profile_04
} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROFILE04_CHECKER_HPP
