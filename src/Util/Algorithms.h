// Copyright (c) 2026, kiwec, All rights reserved.
// replacements for <algorithm> function templates
#pragma once

#include <cstddef>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <vector>

namespace Mc {

namespace detail {
struct less_t {
    template <typename A, typename B>
    constexpr bool operator()(const A& a, const B& b) const {
        return a < b;
    }
};

struct equal_t {
    template <typename A, typename B>
    constexpr bool operator()(const A& a, const B& b) const {
        return a == b;
    }
};

template <typename It>
using iter_value_t = std::remove_cvref_t<decltype(*std::declval<It&>())>;

template <typename It>
constexpr void iter_swap(It a, It b) {
    using std::swap;
    swap(*a, *b);
}
}  // namespace detail

// ---- min/max ----

template <typename T>
constexpr const T& min(const T& a, const T& b) {
    return (b < a) ? b : a;
}

template <typename T, typename Comp>
constexpr const T& min(const T& a, const T& b, Comp comp) {
    return comp(b, a) ? b : a;
}

template <typename T>
constexpr const T& max(const T& a, const T& b) {
    return (a < b) ? b : a;
}

template <typename T, typename Comp>
constexpr const T& max(const T& a, const T& b, Comp comp) {
    return comp(a, b) ? b : a;
}

template <typename T>
constexpr T min(std::initializer_list<T> il) {
    const T* best = il.begin();
    for(const T* it = il.begin(); it != il.end(); ++it) {
        if(*it < *best) best = it;
    }
    return *best;
}

template <typename T>
constexpr T max(std::initializer_list<T> il) {
    const T* best = il.begin();
    for(const T* it = il.begin(); it != il.end(); ++it) {
        if(*best < *it) best = it;
    }
    return *best;
}

template <typename T>
constexpr std::pair<const T&, const T&> minmax(const T& a, const T& b) {
    return (b < a) ? std::pair<const T&, const T&>(b, a) : std::pair<const T&, const T&>(a, b);
}

template <typename T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

template <typename It, typename Comp = detail::less_t>
constexpr It min_element(It first, It last, Comp comp = {}) {
    It best = first;
    if(first == last) return best;
    while(++first != last) {
        if(comp(*first, *best)) best = first;
    }
    return best;
}

template <typename It, typename Comp = detail::less_t>
constexpr It max_element(It first, It last, Comp comp = {}) {
    It best = first;
    if(first == last) return best;
    while(++first != last) {
        if(comp(*best, *first)) best = first;
    }
    return best;
}

// ---- non-modifying ----

template <typename It, typename Pred>
constexpr It find_if(It first, It last, Pred pred) {
    for(; first != last; ++first) {
        if(pred(*first)) break;
    }
    return first;
}

template <typename It, typename Pred>
constexpr It find_if_not(It first, It last, Pred pred) {
    for(; first != last; ++first) {
        if(!pred(*first)) break;
    }
    return first;
}

template <typename It, typename T>
constexpr It find(It first, It last, const T& val) {
    for(; first != last; ++first) {
        if(*first == val) break;
    }
    return first;
}

template <typename It, typename Pred>
constexpr bool any_of(It first, It last, Pred pred) {
    return Mc::find_if(first, last, pred) != last;
}

template <typename It, typename Pred>
constexpr bool all_of(It first, It last, Pred pred) {
    return Mc::find_if_not(first, last, pred) == last;
}

template <typename It, typename Pred>
constexpr bool none_of(It first, It last, Pred pred) {
    return Mc::find_if(first, last, pred) == last;
}

template <typename It, typename Pred>
constexpr std::ptrdiff_t count_if(It first, It last, Pred pred) {
    std::ptrdiff_t n = 0;
    for(; first != last; ++first) {
        if(pred(*first)) ++n;
    }
    return n;
}

template <typename It, typename T>
constexpr std::ptrdiff_t count(It first, It last, const T& val) {
    std::ptrdiff_t n = 0;
    for(; first != last; ++first) {
        if(*first == val) ++n;
    }
    return n;
}

template <typename It1, typename It2, typename Pred = detail::equal_t>
constexpr bool equal(It1 first1, It1 last1, It2 first2, It2 last2, Pred pred = {}) {
    for(; first1 != last1 && first2 != last2; ++first1, ++first2) {
        if(!pred(*first1, *first2)) return false;
    }
    return first1 == last1 && first2 == last2;
}

template <typename It1, typename It2, typename Pred = detail::equal_t>
constexpr bool equal(It1 first1, It1 last1, It2 first2, Pred pred = {}) {
    for(; first1 != last1; ++first1, ++first2) {
        if(!pred(*first1, *first2)) return false;
    }
    return true;
}

template <typename It1, typename It2, typename Pred = detail::equal_t>
constexpr It1 search(It1 first, It1 last, It2 s_first, It2 s_last, Pred pred = {}) {
    for(;; ++first) {
        It1 it = first;
        for(It2 s = s_first;; ++it, ++s) {
            if(s == s_last) return first;
            if(it == last) return last;
            if(!pred(*it, *s)) break;
        }
    }
}

template <typename It1, typename It2, typename Comp = detail::less_t>
constexpr bool lexicographical_compare(It1 first1, It1 last1, It2 first2, It2 last2, Comp comp = {}) {
    for(; first1 != last1 && first2 != last2; ++first1, ++first2) {
        if(comp(*first1, *first2)) return true;
        if(comp(*first2, *first1)) return false;
    }
    return first1 == last1 && first2 != last2;
}

template <typename It, typename Comp = detail::less_t>
constexpr bool is_sorted(It first, It last, Comp comp = {}) {
    if(first == last) return true;
    for(It next = first; ++next != last; first = next) {
        if(comp(*next, *first)) return false;
    }
    return true;
}

// ---- binary search (random access) ----

template <typename It, typename Pred>
constexpr It partition_point(It first, It last, Pred pred) {
    auto len = last - first;
    while(len > 0) {
        const auto half = len / 2;
        It mid = first + half;
        if(pred(*mid)) {
            first = mid + 1;
            len -= half + 1;
        } else {
            len = half;
        }
    }
    return first;
}

template <typename It, typename T, typename Comp = detail::less_t>
constexpr It lower_bound(It first, It last, const T& val, Comp comp = {}) {
    return Mc::partition_point(first, last, [&](const auto& e) -> bool { return comp(e, val); });
}

template <typename It, typename T, typename Comp = detail::less_t>
constexpr It upper_bound(It first, It last, const T& val, Comp comp = {}) {
    return Mc::partition_point(first, last, [&](const auto& e) -> bool { return !comp(val, e); });
}

template <typename It, typename T, typename Comp = detail::less_t>
constexpr bool binary_search(It first, It last, const T& val, Comp comp = {}) {
    first = Mc::lower_bound(first, last, val, comp);
    return first != last && !comp(val, *first);
}

// ---- modifying ----

template <typename It, typename Out>
constexpr Out copy(It first, It last, Out out) {
    for(; first != last; ++first, ++out) *out = *first;
    return out;
}

template <typename It, typename Size, typename Out>
constexpr Out copy_n(It first, Size n, Out out) {
    for(Size i = 0; i < n; ++i, ++first, ++out) *out = *first;
    return out;
}

template <typename It, typename T>
constexpr void fill(It first, It last, const T& val) {
    for(; first != last; ++first) *first = val;
}

template <typename It, typename Size, typename T>
constexpr It fill_n(It first, Size n, const T& val) {
    for(Size i = 0; i < n; ++i, ++first) *first = val;
    return first;
}

template <typename It, typename Out, typename Op>
constexpr Out transform(It first, It last, Out out, Op op) {
    for(; first != last; ++first, ++out) *out = op(*first);
    return out;
}

template <typename It, typename T>
constexpr void replace(It first, It last, const T& old_value, const T& new_value) {
    for(; first != last; ++first) {
        if(*first == old_value) *first = new_value;
    }
}

template <typename It, typename Pred, typename T>
constexpr void replace_if(It first, It last, Pred pred, const T& new_value) {
    for(; first != last; ++first) {
        if(pred(*first)) *first = new_value;
    }
}

template <typename It, typename Pred = detail::equal_t>
constexpr It unique(It first, It last, Pred pred = {}) {
    if(first == last) return last;
    // start at the first adjacent pair of equals, so that no element is ever moved onto itself
    It next = first;
    while(++next != last && !pred(*first, *next)) first = next;
    if(next == last) return last;
    while(++next != last) {
        if(!pred(*first, *next)) *++first = std::move(*next);
    }
    return ++first;
}

template <typename It>
constexpr void reverse(It first, It last) {
    while(first != last && first != --last) {
        detail::iter_swap(first, last);
        ++first;
    }
}

template <typename It>
constexpr It rotate(It first, It middle, It last) {
    if(first == middle) return last;
    if(middle == last) return first;
    Mc::reverse(first, middle);
    Mc::reverse(middle, last);
    Mc::reverse(first, last);
    It result = first;
    for(It it = middle; it != last; ++it) ++result;
    return result;
}

template <typename It, typename Pred>
constexpr It partition(It first, It last, Pred pred) {
    first = Mc::find_if_not(first, last, pred);
    if(first == last) return first;
    for(It it = first; ++it != last;) {
        if(pred(*it)) {
            detail::iter_swap(it, first);
            ++first;
        }
    }
    return first;
}

template <typename It, typename Pred>
It stable_partition(It first, It last, Pred pred) {
    first = Mc::find_if_not(first, last, pred);
    std::vector<detail::iter_value_t<It>> rejected;
    It out = first;
    for(It it = first; it != last; ++it) {
        if(pred(*it)) {
            *out = std::move(*it);
            ++out;
        } else {
            rejected.push_back(std::move(*it));
        }
    }
    It split = out;
    for(auto& e : rejected) {
        *out = std::move(e);
        ++out;
    }
    return split;
}

// ---- sorted ranges ----

template <typename It1, typename It2, typename Out, typename Comp = detail::less_t>
constexpr Out merge(It1 first1, It1 last1, It2 first2, It2 last2, Out out, Comp comp = {}) {
    for(; first1 != last1 && first2 != last2; ++out) {
        if(comp(*first2, *first1)) {
            *out = *first2;
            ++first2;
        } else {
            *out = *first1;
            ++first1;
        }
    }
    return Mc::copy(first2, last2, Mc::copy(first1, last1, out));
}

template <typename It1, typename It2, typename Out, typename Comp = detail::less_t>
constexpr Out set_difference(It1 first1, It1 last1, It2 first2, It2 last2, Out out, Comp comp = {}) {
    while(first1 != last1) {
        if(first2 == last2) return Mc::copy(first1, last1, out);
        if(comp(*first1, *first2)) {
            *out = *first1;
            ++out;
            ++first1;
        } else {
            if(!comp(*first2, *first1)) ++first1;
            ++first2;
        }
    }
    return out;
}

template <typename It1, typename It2, typename Out, typename Comp = detail::less_t>
constexpr Out set_union(It1 first1, It1 last1, It2 first2, It2 last2, Out out, Comp comp = {}) {
    for(; first1 != last1; ++out) {
        if(first2 == last2) return Mc::copy(first1, last1, out);
        if(comp(*first2, *first1)) {
            *out = *first2;
            ++first2;
        } else {
            *out = *first1;
            if(!comp(*first1, *first2)) ++first2;
            ++first1;
        }
    }
    return Mc::copy(first2, last2, out);
}

template <typename It1, typename It2, typename Comp = detail::less_t>
constexpr bool includes(It1 first1, It1 last1, It2 first2, It2 last2, Comp comp = {}) {
    for(; first2 != last2; ++first1) {
        if(first1 == last1 || comp(*first2, *first1)) return false;
        if(!comp(*first1, *first2)) ++first2;
    }
    return true;
}

// ---- heaps (random access) ----

namespace detail {
template <typename It, typename Comp>
constexpr void sift_down(It first, std::ptrdiff_t len, std::ptrdiff_t i, Comp& comp) {
    auto val = std::move(first[i]);
    for(;;) {
        std::ptrdiff_t child = 2 * i + 1;
        if(child >= len) break;
        if(child + 1 < len && comp(first[child], first[child + 1])) ++child;
        if(!comp(val, first[child])) break;
        first[i] = std::move(first[child]);
        i = child;
    }
    first[i] = std::move(val);
}
}  // namespace detail

template <typename It, typename Comp = detail::less_t>
constexpr void make_heap(It first, It last, Comp comp = {}) {
    const std::ptrdiff_t len = last - first;
    for(std::ptrdiff_t i = len / 2 - 1; i >= 0; --i) detail::sift_down(first, len, i, comp);
}

template <typename It, typename Comp = detail::less_t>
constexpr void push_heap(It first, It last, Comp comp = {}) {
    std::ptrdiff_t i = (last - first) - 1;
    if(i <= 0) return;
    auto val = std::move(first[i]);
    while(i > 0) {
        const std::ptrdiff_t parent = (i - 1) / 2;
        if(!comp(first[parent], val)) break;
        first[i] = std::move(first[parent]);
        i = parent;
    }
    first[i] = std::move(val);
}

template <typename It, typename Comp = detail::less_t>
constexpr void pop_heap(It first, It last, Comp comp = {}) {
    const std::ptrdiff_t len = last - first;
    if(len <= 1) return;
    detail::iter_swap(first, first + (len - 1));
    detail::sift_down(first, len - 1, 0, comp);
}

template <typename It, typename Comp = detail::less_t>
constexpr void sort_heap(It first, It last, Comp comp = {}) {
    for(std::ptrdiff_t len = last - first; len > 1; --len) {
        detail::iter_swap(first, first + (len - 1));
        detail::sift_down(first, len - 1, 0, comp);
    }
}

// ---- sorting (random access) ----

namespace detail {
inline constexpr std::ptrdiff_t insertion_sort_threshold = 16;

template <typename It, typename Comp>
constexpr void insertion_sort(It first, It last, Comp& comp) {
    if(first == last) return;
    for(It i = first + 1; i != last; ++i) {
        auto val = std::move(*i);
        It j = i;
        for(; j != first && comp(val, *(j - 1)); --j) *j = std::move(*(j - 1));
        *j = std::move(val);
    }
}

// moves the median of *a, *b and *c to *result
template <typename It, typename Comp>
constexpr void move_median_to_first(It result, It a, It b, It c, Comp& comp) {
    if(comp(*a, *b)) {
        if(comp(*b, *c)) {
            detail::iter_swap(result, b);
        } else if(comp(*a, *c)) {
            detail::iter_swap(result, c);
        } else {
            detail::iter_swap(result, a);
        }
    } else if(comp(*a, *c)) {
        detail::iter_swap(result, a);
    } else if(comp(*b, *c)) {
        detail::iter_swap(result, c);
    } else {
        detail::iter_swap(result, b);
    }
}

// partitions [first, last) around *pivot, which must lie outside it
// the median-of-three guarantees an element on each side that stops the scans
template <typename It, typename Comp>
constexpr It unguarded_partition(It first, It last, It pivot, Comp& comp) {
    for(;;) {
        while(comp(*first, *pivot)) ++first;
        --last;
        while(comp(*pivot, *last)) --last;
        if(!(first < last)) return first;
        detail::iter_swap(first, last);
        ++first;
    }
}

template <typename It, typename Comp>
constexpr It partition_pivot(It first, It last, Comp& comp) {
    It mid = first + (last - first) / 2;
    move_median_to_first(first, first + 1, mid, last - 1, comp);
    return unguarded_partition(first + 1, last, first, comp);
}

template <typename It, typename Comp>
constexpr void introsort_loop(It first, It last, int depth_limit, Comp& comp) {
    while(last - first > insertion_sort_threshold) {
        if(depth_limit == 0) {
            Mc::make_heap(first, last, comp);
            Mc::sort_heap(first, last, comp);
            return;
        }
        --depth_limit;
        It cut = partition_pivot(first, last, comp);
        introsort_loop(cut, last, depth_limit, comp);
        last = cut;
    }
}

template <typename It, typename Buf, typename Comp>
void merge_sort(It first, It last, Buf& buf, Comp& comp) {
    const std::ptrdiff_t len = last - first;
    if(len <= insertion_sort_threshold) {
        insertion_sort(first, last, comp);
        return;
    }
    It mid = first + len / 2;
    merge_sort(first, mid, buf, comp);
    merge_sort(mid, last, buf, comp);
    if(!comp(*mid, *(mid - 1))) return;

    // merge the left half (moved out to buf) with the right half, taking from the left on ties
    buf.clear();
    for(It it = first; it != mid; ++it) buf.push_back(std::move(*it));
    auto left = buf.begin();
    It right = mid;
    It out = first;
    while(left != buf.end() && right != last) {
        if(comp(*right, *left)) {
            *out = std::move(*right);
            ++right;
        } else {
            *out = std::move(*left);
            ++left;
        }
        ++out;
    }
    for(; left != buf.end(); ++left, ++out) *out = std::move(*left);
}
}  // namespace detail

template <typename It, typename Comp = detail::less_t>
constexpr void sort(It first, It last, Comp comp = {}) {
    if(last - first < 2) return;
    int depth_limit = 0;
    for(auto n = last - first; n > 1; n >>= 1) depth_limit += 2;
    detail::introsort_loop(first, last, depth_limit, comp);
    detail::insertion_sort(first, last, comp);
}

template <typename It, typename Comp = detail::less_t>
void stable_sort(It first, It last, Comp comp = {}) {
    const std::ptrdiff_t len = last - first;
    if(len < 2) return;
    std::vector<detail::iter_value_t<It>> buf;
    buf.reserve(static_cast<size_t>(len / 2 + 1));
    detail::merge_sort(first, last, buf, comp);
}

template <typename It, typename Comp = detail::less_t>
void inplace_merge(It first, It middle, It last, Comp comp = {}) {
    if(first == middle || middle == last || !comp(*middle, *(middle - 1))) return;
    std::vector<detail::iter_value_t<It>> left;
    left.reserve(static_cast<size_t>(middle - first));
    for(It it = first; it != middle; ++it) left.push_back(std::move(*it));
    auto l = left.begin();
    It r = middle;
    It out = first;
    while(l != left.end() && r != last) {
        if(comp(*r, *l)) {
            *out = std::move(*r);
            ++r;
        } else {
            *out = std::move(*l);
            ++l;
        }
        ++out;
    }
    for(; l != left.end(); ++l, ++out) *out = std::move(*l);
}

template <typename It, typename Comp = detail::less_t>
constexpr void partial_sort(It first, It middle, It last, Comp comp = {}) {
    if(first == middle) return;
    Mc::make_heap(first, middle, comp);
    const std::ptrdiff_t len = middle - first;
    for(It it = middle; it != last; ++it) {
        if(comp(*it, *first)) {
            detail::iter_swap(it, first);
            detail::sift_down(first, len, 0, comp);
        }
    }
    Mc::sort_heap(first, middle, comp);
}

template <typename It, typename Comp = detail::less_t>
constexpr void nth_element(It first, It nth, It last, Comp comp = {}) {
    if(nth == last) return;
    while(last - first > 3) {
        It cut = detail::partition_pivot(first, last, comp);
        if(cut <= nth) {
            first = cut;
        } else {
            last = cut;
        }
    }
    detail::insertion_sort(first, last, comp);
}

}  // namespace Mc
