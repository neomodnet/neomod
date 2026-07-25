#pragma once
// Copyright (c) 2025, kiwec, 2025-2026, WH, All rights reserved.

#include <charconv>  // from_chars
#include <cstring>   // strlen, strncmp
#include <string_view>
#include <optional>

#include "types.h"
#include "Vectors.h"
#include "SString.h"

namespace Parsing {

// use this instead of ' ' for space-separated elements, since Parsing::parse skips whitespace by default
enum class sig_whitespace_t : char {};
inline constexpr sig_whitespace_t SPC{' '};
inline constexpr sig_whitespace_t TAB{'\t'};

// use this to skip parsed values (like sscanf's %* modifier)
template <typename T>
struct skip_t {
    using type = T;
};

template <typename T>
inline constexpr skip_t<T> skip{};

// NOLINTBEGIN(cppcoreguidelines-init-variables)
namespace detail {

template <typename T>
struct is_skip : std::false_type {};

template <typename T>
struct is_skip<skip_t<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_skip_v = is_skip<T>::value;

// the closed set of types parse_str supports; long long/unsigned long long spellings (instead of i64/u64)
// keep the set alias-collision-free across LP64/LLP64/ILP32
template <typename T>
concept parseable =
    std::is_same_v<T, char> || std::is_same_v<T, bool> || std::is_same_v<T, i8> || std::is_same_v<T, u8> ||
    std::is_same_v<T, i32> || std::is_same_v<T, u32> || std::is_same_v<T, long> || std::is_same_v<T, unsigned long> ||
    std::is_same_v<T, long long> || std::is_same_v<T, unsigned long long> || std::is_same_v<T, f32> ||
    std::is_same_v<T, f64> || std::is_same_v<T, std::string> || std::is_same_v<T, std::unique_ptr<char[]>>;

// defined in Parsing.cpp with explicit instantiations for the parseable set (compile time reduction)
template <parseable T>
const char* parse_str(const char* begin, const char* end, T* arg) noexcept;

// base case for recursive parse_impl
inline const char* parse_impl(const char* begin, const char* /* end */) noexcept { return begin; }

template <typename T, typename... Extra>
const char* parse_impl(const char* begin, const char* end, T arg, Extra... extra) noexcept {
    // always skip whitespace (unless we actually want to split by it)
    if constexpr(!std::is_same_v<T, sig_whitespace_t> && !is_skip_v<T>) {
        while(begin < end && (*begin == ' ' || *begin == '\t')) begin++;
    }

    if constexpr((std::is_same_v<T, std::string*> || std::is_same_v<T, std::unique_ptr<char[]>*>) &&
                 sizeof...(extra) > 0) {
        // you can only parse an std::string if it is the LAST parameter,
        // because it will consume the WHOLE string.
        static_assert(Env::always_false_v<T>, "cannot parse to a string in the middle of the parsing chain");
        return nullptr;
    } else if constexpr(is_skip_v<T>) {
        // parse and discard the value
        while(begin < end && (*begin == ' ' || *begin == '\t')) begin++;
        typename T::type tmp;
        begin = parse_str(begin, end, &tmp);
        if(begin == nullptr) return nullptr;
        return parse_impl(begin, end, extra...);
    } else if constexpr(std::is_same_v<T, char> || std::is_same_v<T, sig_whitespace_t>) {
        // assert char separator. return position after separator.
        if(begin >= end || *begin != static_cast<char>(arg)) return nullptr;
        return parse_impl(begin + 1, end, extra...);
    } else if constexpr(std::is_same_v<T, const char*>) {
        // assert string label. return position after label.
        auto arg_len = strlen(arg);
        if(end - begin < static_cast<ptrdiff_t>(arg_len)) return nullptr;
        if(strncmp(begin, arg, arg_len) != 0) return nullptr;
        return parse_impl(begin + arg_len, end, extra...);
    } else if constexpr(std::is_pointer_v<T>) {
        // storing result in tmp var, so we only modify *arg once parsing fully succeeded
        using T_val = std::remove_pointer_t<T>;
        T_val arg_tmp;
        begin = parse_str(begin, end, &arg_tmp);
        if(begin == nullptr) return nullptr;

        begin = parse_impl(begin, end, extra...);
        if(begin == nullptr) return nullptr;

        *arg = std::move(arg_tmp);
        return begin;
    } else {
        static_assert(Env::always_false_v<T>, "expected pointer parameter");
        return nullptr;
    }
}

}  // namespace detail

template <typename S = const char*, typename T, typename... Extra>
bool parse(S str, T arg, Extra... extra) noexcept
    requires(std::is_same_v<std::decay_t<S>, std::string> || std::is_same_v<std::decay_t<S>, std::string_view> ||
             std::is_same_v<std::decay_t<S>, const char*>)
{
    const char *begin, *end;

    if constexpr(std::is_same_v<std::decay_t<S>, std::string_view> || std::is_same_v<std::decay_t<S>, std::string>) {
        begin = str.data();
        end = str.data() + str.size();
    } else if constexpr(std::is_same_v<std::decay_t<S>, const char*>) {
        begin = str;
        end = str + strlen(str);
    } else {
        static_assert(Env::always_false_v<S>, "invalid first parameter type");
    }

    return !!detail::parse_impl(begin, end, arg, extra...);
}

// NOLINTEND(cppcoreguidelines-init-variables)

// Since strtok_r SUCKS I'll just make my own
// Returns the token start, and edits str to after the token end (unless '\0').
inline char* strtok_x(char d, char** str) noexcept {
    char* old = *str;
    while(**str != '\0' && **str != d) {
        (*str)++;
    }
    if(**str != '\0') {
        **str = '\0';
        (*str)++;
    }
    return old;
}

// _s for "safe"
// does not modify "inout" unless parsing succeeded
template <typename T>
inline bool strto_s(std::string_view str, T& inout) noexcept {
    if(unlikely(str.empty())) return false;

    // from cppreference: "leading whitespace is not ignored."
    // this is different behavior from C strtol, so trim it to have matching/predictable behavior
    SString::trim_inplace(str);

    // if it was only whitespace, nothing to do
    if(unlikely(str.empty())) return false;

    T retval = inout;

    std::errc reterr = std::errc::invalid_argument;
    std::chars_format floatfmt = std::chars_format::general;
    int base = 10;

    // if we were given hex characters as input, we have to manually change the base to 16
    // (std::from_chars doesn't do this automatically)
    // this is not likely across our expected inputs, though, so mark it as such
    if(size_t len = str.length(); len >= 2 && unlikely(str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))) {
        // if the string was literally just "0x" and nothing else, the input is malformed
        if(unlikely(len == 2)) return false;

        base = 16;
        floatfmt = std::chars_format::hex;
        str = str.substr(2);
    }

