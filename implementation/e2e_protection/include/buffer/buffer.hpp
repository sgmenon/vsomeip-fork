// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_BUFFER_HPP
#define VSOMEIP_V3_E2E_BUFFER_HPP

#include <cstdint>
#include <ostream>
#include <vector>

#include <vsomeip/span.hpp>

namespace vsomeip_v3 {

using e2e_buffer = std::vector<uint8_t>;

// Non-owning byte range for E2E protect/check/CRC paths (same role as std::span).
using buffer_view = span<const uint8_t>;

std::ostream& operator<<(std::ostream& _os, const e2e_buffer& _buffer);

} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_BUFFER_HPP
