// Copyright (c) 2026, WH, All rights reserved.
#include "Snapping.h"

#include "ContainerRanges.h"
#include "GameRules.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace {
constexpr vec2 FIELD{GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT};
}  // namespace

namespace Snapping {

i32 clampGridSize(i32 size) {
    return *Mc::ranges::min_element(GRID_SIZES, {}, [size](i32 s) { return std::abs((i64)s - size); });
}

f32 clampSpacing(f32 spacing) {
    // NaN too
    if(!(spacing >= 0.1f)) return 0.1f;
    return std::round(std::min(spacing, 6.f) * 100.f) / 100.f;
}

std::optional<vec2> distance(const Reference &reference, vec2 toward, i32 timeMS, f64 velocity, f32 spacing) {
    const vec2 direction = toward - reference.end;
    const f32 length = vec::length(direction);
    if(!(length > 0.f)) return std::nullopt;
    const vec2 at = reference.end + direction / length * (f32)(velocity * spacing * (f64)(timeMS - reference.endTime));
    if(!(at.x >= 0.f && at.y >= 0.f && at.x <= FIELD.x && at.y <= FIELD.y)) return std::nullopt;
    return at;
}

std::optional<vec2> angle(vec2 from, vec2 p) {
    const vec2 direction = p - from;
    const f32 length = vec::length(direction);
    if(!(length > 0.f)) return std::nullopt;
    const f64 degrees = std::atan2(direction.y, direction.x) * 180.0 / std::numbers::pi;
    const f64 nearest = std::round(degrees / 45.0) * 45.0;
    if(std::abs(degrees - nearest) >= 10.0) return std::nullopt;
    const f64 radians = nearest * std::numbers::pi / 180.0;
    return from + vec2{(f32)std::cos(radians), (f32)std::sin(radians)} * length;
}

vec2 grid(vec2 p, i32 size) {
    const f32 cell = (f32)size;
    return {std::round(p.x / cell) * cell, std::round(p.y / cell) * cell};
}

}  // namespace Snapping
