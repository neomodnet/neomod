#pragma once
// Copyright (c) 2015, PG & Jeffrey Han (opsu!), All rights reserved.
#if __has_include("config.h")
#include "config.h"
#endif

#include "Vectors.h"
#include "types.h"

#include <optional>
#include <vector>
#include <span>

namespace neomod {

enum class SLIDERCURVETYPE : char {
    CATMULL = 'C',
    BEZIER = 'B',
    LINEAR = 'L',
    PASSTHROUGH = 'P',
};

// the circle through three points and the arc on it from the first through the second to the third, as osu! finds
// them (angles in radians, the end's on the side the arc goes)
struct CircularArc {
    vec2 center;
    f32 radius;
    f32 startAngle;
    f32 endAngle;
};
// none: the points are on a line, or too nearly so for floats to find a circle through them (osu! draws a perfect circle
// slider through such points as lines)
[[nodiscard]] std::optional<CircularArc> circularArcThrough(vec2 start, vec2 mid, vec2 end);

// calls piece() with each piece osu! draws a bezier slider's points as: the runs between its red anchors (two equal
// points), each starting where the one before ends (a doubled last point stays in its run)
template <typename F>
void forEachBezierPiece(std::span<const vec2> points, F &&piece) {
    uSz start = 0;
    for(uSz i = 1; i < points.size(); i++) {
        if(points[i] != points[i - 1] || i + 1 == points.size()) continue;
        if(i - start >= 2) piece(points.subspan(start, i - start));
        start = i;
    }
    if(points.size() - start >= 2) piece(points.subspan(start));
}

//**********************//
//	 Curve Base Class	//
//**********************//

class SliderCurve final {
   public:
    SliderCurve() = delete;

    // a pixelLength of 0 makes the curve as long as its control points, as osu! plays a slider without a length
    SliderCurve(SLIDERCURVETYPE type, std::span<const vec2> controlPoints, f32 pixelLength);
    SliderCurve(SLIDERCURVETYPE type, std::span<const vec2> controlPoints, f32 pixelLength, f32 curvePointsSeparation);

    SliderCurve(const SliderCurve &) = default;
    SliderCurve &operator=(const SliderCurve &) = default;
    SliderCurve(SliderCurve &&) noexcept = default;
    SliderCurve &operator=(SliderCurve &&) noexcept = default;
    ~SliderCurve() = default;

    [[nodiscard]] vec2 pointAt(f32 t) const;  // NOTE: not adjusted for stacking/HR

    [[nodiscard]] inline f32 getStartAngle() const { return m_startAngle; }
    [[nodiscard]] inline f32 getEndAngle() const { return m_endAngle; }

    [[nodiscard]] std::span<const vec2> getPoints() const;  // NOTE: not adjusted for stacking/HR

    [[nodiscard]] inline f32 getPixelLength() const { return m_pixelLength; }

    [[nodiscard]] inline vec4 getBounds() const { return m_vBounds; }  // NOTE: not adjusted for stacking/HR

   private:
    std::vector<vec2> m_curvePoints;

    vec4 m_vBounds;

    f32 m_startAngle;
    f32 m_endAngle;
    f32 m_pixelLength;

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

    union {
        struct {
            // type == CATMULL || type == BEZIER
            u32 m_NCurve;
        };
        struct {
            // type == CIRCULAR
            f32 m_circCenterX, m_circCenterY;
            f32 m_circRadius;
            f32 m_circStartAngle, m_circEndAngle;
        };
    };

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

    SLIDERCURVETYPE m_type;

    void constructBezier(std::span<const vec2> controlPoints, f32 curvePointsSeparation, bool line);
    void constructCatmull(std::span<const vec2> controlPoints, f32 curvePointsSeparation);
    void constructCircular(const CircularArc &arc, f32 curvePointsSeparation);
};  // namespace neomod
}  // namespace neomod
