// Copyright (c) 2026, WH, All rights reserved.
#include "SliderPath.h"

#include "ContainerRanges.h"
#include "Parsing.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <string_view>

using namespace neomod;
using HO = BeatmapFile::HitObject;

namespace {
// a perfect circle arc this wide or high (osu!px) is a bezier instead (it would go far off the screen)
constexpr vec2 MAX_ARC_BOX{640.f, 480.f};

vec2 arcPoint(const CircularArc &arc, f64 angle) {
    return arc.center + vec2{(f32)(arc.radius * std::cos(angle)), (f32)(arc.radius * std::sin(angle))};
}

// how wide and high an arc is
vec2 arcBox(const CircularArc &arc) {
    // its ends, and where it's furthest out along the axes
    const vec2 a = arcPoint(arc, arc.startAngle);
    const vec2 c = arcPoint(arc, arc.endAngle);
    f64 minX = std::min(a.x, c.x), maxX = std::max(a.x, c.x);
    f64 minY = std::min(a.y, c.y), maxY = std::max(a.y, c.y);
    for(i32 k = -8; k <= 8; k++) {
        const f64 angle = k * std::numbers::pi / 2.0;
        const f64 along = (angle - arc.startAngle) / ((f64)arc.endAngle - arc.startAngle);
        if(along <= 0.0 || along >= 1.0) continue;
        const vec2 p = arcPoint(arc, angle);
        minX = std::min(minX, (f64)p.x);
        maxX = std::max(maxX, (f64)p.x);
        minY = std::min(minY, (f64)p.y);
        maxY = std::max(maxY, (f64)p.y);
    }
    return {(f32)(maxX - minX), (f32)(maxY - minY)};
}

// an arc in cubic bezier pieces of at most a quarter circle each, joined at red anchors, in whole osu!px
std::vector<vec2> arcBeziers(const CircularArc &arc, f64 from, f64 sweep) {
    const i32 pieces = std::max((i32)std::ceil(std::abs(sweep) / (std::numbers::pi / 2.0) - 1e-9), 1);
    const f64 step = sweep / pieces;
    // how far along their tangents the inner points are from the ends
    const f64 reach = 4.0 / 3.0 * std::tan(step / 4.0) * arc.radius;
    std::vector<vec2> out;
    for(i32 i = 0; i < pieces; i++) {
        const f64 a = from + step * i;
        const f64 b = a + step;
        const vec2 tangentA{(f32)(-std::sin(a) * reach), (f32)(std::cos(a) * reach)};
        const vec2 tangentB{(f32)(-std::sin(b) * reach), (f32)(std::cos(b) * reach)};
        for(const vec2 p :
            {arcPoint(arc, a), arcPoint(arc, a) + tangentA, arcPoint(arc, b) - tangentB, arcPoint(arc, b)}) {
            out.push_back(vec::round(p));
        }
    }
    return out;
}

// the whole osu!px nearest to p that isn't `other`
vec2 wholeNear(vec2 p, vec2 other) {
    const vec2 nearest = vec::round(p);
    if(nearest != other) return nearest;
    vec2 best{0.f};
    f32 bestDistance = std::numeric_limits<f32>::max();
    for(const vec2 by : {vec2{1.f, 0.f}, vec2{-1.f, 0.f}, vec2{0.f, 1.f}, vec2{0.f, -1.f}, vec2{1.f, 1.f},
                         vec2{1.f, -1.f}, vec2{-1.f, 1.f}, vec2{-1.f, -1.f}}) {
        if(const f32 d = vec::distance(nearest + by, p); d < bestDistance) {
            best = nearest + by;
            bestDistance = d;
        }
    }
    return best;
}

// how far (osu!px at most) the perfect circle from a through b to c, as long as `length` (past the arc end on along its
// tangent), is from `curve` backwards, along their lengths (none: no circle goes through them)
std::optional<f32> arcOffBackwards(const SliderCurve &curve, vec2 a, vec2 b, vec2 c, f32 length) {
    const auto arc = circularArcThrough(a, b, c);
    if(!arc) return std::nullopt;
    const f64 sweep = (f64)arc->endAngle - arc->startAngle;
    const f64 arcLength = std::abs(sweep) * arc->radius;
    const f64 way = sweep > 0.0 ? 1.0 : -1.0;
    const f64 end = arc->endAngle;
    f32 most = 0.f;
    for(i32 i = 0; i <= 8; i++) {
        const f64 along = length * i / 8.0;
        const vec2 p = along <= arcLength
                           ? arcPoint(*arc, arc->startAngle + way * along / arc->radius)
                           : arcPoint(*arc, end) + vec2{(f32)(-std::sin(end) * way * (along - arcLength)),
                                                        (f32)(std::cos(end) * way * (along - arcLength))};
        most = std::max(most, vec::distance(curve.pointAt(1.f - (f32)i / 8.f), p));
    }
    return most;
}

// what an edge the slider lists leave out plays
SliderPath::Edge bodyEdge(const HO &slider) {
    return {.sounds = (u8)slider.hitSounds,
            .sets = {.normalSet = slider.sample.normalSet, .additionSet = slider.sample.additionSet}};
}

// the control points for the part of a bezier from its start to t (the first point on each level, as in de Casteljau)
std::vector<vec2> bezierTo(std::span<const vec2> points, f32 t) {
    std::vector<vec2> level(points.begin(), points.end());
    std::vector<vec2> part{level.front()};
    while(level.size() > 1) {
        for(uSz i = 0; i + 1 < level.size(); i++) level[i] += (level[i + 1] - level[i]) * t;
        level.pop_back();
        part.push_back(level.front());
    }
    return part;
}

// where the length a bezier has from its start is `fraction` of all of it, as a parameter (its length measured in
// chords of a few osu!px, enough to be within a hundredth of one)
f32 bezierParameterAt(std::span<const vec2> points, f64 fraction, f64 length) {
    const i32 chords = std::clamp((i32)(length / 2.0), 16, 4096);
    std::vector<f64> lengths{0.0};
    lengths.reserve((uSz)chords + 1);
    std::vector<vec2> level;
    vec2 last = points.front();
    for(i32 i = 1; i <= chords; i++) {
        // its point there: the de Casteljau levels folded in place
        const f32 t = (f32)i / (f32)chords;
        level.assign(points.begin(), points.end());
        for(uSz n = level.size(); n > 1; n--) {
            for(uSz j = 0; j + 1 < n; j++) level[j] += (level[j + 1] - level[j]) * t;
        }
        lengths.push_back(lengths.back() + vec::distance(last, level.front()));
        last = level.front();
    }
    const f64 target = fraction * lengths.back();
    const auto after = Mc::ranges::upper_bound(lengths, target);
    if(after == lengths.end()) return 1.f;
    const uSz i = after - lengths.begin();
    return (f32)(((f64)(i - 1) + (target - lengths[i - 1]) / (lengths[i] - lengths[i - 1])) / chords);
}

// the visible part of a bezier slider, backwards (see SliderPath::reversed())
std::vector<vec2> reversedBezier(std::span<const vec2> points, f32 length, vec2 visualEnd) {
    std::vector<std::vector<vec2>> pieces;
    forEachBezierPiece(points, [&](std::span<const vec2> piece) { pieces.emplace_back(piece.begin(), piece.end()); });
    if(pieces.empty()) return {vec::round(visualEnd), points.front()};

    // the pieces up to where the length ends, the last one cut there (unless that's within half an osu!px from its end;
    // its new points in whole osu!px, none on the one before it, which would make a red anchor), else all of them and
    // a line on to the visual end (when that's further by a whole osu!px or more)
    f32 walked = 0.f;
    for(uSz i = 0; i < pieces.size(); i++) {
        std::vector<vec2> &piece = pieces[i];
        const f32 pieceLength = SliderCurve{SLIDERCURVETYPE::BEZIER, piece, 0.f, 100.f}.getPixelLength();
        if(!(pieceLength > 0.f) || walked + pieceLength < length) {
            walked += pieceLength;
            continue;
        }
        if(walked + pieceLength - length >= 0.5f) {
            const std::vector<vec2> cut =
                bezierTo(piece, bezierParameterAt(piece, (length - walked) / pieceLength, pieceLength));
            piece.assign(1, cut.front());
            for(uSz j = 1; j < cut.size(); j++) piece.push_back(wholeNear(cut[j], piece.back()));
        }
        pieces.resize(i + 1);
        walked = length;
        break;
    }
    if(const vec2 end = vec::round(visualEnd); walked < length && end != pieces.back().back()) {
        pieces.push_back({pieces.back().back(), end});
    }

    // a piece ending on a doubled point now starts on it, which would read as a red anchor, or as the head alone: its
    // second point one osu!px on, which hardly changes the curve
    std::vector<vec2> out;
    for(auto piece = pieces.rbegin(); piece != pieces.rend(); ++piece) {
        const uSz first = out.size();
        out.insert(out.end(), piece->rbegin(), piece->rend());
        if(piece->size() > 2 && out[first] == out[first + 1]) {
            out[first + 1] = wholeNear(out[first] + vec::normalize(out[first + 2] - out[first]), out[first]);
        }
    }
    return out;
}
}  // namespace

