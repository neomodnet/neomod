#pragma once
// Copyright (c) 2023, kiwec & 2025-2026, WH, All rights reserved.

#include "ankerl/unordered_dense.h"

#include <string>
#include <string_view>
#include <cctype>
#include <algorithm>

namespace Hash {
namespace flat = ankerl::unordered_dense;

// transparent hash and equality for heterogeneous lookup
struct UnstableStringHash {
    using is_transparent = void;  // enable heterogeneous overloads
    using is_avalanching = void;  // mark class as high quality avalanching hash

    [[nodiscard]] auto operator()(std::string_view str) const noexcept -> uint64_t {
        return flat::hash<std::string_view>{}(str);
    }
};

// unstable because unordered_dense doesn't guarantee iterator/reference stability
template <typename T>
using unstable_stringmap = flat::map<std::string, T, UnstableStringHash, std::equal_to<>>;

struct StableStringHash {
    using is_transparent = void;

    uint64_t operator()(std::string_view sv) const { return std::hash<std::string_view>{}(sv); }
    uint64_t operator()(const std::string &s) const { return std::hash<std::string>{}(s); }
    uint64_t operator()(const char *s) const { return std::hash<std::string_view>{}(std::string_view(s)); }
};

template <typename T>
using stable_stringmap = std::unordered_map<std::string, T, StableStringHash, std::equal_to<>>;

namespace detail {
// duplicated here to avoid including SString.h
constexpr unsigned char ascii_tolower(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
}
}  // namespace detail

struct StringHashNcase {
   private:
    [[nodiscard]] inline uint64_t hashFunc(std::string_view str) const {
        uint64_t hash = 0;
        for(auto &c : str) {
            hash = hash * 31 + detail::ascii_tolower(static_cast<unsigned char>(c));
        }
        return hash;
    }

   public:
    using is_transparent = void;

    uint64_t operator()(const std::string &s) const { return hashFunc(s); }
    uint64_t operator()(std::string_view sv) const { return hashFunc(sv); }
};

struct StringEqualNcase {
   private:
    [[nodiscard]] inline bool equality(std::string_view lhs, std::string_view rhs) const {
        return std::ranges::equal(lhs, rhs, [](unsigned char a, unsigned char b) -> bool {
            return detail::ascii_tolower(a) == detail::ascii_tolower(b);
        });
    }

   public:
    using is_transparent = void;

    bool operator()(const std::string &lhs, const std::string &rhs) const { return equality(lhs, rhs); }
    bool operator()(const std::string &lhs, std::string_view rhs) const { return equality(lhs, rhs); }
    bool operator()(std::string_view lhs, const std::string &rhs) const { return equality(lhs, rhs); }
};

// case-insensitive string map
template <typename T>
using unstable_ncase_stringmap = Hash::flat::map<std::string, T, StringHashNcase, StringEqualNcase>;

template <typename T>
using unstable_ncase_set = Hash::flat::set<T, StringHashNcase, StringEqualNcase>;

}  // namespace Hash
