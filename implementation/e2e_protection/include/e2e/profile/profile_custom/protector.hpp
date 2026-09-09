// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_PROFILE_CUSTOM_PROTECTOR_HPP
#define VSOMEIP_V3_E2E_PROFILE_CUSTOM_PROTECTOR_HPP

#include <mutex>
#include "../profile_custom/profile_custom.hpp"
#include "../profile_interface/protector.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile_custom {

class protector final : public e2e::profile_interface::protector {
public:
    protector(void) = delete;

    explicit protector(const profile_config& _config) : config_(_config) {};

    protect_result protect(buffer_view _someip_header, buffer_view _app_payload, instance_t _instance) override final;

private:
    profile_config config_;
    std::mutex protect_mutex_;
};

} // namespace profile_custom
} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROFILE_CUSTOM_PROTECTOR_HPP
