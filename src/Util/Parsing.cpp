// Copyright (c) 2025, kiwec, 2025-2026, WH, All rights reserved.
#include "Parsing.h"

#include "SString.h"

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "fast_float/fast_float.h"

// std's floating-point from_chars needs runtime support that's only available in macOS 26+'s
// libc++; hex floats (which fast_float doesn't implement) go through strtof_l/strtod_l there
#if defined(_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT) && !_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT
#include <cerrno>
#include <xlocale.h>
#endif

namespace Parsing {

// NOLINTBEGIN(cppcoreguidelines-init-variables)
namespace detail {

template <parseable T>
const char* parse_str(const char* begin, const char* end, T* arg) noexcept {
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
        auto [ptr, ec] = Parsing::from_chars(begin, end, f);
        if(ec != std::errc()) return nullptr;
        *arg = f;
        return ptr;
    } else if constexpr(std::is_same_v<T, f64>) {
        f64 d;
        auto [ptr, ec] = Parsing::from_chars(begin, end, d);
        if(ec != std::errc()) return nullptr;
        *arg = d;
        return ptr;
    } else if constexpr(std::is_same_v<T, i32>) {
        i32 i;
        auto [ptr, ec] = std::from_chars(begin, end, i);
        if(ec != std::errc()) return nullptr;
        *arg = i;
        return ptr;
    } else if constexpr(std::is_same_v<T, long long>) {
        long long ll;
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
    } else if constexpr(std::is_same_v<T, unsigned long long>) {
        unsigned long long ull;
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

// the parseable set; keep in sync with the concept in Parsing.h
template const char* parse_str<char>(const char*, const char*, char*) noexcept;
template const char* parse_str<bool>(const char*, const char*, bool*) noexcept;
template const char* parse_str<i8>(const char*, const char*, i8*) noexcept;
template const char* parse_str<u8>(const char*, const char*, u8*) noexcept;
template const char* parse_str<i32>(const char*, const char*, i32*) noexcept;
template const char* parse_str<u32>(const char*, const char*, u32*) noexcept;
template const char* parse_str<long>(const char*, const char*, long*) noexcept;
template const char* parse_str<unsigned long>(const char*, const char*, unsigned long*) noexcept;
template const char* parse_str<long long>(const char*, const char*, long long*) noexcept;
template const char* parse_str<unsigned long long>(const char*, const char*, unsigned long long*) noexcept;
template const char* parse_str<f32>(const char*, const char*, f32*) noexcept;
template const char* parse_str<f64>(const char*, const char*, f64*) noexcept;
template const char* parse_str<std::string>(const char*, const char*, std::string*) noexcept;
template const char* parse_str<std::unique_ptr<char[]>>(const char*, const char*, std::unique_ptr<char[]>*) noexcept;

}  // namespace detail
// NOLINTEND(cppcoreguidelines-init-variables)

std::optional<ivec2> parse_resolution(std::string_view width_x_height) noexcept {
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

namespace {

template <typename T>
std::from_chars_result fp_from_chars(const char* first, const char* last, T& value, std::chars_format fmt) noexcept {
    if(likely(fmt != std::chars_format::hex)) {
        // the enumerator values differ between std::chars_format and fast_float::chars_format
        auto ffmt = fmt == std::chars_format::scientific ? fast_float::chars_format::scientific
                    : fmt == std::chars_format::fixed    ? fast_float::chars_format::fixed
                                                         : fast_float::chars_format::general;
        // parse into a temporary: std::from_chars leaves the output unmodified unless parsing
        // fully succeeded, but fast_float writes ±inf/±0 on out-of-range inputs
        T tmp;
        auto res = fast_float::from_chars(first, last, tmp, ffmt);
        if(res.ec == std::errc()) value = tmp;
        return {.ptr = res.ptr, .ec = res.ec};
    }

#if defined(_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT) && !_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT
    // hex floats via strtof_l/strtod_l, which expect null termination and a "0x" prefix
    // behind the sign (which std::from_chars input lacks)
    const bool neg = first < last && *first == '-';
    std::string buf(neg ? "-0x" : "0x");
    buf.append(neg ? first + 1 : first, last);

    errno = 0;
    char* endp = nullptr;
    T res;
    // the _l convenience functions use the C locale when passed LC_C_LOCALE (NULL), see xlocale(3)
    if constexpr(std::is_same_v<T, f32>)
        res = strtof_l(buf.c_str(), &endp, LC_C_LOCALE);
    else
        res = strtod_l(buf.c_str(), &endp, LC_C_LOCALE);

    const char* payload = buf.c_str() + (neg ? 3 : 2);
    if(endp <= payload) return {.ptr = first, .ec = std::errc::invalid_argument};

    const char* ptr = first + (neg ? 1 : 0) + (endp - payload);
    if(errno == ERANGE) return {.ptr = ptr, .ec = std::errc::result_out_of_range};

    value = res;
    return {.ptr = ptr, .ec = std::errc()};
#else
    return std::from_chars(first, last, value, fmt);
#endif
}

}  // namespace

std::from_chars_result from_chars(const char* first, const char* last, f32& value, std::chars_format fmt) noexcept {
    return fp_from_chars(first, last, value, fmt);
}

std::from_chars_result from_chars(const char* first, const char* last, f64& value, std::chars_format fmt) noexcept {
    return fp_from_chars(first, last, value, fmt);
}

}  // namespace Parsing