namespace SliderPath {

std::vector<vec2> pointsOf(const HO &slider) {
    const auto &curve = slider.curvePoints;
    std::vector<vec2> points;
    points.reserve(curve.size() + 1);
    points.emplace_back(slider.x, slider.y);
    const auto from = curve.begin() + (!curve.empty() && curve.front() == points.front() ? 1 : 0);
    points.insert(points.end(), from, curve.end());
    return points;
}

void setPoints(HO &slider, std::span<const vec2> points) {
    slider.x = points[0].x;
    slider.y = points[0].y;
    slider.curvePoints.assign(points.begin() + 1, points.end());
}

SLIDERCURVETYPE curveTypeFor(std::span<const vec2> points) {
    using enum SLIDERCURVETYPE;
    if(points.size() < 3) return LINEAR;
    if(points.size() > 3) return BEZIER;
    if(makesCircle(points)) return PASSTHROUGH;
    return circularArcThrough(points[0], points[1], points[2]) ? BEZIER : LINEAR;
}

bool makesCircle(std::span<const vec2> points) {
    const auto arc = points.size() == 3 ? circularArcThrough(points[0], points[1], points[2]) : std::nullopt;
    if(!arc) return false;
    const vec2 box = arcBox(*arc);
    return box.x < MAX_ARC_BOX.x && box.y < MAX_ARC_BOX.y;
}

SLIDERCURVETYPE typeAfterEdit(SLIDERCURVETYPE type, std::span<const vec2> points, bool moved) {
    if(type == SLIDERCURVETYPE::CATMULL) return type;
    if(moved && (type != SLIDERCURVETYPE::PASSTHROUGH || makesCircle(points))) return type;
    return curveTypeFor(points);
}

f64 curveLength(SLIDERCURVETYPE type, std::span<const vec2> points) {
    if(points.size() < 2) return 0.0;
    // the widest separation the curve takes: its resampling is wasted here
    return SliderCurve{type, points, 0.f, 100.f}.getPixelLength();
}

f64 snappedLength(f64 length, f64 tick) {
    // the slack doesn't make a curve without a length one tick long
    if(!(length > 0.0)) return 0.0;
    if(!(tick > 0.0)) return length;
    // at the 15 significant digits lengths are written with: a tick computed as 57.49999999999999 is 57.5
    std::array<char, 32> text{};
    const f64 snapped = std::floor((length + 1.0) / tick) * tick;
    const auto written = std::to_chars(text.data(), text.data() + text.size(), snapped, std::chars_format::general, 15);
    f64 read = snapped;
    Parsing::parse(std::string_view{text.data(), written.ptr}, &read);
    return read;
}

uSz insertionIndex(std::span<const vec2> points, vec2 p) {
    uSz best = points.size();
    f32 nearest = std::numeric_limits<f32>::max();
    for(uSz i = 1; i < points.size(); i++) {
        const vec2 from = points[i - 1];
        const vec2 edge = points[i] - from;
        const f32 lengthSq = vec::dot(edge, edge);
        if(!(lengthSq > 0.f)) continue;
        const f32 along = std::clamp(vec::dot(p - from, edge) / lengthSq, 0.f, 1.f);
        if(const f32 d = vec::distance(p, from + edge * along); d < nearest) {
            best = i;
            nearest = d;
        }
    }
    return best;
}

std::vector<Edge> edgesOf(const HO &slider) {
    // listed as many as the longer list has, from the body where the other one stops; one listed is for both the head
    // and the tail, and a repeat past the listed ones plays the last one before the tail
    std::vector<Edge> listed(std::max({slider.edgeSounds.size(), slider.edgeSets.size(), uSz{1}}), bodyEdge(slider));
    for(uSz i = 0; i < slider.edgeSounds.size(); i++) listed[i].sounds = slider.edgeSounds[i];
    for(uSz i = 0; i < slider.edgeSets.size(); i++) listed[i].sets = slider.edgeSets[i];
    if(listed.size() == 1) listed.push_back(listed.front());

    const uSz spans = (uSz)std::max(slider.slides, 1);
    std::vector<Edge> edges;
    edges.reserve(spans + 1);
    for(uSz i = 0; i <= spans; i++) {
        edges.push_back(i == 0                  ? listed.front()
                        : i == spans            ? listed.back()
                        : i + 1 < listed.size() ? listed[i]
                                                : listed[listed.size() - 2]);
    }
    return edges;
}

void setEdges(HO &slider, std::span<const Edge> edges) {
    slider.edgeSounds.clear();
    slider.edgeSets.clear();
    for(const Edge &edge : edges) {
        slider.edgeSounds.push_back(edge.sounds);
        slider.edgeSets.push_back(edge.sets);
    }
    if(slider.sample.parts == 0) slider.sample.parts = 5;
}

void setSpans(HO &slider, i32 spans) {
    const bool sounds = !slider.edgeSounds.empty();
    const bool sets = !slider.edgeSets.empty();
    if(sounds || sets) {
        std::vector<Edge> edges = edgesOf(slider);
        const Edge tail = edges.back();
        edges.pop_back();
        edges.resize((uSz)spans, bodyEdge(slider));
        edges.push_back(tail);
        slider.edgeSounds.clear();
        slider.edgeSets.clear();
        for(const Edge &edge : edges) {
            if(sounds) slider.edgeSounds.push_back(edge.sounds);
            if(sets) slider.edgeSets.push_back(edge.sets);
        }
    }
    slider.slides = spans;
}

Path reversed(SLIDERCURVETYPE type, std::span<const vec2> points, f64 length) {
    using enum SLIDERCURVETYPE;
    if(points.size() < 2) return {.type = type, .points = {points.begin(), points.end()}};
    // as drawn by the game: how long, and where it ends
    const SliderCurve curve{type, points, (f32)length};
    const f32 shown = curve.getPixelLength();
    const vec2 visualEnd = curve.pointAt(1.f);
    const vec2 end = vec::round(visualEnd);
    // `first`, then the first `kept` points backwards (`first` left out when it's the last of them, unless that's the
    // head alone)
    const auto from = [&](vec2 first, uSz kept) {
        std::vector<vec2> out;
        if(first != points[kept - 1] || kept == 1) out.push_back(first);
        out.insert(out.end(), std::make_reverse_iterator(points.begin() + (sSz)kept), points.rend());
        return out;
    };

    if(const auto arc = type == PASSTHROUGH && points.size() == 3 ? circularArcThrough(points[0], points[1], points[2])
                                                                  : std::nullopt) {
        // through the visual end, the arc middle and the head: among the whole osu!px around those two, the pair whose
        // arc follows it best (around a short chord rounding turns the circle a lot)
        const vec2 middle = curve.pointAt(0.5f);
        const auto around = [](vec2 p) {
            return std::array{vec2{std::floor(p.x), std::floor(p.y)}, vec2{std::ceil(p.x), std::floor(p.y)},
                              vec2{std::floor(p.x), std::ceil(p.y)}, vec2{std::ceil(p.x), std::ceil(p.y)}};
        };
        Path out{.type = LINEAR, .points = {end, points[0]}};
        f32 best = std::numeric_limits<f32>::max();
        for(const vec2 e : around(visualEnd)) {
            for(const vec2 m : around(middle)) {
                if(const auto off = arcOffBackwards(curve, e, m, points[0], shown); off && *off < best) {
                    best = *off;
                    out = {.type = PASSTHROUGH, .points = {e, m, points[0]}};
                }
            }
        }
        // a perfect circle can't run on past its arc: further than that allows, a bezier (the line, then the arc)
        const f64 sweep = (f64)arc->endAngle - arc->startAngle;
        if(best > 1.f && shown > std::abs(sweep) * arc->radius + 1.0) {
            out = {.type = BEZIER, .points = {end, vec::round(arcPoint(*arc, arc->endAngle))}};
            Mc::ranges::append(out.points, arcBeziers(*arc, arc->endAngle, -sweep));
        }
        return out;
    }
    if(type == CATMULL) {
        uSz kept = 1;
        while(kept < points.size() && curveLength(CATMULL, points.first(kept + 1)) < shown - 0.5f) kept++;
        return {.type = CATMULL, .points = from(end, kept)};
    }
    if(type == LINEAR || (type == PASSTHROUGH && points.size() == 3)) {
        // lines (perfect circle points on one, or too nearly for a circle through them): cut on the line the length
        // ends on
        f32 walked = 0.f;
        uSz kept = 1;
        for(; kept < points.size(); kept++) {
            const f32 line = vec::distance(points[kept - 1], points[kept]);
            if(line > 0.f && walked + line >= shown) {
                const vec2 cut = points[kept - 1] + (points[kept] - points[kept - 1]) * ((shown - walked) / line);
                return {.type = LINEAR, .points = from(vec::round(cut), kept)};
            }
            walked += line;
        }
        // run on: the last line ends at the visual end
        return {.type = LINEAR, .points = from(end, end == points.back() ? kept : kept - 1)};
    }

    return {.type = BEZIER, .points = reversedBezier(points, shown, end)};
}

}  // namespace SliderPath
