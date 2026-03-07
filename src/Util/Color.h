#pragma once
// Copyright (c) 2025, WH, All rights reserved.
#ifndef MCENGINE_COLOR_H
#define MCENGINE_COLOR_H

#include "noinclude.h"
#include "types.h"

#ifndef BUILD_TOOLS_ONLY  // avoid an unnecessary dependency on fmt when building tools only
#include "fmt/format.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>

static_assert(true);  // clangd breaks otherwise... lol

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201)  // nonstandard extension used : nameless struct/union
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
#define IS_LITTLE_ENDIAN (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#elif defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || defined(__AARCH64EL__) || \
    defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__) || defined(__i386) || defined(__i386__) || \
    defined(__x86_64) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64) || defined(_M_AMD64)
#define IS_LITTLE_ENDIAN 1
#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || \
    defined(_MIPSEB) || defined(__MIPSEB) || defined(__MIPSEB__)
#define IS_LITTLE_ENDIAN 0
#else
#error "impossible"
#endif

using Channel = u8;
namespace Colors {
template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

// helper to detect if types are "compatible"
template <typename T, typename U>
inline constexpr bool is_compatible_v =
    std::is_same_v<T, U> ||
    // integer literals
    (std::is_integral_v<T> && std::is_integral_v<U> && (std::is_convertible_v<T, U> || std::is_convertible_v<U, T>)) ||
    // same "family" of types (all floating or all integral)
    (std::is_floating_point_v<T> && std::is_floating_point_v<U>) || (std::is_integral_v<T> && std::is_integral_v<U>);

// check if all four types are compatible with each other
template <typename A, typename R, typename G, typename B>
inline constexpr bool all_compatible_v = is_compatible_v<A, R> && is_compatible_v<A, G> && is_compatible_v<A, B> &&
                                         is_compatible_v<R, G> && is_compatible_v<R, B> && is_compatible_v<G, B>;

forceinline constexpr Channel to_byte(Numeric auto value) {
    if constexpr(std::is_floating_point_v<decltype(value)>)
        return static_cast<Channel>(std::clamp<decltype(value)>(value, 0, 1) * 255);
    else
        return static_cast<Channel>(std::clamp<Channel>(value, 0, 255));
}
}  // namespace Colors

// argb colors (TODO: non-argb)
struct alignas(u32) Color {
    union {
#if IS_LITTLE_ENDIAN
        struct {
            u8 b, g, r, a;
        };
        struct {
            u8 u4, u3, u2, u1;
        };
#else
        struct {
            u8 a, r, g, b;
        };
        struct {
            u8 u1, u2, u3, u4;
        };
#endif
        u32 data;
    };

#undef IS_LITTLE_ENDIAN

#define A_ this->a
#define R_ this->r
#define G_ this->g
#define B_ this->b

    constexpr Color() = default;

    constexpr operator u32() const { return data; }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init, hicpp-member-init)
    constexpr Color(u32 val) { data = val; }
    constexpr Color(i32 val) : Color(static_cast<u32>(val)) {}
    constexpr Color(u8 val) : Color(static_cast<u32>(val)) {}
    constexpr Color(i8 val) : Color(static_cast<u32>(val)) {}

    template <typename A, typename R, typename G, typename B>
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init, hicpp-member-init)
    constexpr Color(A a, R r, G g, B b)
        requires Colors::Numeric<A> && Colors::Numeric<R> && Colors::Numeric<G> && Colors::Numeric<B> &&
                 Colors::all_compatible_v<A, R, G, B>
        : data{(static_cast<u32>(Colors::to_byte(a)) << 24) | (static_cast<u32>(Colors::to_byte(r)) << 16) |
               (static_cast<u32>(Colors::to_byte(g)) << 8) | static_cast<u32>(Colors::to_byte(b))} {}

    template <typename A, typename R, typename G, typename B>
    constexpr Color(A a, R r, G g, B b)
        requires Colors::Numeric<A> && Colors::Numeric<R> && Colors::Numeric<G> && Colors::Numeric<B> &&
                     (!Colors::all_compatible_v<A, R, G, B>)
    = delete; /* ("parameters should have compatible types"); */

    friend inline bool operator==(Color a, Color b) { return a.data == b.data; }
    friend inline bool operator==(Color a, u32 b) { return a.data == b; }
    friend inline bool operator==(u32 a, Color b) { return a == b.data; }
    friend inline bool operator==(Color a, i32 b) { return a.data == static_cast<u32>(b); }
    friend inline bool operator==(i32 a, Color b) { return static_cast<u32>(a) == b.data; }

    // clang-format off

	// float accessors (normalized to 0.0-1.0)
	template <typename T = float>
	[[nodiscard]] constexpr T Af() const { return static_cast<T>(static_cast<float>(A_) / 255.0f); }
	template <typename T = float>
	[[nodiscard]] constexpr T Rf() const { return static_cast<T>(static_cast<float>(R_) / 255.0f); }
	template <typename T = float>
	[[nodiscard]] constexpr T Gf() const { return static_cast<T>(static_cast<float>(G_) / 255.0f); }
	template <typename T = float>
	[[nodiscard]] constexpr T Bf() const { return static_cast<T>(static_cast<float>(B_) / 255.0f); }

	template <typename T = Channel>
	constexpr Color &setA(T a) { A_ = Colors::to_byte(a); return *this; }
	template <typename T = Channel>
	constexpr Color &setR(T r) { R_ = Colors::to_byte(r); return *this; }
	template <typename T = Channel>
	constexpr Color &setG(T g) { G_ = Colors::to_byte(g); return *this; }
	template <typename T = Channel>
	constexpr Color &setB(T b) { B_ = Colors::to_byte(b); return *this; }

    // clang-format on
