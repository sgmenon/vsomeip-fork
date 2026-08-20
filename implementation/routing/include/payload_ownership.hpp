// Copyright (C) 2026 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_PAYLOAD_OWNERSHIP_HPP_
#define VSOMEIP_V3_PAYLOAD_OWNERSHIP_HPP_

#include <memory>

#include "../../endpoints/include/buffer.hpp"
#include "../../message/include/payload_impl.hpp"
#include <vsomeip/message.hpp>
#include <vsomeip/payload.hpp>

namespace vsomeip_v3 {

/**
 * At the notify API boundary: if `_payload` is the sole remaining owner
 * (`use_count() == 1`, typically after `std::move`), return it for pinning.
 * If the caller still holds a live shared_ptr, snapshot bytes into a fresh
 * payload so later `set_data` cannot corrupt in-flight sends.
 */
inline std::shared_ptr<payload> snapshot_payload_if_shared(std::shared_ptr<payload> _payload) {
    if (!_payload || _payload.use_count() == 1) {
        return _payload;
    }
    return std::make_shared<payload_impl>(_payload->get_data(), _payload->get_length());
}

/**
 * At the send(message) API boundary: ensure the message's payload is safe to
 * pin in routing. Routing always pins; any payload-sized copy happens here.
 *
 * - Shared message (`use_count() > 1`): snapshot onto the message (other holders
 *   could call get_payload()/set_data later).
 * - Exclusive message but payload still shared with the caller: snapshot and
 *   replace on the message; caller's separate payload shared_ptr is untouched.
 */
inline void ensure_exclusive_message_payload(const std::shared_ptr<message>& _message) {
    if (!_message) {
        return;
    }
    auto its_payload = _message->get_payload();
    if (!its_payload || its_payload->get_length() == 0) {
        return;
    }
    // get_payload() adds one local ref on top of the message's stored ptr.
    const bool message_shared = (_message.use_count() > 1);
    const bool payload_shared_beyond_message = (its_payload.use_count() > 2);
    if (message_shared || payload_shared_beyond_message) {
        _message->set_payload(std::make_shared<payload_impl>(its_payload->get_data(), its_payload->get_length()));
    }
}

/** Routing always pins; call only after ensure_exclusive_message_payload / notify snapshot. */
inline void append_message_payload(send_buffer_sequence& _sequence, const std::shared_ptr<payload>& _payload) {
    if (!_payload || _payload->get_length() == 0) {
        return;
    }
    _sequence.append_payload(_payload);
}

} // namespace vsomeip_v3

#endif // VSOMEIP_V3_PAYLOAD_OWNERSHIP_HPP_
