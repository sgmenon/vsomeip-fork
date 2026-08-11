// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_PROFILE_INTERFACE_CHECKER_HPP
#define VSOMEIP_V3_E2E_PROFILE_INTERFACE_CHECKER_HPP

#include <mutex>

#include <vsomeip/primitive_types.hpp>
#include <vsomeip/span.hpp>

#include "../profile_interface/profile_interface.hpp"
#include "../../../buffer/buffer.hpp"

namespace vsomeip_v3 {
namespace e2e {
namespace profile_interface {

class checker : public profile_interface {
public:
    virtual void check(const e2e_buffer& _buffer, instance_t _instance, check_status_t& _generic_check_status) = 0;

    /**
     * Non-owning view of the app payload within a contiguous protected area
     * (E2E header / fields stripped). Returns false if the buffer is too short
     * for this profile layout.
     */
    virtual bool get_unprotected_payload(buffer_view _protected_area, span<const uint8_t>& _out) const {
        (void)_protected_area;
        (void)_out;
        return false;
    }
};

} // namespace profile_interface
} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROFILE_INTERFACE_CHECKER_HPP
