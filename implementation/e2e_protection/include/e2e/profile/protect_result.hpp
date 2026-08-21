// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_PROTECT_RESULT_HPP
#define VSOMEIP_V3_E2E_PROTECT_RESULT_HPP

#include <cstddef>
#include <memory>

#include <vsomeip/span.hpp>

#include "../../buffer/buffer.hpp"
#include "profile_interface/profile_interface.hpp"

namespace vsomeip_v3 {
namespace e2e {

// Scatter pieces for the protected area (after SOME/IP base). Any piece may be
// empty. Offset padding belongs at the front of e2e_header. Trailers (CRC/MAC)
// belong in e2e_footer.
//
// app_payload is a non-owning view into the caller's input (same lifetime as
// protect()'s buffer_view). owned_app_payload is set only when the profile must
// pack bytes (Profile 01); routing appends that buffer instead of pinning input.
struct protect_result {
    bool valid{false};
    std::shared_ptr<e2e_buffer> e2e_header;
    span<const uint8_t> app_payload;
    std::shared_ptr<e2e_buffer> owned_app_payload;
    std::shared_ptr<e2e_buffer> e2e_footer;

    std::size_t size() const {
        std::size_t total = 0;
        if (e2e_header) {
            total += e2e_header->size();
        }
        if (owned_app_payload) {
            total += owned_app_payload->size();
        } else {
            total += app_payload.size();
        }
        if (e2e_footer) {
            total += e2e_footer->size();
        }
        return total;
    }
};

// Non-owning view of the same three sections inside a contiguous receive buffer.
// Spans are valid only while that buffer lives.
struct check_result {
    profile_interface::check_status_t status{profile_interface::generic_check_status::E2E_ERROR};
    span<const uint8_t> e2e_header;
    span<const uint8_t> app_payload;
    span<const uint8_t> e2e_footer;
};

inline check_result make_check_result(profile_interface::check_status_t _status, buffer_view _buffer, std::size_t _header_size,
                                      std::size_t _footer_size = 0) {
    check_result its_result;
    its_result.status = _status;
    if (_buffer.size() < _header_size + _footer_size) {
        return its_result;
    }
    its_result.e2e_header = _buffer.subspan(0, _header_size);
    its_result.app_payload = _buffer.subspan(_header_size, _buffer.size() - _header_size - _footer_size);
    its_result.e2e_footer = _buffer.subspan(_buffer.size() - _footer_size, _footer_size);
    return its_result;
}

} // namespace e2e
} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_PROTECT_RESULT_HPP
