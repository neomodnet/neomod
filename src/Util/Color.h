#pragma once
// Copyright (c) 2025, WH, All rights reserved.
#ifndef COLOR_H
#define COLOR_H

#include "types.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

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

constexpr Channel to_byte(Numeric auto value) {
    if constexpr(std::is_floating_point_v<decltype(value)>)
        return static_cast<Channel>(std::clamp<decltype(value)>(value, 0, 1) * 255);
    else
        return static_cast<Channel>(std::clamp<Channel>(value, 0, 255));
}
}  // namespace Colors

// argb colors (TODO: non-argb)
struct Color {
    u32 v;

    constexpr Color() : v{0} {}
    constexpr Color(i32 val) : v{static_cast<u32>(val)} {}
    constexpr Color(u32 val) : v{val} {}

    template <typename A, typename R, typename G, typename B>
    constexpr Color(A a, R r, G g, B b)
        requires Colors::Numeric<A> && Colors::Numeric<R> && Colors::Numeric<G> && Colors::Numeric<B> &&
                 Colors::all_compatible_v<A, R, G, B>
        : v{(static_cast<u32>(Colors::to_byte(a)) << 24) | (static_cast<u32>(Colors::to_byte(r)) << 16) |
            (static_cast<u32>(Colors::to_byte(g)) << 8) | static_cast<u32>(Colors::to_byte(b))} {}

    template <typename A, typename R, typename G, typename B>
    constexpr Color(A a, R r, G g, B b)
        requires Colors::Numeric<A> && Colors::Numeric<R> && Colors::Numeric<G> && Colors::Numeric<B> &&
                     (!Colors::all_compatible_v<A, R, G, B>)
    = delete; /* ("parameters should have compatible types"); */

    friend inline bool operator==(Color a, Color b) { return a.v == b.v; }
    friend inline bool operator==(Color a, u32 b) { return a.v == b; }
    friend inline bool operator==(u32 a, Color b) { return a == b.v; }
    friend inline bool operator==(Color a, i32 b) { return a.v == static_cast<u32>(b); }
    friend inline bool operator==(i32 a, Color b) { return static_cast<u32>(a) == b.v; }
    operator u32() const { return v; }

    // clang-format off
	// channel accessors
	[[nodiscard]] constexpr Channel A() const { return static_cast<Channel>((v >> 24) & 0xFF); }
	[[nodiscard]] constexpr Channel R() const { return static_cast<Channel>((v >> 16) & 0xFF); }
	[[nodiscard]] constexpr Channel G() const { return static_cast<Channel>((v >> 8) & 0xFF); }
	[[nodiscard]] constexpr Channel B() const { return static_cast<Channel>(v & 0xFF); }

	// float accessors (normalized to 0.0-1.0)
	template <typename T = float>
	[[nodiscard]] constexpr T Af() const { return static_cast<T>(static_cast<float>((v >> 24) & 0xFF) / 255.0f); }
	template <typename T = float>
	[[nodiscard]] constexpr T Rf() const { return static_cast<T>(static_cast<float>((v >> 16) & 0xFF) / 255.0f); }
	template <typename T = float>
	[[nodiscard]] constexpr T Gf() const { return static_cast<T>(static_cast<float>((v >> 8) & 0xFF) / 255.0f); }
	template <typename T = float>
	[[nodiscard]] constexpr T Bf() const { return static_cast<T>(static_cast<float>(v & 0xFF) / 255.0f); }

	template <typename T = Channel>
	constexpr Color &setA(T a) { *this = ((*this & 0x00FFFFFF) | (Colors::to_byte(a) << 24)); return *this; }
	template <typename T = Channel>
	constexpr Color &setR(T r) { *this = ((*this & 0xFF00FFFF) | (Colors::to_byte(r) << 16)); return *this; }
	template <typename T = Channel>
	constexpr Color &setG(T g) { *this = ((*this & 0xFFFF00FF) | (Colors::to_byte(g) << 8)); return *this; }
	template <typename T = Channel>
	constexpr Color &setB(T b) { *this = ((*this & 0xFFFFFF00) | (Colors::to_byte(b) << 0)); return *this; }

    // clang-format on
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

constexpr Color argb(Color rgbacol) { return Color{rgbacol.B(), rgbacol.A(), rgbacol.R(), rgbacol.G()}; }

// for opengl
constexpr Color rgba(Color argbcol) { return Color{argbcol.R(), argbcol.G(), argbcol.B(), argbcol.A()}; }

// for opengl
constexpr Color abgr(Color argbcol) { return Color{argbcol.A(), argbcol.B(), argbcol.G(), argbcol.R()}; }

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

constexpr Color invert(Color color) { return {color.A(), 255 - color.R(), 255 - color.G(), 255 - color.B()}; }

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

#endif
