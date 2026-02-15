// Copyright (c) 2023-2024, kiwec & 2025, WH, All rights reserved.

#include "config.h"

#include "SString.h"

#include <cstring>

#if !defined(__GLIBCXX__) || defined(__EMSCRIPTEN__)
#include "fmt/format.h"
#else
#include <cinttypes>
#endif

namespace SString {
// alphanumeric string comparator that ignores special characters at the start of strings
bool alnum_comp(std::string_view a, std::string_view b) {
    int i = 0;
    int j = 0;
    while(i < a.length() && j < b.length()) {
        if(!isalnum((unsigned char)a[i])) {
            i++;
            continue;
        }
        if(!isalnum((unsigned char)b[j])) {
            j++;
            continue;
        }
        auto la = tolower(a[i]);
        auto lb = tolower(b[j]);
        if(la != lb) return la < lb;
        i++;
        j++;
    }
    return false;
}

template <typename R, typename S, split_ret_enabled_t<R>, split_join_enabled_t<S>>
void split(std::vector<R>& r, std::string_view s, S delim, size_t delim_len) {
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
        r.emplace_back(s, i, s.size() - i);
    } else {  // string_view
        while((j = s.find(delim, i)) != s.npos) r.emplace_back(s.substr(i, j - i)), i = j + delim_len;
        r.emplace_back(s.substr(i));
    }

    return;
}

// split a string by a delimiter
template <typename R, typename S, split_ret_enabled_t<R>, split_join_enabled_t<S>>
std::vector<R> split(std::string_view s, S delim) {
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
template std::vector<std::string> split<std::string, char>(std::string_view, char);
template std::vector<std::string> split<std::string, const char*>(std::string_view, const char*);
template std::vector<std::string> split<std::string, std::string_view>(std::string_view, std::string_view);

template std::vector<std::string_view> split<std::string_view, char>(std::string_view, char);
template std::vector<std::string_view> split<std::string_view, const char*>(std::string_view, const char*);
template std::vector<std::string_view> split<std::string_view, std::string_view>(std::string_view, std::string_view);

template void split<std::string, char>(std::vector<std::string>&, std::string_view, char, size_t);
template void split<std::string, const char*>(std::vector<std::string>&, std::string_view, const char*, size_t);
template void split<std::string, std::string_view>(std::vector<std::string>&, std::string_view, std::string_view,
                                                   size_t);

template void split<std::string_view, char>(std::vector<std::string_view>&, std::string_view, char, size_t);
template void split<std::string_view, const char*>(std::vector<std::string_view>&, std::string_view, const char*,
                                                   size_t);
template void split<std::string_view, std::string_view>(std::vector<std::string_view>&, std::string_view,
                                                        std::string_view, size_t);

template <typename R, split_ret_enabled_t<R>>
void split_newlines(std::vector<R>& r, std::string_view s) {
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

    if constexpr(std::is_same_v<std::decay_t<R>, std::string>) {
        r.emplace_back(s, i, end - i);
    } else {
        r.emplace_back(s.substr(i, end - i));
    }

    return;
}

template <typename R, split_ret_enabled_t<R>>
std::vector<R> split_newlines(std::string_view s) {
    std::vector<R> r;
    size_t count = std::ranges::count(s, '\n');
    r.reserve(count + 1);

    split_newlines(r, s);
    return r;
}

template std::vector<std::string> split_newlines<std::string>(std::string_view);
template std::vector<std::string_view> split_newlines<std::string_view>(std::string_view);

template void split_newlines<std::string>(std::vector<std::string>&, std::string_view);
template void split_newlines<std::string_view>(std::vector<std::string_view>&, std::string_view);

template <typename S, split_join_enabled_t<S>>
std::string join(const std::vector<std::string>& strings, S delim) {
    if(strings.empty()) return {};

    std::string result = strings[0];

    for(size_t i = 1; i < strings.size(); ++i) {
        result += delim;
        result += strings[i];
    }

    return result;
}

// explicit instantiations
template std::string join<char>(const std::vector<std::string>&, char);
template std::string join<const char*>(const std::vector<std::string>&, const char*);
template std::string join<std::string_view>(const std::vector<std::string>&, std::string_view);

template <Integral T>
std::string thousands(T n) {
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

template std::string thousands(int64_t n);
template std::string thousands(uint64_t n);
template std::string thousands(int32_t n);
template std::string thousands(uint32_t n);

}  // namespace SString
