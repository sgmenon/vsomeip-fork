// Copyright (C) 2026 GM Global Technology Operations LLC.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_E2E_RECEIVE_HELPERS_HPP_
#define VSOMEIP_V3_E2E_RECEIVE_HELPERS_HPP_

#include <memory>

#include <vsomeip/defines.hpp>
#include <vsomeip/payload.hpp>
#include <vsomeip/primitive_types.hpp>
#include <vsomeip/runtime.hpp>

#include "../../endpoints/include/buffer.hpp"
#include "../../message/include/message_impl.hpp"
#include "../../message/include/payload_impl.hpp"
#include "../../utility/include/bithelper.hpp"

#ifndef ANDROID
#include <vsomeip/span.hpp>
#include "../../e2e_protection/include/e2e/profile/protect_result.hpp"
#endif

namespace vsomeip_v3 {

/**
 * View a relative range of an owned receive frame as payload (0-copy).
 * `_rel_offset` / `_length` are relative to the slice, not the backing buffer.
 */
inline std::shared_ptr<payload> make_payload_from_frame(const owned_buffer_slice& _frame, std::size_t _rel_offset, std::size_t _length) {
    if (_length == 0) {
        return runtime::get()->create_payload();
    }
    if (!_frame.valid() || _rel_offset + _length > _frame.length) {
        return runtime::get()->create_payload();
    }
    return std::make_shared<payload_impl>(_frame.buffer, _frame.offset + _rel_offset, _length);
}

#ifndef ANDROID
/**
 * Prefer a payload_impl pin when `_span` lies inside `_frame.buffer`; else copy.
 */
inline std::shared_ptr<payload> make_payload_from_span(span<const uint8_t> _span, const owned_buffer_slice& _frame) {
    if (_span.empty()) {
        return runtime::get()->create_payload();
    }
    if (_frame.valid() && _span.data() >= _frame.buffer->data()
        && (_span.data() + _span.size()) <= (_frame.buffer->data() + _frame.buffer->size())) {
        const std::size_t its_offset = static_cast<std::size_t>(_span.data() - _frame.buffer->data());
        return std::make_shared<payload_impl>(_frame.buffer, its_offset, _span.size());
    }
    return runtime::get()->create_payload(_span.data(), static_cast<length_t>(_span.size()));
}

/**
 * Hole-free SOME/IP as scatter: owned 16B header (length patched) + app payload
 * slice (pin when possible). No header+payload concat.
 */
inline buffer_sequence_ptr_t compose_e2e_stripped_sequence(const owned_buffer_slice& _frame, const e2e::check_result& _checked) {
    if (!_frame.valid() || _frame.length < VSOMEIP_FULL_HEADER_SIZE) {
        return nullptr;
    }

    const byte_t* _data = _frame.data();
    auto its_header = std::make_shared<message_buffer_t>(_data, _data + VSOMEIP_FULL_HEADER_SIZE);
    const uint32_t its_new_total = static_cast<uint32_t>(VSOMEIP_FULL_HEADER_SIZE + _checked.app_payload.size());
    bithelper::write_uint32_be(its_new_total - 8U, its_header->data() + VSOMEIP_LENGTH_POS_MIN);

    auto its_sequence = std::make_shared<buffer_sequence>();
    its_sequence->append(std::move(its_header));

    if (_checked.app_payload.empty()) {
        return its_sequence;
    }

    const byte_t* its_app = _checked.app_payload.data();
    const std::size_t its_app_len = _checked.app_payload.size();
    if (its_app >= _frame.buffer->data() && (its_app + its_app_len) <= (_frame.buffer->data() + _frame.buffer->size())) {
        const std::size_t its_offset = static_cast<std::size_t>(its_app - _frame.buffer->data());
        its_sequence->append_buffer_slice(_frame.buffer, its_offset, its_app_len);
    } else {
        its_sequence->append_bytes(its_app, its_app_len);
    }
    return its_sequence;
}
#endif // !ANDROID

inline std::shared_ptr<message_impl> build_message_from_header_and_payload(const byte_t* _header, std::shared_ptr<payload> _payload) {
    if (!_header) {
        return nullptr;
    }
    auto its_message = std::make_shared<message_impl>();
    its_message->set_service(bithelper::read_uint16_be(&_header[VSOMEIP_SERVICE_POS_MIN]));
    its_message->set_method(bithelper::read_uint16_be(&_header[VSOMEIP_METHOD_POS_MIN]));
    its_message->set_client(bithelper::read_uint16_be(&_header[VSOMEIP_CLIENT_POS_MIN]));
    its_message->set_session(bithelper::read_uint16_be(&_header[VSOMEIP_SESSION_POS_MIN]));
    its_message->set_protocol_version(_header[VSOMEIP_PROTOCOL_VERSION_POS]);
    its_message->set_interface_version(_header[VSOMEIP_INTERFACE_VERSION_POS]);
    its_message->set_message_type(static_cast<message_type_e>(_header[VSOMEIP_MESSAGE_TYPE_POS]));
    its_message->set_return_code(static_cast<return_code_e>(_header[VSOMEIP_RETURN_CODE_POS]));
    if (_payload) {
        its_message->set_payload(std::move(_payload));
    }
    return its_message;
}

inline std::shared_ptr<message_impl> build_message_from_buffer(const owned_buffer_slice& _frame) {
    if (!_frame.valid() || _frame.length < VSOMEIP_FULL_HEADER_SIZE) {
        return nullptr;
    }
    const length_t its_payload_length =
            (_frame.length > VSOMEIP_FULL_HEADER_SIZE) ? static_cast<length_t>(_frame.length - VSOMEIP_FULL_HEADER_SIZE) : 0;
    auto its_payload = make_payload_from_frame(_frame, VSOMEIP_FULL_HEADER_SIZE, its_payload_length);
    return build_message_from_header_and_payload(_frame.data(), std::move(its_payload));
}

#ifndef ANDROID
inline std::shared_ptr<message_impl> build_message_from_check_result(const owned_buffer_slice& _frame, const e2e::check_result& _checked) {
    if (!_frame.valid() || _frame.length < VSOMEIP_FULL_HEADER_SIZE) {
        return nullptr;
    }
    auto its_payload = make_payload_from_span(_checked.app_payload, _frame);
    return build_message_from_header_and_payload(_frame.data(), std::move(its_payload));
}
#endif

} // namespace vsomeip_v3

#endif // VSOMEIP_V3_E2E_RECEIVE_HELPERS_HPP_
