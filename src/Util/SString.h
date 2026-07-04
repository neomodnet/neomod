// Copyright (c) 2023-2024, kiwec & 2025, WH, All rights reserved.
#pragma once
#include "noinclude.h"

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <span>

// fast and small string manipulation helpers

namespace SString {

constexpr forceinline unsigned char ascii_tolower(unsigned char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c;
}
constexpr forceinline bool ascii_isalnum(unsigned char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

constexpr int alnum_cmp3(std::string_view a, std::string_view b) noexcept {
    size_t i = 0, j = 0;
    for(;;) {
        while(i < a.size() && !ascii_isalnum(static_cast<unsigned char>(a[i]))) ++i;
        while(j < b.size() && !ascii_isalnum(static_cast<unsigned char>(b[j]))) ++j;
        if(i == a.size() || j == b.size()) break;
        const unsigned char ca = ascii_tolower(static_cast<unsigned char>(a[i]));
        const unsigned char cb = ascii_tolower(static_cast<unsigned char>(b[j]));
        if(ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
        ++i;
        ++j;
    }
    // The break guarantees at least one string reached its end,
    // so at most one of these is true.
    const bool a_has = i < a.size();
    const bool b_has = j < b.size();
    if(a_has != b_has) return a_has ? 1 : -1;
    return 0;
}

constexpr int strcase_cmp3(std::string_view a, std::string_view b) noexcept {
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for(size_t i = 0; i < n; ++i) {
        const unsigned char ca = ascii_tolower(static_cast<unsigned char>(a[i]));
        const unsigned char cb = ascii_tolower(static_cast<unsigned char>(b[i]));
        if(ca != cb) return static_cast<int>(ca) - static_cast<int>(cb);
    }
    // common prefix is equal: shorter sorts first
    if(a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return 0;
}

// strcasecmp accepting string_views
constexpr forceinline bool strcase_comp(std::string_view a, std::string_view b) noexcept {
    return strcase_cmp3(a, b) < 0;
}
constexpr forceinline bool strcase_equal(std::string_view a, std::string_view b) noexcept {
    return strcase_cmp3(a, b) == 0;
}

// alphanumeric string comparator that ignores special characters at the start of strings
constexpr forceinline bool alnum_comp(std::string_view a, std::string_view b) noexcept { return alnum_cmp3(a, b) < 0; }

template <typename S = char>
using split_join_enabled_t =
    std::enable_if_t<std::is_same_v<std::decay_t<S>, char> || std::is_same_v<std::decay_t<S>, const char*> ||
                         std::is_same_v<std::decay_t<S>, std::string_view>,
                     bool>;

template <typename R = std::string_view>
using split_ret_enabled_t =
    std::enable_if_t<std::is_same_v<std::decay_t<R>, std::string> || std::is_same_v<std::decay_t<R>, std::string_view>,
                     bool>;

// std string splitting
template <typename R = std::string_view, typename S = char, split_ret_enabled_t<R> = true,
          split_join_enabled_t<S> = true>
std::vector<R> split(std::string_view s, S d);

// split on newlines, handling both \n and \r\n line endings
template <typename R = std::string_view, split_ret_enabled_t<R> = true>
std::vector<R> split_newlines(std::string_view s);

// same as above, except put the results into an existing std::vector<R> instead of creating a new one
// NOTE: the given vector will unconditionally be .clear()ed
// NOTE: delim_len is unnecessary to pass, only used as an internal optimization
template <typename R = std::string_view, typename S = char, split_ret_enabled_t<R> = true,
          split_join_enabled_t<S> = true>
void split(std::vector<R>& ret, std::string_view s, S d, size_t delim_len = 0);

// split on newlines, handling both \n and \r\n line endings
template <typename R = std::string_view, split_ret_enabled_t<R> = true>
void split_newlines(std::vector<R>& ret, std::string_view s);

// join a vector of std::strings
template <typename S = char, split_join_enabled_t<S> = true>
std::string join(std::span<const std::string> strings, S delim = ' ');

// in-place whitespace/newline trimming (both sides)
void trim_inplace(std::string& str);

// in-place whitespace/newline trimming (both sides)
// adjusts the view to exclude leading/trailing whitespace
void trim_inplace(std::string_view& str);

// case-insensitive strstr
bool contains_ncase(const std::string_view haystack, const std::string_view needle);

// empty or whitespace only
bool is_wspace_only(const std::string_view str);

// check if first non-whitespace sequence matches comment token
bool is_comment(const std::string_view str, const std::string_view token = "//");

// only really valid for ASCII
void lower_inplace(std::string& str);

// only really valid for ASCII
std::string to_lower(const std::string_view str);

std::unique_ptr<char[]> strcpy_u(std::string_view sv);
std::unique_ptr<char[]> strcpy_u(const char* data);

// extern template decls, we only instantiate+compile them in SString.cpp

extern template std::vector<std::string> split<std::string, char>(std::string_view, char);
extern template std::vector<std::string> split<std::string, const char*>(std::string_view, const char*);
extern template std::vector<std::string> split<std::string, std::string_view>(std::string_view, std::string_view);

extern template std::vector<std::string_view> split<std::string_view, char>(std::string_view, char);
extern template std::vector<std::string_view> split<std::string_view, const char*>(std::string_view, const char*);
extern template std::vector<std::string_view> split<std::string_view, std::string_view>(std::string_view,
                                                                                        std::string_view);

extern template void split<std::string, char>(std::vector<std::string>&, std::string_view, char, size_t);
extern template void split<std::string, const char*>(std::vector<std::string>&, std::string_view, const char*, size_t);
extern template void split<std::string, std::string_view>(std::vector<std::string>&, std::string_view, std::string_view,
                                                          size_t);
extern template void split<std::string_view, char>(std::vector<std::string_view>&, std::string_view, char, size_t);
extern template void split<std::string_view, const char*>(std::vector<std::string_view>&, std::string_view, const char*,
                                                          size_t);
extern template void split<std::string_view, std::string_view>(std::vector<std::string_view>&, std::string_view,
                                                               std::string_view, size_t);
extern template std::vector<std::string> split_newlines<std::string>(std::string_view);
extern template std::vector<std::string_view> split_newlines<std::string_view>(std::string_view);

extern template void split_newlines<std::string>(std::vector<std::string>&, std::string_view);
extern template void split_newlines<std::string_view>(std::vector<std::string_view>&, std::string_view);

extern template std::string join<char>(std::span<const std::string>, char);
extern template std::string join<const char*>(std::span<const std::string>, const char*);
extern template std::string join<std::string_view>(std::span<const std::string>, std::string_view);

#ifndef BUILD_TOOLS_ONLY
// format an integer with thousands separators (locale-dependent commas/spaces/periods)
template <typename T>
concept Integral = std::is_integral_v<T>;

template <Integral T>
std::string thousands(T n);

extern template std::string thousands(int64_t n);
extern template std::string thousands(uint64_t n);
extern template std::string thousands(int32_t n);
extern template std::string thousands(uint32_t n);
#endif

}  // namespace SString
