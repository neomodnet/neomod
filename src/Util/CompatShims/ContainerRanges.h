// Copyright (c) 2026, WH, All rights reserved.
// compatibility wrappers for some C++23+ ranges features, and lightweight versions of the std::ranges algorithms
#pragma once

#if __has_include("config.h")
#include "config.h"
#endif

#include "noinclude.h"
#include "Algorithms.h"

#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

namespace Mc::ranges {

// NOLINTBEGIN(cppcoreguidelines-missing-std-forward)

template <typename Container, typename R>
constexpr void assign(Container& c, R&& rg) {
    c.assign(std::begin(rg), std::end(rg));
}

template <typename Container, typename R>
constexpr void append(Container& c, R&& rg) {
    if(c.empty()) {
        assign(c, std::forward<R>(rg));
    } else {
        c.insert(c.end(), std::begin(rg), std::end(rg));
    }
}

template <typename Container, typename R>
constexpr typename Container::iterator insert(Container& c, typename Container::const_iterator pos, R&& rg) {
    if(pos == c.end()) {
        const auto offset = c.size();
        append(c, std::forward<R>(rg));
        auto it = c.begin();
        std::advance(it, static_cast<typename Container::difference_type>(offset));
        return it;
    } else {
        return c.insert(pos, std::begin(rg), std::end(rg));
    }
}

// unconstrained versions of the std::ranges algorithms (same arguments, including projections), several times cheaper
// to compile per call site. iterator-returning ones only take lvalue ranges, so they can't dangle.

namespace detail {
// comparator that compares the projected elements, or the plain comparator without a projection
template <typename Comp, typename Proj>
constexpr forceinline auto projected(Comp comp, Proj proj) {
    if constexpr(std::is_same_v<Proj, std::identity>) {
        return comp;
    } else {
        return [comp, proj](auto&& a, auto&& b) -> bool {
            return std::invoke(comp, std::invoke(proj, a), std::invoke(proj, b));
        };
    }
}
}  // namespace detail

template <typename R, typename T, typename Proj = std::identity>
constexpr forceinline auto find(R& r, const T& val, Proj proj = {}) {
    if constexpr(std::is_same_v<Proj, std::identity>) {
        return Mc::find(std::begin(r), std::end(r), val);
    } else {
        auto it = std::begin(r);
        for(const auto end = std::end(r); it != end; ++it) {
            if(std::invoke(proj, *it) == val) break;
        }
        return it;
    }
}

template <typename R, typename Pred, typename Proj = std::identity>
constexpr forceinline auto find_if(R& r, Pred pred, Proj proj = {}) {
    auto it = std::begin(r);
    for(const auto end = std::end(r); it != end; ++it) {
        if(std::invoke(pred, std::invoke(proj, *it))) break;
    }
    return it;
}

template <typename R, typename T, typename Proj = std::identity>
constexpr forceinline bool contains(const R& r, const T& val, Proj proj = {}) {
    return Mc::ranges::find(r, val, proj) != std::end(r);
}

template <typename R, typename Pred, typename Proj = std::identity>
constexpr forceinline bool any_of(R&& r, Pred pred, Proj proj = {}) {
    for(auto&& e : r) {
        if(std::invoke(pred, std::invoke(proj, e))) return true;
    }
    return false;
}

template <typename R, typename Pred, typename Proj = std::identity>
constexpr forceinline bool all_of(R&& r, Pred pred, Proj proj = {}) {
    for(auto&& e : r) {
        if(!std::invoke(pred, std::invoke(proj, e))) return false;
    }
    return true;
}

template <typename R, typename Pred, typename Proj = std::identity>
constexpr forceinline bool none_of(R&& r, Pred pred, Proj proj = {}) {
    return !Mc::ranges::any_of(r, pred, proj);
}

template <typename R, typename Pred, typename Proj = std::identity>
constexpr forceinline std::ptrdiff_t count_if(R&& r, Pred pred, Proj proj = {}) {
    std::ptrdiff_t n = 0;
    for(auto&& e : r) {
        if(std::invoke(pred, std::invoke(proj, e))) ++n;
    }
    return n;
}

template <typename R, typename T, typename Proj = std::identity>
constexpr forceinline std::ptrdiff_t count(R&& r, const T& val, Proj proj = {}) {
    if constexpr(std::is_same_v<Proj, std::identity>) {
        return Mc::count(std::begin(r), std::end(r), val);
    } else {
        return Mc::ranges::count_if(r, [&val](const auto& e) -> bool { return e == val; }, proj);
    }
}

template <typename R1, typename R2, typename Pred = std::equal_to<>>
constexpr forceinline bool equal(const R1& r1, const R2& r2, Pred pred = {}) {
    return Mc::equal(std::begin(r1), std::end(r1), std::begin(r2), std::end(r2), pred);
}

template <typename R, typename T>
constexpr forceinline void replace(R&& r, const T& old_value, const T& new_value) {
    Mc::replace(std::begin(r), std::end(r), old_value, new_value);
}

template <typename R, typename Comp = std::less<>, typename Proj = std::identity>
constexpr forceinline void sort(R&& r, Comp comp = {}, Proj proj = {}) {
    Mc::sort(std::begin(r), std::end(r), detail::projected(comp, proj));
}

template <typename R, typename Comp = std::less<>, typename Proj = std::identity>
constexpr forceinline void stable_sort(R&& r, Comp comp = {}, Proj proj = {}) {
    Mc::stable_sort(std::begin(r), std::end(r), detail::projected(comp, proj));
}

template <typename R, typename Comp = std::less<>, typename Proj = std::identity>
constexpr forceinline auto min_element(R& r, Comp comp = {}, Proj proj = {}) {
    return Mc::min_element(std::begin(r), std::end(r), detail::projected(comp, proj));
}

template <typename R, typename Comp = std::less<>, typename Proj = std::identity>
constexpr forceinline auto max_element(R& r, Comp comp = {}, Proj proj = {}) {
    return Mc::max_element(std::begin(r), std::end(r), detail::projected(comp, proj));
}

template <typename R, typename T, typename Comp = std::less<>, typename Proj = std::identity>
constexpr forceinline auto lower_bound(R& r, const T& val, Comp comp = {}, Proj proj = {}) {
    return Mc::lower_bound(std::begin(r), std::end(r), val, [comp, proj](auto&& e, const T& v) -> bool {
        return std::invoke(comp, std::invoke(proj, e), v);
    });
}

template <typename R, typename T, typename Comp = std::less<>, typename Proj = std::identity>
constexpr forceinline auto upper_bound(R& r, const T& val, Comp comp = {}, Proj proj = {}) {
    return Mc::upper_bound(std::begin(r), std::end(r), val, [comp, proj](const T& v, auto&& e) -> bool {
        return std::invoke(comp, v, std::invoke(proj, e));
    });
}

// iterates an lvalue range backwards, like std::views::reverse
template <typename R>
struct reversed {
    R& r;
    constexpr auto begin() const { return std::rbegin(r); }
    constexpr auto end() const { return std::rend(r); }
};
template <typename R>
reversed(R&) -> reversed<R>;

// the integers in [first, last), like std::views::iota
template <typename T>
struct iota {
    struct iterator {
        T value;
        constexpr T operator*() const { return value; }
        constexpr iterator& operator++() {
            ++value;
            return *this;
        }
        constexpr iterator operator+(std::ptrdiff_t n) const { return {static_cast<T>(value + n)}; }
        constexpr std::ptrdiff_t operator-(iterator other) const {
            return static_cast<std::ptrdiff_t>(value - other.value);
        }
        constexpr bool operator==(const iterator&) const = default;
    };

    T first, last;
    constexpr iota(T first, T last) : first(first), last(last) {}
    constexpr iterator begin() const { return {first}; }
    constexpr iterator end() const { return {last}; }
};

// NOLINTEND(cppcoreguidelines-missing-std-forward)

}  // namespace Mc::ranges
