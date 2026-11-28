// Copyright (c) 2026, kiwec, All rights reserved.
#include "AlgorithmsTest.h"

#include "Algorithms.h"
#include "ContainerRanges.h"
#include "TestMacros.h"
#include "Engine.h"

#include <array>
#include <cstdint>
#include <string>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

namespace Mc::Tests {

namespace {

struct Lcg {
    u64 state;
    u32 next(u32 bound) {
        this->state = this->state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (u32)(this->state >> 33) % bound;
    }
};

// random, few distinct, sorted, reversed, all equal and organ pipe inputs in sizes from 0 to 5000
std::vector<std::vector<int>> make_inputs() {
    std::vector<std::vector<int>> inputs;
    Lcg rng{12345};
    for(int size : {0, 1, 2, 3, 15, 16, 17, 31, 64, 100, 1000, 5000}) {
        std::vector<int> random(size), few(size), sorted(size), reversed(size), equal(size, 7), pipe(size);
        for(int i = 0; i < size; i++) {
            random[i] = (int)rng.next(1000000);
            few[i] = (int)rng.next(4);
            sorted[i] = i;
            reversed[i] = size - i;
            pipe[i] = i < size / 2 ? i : size - i;
        }
        for(auto *v : {&random, &few, &sorted, &reversed, &equal, &pipe}) inputs.push_back(std::move(*v));
    }
    return inputs;
}

// whether a and b hold the same values in any order. stable_sort gets its own checks below.
bool same_values(const std::vector<int> &a, const std::vector<int> &b) {
    if(a.size() != b.size()) return false;
    std::vector<int> sa = a, sb = b;
    Mc::stable_sort(sa.begin(), sa.end());
    Mc::stable_sort(sb.begin(), sb.end());
    return sa == sb;
}

}  // namespace

AlgorithmsTest::AlgorithmsTest() { logRaw("AlgorithmsTest created"); }

void AlgorithmsTest::update() {
    if(m_done) return;
    m_done = true;

    const auto inputs = make_inputs();
    const auto greater = [](int a, int b) { return a > b; };

    TEST_SECTION("min and max");
    {
        TEST_ASSERT_EQ(Mc::min(3, 5), 3, "min");
        TEST_ASSERT_EQ(Mc::max(3, 5), 5, "max");
        TEST_ASSERT_EQ(Mc::min({4, 2, 8}), 2, "min of an initializer list");
        TEST_ASSERT_EQ(Mc::max({4, 2, 8}), 8, "max of an initializer list");
        TEST_ASSERT_EQ(Mc::clamp(12, 0, 10), 10, "clamp lowers a value above the range");
        TEST_ASSERT_EQ(Mc::clamp(-2, 0, 10), 0, "clamp raises a value below the range");
        TEST_ASSERT_EQ(Mc::clamp(4, 0, 10), 4, "clamp keeps a value inside the range");
        const int nine = 9, one = 1;
        const auto [lo, hi] = Mc::minmax(nine, one);
        TEST_ASSERT(lo == 1 && hi == 9, "minmax");
        // std::min and std::max return the first argument on a tie
        const std::pair<int, int> a{1, 0}, b{1, 1};
        const auto by_first = [](const auto &x, const auto &y) { return x.first < y.first; };
        TEST_ASSERT(Mc::min(a, b, by_first).second == 0, "min returns the first argument on a tie");
        TEST_ASSERT(Mc::max(a, b, by_first).second == 0, "max returns the first argument on a tie");
        const std::array v{3, 9, 1, 9, 1};
        TEST_ASSERT(Mc::min_element(v.begin(), v.end()) - v.begin() == 2, "min_element finds the first smallest value");
        TEST_ASSERT(Mc::max_element(v.begin(), v.end()) - v.begin() == 1, "max_element finds the first largest value");
    }

    TEST_SECTION("sort");
    {
        bool sorted = true, permuted = true, sorted_desc = true;
        for(const auto &in : inputs) {
            auto v = in;
            Mc::sort(v.begin(), v.end());
            sorted = sorted && Mc::is_sorted(v.begin(), v.end());
            permuted = permuted && same_values(v, in);
            auto d = in;
            Mc::sort(d.begin(), d.end(), greater);
            sorted_desc = sorted_desc && Mc::is_sorted(d.begin(), d.end(), greater);
        }
        TEST_ASSERT(sorted, "sort orders every input");
        TEST_ASSERT(permuted, "sort keeps the same values");
        TEST_ASSERT(sorted_desc, "sort honors the comparator");

        bool strings = true;
        for(const auto &in : inputs) {
            std::vector<std::string> v, sv;
            for(int x : in) v.push_back(std::to_string(x % 50));
            sv = v;
            Mc::sort(v.begin(), v.end());
            Mc::stable_sort(sv.begin(), sv.end());
            strings = strings && Mc::is_sorted(v.begin(), v.end()) && v == sv &&
                      Mc::none_of(v.begin(), v.end(), [](const std::string &e) { return e.empty(); });
        }
        TEST_ASSERT(strings, "sort and stable_sort keep every string intact");
    }

    TEST_SECTION("stable_sort");
    {
        bool stable = true, permuted = true;
        for(const auto &in : inputs) {
            std::vector<std::pair<int, int>> v;
            for(int i = 0; i < (int)in.size(); i++) v.emplace_back(in[i] % 8, i);
            Mc::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            for(size_t i = 1; i < v.size(); i++) {
                const bool ordered =
                    v[i - 1].first < v[i].first || (v[i - 1].first == v[i].first && v[i - 1].second < v[i].second);
                stable = stable && ordered;
            }
            std::vector<bool> seen(v.size(), false);
            for(const auto &[key, index] : v) {
                permuted = permuted && !seen[index] && key == in[index] % 8;
                seen[index] = true;
            }
        }
        TEST_ASSERT(stable, "stable_sort keeps equal elements in their original order");
        TEST_ASSERT(permuted, "stable_sort keeps the same elements");

        std::vector<std::vector<int>> moveonly(40);
        for(int i = 0; i < 40; i++) moveonly[i] = std::vector<int>(1, (i * 7) % 40);
        Mc::stable_sort(moveonly.begin(), moveonly.end(), [](const auto &a, const auto &b) { return a[0] < b[0]; });
        bool ok = true;
        for(int i = 0; i < 40; i++) ok = ok && moveonly[i].size() == 1 && moveonly[i][0] == i;
        TEST_ASSERT(ok, "stable_sort moves non-trivial elements");
    }

    TEST_SECTION("partial_sort, nth_element and heaps");
    {
        bool partial = true, nth = true, heap = true;
        for(const auto &in : inputs) {
            if(in.empty()) continue;
            auto full = in;
            Mc::sort(full.begin(), full.end());

            auto p = in;
            const size_t k = p.size() / 3;
            Mc::partial_sort(p.begin(), p.begin() + (std::ptrdiff_t)k, p.end());
            partial =
                partial && Mc::equal(p.begin(), p.begin() + (std::ptrdiff_t)k, full.begin()) && same_values(p, in);

            auto n = in;
            const size_t mid = n.size() / 2;
            Mc::nth_element(n.begin(), n.begin() + (std::ptrdiff_t)mid, n.end());
            bool split = n[mid] == full[mid];
            for(size_t i = 0; i < mid; i++) split = split && n[i] <= n[mid];
            for(size_t i = mid; i < n.size(); i++) split = split && n[i] >= n[mid];
            nth = nth && split && same_values(n, in);

            std::vector<int> h;
            for(int x : in) {
                h.push_back(x);
                Mc::push_heap(h.begin(), h.end());
            }
            std::vector<int> popped;
            while(!h.empty()) {
                Mc::pop_heap(h.begin(), h.end());
                popped.push_back(h.back());
                h.pop_back();
            }
            heap = heap && Mc::is_sorted(popped.begin(), popped.end(), greater) && same_values(popped, in);
        }
        TEST_ASSERT(partial, "partial_sort puts the smallest elements in order at the front");
        TEST_ASSERT(nth, "nth_element places the nth element and splits around it");
        TEST_ASSERT(heap, "push_heap and pop_heap pop in descending order");
    }

    TEST_SECTION("merge and set operations");
    {
        const std::vector<int> a{1, 2, 2, 4, 7}, b{2, 3, 4, 4, 8};
        std::vector<int> out;
        Mc::merge(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
        TEST_ASSERT((out == std::vector<int>{1, 2, 2, 2, 3, 4, 4, 4, 7, 8}), "merge");
        out.clear();
        Mc::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
        TEST_ASSERT((out == std::vector<int>{1, 2, 7}), "set_difference");
        out.clear();
        Mc::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
        TEST_ASSERT((out == std::vector<int>{1, 2, 2, 3, 4, 4, 7, 8}), "set_union");
        const std::vector<int> sub{2, 4};
        TEST_ASSERT(Mc::includes(a.begin(), a.end(), sub.begin(), sub.end()), "includes accepts a subset");
        TEST_ASSERT(!Mc::includes(sub.begin(), sub.end(), a.begin(), a.end()), "includes rejects a superset");

        std::vector<std::pair<int, int>> m{{1, 0}, {3, 1}, {5, 2}, {1, 3}, {3, 4}, {6, 5}};
        Mc::inplace_merge(m.begin(), m.begin() + 3, m.end(),
                          [](const auto &x, const auto &y) { return x.first < y.first; });
        const std::vector<std::pair<int, int>> merged{{1, 0}, {1, 3}, {3, 1}, {3, 4}, {5, 2}, {6, 5}};
        TEST_ASSERT(m == merged, "inplace_merge is stable");
    }

    TEST_SECTION("binary search");
    {
        const std::vector<int> v{1, 3, 3, 3, 5, 8};
        TEST_ASSERT(Mc::lower_bound(v.begin(), v.end(), 3) - v.begin() == 1, "lower_bound");
        TEST_ASSERT(Mc::upper_bound(v.begin(), v.end(), 3) - v.begin() == 4, "upper_bound");
        TEST_ASSERT(Mc::lower_bound(v.begin(), v.end(), 9) == v.end(), "lower_bound returns end for a larger value");
        TEST_ASSERT(Mc::binary_search(v.begin(), v.end(), 5), "binary_search finds a present value");
        TEST_ASSERT(!Mc::binary_search(v.begin(), v.end(), 4), "binary_search misses an absent value");
        TEST_ASSERT(Mc::partition_point(v.begin(), v.end(), [](int x) { return x < 5; }) - v.begin() == 4,
                    "partition_point");

        const Mc::ranges::iota indices(10u, 20u);
        const auto squared_at_least_200 = Mc::ranges::lower_bound(indices, 200u, {}, [](unsigned i) { return i * i; });
        TEST_ASSERT(*squared_at_least_200 == 15u, "lower_bound searches an iota with a projection");
        TEST_ASSERT(indices.end() - indices.begin() == 10, "iota spans its range");
    }

    TEST_SECTION("modifying");
    {
        std::vector<int> v{1, 1, 2, 2, 2, 3, 1};
        v.erase(Mc::unique(v.begin(), v.end()), v.end());
        TEST_ASSERT((v == std::vector<int>{1, 2, 3, 1}), "unique");

        // libc++ empties a std::string that is moved onto itself. these checks catch such a move.
        std::vector<std::string> s{"a", "b", "b", "c", "a", "a"};
        s.erase(Mc::unique(s.begin(), s.end()), s.end());
        TEST_ASSERT((s == std::vector<std::string>{"a", "b", "c", "a"}),
                    "unique keeps the elements before a duplicate");
        std::vector<std::string> sp{"x1", "x2", "y1", "x3", "y2"};
        Mc::stable_partition(sp.begin(), sp.end(), [](const std::string &e) { return e[0] == 'x'; });
        TEST_ASSERT((sp == std::vector<std::string>{"x1", "x2", "x3", "y1", "y2"}),
                    "stable_partition keeps the leading elements");

        std::vector<int> r{1, 2, 3, 4, 5};
        Mc::reverse(r.begin(), r.end());
        TEST_ASSERT((r == std::vector<int>{5, 4, 3, 2, 1}), "reverse");
        const auto it = Mc::rotate(r.begin(), r.begin() + 2, r.end());
        TEST_ASSERT((r == std::vector<int>{3, 2, 1, 5, 4}) && it - r.begin() == 3, "rotate");

        std::vector<int> p{1, 2, 3, 4, 5, 6, 7};
        const auto even = [](int x) { return x % 2 == 0; };
        const auto split = Mc::stable_partition(p.begin(), p.end(), even);
        TEST_ASSERT((p == std::vector<int>{2, 4, 6, 1, 3, 5, 7}) && split - p.begin() == 3, "stable_partition");
        std::vector<int> q{1, 2, 3, 4, 5, 6, 7};
        const auto qsplit = Mc::partition(q.begin(), q.end(), even);
        TEST_ASSERT(
            qsplit - q.begin() == 3 && Mc::all_of(q.begin(), qsplit, even) && Mc::none_of(qsplit, q.end(), even),
            "partition");

        std::vector<int> f(4);
        Mc::fill(f.begin(), f.end(), 9);
        Mc::fill_n(f.begin(), 2, 1);
        TEST_ASSERT((f == std::vector<int>{1, 1, 9, 9}), "fill and fill_n");
        Mc::replace(f.begin(), f.end(), 9, 4);
        Mc::replace_if(f.begin(), f.end(), [](int x) { return x == 1; }, 0);
        TEST_ASSERT((f == std::vector<int>{0, 0, 4, 4}), "replace and replace_if");
        std::vector<int> t(4);
        Mc::transform(f.begin(), f.end(), t.begin(), [](int x) { return x + 1; });
        TEST_ASSERT((t == std::vector<int>{1, 1, 5, 5}), "transform");
        TEST_ASSERT(Mc::count(t.begin(), t.end(), 5) == 2 && Mc::count_if(t.begin(), t.end(), even) == 0,
                    "count and count_if");
    }

    TEST_SECTION("comparisons");
    {
        constexpr std::string_view hay = "the quick brown fox", needle = "brown", missing = "brawn";
        TEST_ASSERT(Mc::search(hay.begin(), hay.end(), needle.begin(), needle.end()) - hay.begin() == 10, "search");
        TEST_ASSERT(Mc::search(hay.begin(), hay.end(), missing.begin(), missing.end()) == hay.end(),
                    "search misses an absent needle");
        constexpr std::string_view a = "abc", b = "abd", c = "ab";
        TEST_ASSERT(Mc::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end()), "lexicographical_compare");
        TEST_ASSERT(!Mc::lexicographical_compare(a.begin(), a.end(), c.begin(), c.end()),
                    "lexicographical_compare puts a prefix first");
        TEST_ASSERT(
            Mc::equal(a.begin(), a.end(), a.begin(), a.end()) && !Mc::equal(a.begin(), a.end(), c.begin(), c.end()),
            "equal");
        static_assert(Mc::is_sorted(c.begin(), c.end()));
    }

    TEST_PRINT_RESULTS("AlgorithmsTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
