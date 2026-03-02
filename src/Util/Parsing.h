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

template <typename T>
const char* parse_str(const char* begin, const char* end, T* arg) {
    if constexpr(std::is_same_v<T, char>) {
        if(begin >= end) return nullptr;
        *arg = *begin;
        return begin + 1;
    } else if constexpr(std::is_same_v<T, long>) {
        long l;
        auto [ptr, ec] = std::from_chars(begin, end, l);
        if(ec != std::errc()) return nullptr;
        *arg = l;
        return ptr;
    } else if constexpr(std::is_same_v<T, unsigned long>) {
        unsigned long ul;
        auto [ptr, ec] = std::from_chars(begin, end, ul);
        if(ec != std::errc()) return nullptr;
        *arg = ul;
        return ptr;
    } else if constexpr(std::is_same_v<T, f32>) {
        f32 f;
        auto [ptr, ec] = std::from_chars(begin, end, f);
        if(ec != std::errc()) return nullptr;
        *arg = f;
        return ptr;
    } else if constexpr(std::is_same_v<T, f64>) {
        f64 d;
        auto [ptr, ec] = std::from_chars(begin, end, d);
        if(ec != std::errc()) return nullptr;
        *arg = d;
        return ptr;
    } else if constexpr(std::is_same_v<T, i32>) {
        i32 i;
        auto [ptr, ec] = std::from_chars(begin, end, i);
        if(ec != std::errc()) return nullptr;
        *arg = i;
        return ptr;
    } else if constexpr(std::is_same_v<T, i64>) {
        i64 ll;
        auto [ptr, ec] = std::from_chars(begin, end, ll);
        if(ec != std::errc()) return nullptr;
        *arg = ll;
        return ptr;
    } else if constexpr(std::is_same_v<T, u32>) {
        u32 u;
        auto [ptr, ec] = std::from_chars(begin, end, u);
        if(ec != std::errc()) return nullptr;
        *arg = u;
        return ptr;
    } else if constexpr(std::is_same_v<T, u64>) {
        u64 ull;
        auto [ptr, ec] = std::from_chars(begin, end, ull);
        if(ec != std::errc()) return nullptr;
        *arg = ull;
        return ptr;
    } else if constexpr(std::is_same_v<T, bool>) {
        long l;
        auto [ptr, ec] = std::from_chars(begin, end, l);
        if(ec != std::errc()) return nullptr;
        *arg = (l != 0);
        return ptr;
    } else if constexpr(std::is_same_v<T, i8>) {
        unsigned int c;
        auto [ptr, ec] = std::from_chars(begin, end, c);
        if(ec != std::errc() || c > 127 || c < -128) return nullptr;
        *arg = static_cast<i8>(c);
        return ptr;
    } else if constexpr(std::is_same_v<T, u8>) {
        unsigned int b;
        auto [ptr, ec] = std::from_chars(begin, end, b);
        if(ec != std::errc() || b > 255) return nullptr;
        *arg = static_cast<u8>(b);
        return ptr;
    } else if constexpr(std::is_same_v<T, std::string>) {
        // special handling for quoted string values
        if(begin < end && *begin == '"') {
            begin++;
            const char* start = begin;
            while(begin < end && *begin != '"') {
                begin++;
            }

            // expected closing '"', reached end instead
            if(begin == end) return nullptr;

            *arg = std::string(start, begin - start);
            begin++;
            return begin;
        } else {
            *arg = std::string(begin, end);
            SString::trim_inplace(*arg);
            return end;
        }
    } else if constexpr(std::is_same_v<T, std::unique_ptr<char[]>>) {
        if(begin < end && *begin == '"') {
            begin++;
            const char* start = begin;
            while(begin < end && *begin != '"') {
                begin++;
            }

            if(begin == end) return nullptr;

            *arg = SString::strcpy_u(std::string_view{start, static_cast<uSz>(begin - start)});
            begin++;
            return begin;
        } else {
            std::string_view tmp{begin, end};
            SString::trim_inplace(tmp);
            *arg = SString::strcpy_u(tmp);
            return end;
        }
    } else {
        static_assert(Env::always_false_v<T>, "parsing for this type is not implemented");
        return nullptr;
    }
}

// base case for recursive parse_impl
inline const char* parse_impl(const char* begin, const char* /* end */) { return begin; }

template <typename T, typename... Extra>
const char* parse_impl(const char* begin, const char* end, T arg, Extra... extra) {
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
bool parse(S str, T arg, Extra... extra)
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
inline char* strtok_x(char d, char** str) {
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
static inline bool strto_s(std::string_view str, T& inout) {
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
static inline T strto(std::string_view str) {
    T ret{};
    (void)strto_s(str, ret);
    return ret;
}

// this is commonly used in a few places to parse some arbitrary width x height string, might as well make it a function
inline std::optional<ivec2> parse_resolution(std::string_view width_x_height) {
    // don't allow e.g. < 100x100 or > 9999x9999
    if(width_x_height.length() < 7 || width_x_height.length() > 9) {
        return std::nullopt;
    }

    auto resolution = SString::split(width_x_height, 'x');
    if(resolution.size() != 2) {
        return std::nullopt;
    }

    bool good = false;
    i32 width{0}, height{0};
    do {
        {
            auto [ptr, ec] = std::from_chars(resolution[0].data(), resolution[0].data() + resolution[0].size(), width);
            if(ec != std::errc() || width < 320) break;  // 320x240 sanity check
        }
        {
            auto [ptr, ec] = std::from_chars(resolution[1].data(), resolution[1].data() + resolution[1].size(), height);
            if(ec != std::errc() || height < 240) break;
        }
        good = true;
    } while(false);

    if(!good) {
        return std::nullopt;
    }

    // success
    return ivec2{width, height};
}

}  // namespace Parsing