#undef A_
#undef R_
#undef G_
#undef B_
};

// main conversion func
template <typename A, typename R, typename G, typename B>
constexpr Color argb(A a, R r, G g, B b) {
    return Color{a, r, g, b};
}

// convenience
template <typename R, typename G, typename B, typename A>
constexpr Color rgba(R r, G g, B b, A a) {
    return Color{a, r, g, b};
}

constexpr Color argb(Color rgbacol) { return Color{rgbacol.b, rgbacol.a, rgbacol.r, rgbacol.g}; }

// for opengl
constexpr Color rgba(Color argbcol) { return Color{argbcol.r, argbcol.g, argbcol.b, argbcol.a}; }

// for opengl
constexpr Color abgr(Color argbcol) { return Color{argbcol.a, argbcol.b, argbcol.g, argbcol.r}; }

template <typename R, typename G, typename B>
constexpr Color rgb(R r, G g, B b)
    requires Colors::Numeric<R> && Colors::Numeric<G> && Colors::Numeric<B> && Colors::all_compatible_v<R, G, B, R>
{
    return {255, Colors::to_byte(r), Colors::to_byte(g), Colors::to_byte(b)};
}

template <typename R, typename G, typename B>
[[deprecated("parameters should have compatible types")]]
constexpr Color rgb(R r, G g, B b)
    requires Colors::Numeric<R> && Colors::Numeric<G> && Colors::Numeric<B> && (!Colors::all_compatible_v<R, G, B, R>)
{
    return {255, Colors::to_byte(r), Colors::to_byte(g), Colors::to_byte(b)};
}

namespace Colors {
constexpr Color scale(Color color, float factor) {
    return argb(color.Af(), color.Rf() * factor, color.Gf() * factor, color.Bf() * factor);
}

constexpr Color invert(Color color) { return {color.a, 255 - color.r, 255 - color.g, 255 - color.b}; }

constexpr Color multiply(Color color1, Color color2) {
    return rgb(color1.Rf() * color2.Rf(), color1.Gf() * color2.Gf(), color1.Bf() * color2.Bf());
}

constexpr Color add(Color color1, Color color2) {
    return rgb(std::clamp(color1.Rf() + color2.Rf(), 0.0f, 1.0f), std::clamp(color1.Gf() + color2.Gf(), 0.0f, 1.0f),
               std::clamp(color1.Bf() + color2.Bf(), 0.0f, 1.0f));
}

constexpr Color subtract(Color color1, Color color2) {
    return rgb(std::clamp(color1.Rf() - color2.Rf(), 0.0f, 1.0f), std::clamp(color1.Gf() - color2.Gf(), 0.0f, 1.0f),
               std::clamp(color1.Bf() - color2.Bf(), 0.0f, 1.0f));
}

}  // namespace Colors

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#ifndef BUILD_TOOLS_ONLY
namespace fmt {
template <>
struct formatter<Color> : formatter<u32> {
    template <typename FormatContext>
    auto format(const Color &col, FormatContext &ctx) const noexcept {
        return formatter<u32>::format(static_cast<u32>(col), ctx);
    }
};
}  // namespace fmt
#endif

#endif /* MCENGINE_COLOR_H */
