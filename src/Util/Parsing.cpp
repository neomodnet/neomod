// Copyright (c) 2025, kiwec, 2025-2026, WH, All rights reserved.
#include "Parsing.h"

#include "SString.h"

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Parsing {

// NOLINTBEGIN(cppcoreguidelines-init-variables)
namespace detail {

template <parseable T>
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
template const char* parse_str<char>(const char*, const char*, char*);
template const char* parse_str<bool>(const char*, const char*, bool*);
template const char* parse_str<i8>(const char*, const char*, i8*);
template const char* parse_str<u8>(const char*, const char*, u8*);
template const char* parse_str<i32>(const char*, const char*, i32*);
template const char* parse_str<u32>(const char*, const char*, u32*);
template const char* parse_str<long>(const char*, const char*, long*);
template const char* parse_str<unsigned long>(const char*, const char*, unsigned long*);
template const char* parse_str<long long>(const char*, const char*, long long*);
template const char* parse_str<unsigned long long>(const char*, const char*, unsigned long long*);
template const char* parse_str<f32>(const char*, const char*, f32*);
template const char* parse_str<f64>(const char*, const char*, f64*);
template const char* parse_str<std::string>(const char*, const char*, std::string*);
template const char* parse_str<std::unique_ptr<char[]>>(const char*, const char*, std::unique_ptr<char[]>*);

}  // namespace detail
// NOLINTEND(cppcoreguidelines-init-variables)

std::optional<ivec2> parse_resolution(std::string_view width_x_height) {
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
