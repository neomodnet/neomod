// Copyright (c) 2025, WH, All rights reserved.
#ifndef UTIL_VECTORS_H
#define UTIL_VECTORS_H

#include "types.h"
#include "glm/ext/vector_float1.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"

#include "glm/geometric.hpp"
#include "glm/trigonometric.hpp"

#ifndef BUILD_TOOLS_ONLY  // avoid an unnecessary dependency on fmt when building tools only
#include "fmt/format.h"
#endif

// typedefs
#include "Vectors_fwd.h"

namespace vec {

using glm::abs;
using glm::all;
using glm::any;
using glm::ceil;
using glm::clamp;
using glm::cross;
using glm::degrees;
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
using glm::radians;
using glm::round;

inline constexpr auto FLOAT_NORMALIZE_EPSILON = 0.000001f;
inline constexpr auto DOUBLE_NORMALIZE_EPSILON = FLOAT_NORMALIZE_EPSILON / 10e6;

// more defs in Vectors_fwd.h

template <typename T, typename V>
    requires(std::is_floating_point_v<V>) &&
            (std::is_same_v<T, vec2> || std::is_same_v<T, vec3> || std::is_same_v<T, vec4>)
extern void setLength(T& vec, V len);

extern template void setLength(vec2&, float);
extern template void setLength(vec3&, float);
extern template void setLength(vec4&, float);

template <typename T, typename V>
    requires(std::is_floating_point_v<V>) &&
            (std::is_same_v<T, dvec2> || std::is_same_v<T, dvec3> || std::is_same_v<T, dvec4>)
extern void setLength(T& vec, V len);

extern template void setLength(dvec2&, float);
extern template void setLength(dvec3&, float);
extern template void setLength(dvec4&, float);

template <typename V>
    requires(std::is_same_v<V, vec2> || std::is_same_v<V, vec3> || std::is_same_v<V, vec4> ||
             std::is_same_v<V, dvec2> || std::is_same_v<V, dvec3> || std::is_same_v<V, dvec4> ||
             std::is_same_v<V, ivec2> || std::is_same_v<V, ivec3> || std::is_same_v<V, ivec4> ||
             std::is_same_v<V, lvec2> || std::is_same_v<V, lvec3> || std::is_same_v<V, lvec4>)
extern bool allEqual(const V& vec1, const V& vec2);

extern template bool allEqual(const vec2&, const vec2&);
extern template bool allEqual(const vec3&, const vec3&);
extern template bool allEqual(const vec4&, const vec4&);

extern template bool allEqual(const dvec2&, const dvec2&);
extern template bool allEqual(const dvec3&, const dvec3&);
extern template bool allEqual(const dvec4&, const dvec4&);

extern template bool allEqual(const ivec2&, const ivec2&);
extern template bool allEqual(const ivec3&, const ivec3&);
extern template bool allEqual(const ivec4&, const ivec4&);

extern template bool allEqual(const lvec2&, const lvec2&);
extern template bool allEqual(const lvec3&, const lvec3&);
extern template bool allEqual(const lvec4&, const lvec4&);

}  // namespace vec

#ifndef BUILD_TOOLS_ONLY
namespace fmt {
template <typename Vec, int N>
struct float_vec_formatter {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Vec& p, FormatContext& ctx) const {
        if constexpr(N == 2) {
            return format_to(ctx.out(), "({:.2f}, {:.2f})", p.x, p.y);
        } else if constexpr(N == 3) {
            return format_to(ctx.out(), "({:.2f}, {:.2f}, {:.2f})", p.x, p.y, p.z);
        } else {
            return format_to(ctx.out(), "({:.2f}, {:.2f}, {:.2f}, {:.2f})", p.x, p.y, p.z, p.w);
        }
    }
};

template <typename Vec, int N>
struct int_vec_formatter {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Vec& p, FormatContext& ctx) const {
        if constexpr(N == 2) {
            return format_to(ctx.out(), "({}, {})", p.x, p.y);
        } else if constexpr(N == 3) {
            return format_to(ctx.out(), "({}, {}, {})", p.x, p.y, p.z);
        } else {
            return format_to(ctx.out(), "({}, {}, {}, {})", p.x, p.y, p.z, p.w);
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

#endif
