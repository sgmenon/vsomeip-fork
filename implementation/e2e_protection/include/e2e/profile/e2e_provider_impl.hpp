// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_PROVIDER_IMPL_HPP
#define VSOMEIP_V3_E2E_PROVIDER_IMPL_HPP

#include <map>
#include <memory>
#include <algorithm>

#include "e2e_provider.hpp"
#include "profile_interface/checker.hpp"
#include "profile_interface/protector.hpp"

#include "profile01/profile_01.hpp"
#include "profile04/profile_04.hpp"
#include "profile05/profile_05.hpp"
#include "profile07/profile_07.hpp"
#include "profile_custom/profile_custom.hpp"

#include "../../../../../interface/vsomeip/export.hpp"
#include "../../../../../interface/vsomeip/plugin.hpp"

namespace vsomeip_v3 {
namespace e2e {

class e2e_provider_impl : public e2e_provider,
                          public plugin_impl<e2e_provider_impl>,
                          public std::enable_shared_from_this<e2e_provider_impl> {
public:
    VSOMEIP_EXPORT e2e_provider_impl();
    VSOMEIP_EXPORT ~e2e_provider_impl();

    VSOMEIP_EXPORT bool add_configuration(std::shared_ptr<cfg::e2e> config) override;

    VSOMEIP_EXPORT bool is_protected(e2exf::data_identifier_t id) const override;
    VSOMEIP_EXPORT bool is_checked(e2exf::data_identifier_t id) const override;

    VSOMEIP_EXPORT std::size_t get_protection_base(e2exf::data_identifier_t _id) const override;

    VSOMEIP_EXPORT protect_result protect_parts(e2exf::data_identifier_t id, buffer_view app_payload, instance_t instance) override;
    VSOMEIP_EXPORT void check(e2exf::data_identifier_t id, const e2e_buffer& _buffer, instance_t _instance,
                              profile_interface::check_status_t& _generic_check_status) override;
    VSOMEIP_EXPORT bool get_unprotected_payload(e2exf::data_identifier_t id, buffer_view _protected_area,
                                                span<const uint8_t>& _out) const override;

private:
    std::map<e2exf::data_identifier_t, std::shared_ptr<profile_interface::protector>> custom_protectors_;
    std::map<e2exf::data_identifier_t, std::shared_ptr<profile_interface::checker>> custom_checkers_;
    std::map<e2exf::data_identifier_t, std::size_t> custom_bases_;
    // Byte offset of app payload within the protected area (after E2E header/fields).
    std::map<e2exf::data_identifier_t, std::size_t> custom_payload_starts_;

    template<typename config_t>
    config_t make_e2e_profile_config(const std::shared_ptr<cfg::e2e>& config);

    static std::size_t payload_start(const profile01::profile_config& _config) {
        return std::max({static_cast<std::size_t>(_config.crc_offset_) + 1U, static_cast<std::size_t>(_config.counter_offset_ / 8) + 1U,
                         static_cast<std::size_t>(_config.data_id_nibble_offset_ / 8) + 1U});
    }
    static std::size_t payload_start(const profile04::profile_config& _config) { return _config.offset_ + 12U; }
    static std::size_t payload_start(const profile05::profile_config& _config) { return _config.offset_ + 3U; }
    static std::size_t payload_start(const profile07::profile_config& _config) { return _config.offset_ + 20U; }
    static std::size_t payload_start(const profile_custom::profile_config& _config) {
        return static_cast<std::size_t>(_config.crc_offset_) + 4U;
    }

    template<typename config_t, typename checker_t, typename protector_t>
    void process_e2e_profile(std::shared_ptr<cfg::e2e> config) {
        const e2exf::data_identifier_t data_identifier = {config->service_id, config->event_id};
        config_t profile_config = make_e2e_profile_config<config_t>(config);

        std::shared_ptr<e2e::profile_interface::checker> checker;
        if ((config->variant == "checker") || (config->variant == "both")) {
            custom_checkers_[data_identifier] = std::make_shared<checker_t>(profile_config);
        }

        std::shared_ptr<e2e::profile_interface::protector> protector;
        if ((config->variant == "protector") || (config->variant == "both")) {
            custom_protectors_[data_identifier] = std::make_shared<protector_t>(profile_config);
        }

        custom_bases_[data_identifier] = profile_config.base_;
        custom_payload_starts_[data_identifier] = payload_start(profile_config);
    }
};

} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROVIDER_IMPL_HPP
