// Copyright (c) 2023-2024, kiwec & 2025, WH, All rights reserved.
#if __has_include("config.h")
#include "config.h"
#endif

#include "SString.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>

#ifndef BUILD_TOOLS_ONLY
#include "BaseEnvironment.h"
#if !defined(__GLIBCXX__) || defined(__EMSCRIPTEN__)
#include "fmt/format.h"
#else
#include <cinttypes>
#endif
#endif

namespace SString {

static_assert(strcase_comp("ab", "ABC"));  // "ab" < "abc"
static_assert(!strcase_comp("ABC", "ab"));
static_assert(strcase_equal("Hello", "hello"));
static_assert(alnum_cmp3("!!cat", "Cat...") == 0);
static_assert(alnum_comp("cat", "category"));  // shorter alnum run first

void trim_inplace(std::string& str) noexcept {
    if(str.empty()) return;
    str.erase(0, str.find_first_not_of(" \t\r\n"));
    str.erase(str.find_last_not_of(" \t\r\n") + 1);
}

// adjusts the view to exclude leading/trailing whitespace
void trim_inplace(std::string_view& str) noexcept {
    if(str.empty()) return;
    size_t start = str.find_first_not_of(" \t\r\n");
    if(start == std::string_view::npos) {
        str = std::string_view();
        return;
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    str = str.substr(start, end - start + 1);
}

bool contains_ncase(const std::string_view haystack, const std::string_view needle) noexcept {
    return !haystack.empty() && !std::ranges::search(haystack, needle, [](unsigned char ch1, unsigned char ch2) {
                                     return ascii_tolower(ch1) == ascii_tolower(ch2);
                                 }).empty();
}

bool is_wspace_only(const std::string_view str) noexcept {
    return str.empty() || std::ranges::all_of(str, [](unsigned char c) { return std::isspace(c) != 0; });
}

bool is_comment(const std::string_view str, const std::string_view token) noexcept {
    size_t start = str.find_first_not_of(" \t\r\n");
    if(start == std::string_view::npos) return false;
    return str.substr(start).starts_with(token);
}

void lower_inplace(std::string& str) noexcept {
    if(str.empty()) return;
    std::ranges::transform(str, str.begin(), [](unsigned char c) { return ascii_tolower(c); });
}

std::string to_lower(const std::string_view str) noexcept {
    std::string lstr{str.data(), str.length()};
    if(str.empty()) return lstr;
    lower_inplace(lstr);
    return lstr;
}

std::unique_ptr<char[]> strcpy_u(std::string_view sv) noexcept {
    if(sv.empty()) return nullptr;

    const size_t len = sv.length();
    std::unique_ptr<char[]> ret = std::make_unique_for_overwrite<char[]>(len + 1);
    std::memcpy(ret.get(), sv.data(), len);
    ret[len] = '\0';

    return ret;
}

std::unique_ptr<char[]> strcpy_u(const char* data) noexcept {
    if(!data) return nullptr;

    const size_t len = std::strlen(data);
    std::unique_ptr<char[]> ret = std::make_unique_for_overwrite<char[]>(len + 1);
    std::memcpy(ret.get(), data, len);
    ret[len] = '\0';

    return ret;
}

template <typename R, typename S, split_ret_enabled_t<R>, split_join_enabled_t<S>>
void split(std::vector<R>& r, std::string_view s, S delim, size_t delim_len) noexcept {
    r.clear();

    if(delim_len == 0) {
        if constexpr(std::is_same_v<std::decay_t<S>, const char*>) {
            delim_len = strlen(delim);
        } else if constexpr(std::is_same_v<std::decay_t<S>, std::string_view>) {
            delim_len = delim.size();
        } else {  // single char
            delim_len = 1;
        }
    }

    size_t i = 0, j = 0;
    if constexpr(std::is_same_v<std::decay_t<R>, std::string>) {
        while((j = s.find(delim, i)) != s.npos) r.emplace_back(s, i, j - i), i = j + delim_len;
        if(i < s.size()) r.emplace_back(s, i, s.size() - i);
    } else {  // string_view
        while((j = s.find(delim, i)) != s.npos) r.emplace_back(s.substr(i, j - i)), i = j + delim_len;
        if(i < s.size()) r.emplace_back(s.substr(i));
    }

    return;
}

// split a string by a delimiter
template <typename R, typename S, split_ret_enabled_t<R>, split_join_enabled_t<S>>
std::vector<R> split(std::string_view s, S delim) noexcept {
    std::vector<R> r;

    size_t delim_len = 0;
    if constexpr(std::is_same_v<std::decay_t<S>, const char*>) {
        delim_len = strlen(delim);
    } else if constexpr(std::is_same_v<std::decay_t<S>, std::string_view>) {
        delim_len = delim.size();
    } else {  // single char
        delim_len = 1;
    }

    // pre-count delimiter occurrences and reserve that amount (to avoid reallocations)
    size_t count = 0;
    if constexpr(std::is_same_v<std::decay_t<S>, char>) {
        count = std::ranges::count(s, delim);
    } else {
        for(size_t pos = 0; (pos = s.find(delim, pos)) != s.npos; pos += delim_len) count++;
    }
    r.reserve(count + 1);

    split(r, s, delim, delim_len);
    return r;
}

// explicit instantiations
template std::vector<std::string> split<std::string, char>(std::string_view, char) noexcept;
template std::vector<std::string> split<std::string, const char*>(std::string_view, const char*) noexcept;
template std::vector<std::string> split<std::string, std::string_view>(std::string_view, std::string_view) noexcept;

template std::vector<std::string_view> split<std::string_view, char>(std::string_view, char) noexcept;
template std::vector<std::string_view> split<std::string_view, const char*>(std::string_view, const char*) noexcept;
template std::vector<std::string_view> split<std::string_view, std::string_view>(std::string_view,
                                                                                 std::string_view) noexcept;

template void split<std::string, char>(std::vector<std::string>&, std::string_view, char, size_t) noexcept;
template void split<std::string, const char*>(std::vector<std::string>&, std::string_view, const char*,
                                              size_t) noexcept;
template void split<std::string, std::string_view>(std::vector<std::string>&, std::string_view, std::string_view,
                                                   size_t) noexcept;

template void split<std::string_view, char>(std::vector<std::string_view>&, std::string_view, char, size_t) noexcept;
template void split<std::string_view, const char*>(std::vector<std::string_view>&, std::string_view, const char*,
                                                   size_t) noexcept;
template void split<std::string_view, std::string_view>(std::vector<std::string_view>&, std::string_view,
                                                        std::string_view, size_t) noexcept;

template <typename R, split_ret_enabled_t<R>>
void split_newlines(std::vector<R>& r, std::string_view s) noexcept {
    r.clear();

    size_t i = 0, j = 0;
    while((j = s.find('\n', i)) != s.npos) {
        size_t end = j;
        if(end > i && s[end - 1] == '\r') end--;

        if constexpr(std::is_same_v<std::decay_t<R>, std::string>) {
            r.emplace_back(s, i, end - i);
        } else {
            r.emplace_back(s.substr(i, end - i));
        }
        i = j + 1;
    }

    // remainder after last \n (or entire string if no \n found)
    size_t end = s.size();
    if(end > i && s[end - 1] == '\r') end--;

    if(i < end) {
        if constexpr(std::is_same_v<std::decay_t<R>, std::string>) {
            r.emplace_back(s, i, end - i);
        } else {
            r.emplace_back(s.substr(i, end - i));
        }
    }

    return;
}

template <typename R, split_ret_enabled_t<R>>
std::vector<R> split_newlines(std::string_view s) noexcept {
    std::vector<R> r;
    size_t count = std::ranges::count(s, '\n');
    r.reserve(count + 1);

    split_newlines(r, s);
    return r;
}

template std::vector<std::string> split_newlines<std::string>(std::string_view) noexcept;
template std::vector<std::string_view> split_newlines<std::string_view>(std::string_view) noexcept;

template void split_newlines<std::string>(std::vector<std::string>&, std::string_view) noexcept;
template void split_newlines<std::string_view>(std::vector<std::string_view>&, std::string_view) noexcept;

template <typename S, split_join_enabled_t<S>>
std::string join(std::span<const std::string> strings, S delim) noexcept {
    if(strings.empty()) return {};

    std::string result = strings[0];

    for(size_t i = 1; i < strings.size(); ++i) {
        result += delim;
        result += strings[i];
    }

    return result;
}

// explicit instantiations
template std::string join<char>(std::span<const std::string>, char) noexcept;
template std::string join<const char*>(std::span<const std::string>, const char*) noexcept;
template std::string join<std::string_view>(std::span<const std::string>, std::string_view) noexcept;

#ifndef BUILD_TOOLS_ONLY
template <Integral T>
std::string thousands(T n) noexcept {
    // I don't know how to check for support for the ' format specifier,
    // but we know it's broken on these platforms at least.
#if !defined(__GLIBCXX__) || defined(__EMSCRIPTEN__)
    return fmt::format("{:L}", n);
#else
    std::string ret;
    ret.resize(28);
    int written = 0;
    if constexpr(std::is_same_v<T, int64_t>) {
        written = std::snprintf(ret.data(), ret.size(), "%'" PRId64 "", n);
    } else if constexpr(std::is_same_v<T, uint64_t>) {
        written = std::snprintf(ret.data(), ret.size(), "%'" PRIu64 "", n);
    } else if constexpr(std::is_same_v<T, int32_t>) {
        written = std::snprintf(ret.data(), ret.size(), "%'" PRId32 "", n);
    } else if constexpr(std::is_same_v<T, uint32_t>) {
        written = std::snprintf(ret.data(), ret.size(), "%'" PRIu32 "", n);
    }
    ret.resize(written >= 0 ? written : 0);
    return ret;
#endif
}

template std::string thousands(int64_t n) noexcept;
template std::string thousands(uint64_t n) noexcept;
template std::string thousands(int32_t n) noexcept;
template std::string thousands(uint32_t n) noexcept;
#endif

}  // namespace SString
