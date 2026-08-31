// Copyright (C) 2026 GM GLOBAL TECHNOLOGY OPERATIONS LLC ALL RIGHTS RESERVED.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_MESSAGE_BUFFER_POOL_HPP_
#define VSOMEIP_V3_MESSAGE_BUFFER_POOL_HPP_

#include <cstddef>
#include <algorithm>
#include <memory>
#include <mutex>
#include <stack>
#include <utility>

#include "buffer.hpp"

namespace vsomeip_v3 {

/**
 * LIFO pool of message_buffer_t for TCP receive.
 *
 * lease()/adopt() return shared_ptr with a custom deleter that recycles the
 * buffer when the last reference drops (payload / owned_buffer_slice pins).
 * Empty pool: try_lease returns nullptr — callers must drop, not allocate.
 *
 * depth is the maximum number of buffers retained on the stack. adopt() may
 * temporarily create an outstanding buffer beyond depth; recycle discards when
 * the stack is already full.
 */
class message_buffer_pool : public std::enable_shared_from_this<message_buffer_pool> {
    struct private_token {
        explicit private_token() = default;
    };

public:
    static std::shared_ptr<message_buffer_pool> create(std::size_t _depth, std::size_t _initial_capacity) {
        auto pool = std::make_shared<message_buffer_pool>(private_token{});
        pool->depth_ = _depth;
        pool->initial_capacity_ = _initial_capacity;
        for (std::size_t i = 0; i < _depth; ++i) {
            pool->stack_.push(std::make_unique<message_buffer_t>(_initial_capacity, byte_t{0}));
        }
        return pool;
    }

    explicit message_buffer_pool(private_token) { }

    message_buffer_pool(const message_buffer_pool&) = delete;
    message_buffer_pool& operator=(const message_buffer_pool&) = delete;

    std::size_t depth() const noexcept { return depth_; }

    std::size_t available() const {
        std::scoped_lock guard{mtx_};
        return stack_.size();
    }

    /**
     * Pop a buffer for receive/frame use. nullptr if the stack is empty.
     *
     * Resizes so size == max(_min_capacity, capacity). TCP receive() advertises
     * capacity-used to asio, so size must cover the full capacity (recycled
     * buffers often keep a large capacity after clear()).
     */
    message_buffer_ptr_t try_lease(std::size_t _min_capacity = 0) {
        std::unique_ptr<message_buffer_t> buf;
        {
            std::scoped_lock guard{mtx_};
            if (stack_.empty()) {
                return nullptr;
            }
            buf = std::move(stack_.top());
            stack_.pop();
        }
        const std::size_t need = std::max(_min_capacity, buf->capacity());
        if (need > 0) {
            buf->resize(need, byte_t{0});
        }
        return wrap(std::move(buf));
    }

    /**
     * Take ownership of existing storage (e.g. moved stream window) and return
     * a pooled shared_ptr. Always succeeds.
     */
    message_buffer_ptr_t adopt(message_buffer_t&& _storage) {
        return wrap(std::make_unique<message_buffer_t>(std::move(_storage)));
    }

private:
    message_buffer_ptr_t wrap(std::unique_ptr<message_buffer_t> _buf) {
        message_buffer_t* raw = _buf.release();
        std::weak_ptr<message_buffer_pool> self = weak_from_this();
        return message_buffer_ptr_t(raw, [self](message_buffer_t* p) {
            std::unique_ptr<message_buffer_t> owned(p);
            if (auto pool = self.lock()) {
                pool->recycle(std::move(owned));
            }
        });
    }

    void recycle(std::unique_ptr<message_buffer_t> _buf) noexcept {
        try {
            std::scoped_lock guard{mtx_};
            if (!_buf || stack_.size() >= depth_) {
                return; // discard; unique_ptr deletes
            }
            // Keep capacity warm; clear logical size for the next lease resize.
            _buf->clear();
            stack_.push(std::move(_buf));
        } catch (...) {
            // Buffer is dropped if push fails.
        }
    }

    std::size_t depth_{0};
    std::size_t initial_capacity_{0};
    std::stack<std::unique_ptr<message_buffer_t>> stack_;
    mutable std::mutex mtx_;
};

/**
 * Take one complete stream frame out of a compaction receive window.
 *
 * When `_gap == 0` and `_message_size == _used`, the frame fills the entire used
 * region at the front of the window: move window storage into a shared_ptr
 * (no payload memcpy) and reset `_window` to `_fresh_capacity`. Otherwise
 * copy-out `[gap, gap + message_size)`.
 *
 * When `_pool` is set:
 * - Handout requires a free lease (replacement window on move, or frame buffer
 *   on copy). If the pool is empty, the frame is **dropped**: return nullptr,
 *   set `_dropped`, and consume the bytes from the stream without allocating.
 * - No unbounded fallback allocation under stall.
 *
 * When `_pool` is null: allocate with make_shared (legacy / pool disabled).
 *
 * On move (including drop-on-move): `_used` is set to 0 — caller must not also
 * subtract `_message_size`. On copy: caller still subtracts / advances gap
 * (including when dropped).
 */
inline message_buffer_ptr_t take_stream_frame(message_buffer_t& _window, std::size_t& _used, std::size_t _gap,
                                              std::size_t _message_size, std::size_t _fresh_capacity, bool& _moved,
                                              const std::shared_ptr<message_buffer_pool>& _pool = nullptr,
                                              bool* _dropped = nullptr) {
    _moved = false;
    if (_dropped) {
        *_dropped = false;
    }

    auto mark_dropped = [&]() {
        if (_dropped) {
            *_dropped = true;
        }
    };

    if (_gap == 0 && _message_size == _used && _message_size > 0) {
        if (_pool) {
            // Reserve the next window before handing out the current one.
            auto next = _pool->try_lease(_fresh_capacity);
            if (!next) {
                // Pool exhausted: drop in place; keep window for further recv.
                _used = 0;
                _moved = true;
                mark_dropped();
                return nullptr;
            }
            auto frame = _pool->adopt(std::move(_window));
            if (frame->size() > _message_size) {
                frame->resize(_message_size);
            }
            _window = std::move(*next);
            _used = 0;
            _moved = true;
            return frame;
        }

        auto frame = std::make_shared<message_buffer_t>(std::move(_window));
        if (frame->size() > _message_size) {
            frame->resize(_message_size);
        }
        _window = message_buffer_t(_fresh_capacity, byte_t{0});
        _used = 0;
        _moved = true;
        return frame;
    }

    if (_pool) {
        auto frame = _pool->try_lease(_message_size);
        if (!frame) {
            mark_dropped();
            return nullptr;
        }
        frame->assign(_window.data() + _gap, _window.data() + _gap + _message_size);
        return frame;
    }
    return std::make_shared<message_buffer_t>(_window.data() + _gap, _window.data() + _gap + _message_size);
}

} // namespace vsomeip_v3

#endif // VSOMEIP_V3_MESSAGE_BUFFER_POOL_HPP_
