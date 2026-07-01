// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

// `deserializer::deserialize_message()` was extracted from `deserializer.cpp`
// so that the primitive `deserializer` (used by SD and the rest of the
// SOME/IP framing layer) does not pull `message_impl` into
// `libvsomeip3-core.so`. The SOME/IP message factory belongs in the
// routing layer alongside the routing manager that consumes it.

#include <exception>
#include <memory>

#include <vsomeip/internal/logger.hpp>

#include "../include/deserializer.hpp"
#include "../include/message_impl.hpp"

namespace vsomeip_v3 {

std::unique_ptr<message_impl> deserializer::deserialize_message() try {
    auto deserialized_message = std::make_unique<message_impl>();
    if (!deserialized_message->deserialize(this)) {
        VSOMEIP_ERROR << "SOME/IP message deserialization failed!";
        return nullptr;
    }

    return deserialized_message;
} catch (const std::exception& e) {
    VSOMEIP_ERROR << "SOME/IP message deserialization failed with exception: " << e.what();
    return nullptr;
}

} // namespace vsomeip_v3
