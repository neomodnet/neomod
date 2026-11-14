#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFile/BeatmapFile.h"
#include "SliderCurves.h"
#include "Vectors.h"
#include "types.h"

#include <span>
#include <vector>

// a slider path in the terms its line uses: the control points (the head first), the curve type their letter says and
// the length it's played with, and the edge sounds. the rules the editor places and shapes sliders by
namespace SliderPath {
// the head and the points after it (a first point at the head, as older files have it, is the head)
[[nodiscard]] std::vector<vec2> pointsOf(const neomod::BeatmapFile::HitObject &slider);
void setPoints(neomod::BeatmapFile::HitObject &slider, std::span<const vec2> points);

// the type placing gives these points: two, or three on a line (or too nearly so for a circle through them), linear;
// three others a perfect circle (bezier when the arc box is 640x480 osu!px or more); four or more bezier (a red anchor
// is two equal points)
[[nodiscard]] neomod::SLIDERCURVETYPE curveTypeFor(std::span<const vec2> points);
// three points a perfect circle goes through, by that rule
[[nodiscard]] bool makesCircle(std::span<const vec2> points);
// the type a slider of type `type` has once its points are these: moved, it keeps its own (a perfect circle while they
// make one); with points added or taken out, the type placing gives them, but a catmull slider stays one
[[nodiscard]] neomod::SLIDERCURVETYPE typeAfterEdit(neomod::SLIDERCURVETYPE type, std::span<const vec2> points,
                                                    bool moved);
// how long the curve through the points is as drawn by the game (osu!px)
[[nodiscard]] f64 curveLength(neomod::SLIDERCURVETYPE type, std::span<const vec2> points);
// a length down to whole ticks of `tick` osu!px, with 1 osu!px of slack (0: shorter than a tick), at the precision
// lengths are written with
[[nodiscard]] f64 snappedLength(f64 length, f64 tick);
// where a point at p goes in: the index for the end of the control polygon edge nearest to it (edges without a length
// don't count, so red anchors stay together)
[[nodiscard]] uSz insertionIndex(std::span<const vec2> points, vec2 p);

// a path as held by a slider line: the curve type and the control points, the head first
struct Path {
    neomod::SLIDERCURVETYPE type;
    std::vector<vec2> points;
};
// the path the other way, for a slider as long as `length` (0: as its curve): the part that slider shows (cut where the
// length ends, or run on along its last line) from its visual end back to the head, new points in whole osu!px. lines,
// beziers and perfect circles stay as they're drawn (the cut bezier piece split exactly, perfect circle points on its
// arc); a catmull curve keeps its points before the cut and a perfect circle run on past its arc gets a new arc, which
// changes them a little near their ends
[[nodiscard]] Path reversed(neomod::SLIDERCURVETYPE type, std::span<const vec2> points, f64 length);

// what an edge (the head, a repeat, the tail) plays: whistle, finish and clap, and the sample sets
struct Edge {
    u8 sounds{0};
    neomod::BeatmapFile::HitObject::EdgeSet sets{};
    bool operator==(const Edge &) const = default;
};
// the slider edges as played by the game, one per span end and the head first, however its lists are written (an edge
// they leave out plays the body sounds)
[[nodiscard]] std::vector<Edge> edgesOf(const neomod::BeatmapFile::HitObject &slider);
// the slider edge lists become these edges (one per span end, the head first). a line without a hit sample gets one:
// the game reads the lists and the sample together
void setEdges(neomod::BeatmapFile::HitObject &slider, std::span<const Edge> edges);
// the slider with this many spans: the head and the tail keep their edges, a repeat added plays the body sounds, and
// with fewer spans the last repeats are the ones dropped (a list the line doesn't have stays out)
void setSpans(neomod::BeatmapFile::HitObject &slider, i32 spans);
}  // namespace SliderPath
