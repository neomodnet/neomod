// Copyright (c) 2025, WH, All rights reserved.
#pragma once

#include "glm/geometric.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

#ifndef BUILD_TOOLS_ONLY  // avoid an unnecessary dependency on fmt when building tools only
#include "fmt/format.h"
#include "fmt/compile.h"
#endif

using glm::dvec2;
using glm::dvec3;
using glm::dvec4;

using glm::vec2;
using glm::vec3;
using glm::vec4;

using ivec2 = glm::i32vec2;
using ivec3 = glm::i32vec3;
using ivec4 = glm::i32vec4;

using lvec2 = glm::i64vec2;
using lvec3 = glm::i64vec3;
using lvec4 = glm::i64vec4;

using u8vec4 = glm::u8vec4;

using uvec2 = glm::u32vec2;
using uvec3 = glm::u32vec3;
using uvec4 = glm::u32vec4;

using ulvec2 = glm::u64vec2;
using ulvec3 = glm::u64vec3;
using ulvec4 = glm::u64vec4;

namespace vec {

inline constexpr auto FLOAT_NORMALIZE_EPSILON = 0.000001f;
inline constexpr auto DOUBLE_NORMALIZE_EPSILON = FLOAT_NORMALIZE_EPSILON / 10e6;

using glm::abs;
using glm::all;
using glm::any;
using glm::ceil;
using glm::clamp;
using glm::cross;
using glm::distance;
using glm::dot;
using glm::equal;
using glm::floor;
using glm::greaterThan;
using glm::greaterThanEqual;
using glm::length;
using glm::lessThan;
using glm::lessThanEqual;
using glm::max;
using glm::min;
using glm::normalize;
using glm::round;

template <typename T, typename V>
    requires(std::is_floating_point_v<V>) &&
            (std::is_same_v<T, vec2> || std::is_same_v<T, vec3> || std::is_same_v<T, vec4>)
void setLength(T &vec, const V &len) {
    if(length(vec) > FLOAT_NORMALIZE_EPSILON) {
        vec = normalize(vec) * static_cast<float>(len);
    }
}

template <typename T, typename V>
    requires(std::is_floating_point_v<V>) &&
            (std::is_same_v<T, dvec2> || std::is_same_v<T, dvec3> || std::is_same_v<T, dvec4>)
void setLength(T &vec, const V &len) {
    if(length(vec) > DOUBLE_NORMALIZE_EPSILON) {
        vec = normalize(vec) * static_cast<double>(len);
    }
}

template <typename V>
    requires(std::is_same_v<V, vec2> || std::is_same_v<V, vec3> || std::is_same_v<V, vec4> ||
             std::is_same_v<V, dvec2> || std::is_same_v<V, dvec3> || std::is_same_v<V, dvec4> ||
             std::is_same_v<V, ivec2> || std::is_same_v<V, ivec3> || std::is_same_v<V, ivec4> ||
             std::is_same_v<V, lvec2> || std::is_same_v<V, lvec3> || std::is_same_v<V, lvec4>)
inline constexpr bool allEqual(const V &vec1, const V vec2) {
    return vec::all(vec::equal(vec1, vec2));
}

}  // namespace vec

#ifndef BUILD_TOOLS_ONLY
namespace fmt {
template <typename Vec, int N>
struct float_vec_formatter {
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Vec &p, FormatContext &ctx) const {
        if constexpr(N == 2) {
            return format_to(ctx.out(), "({:.2f}, {:.2f})"_cf, p.x, p.y);
        } else if constexpr(N == 3) {
            return format_to(ctx.out(), "({:.2f}, {:.2f}, {:.2f})"_cf, p.x, p.y, p.z);
        } else {
            return format_to(ctx.out(), "({:.2f}, {:.2f}, {:.2f}, {:.2f})"_cf, p.x, p.y, p.z, p.w);
        }
    }
};

template <typename Vec, int N>
struct int_vec_formatter {
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Vec &p, FormatContext &ctx) const {
        if constexpr(N == 2) {
            return format_to(ctx.out(), "({}, {})"_cf, p.x, p.y);
        } else if constexpr(N == 3) {
            return format_to(ctx.out(), "({}, {}, {})"_cf, p.x, p.y, p.z);
        } else {
            return format_to(ctx.out(), "({}, {}, {}, {})"_cf, p.x, p.y, p.z, p.w);
        }
    }
};

template <>
struct formatter<vec2> : float_vec_formatter<vec2, 2> {};

template <>
struct formatter<dvec2> : float_vec_formatter<dvec2, 2> {};

template <>
struct formatter<vec3> : float_vec_formatter<vec3, 3> {};

template <>
struct formatter<dvec3> : float_vec_formatter<dvec3, 3> {};

template <>
struct formatter<vec4> : float_vec_formatter<vec4, 4> {};

template <>
struct formatter<dvec4> : float_vec_formatter<dvec4, 4> {};

template <>
struct formatter<ivec2> : int_vec_formatter<ivec2, 2> {};

template <>
struct formatter<ivec3> : int_vec_formatter<ivec3, 3> {};

template <>
struct formatter<ivec4> : int_vec_formatter<ivec4, 4> {};

template <>
struct formatter<lvec2> : int_vec_formatter<lvec2, 2> {};

template <>
struct formatter<lvec3> : int_vec_formatter<lvec3, 3> {};

template <>
struct formatter<lvec4> : int_vec_formatter<lvec4, 4> {};

template <>
struct formatter<u8vec4> : int_vec_formatter<u8vec4, 4> {};

template <>
struct formatter<uvec2> : int_vec_formatter<uvec2, 2> {};

template <>
struct formatter<uvec3> : int_vec_formatter<uvec3, 3> {};

template <>
struct formatter<uvec4> : int_vec_formatter<uvec4, 4> {};

template <>
struct formatter<ulvec2> : int_vec_formatter<ulvec2, 2> {};

template <>
struct formatter<ulvec3> : int_vec_formatter<ulvec3, 3> {};

template <>
struct formatter<ulvec4> : int_vec_formatter<ulvec4, 4> {};

}  // namespace fmt
#endif
