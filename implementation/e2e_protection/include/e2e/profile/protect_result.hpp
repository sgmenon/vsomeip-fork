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

// Scatter pieces for the protected area (after SOME/IP base). Any piece may be
// empty. Offset padding belongs at the front of e2e_header. Trailers (CRC/MAC)
// belong in e2e_footer.
struct protect_result {
    bool valid{false};
    std::shared_ptr<e2e_buffer> e2e_header;
    std::shared_ptr<e2e_buffer> app_payload;
    std::shared_ptr<e2e_buffer> e2e_footer;

    std::size_t size() const {
        std::size_t total = 0;
        if (e2e_header) {
            total += e2e_header->size();
        }
        if (app_payload) {
            total += app_payload->size();
        }
        if (e2e_footer) {
            total += e2e_footer->size();
        }
        return total;
    }
};

} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROTECT_RESULT_HPP