    const char* begin{str.data()};
    const char* end{str.data() + str.size()};
    if constexpr(std::is_same_v<T, bool>) {
        long temp{};
        auto [_, ec] = std::from_chars(begin, end, temp, base);
        retval = (temp != 0) && ec == std::errc();
        reterr = ec;
    } else if constexpr(std::is_same_v<T, u8>) {
        u32 temp{};
        auto [_, ec] = std::from_chars(begin, end, temp, base);
        if(ec == std::errc()) {
            if(temp < 256) {
                retval = static_cast<u8>(temp);
            } else {
                ec = std::errc::result_out_of_range;
            }
        }
        reterr = ec;
    } else if constexpr(std::is_floating_point_v<std::decay_t<T>>) {
        auto [_, ec] = std::from_chars(begin, end, retval, floatfmt);
        reterr = ec;
    } else {
        auto [_, ec] = std::from_chars(begin, end, retval, base);
        reterr = ec;
    }

    if(reterr == std::errc()) {
        inout = retval;
        return true;
    }

    return false;
}

// same as e.g. strtol if you never checked errno anyways but supports non-cstrings
template <typename T>
inline T strto(std::string_view str) noexcept {
    T ret{};
    (void)strto_s(str, ret);
    return ret;
}

// this is commonly used in a few places to parse some arbitrary width x height string, might as well make it a function
std::optional<ivec2> parse_resolution(std::string_view width_x_height) noexcept;

}  // namespace Parsing
