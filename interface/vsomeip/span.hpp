// Copyright (C) 2026 GM Global Technology Operations LLC.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef VSOMEIP_V3_SPAN_HPP_
#define VSOMEIP_V3_SPAN_HPP_

// Prefer boost::span in C++17 builds; fall back to std::span (C++20) or a local polyfill.
#if defined(__has_include)
#if __has_include(<boost/core/span.hpp>)
#include <boost/core/span.hpp>
namespace vsomeip_v3 {
template<typename T>
using span = boost::span<T>;
}
#elif __has_include(<span>) && __cplusplus >= 202002L
#include <span>
namespace vsomeip_v3 {
template<typename T>
using span = std::span<T>;
}
#else
#define VSOMEIP_V3_NEED_SPAN_POLYFILL 1
#endif
#else
#define VSOMEIP_V3_NEED_SPAN_POLYFILL 1
#endif

#ifdef VSOMEIP_V3_NEED_SPAN_POLYFILL
#undef VSOMEIP_V3_NEED_SPAN_POLYFILL
#include <cstddef>
#include <type_traits>

namespace vsomeip_v3 {

template<typename T>
class span {
public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using iterator = pointer;

    constexpr span() noexcept : data_(nullptr), size_(0) { }

    constexpr span(pointer _data, size_type _size) noexcept : data_(_data), size_(_size) { }

    template<typename Container>
    constexpr span(Container& _c) noexcept : data_(_c.data()), size_(_c.size()) { }

    template<typename Container>
    constexpr span(const Container& _c) noexcept : data_(_c.data()), size_(_c.size()) { }

    constexpr pointer data() const noexcept { return data_; }
    constexpr size_type size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr iterator begin() const noexcept { return data_; }
    constexpr iterator end() const noexcept { return data_ + size_; }

    constexpr reference operator[](size_type _i) const { return data_[_i]; }

    constexpr span<T> subspan(size_type _offset, size_type _count) const {
        return span<T>(data_ + _offset, _count);
    }

    constexpr span<T> subspan(size_type _offset) const {
        return span<T>(data_ + _offset, size_ - _offset);
    }

private:
    pointer data_;
    size_type size_;
};

} // namespace vsomeip_v3
#endif

#endif // VSOMEIP_V3_SPAN_HPP_
