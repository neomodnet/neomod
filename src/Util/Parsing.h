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

// float/double parsing backed by fast_float (see Parsing.cpp), with std::from_chars semantics
// (std's own floating-point from_chars is unavailable before macOS 26's libc++)
std::from_chars_result from_chars(const char* first, const char* last, f32& value,
                                  std::chars_format fmt = std::chars_format::general) noexcept;
std::from_chars_result from_chars(const char* first, const char* last, f64& value,
                                  std::chars_format fmt = std::chars_format::general) noexcept;

// the closed set of types strto_s/strto support (same spelling rules as parseable above)
template <typename T>
concept strto_parseable = std::is_same_v<T, bool> || std::is_same_v<T, i8> || std::is_same_v<T, u8> ||
                          std::is_same_v<T, i32> || std::is_same_v<T, u32> || std::is_same_v<T, long> ||
                          std::is_same_v<T, unsigned long> || std::is_same_v<T, long long> ||
                          std::is_same_v<T, unsigned long long> || std::is_same_v<T, f32> || std::is_same_v<T, f64>;

// _s for "safe"
// does not modify "inout" unless parsing succeeded
// defined in Parsing.cpp with explicit instantiations for the strto_parseable set (compile time reduction)
template <strto_parseable T>
bool strto_s(std::string_view str, T& inout) noexcept;

// same as e.g. strtol if you never checked errno anyways but supports non-cstrings
template <strto_parseable T>
inline T strto(std::string_view str) noexcept {
    T ret{};
    (void)strto_s(str, ret);
    return ret;
}

// this is commonly used in a few places to parse some arbitrary width x height string, might as well make it a function
std::optional<ivec2> parse_resolution(std::string_view width_x_height) noexcept;

}  // namespace Parsing
