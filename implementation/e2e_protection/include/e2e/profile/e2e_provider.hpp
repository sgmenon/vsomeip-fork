// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_PROVIDER_HPP
#define VSOMEIP_V3_E2E_PROVIDER_HPP

#include <string>
#include <memory>

#include "../../buffer/buffer.hpp"
#include "../../e2exf/config.hpp"
#include "../../../../configuration/include/e2e.hpp"
#include "protect_result.hpp"

namespace vsomeip_v3 {
namespace e2e {

class e2e_provider {
public:
    virtual ~e2e_provider() {}
    virtual bool add_configuration(std::shared_ptr<cfg::e2e> config) = 0;

    virtual bool is_protected(e2exf::data_identifier_t id) const = 0;
    virtual bool is_checked(e2exf::data_identifier_t id) const = 0;

    virtual protect_result protect(e2exf::data_identifier_t id, buffer_view app_payload, instance_t instance) = 0;

    // Full SOME/IP message. Returned spans point into `_message`.
    virtual check_result check(e2exf::data_identifier_t id, buffer_view _message, instance_t _instance) = 0;
};

} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROVIDER_HPP
