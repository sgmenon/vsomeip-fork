// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <cstring>

#include "../include/deserializer.hpp"
#include "../include/payload_impl.hpp"
#include "../include/serializer.hpp"

namespace vsomeip_v3 {

payload_impl::payload_impl() {
    reset_owned(std::make_shared<std::vector<byte_t>>());
}

payload_impl::payload_impl(const byte_t* _data, uint32_t _size) {
    auto its_buffer = std::make_shared<std::vector<byte_t>>();
    if (_data && _size > 0) {
        its_buffer->assign(_data, _data + _size);
    }
    reset_owned(std::move(its_buffer));
}

payload_impl::payload_impl(const std::vector<byte_t>& _data) {
    reset_owned(std::make_shared<std::vector<byte_t>>(_data));
}

payload_impl::payload_impl(const payload_impl& _payload) {
    // Snapshot into a fresh buffer so copies do not share mutable storage.
    auto its_buffer = std::make_shared<std::vector<byte_t>>();
    if (_payload.get_length() > 0 && _payload.get_data()) {
        its_buffer->assign(_payload.get_data(), _payload.get_data() + _payload.get_length());
    }
    reset_owned(std::move(its_buffer));
}

payload_impl::payload_impl(std::shared_ptr<std::vector<byte_t>> _buffer, std::size_t _offset, std::size_t _length) {
    if (!_buffer || _offset + _length > _buffer->size()) {
        reset_owned(std::make_shared<std::vector<byte_t>>());
        return;
    }
    buffer_ = std::move(_buffer);
    offset_ = _offset;
    length_ = _length;
}

void payload_impl::reset_owned(std::shared_ptr<std::vector<byte_t>> _buffer) {
    buffer_ = std::move(_buffer);
    if (!buffer_) {
        buffer_ = std::make_shared<std::vector<byte_t>>();
    }
    offset_ = 0;
    length_ = buffer_->size();
}

bool payload_impl::operator==(const payload& _other) const {
    bool is_equal{get_length() == _other.get_length()};
    if (is_equal && get_length() > 0) {
        is_equal = (0 == std::memcmp(get_data(), _other.get_data(), get_length()));
    }
    return is_equal;
}

byte_t* payload_impl::get_data() {
    if (!buffer_) {
        return nullptr;
    }
    return buffer_->data() + offset_;
}

const byte_t* payload_impl::get_data() const {
    if (!buffer_) {
        return nullptr;
    }
    return buffer_->data() + offset_;
}

length_t payload_impl::get_length() const {
    return static_cast<length_t>(length_);
}

void payload_impl::set_capacity(length_t _capacity) {
    // Exclusive owned buffer for subsequent deserialize / fill.
    auto its_buffer = std::make_shared<std::vector<byte_t>>();
    its_buffer->reserve(_capacity);
    reset_owned(std::move(its_buffer));
    length_ = 0;
}

void payload_impl::set_data(const byte_t* _data, const length_t _length) {
    auto its_buffer = std::make_shared<std::vector<byte_t>>();
    if (_data && _length > 0) {
        its_buffer->assign(_data, _data + _length);
    }
    reset_owned(std::move(its_buffer));
}

void payload_impl::set_data(const std::vector<byte_t>& _data) {
    reset_owned(std::make_shared<std::vector<byte_t>>(_data));
}

void payload_impl::set_data(std::vector<byte_t>&& _data) {
    reset_owned(std::make_shared<std::vector<byte_t>>(std::move(_data)));
}

bool payload_impl::serialize(serializer* _to) const {
    if (!_to) {
        return false;
    }
    if (length_ == 0) {
        return true;
    }
    return _to->serialize(get_data(), get_length());
}

bool payload_impl::deserialize(deserializer* _from) {
    if (!_from) {
        return false;
    }
    auto its_buffer = std::make_shared<std::vector<byte_t>>();
    // Preserve capacity reserved via set_capacity for deserializer fill size.
    if (buffer_ && buffer_.use_count() == 1 && offset_ == 0) {
        its_buffer = std::move(buffer_);
        its_buffer->clear();
    }
    if (!_from->deserialize(*its_buffer)) {
        reset_owned(std::make_shared<std::vector<byte_t>>());
        return false;
    }
    reset_owned(std::move(its_buffer));
    return true;
}

} // namespace vsomeip_v3
