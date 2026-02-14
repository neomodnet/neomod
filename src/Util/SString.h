// Copyright (c) 2023-2024, kiwec & 2025, WH, All rights reserved.
#pragma once
#include "noinclude.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <type_traits>
#include <cassert>

// non-UString-related fast and small string manipulation helpers

namespace SString {

template <typename S = char>
using split_join_enabled_t =
    std::enable_if_t<std::is_same_v<std::decay_t<S>, char> || std::is_same_v<std::decay_t<S>, const char*> ||
                         std::is_same_v<std::decay_t<S>, std::string_view>,
                     bool>;

template <typename R = std::string_view>
using split_ret_enabled_t =
    std::enable_if_t<std::is_same_v<std::decay_t<R>, std::string> || std::is_same_v<std::decay_t<R>, std::string_view>,
                     bool>;

// std string splitting, for if we don't want to create UStrings everywhere (slow and heavy)
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
std::string join(const std::vector<std::string>& strings, S delim = ' ');

// alphanumeric string comparator that ignores special characters at the start of strings
bool alnum_comp(std::string_view a, std::string_view b);

// in-place whitespace/newline trimming (both sides)
static forceinline void trim_inplace(std::string& str) {
    if(str.empty()) return;
    str.erase(0, str.find_first_not_of(" \t\r\n"));
    str.erase(str.find_last_not_of(" \t\r\n") + 1);
}

// in-place whitespace/newline trimming (both sides)
// adjusts the view to exclude leading/trailing whitespace
static forceinline void trim_inplace(std::string_view& str) {
    if(str.empty()) return;
    size_t start = str.find_first_not_of(" \t\r\n");
    if(start == std::string_view::npos) {
        str = std::string_view();
        return;
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    str = str.substr(start, end - start + 1);
}

// case-insensitive strstr
static forceinline bool contains_ncase(const std::string_view haystack, const std::string_view needle) {
    return !haystack.empty() && !std::ranges::search(haystack, needle, [](unsigned char ch1, unsigned char ch2) {
                                     return std::tolower(ch1) == std::tolower(ch2);
                                 }).empty();
}

// empty or whitespace only
static forceinline bool is_wspace_only(const std::string_view str) {
    return str.empty() || std::ranges::all_of(str, [](unsigned char c) { return std::isspace(c) != 0; });
}

// check if first non-whitespace sequence matches comment token
static forceinline bool is_comment(const std::string_view str, const std::string_view token = "//") {
    size_t start = str.find_first_not_of(" \t\r\n");
    if(start == std::string_view::npos) return false;
    return str.substr(start).starts_with(token);
}

// only really valid for ASCII
static forceinline void lower_inplace(std::string& str) {
    if(str.empty()) return;
    std::ranges::transform(str, str.begin(), [](unsigned char c) { return std::tolower(c); });
}

// only really valid for ASCII
static forceinline std::string to_lower(const std::string_view str) {
    std::string lstr{str.data(), str.length()};
    if(str.empty()) return lstr;
    lower_inplace(lstr);
    return lstr;
}

// format an integer with thousands separators (locale-dependent commas/spaces/periods)
template <typename T>
concept Integral = std::is_integral_v<T>;

template <Integral T> 
std::string thousands(T n);

}  // namespace SString
