// Copyright (C) 2014-2021 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_BUFFER_HPP_
#define VSOMEIP_V3_BUFFER_HPP_

#include <array>
#include <chrono>
#include <memory>
#include <numeric>
#include <set>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <vsomeip/defines.hpp>
#include <vsomeip/primitive_types.hpp>

#if defined(_WIN32) && !defined(_MSVC_LANG)
#define DEFAULT_NANOSECONDS_MAX 1000000000
#else
#define DEFAULT_NANOSECONDS_MAX std::chrono::nanoseconds::max()
#endif

namespace vsomeip_v3 {

typedef std::vector<byte_t> message_buffer_t;
typedef std::shared_ptr<message_buffer_t> message_buffer_ptr_t;

/**
 * Owning multi-buffer send unit for asio ConstBufferSequence writes.
 * Each storage entry is kept alive until the async send completion handler runs.
 */
struct send_buffer_sequence {
    send_buffer_sequence() = default;

    explicit send_buffer_sequence(message_buffer_ptr_t _buffer) {
        if (_buffer && !_buffer->empty()) {
            append(std::move(_buffer));
        }
    }

    send_buffer_sequence(const byte_t* _data, std::size_t _size) {
        if (_data && _size > 0) {
            append_bytes(_data, _size);
        }
    }

    void append(message_buffer_ptr_t _buffer) {
        if (!_buffer || _buffer->empty()) {
            return;
        }
        storage_.push_back(std::move(_buffer));
        rebuild_buffers();
    }

    void append_bytes(const byte_t* _data, std::size_t _size) {
        if (!_data || _size == 0) {
            return;
        }
        append(std::make_shared<message_buffer_t>(_data, _data + _size));
    }

    void prepend(message_buffer_ptr_t _buffer) {
        if (!_buffer || _buffer->empty()) {
            return;
        }
        storage_.insert(storage_.begin(), std::move(_buffer));
        rebuild_buffers();
    }

    void append_sequence(const send_buffer_sequence& _other) {
        storage_.insert(storage_.end(), _other.storage_.begin(), _other.storage_.end());
        rebuild_buffers();
    }

    std::size_t size() const {
        return std::accumulate(storage_.begin(), storage_.end(), std::size_t{0},
                               [](std::size_t sum, const message_buffer_ptr_t& b) { return sum + (b ? b->size() : 0); });
    }

    bool empty() const { return size() == 0; }

    const std::vector<boost::asio::const_buffer>& buffers() const { return buffers_; }

    const std::vector<message_buffer_ptr_t>& storage() const { return storage_; }

    /**
     * Read a big-endian uint16 from a logical offset across the sequence.
     * Returns false if the sequence is shorter than offset+2.
     */
    bool read_uint16_be(std::size_t _offset, uint16_t& _out) const {
        byte_t high = 0;
        byte_t low = 0;
        if (!read_byte(_offset, high) || !read_byte(_offset + 1, low)) {
            return false;
        }
        _out = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
        return true;
    }

    bool read_byte(std::size_t _offset, byte_t& _out) const {
        std::size_t remaining = _offset;
        for (const auto& buf : storage_) {
            if (!buf) {
                continue;
            }
            if (remaining < buf->size()) {
                _out = (*buf)[remaining];
                return true;
            }
            remaining -= buf->size();
        }
        return false;
    }

    /**
     * Ensure the sequence is a single contiguous buffer (copies only when needed).
     * This is a compatibility fallback for legacy pointer-arithmetic paths
     * (e.g. SOME/IP-TP split / trace callbacks). Normal send uses buffers().
     */
    message_buffer_ptr_t flatten() const {
        if (storage_.size() == 1 && storage_.front()) {
            return storage_.front();
        }
        auto flat = std::make_shared<message_buffer_t>();
        flat->reserve(size());
        for (const auto& buf : storage_) {
            if (buf) {
                flat->insert(flat->end(), buf->begin(), buf->end());
            }
        }
        return flat;
    }

private:
    void rebuild_buffers() {
        buffers_.clear();
        buffers_.reserve(storage_.size());
        for (const auto& buf : storage_) {
            if (buf && !buf->empty()) {
                buffers_.emplace_back(boost::asio::buffer(*buf));
            }
        }
    }

    std::vector<message_buffer_ptr_t> storage_;
    std::vector<boost::asio::const_buffer> buffers_;
};

typedef std::shared_ptr<send_buffer_sequence> send_buffer_sequence_ptr_t;

struct train {
    train() :
        sequence_(std::make_shared<send_buffer_sequence>()), minimal_debounce_time_(DEFAULT_NANOSECONDS_MAX),
        minimal_max_retention_time_(DEFAULT_NANOSECONDS_MAX), departure_(std::chrono::steady_clock::now() + std::chrono::hours(6)) { }

    void reset() {
        sequence_ = std::make_shared<send_buffer_sequence>();
        passengers_.clear();
        minimal_debounce_time_ = DEFAULT_NANOSECONDS_MAX;
        minimal_max_retention_time_ = DEFAULT_NANOSECONDS_MAX;
        departure_ = std::chrono::steady_clock::now() + std::chrono::hours(6);
    }

    send_buffer_sequence_ptr_t sequence_;
    std::set<std::pair<service_t, method_t>> passengers_;

    std::chrono::nanoseconds minimal_debounce_time_;
    std::chrono::nanoseconds minimal_max_retention_time_;

    std::chrono::steady_clock::time_point departure_;
};

} // namespace vsomeip_v3

#endif // VSOMEIP_V3_BUFFER_HPP_
