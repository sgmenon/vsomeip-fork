// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_PROTECT_RESULT_HPP
#define VSOMEIP_V3_E2E_PROTECT_RESULT_HPP

#include <cstddef>
#include <memory>

#include "../../buffer/buffer.hpp"

namespace vsomeip_v3 {
namespace e2e {

// Result of scatter-oriented protect: either one contiguous protected
// buffer, or header (+ optional leading gap) separate from app payload.
struct protect_result {
    bool valid{false};
    // If contiguous is set, it alone is the full protected area (after SOME/IP base).
    std::shared_ptr<e2e_buffer> contiguous;
    // Otherwise scatter: optional leading_gap (for offset>0), then e2e_header, then app_payload.
    std::shared_ptr<e2e_buffer> leading_gap;
    std::shared_ptr<e2e_buffer> e2e_header;
    std::shared_ptr<e2e_buffer> app_payload;

    std::size_t size() const {
        if (contiguous) {
            return contiguous->size();
        }
        std::size_t total = 0;
        if (leading_gap) {
            total += leading_gap->size();
        }
        if (e2e_header) {
            total += e2e_header->size();
        }
        if (app_payload) {
            total += app_payload->size();
        }
        return total;
    }
};

} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROTECT_RESULT_HPP
