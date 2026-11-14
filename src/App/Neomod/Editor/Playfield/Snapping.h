#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Vectors.h"
#include "types.h"

#include <array>
#include <optional>

// the rules positions snap by in the playfield: the grid, the spacing from the object before (distance snap) and 45
// degree angles
namespace Snapping {
// osu!px
inline constexpr std::array<i32, 4> GRID_SIZES{4, 8, 16, 32};
// the grid size among the four nearest to any number
[[nodiscard]] i32 clampGridSize(i32 size);
// from 0.1 to 6, at two decimals
[[nodiscard]] f32 clampSpacing(f32 spacing);

// which of them apply, the held modifiers already counted (see EditorState::snapSettings())
struct Settings {
    bool grid{false};
    i32 gridSize{32};
    bool distance{false};
    f32 spacing{0.8f};
    bool angle{false};
    bool operator==(const Settings &) const = default;
};

// the object a position is spaced from: the last one ending before its time, not a spinner
struct Reference {
    vec2 end;  // its end position, unstacked
    i32 endTime;
};

// `spacing` times as far from the reference end as `velocity` (osu!px per ms) covers from its end time to timeMS,
// toward `toward`; none outside the field (the position stays free then)
[[nodiscard]] std::optional<vec2> distance(const Reference &reference, vec2 toward, i32 timeMS, f64 velocity,
                                           f32 spacing);
// `p` turned about `from` to the nearest multiple of 45 degrees, if it's within 10 degrees of one
[[nodiscard]] std::optional<vec2> angle(vec2 from, vec2 p);
[[nodiscard]] vec2 grid(vec2 p, i32 size);
}  // namespace Snapping
