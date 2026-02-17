// Copyright (c) 2015, PG & Jeffrey Han (opsu!), All rights reserved.
#include "SliderCurves.h"
#include "noinclude.h"
#include "types.h"

#include <algorithm>

#ifndef BUILD_TOOLS_ONLY
#include "Engine.h"
#include "Logging.h"
#include "OsuConVars.h"
#define SLIDER_CURVE_POINTS_SEPARATION cv::slider_curve_points_separation.getFloat()
#define SLIDER_CURVE_MAX_LENGTH cv::slider_curve_max_length.getFloat()
#define SLIDER_CURVE_MAX_POINTS cv::slider_curve_max_points.getVal<u32>()
#else
#include <print>
#define SLIDER_CURVE_POINTS_SEPARATION 2.5f
#define SLIDER_CURVE_MAX_LENGTH 32768.f
#define SLIDER_CURVE_MAX_POINTS 9999U
#define debugLog(...) std::println(__VA_ARGS__)
#endif

//*******************//
//	 Curve Builder	 //
//*******************//

// combines bezier approximation and equal-distance resampling with persistent buffers
// placed up here for friend access to SliderCurve
class SliderCurveBuilder {
   private:
    // For bezier approximator:
    // https://github.com/ppy/osu/blob/master/osu.Game/Rulesets/Objects/BezierApproximator.cs
    // https://github.com/ppy/osu-framework/blob/master/osu.Framework/MathUtils/PathApproximator.cs
    // Copyright (c) ppy Pty Ltd <contact@ppy.sh>. Licensed under the MIT Licence.

    static constexpr f64 TOLERANCE_SQ = 0.25 * 0.25;

    // bezier approximation buffers
    std::vector<vec2> midpoints;
    std::vector<vec2> leftChild;
    std::vector<vec2> rightChild;
    std::vector<std::vector<vec2>> curvePool;
    std::vector<u32> poolStack;

    u32 poolNextFree{0};
    u64 bezierCount{0};

    // accumulated curve data (flat buffer + segment boundaries)
    std::vector<vec2> allPoints;
    std::vector<u64> segmentStarts;  // indices into allPoints where each segment begins

    [[nodiscard]] bool isFlatEnough(const vec2 *curve) const {
        for(u64 i = 1; i < this->bezierCount - 1; i++) {
            const f32 len = vec::length(curve[i - 1] - 2.f * curve[i] + curve[i + 1]);
            if((f64)(len * len) > TOLERANCE_SQ * 4) return false;
        }
        return true;
    }

    void subdivide(const vec2 *curve, vec2 *l, vec2 *r) {
        for(u64 i = 0; i < this->bezierCount; ++i) {
            this->midpoints[i] = curve[i];
        }

        for(u64 i = 0; i < this->bezierCount; i++) {
            l[i] = this->midpoints[0];
            r[this->bezierCount - i - 1] = this->midpoints[this->bezierCount - i - 1];

            for(u64 j = 0; j < this->bezierCount - i - 1; j++) {
                this->midpoints[j] = (this->midpoints[j] + this->midpoints[j + 1]) * 0.5f;
            }
        }
    }

    void approximate(const vec2 *curve) {
        subdivide(curve, this->leftChild.data(), this->rightChild.data());

        for(u64 i = 0; i < this->bezierCount - 1; ++i) {
            this->leftChild[this->bezierCount + i] = this->rightChild[i + 1];
        }

        this->allPoints.push_back(curve[0]);
        for(u64 i = 1; i < this->bezierCount - 1; ++i) {
            const u64 index = 2 * i;
            this->allPoints.push_back(
                0.25f * (this->leftChild[index - 1] + 2.f * this->leftChild[index] + this->leftChild[index + 1]));
        }
    }

    u32 acquireCurve() {
        if(this->poolNextFree >= this->curvePool.size()) {
            this->curvePool.emplace_back();
        }
        u32 idx = this->poolNextFree++;
        if(this->curvePool[idx].size() < this->bezierCount) {
            this->curvePool[idx].resize(this->bezierCount);
        }
        return idx;
    }

    void generateBezierPoints(const vec2 *controlPoints, u64 count) {
        this->bezierCount = count;

        if(this->bezierCount == 0) return;
        if(this->bezierCount < 2) {
            this->allPoints.push_back(controlPoints[0]);
            return;
        }

        if(this->midpoints.size() < this->bezierCount) this->midpoints.resize(this->bezierCount);
        if(this->leftChild.size() < this->bezierCount * 2 - 1) this->leftChild.resize(this->bezierCount * 2 - 1);
        if(this->rightChild.size() < this->bezierCount) this->rightChild.resize(this->bezierCount);

        this->poolNextFree = 0;
        this->poolStack.clear();

        u32 initialIdx = acquireCurve();
        std::copy_n(controlPoints, this->bezierCount, this->curvePool[initialIdx].begin());
        this->poolStack.push_back(initialIdx);

        while(!this->poolStack.empty()) {
            u32 parentIdx = this->poolStack.back();
            this->poolStack.pop_back();
            vec2 *parent = this->curvePool[parentIdx].data();

            if(isFlatEnough(parent)) {
                approximate(parent);
                continue;
            }

            u32 rightIdx = acquireCurve();
            vec2 *right = this->curvePool[rightIdx].data();

            subdivide(parent, this->leftChild.data(), right);

            std::copy_n(this->leftChild.data(), this->bezierCount, parent);

            this->poolStack.push_back(rightIdx);
            this->poolStack.push_back(parentIdx);
        }

        this->allPoints.push_back(controlPoints[this->bezierCount - 1]);
    }

   public:
    void reset() {
        this->allPoints.clear();
        this->segmentStarts.clear();
    }

    void addBezierSegment(const vec2 *controlPoints, u64 count) {
        this->segmentStarts.push_back(this->allPoints.size());
        generateBezierPoints(controlPoints, count);
    }

    void addCatmullSegment(const std::array<vec2, 4> &initPoints) {
        this->segmentStarts.push_back(this->allPoints.size());

        f32 approxLength = 0;
        for(i32 i = 1; i < 4; i++) {
            approxLength += std::max(0.0001f, vec::length(initPoints[i] - initPoints[i - 1]));
        }

        const i32 numPoints = (i32)(approxLength / 8.0f) + 2;

        for(i32 i = 0; i < numPoints; i++) {
            f32 t = (f32)i / (f32)(numPoints - 1);
            t = t + 1;  // t * (2 - 1) + 1

            const vec2 A1 = initPoints[0] * (1 - t) + initPoints[1] * t;
            const vec2 A2 = initPoints[1] * (2 - t) + initPoints[2] * (t - 1);
            const vec2 A3 = initPoints[2] * (3 - t) + initPoints[3] * (t - 2);

            const vec2 B1 = A1 * ((2 - t) * 0.5f) + A2 * (t * 0.5f);
            const vec2 B2 = A2 * ((3 - t) * 0.5f) + A3 * ((t - 1) * 0.5f);

            this->allPoints.push_back(B1 * (2 - t) + B2 * (t - 1));
        }
    }

    [[nodiscard]] bool hasSegments() const { return !this->segmentStarts.empty(); }

    void build(SliderCurve &curve, u32 nCurve, f32 pixelLength);
};

// the final step for building bezier/catmull curves
void SliderCurveBuilder::build(SliderCurve &curve, u32 nCurve, f32 pixelLength) {
    if(this->allPoints.empty()) {
        debugLog("SliderCurveBuilder::build: Error: allPoints.size() == 0!!!");
        return;
    }

    // add sentinel for easier iteration
    this->segmentStarts.push_back(this->allPoints.size());

    u64 curSegment = 0;
    u64 curPoint = 0;
    u64 segmentEnd = this->segmentStarts[1];

    f32 distanceAt = 0.0f;
    f32 lastDistanceAt = 0.0f;

    vec2 lastCurve = this->allPoints[0];

    // length of the curve should be equal to the pixel length
    // for each distance, try to get in between the two points that are between it
    vec2 lastCurvePointForNextSegmentStart{0.f};
    std::vector<vec2> curCurvePoints;
    for(i64 i = 0; i < (nCurve + 1LL); i++) {
        const f32 temp_dist = (f32)((f32)i * pixelLength) / (f32)nCurve;
        const i32 prefDistance = (std::isfinite(temp_dist) && temp_dist >= (f32)(std::numeric_limits<i32>::min()) &&
                                  temp_dist <= (f32)(std::numeric_limits<i32>::max()))
                                     ? (i32)(temp_dist)
                                     : 0;

        while(distanceAt < prefDistance) {
            lastDistanceAt = distanceAt;
            if(curPoint < this->allPoints.size()) lastCurve = this->allPoints[curPoint];

            // jump to next point
            curPoint++;

            if(curPoint >= segmentEnd) {
                // jump to next segment
                curSegment++;

                if(curCurvePoints.size() > 0) {
                    curve.curvePointSegments.push_back(std::move(curCurvePoints));
                    curCurvePoints.clear();

                    // prepare the next segment by setting/adding the starting point to the exact end point of the
                    // previous segment
                    // this also enables an optimization, namely that startcaps only have to be drawn
                    // [for every segment] if the startpoint != endpoint in the loop
                    if(curve.curvePoints.size() > 0) curCurvePoints.push_back(lastCurvePointForNextSegmentStart);
                }

                if(curSegment + 1 < this->segmentStarts.size()) {
                    segmentEnd = this->segmentStarts[curSegment + 1];
                } else {
                    curPoint = this->allPoints.size() - 1;
                    if(lastDistanceAt == distanceAt) {
                        // out of points even though the preferred distance hasn't been reached
                        break;
                    }
                }
            }

            if(curPoint < this->allPoints.size()) {
                distanceAt += vec::length(this->allPoints[curPoint] - this->allPoints[curPoint - 1]);
            }
        }

        const vec2 thisCurve = (curPoint < this->allPoints.size()) ? this->allPoints[curPoint] : vec2{0.f, 0.f};

        // interpolate the point between the two closest distances
        curve.curvePoints.emplace_back(0, 0);
        curCurvePoints.emplace_back(0, 0);
        if(distanceAt - lastDistanceAt > 1) {
            const f32 t = (prefDistance - lastDistanceAt) / (distanceAt - lastDistanceAt);
            curve.curvePoints[i] = vec2(std::lerp(lastCurve.x, thisCurve.x, t), std::lerp(lastCurve.y, thisCurve.y, t));
        } else
            curve.curvePoints[i] = thisCurve;

        // add the point to the current segment
        // (this is not using the lerp'd point! would cause mm mesh artifacts if it did)
        lastCurvePointForNextSegmentStart = thisCurve;
        curCurvePoints.back() = thisCurve;
    }

    // if we only had one segment, no jump to any next curve has occurred (and therefore no insertion of the segment
    // into the vector) manually add the lone segment here
    if(curCurvePoints.size() > 0) curve.curvePointSegments.push_back(std::move(curCurvePoints));

    // sanity check
    // spec: FIXME: at least one of my maps triggers this (in upstream mcosu too), try to fix
    if(curve.curvePoints.size() == 0) {
        debugLog("SliderCurveBuilder::build: Error: curvePoints.size() == 0!!!");
        return;
    }

    // make sure that the uninterpolated segment points are exactly as long as the pixelLength
    // this is necessary because we can't use the lerp'd points for the segments
    f32 segmentedLength = 0.0f;
    for(const auto &curvePointSegment : curve.curvePointSegments) {
        for(u64 p = 0; p < curvePointSegment.size(); p++) {
            segmentedLength += ((p == 0) ? 0 : vec::length(curvePointSegment[p] - curvePointSegment[p - 1]));
        }
    }

    // TODO: this is still incorrect. sliders are sometimes too long or start too late, even if only for a few
    // pixels
    if(segmentedLength > pixelLength && curve.curvePointSegments.size() > 1 && curve.curvePointSegments[0].size() > 1) {
        f32 excess = segmentedLength - pixelLength;
        while(excess > 0) {
            for(i64 s = (i64)curve.curvePointSegments.size() - 1; s >= 0; s--) {
                for(i64 p = (i64)curve.curvePointSegments[s].size() - 1; p >= 0; p--) {
                    const f32 curLength =
                        (p == 0) ? 0 : vec::length(curve.curvePointSegments[s][p] - curve.curvePointSegments[s][p - 1]);
                    if(curLength >= excess && p != 0) {
                        vec2 segmentVector =
                            vec::normalize(curve.curvePointSegments[s][p] - curve.curvePointSegments[s][p - 1]);
                        curve.curvePointSegments[s][p] = curve.curvePointSegments[s][p] - segmentVector * excess;
                        excess = 0.0f;
                        break;
                    } else {
                        curve.curvePointSegments[s].erase(curve.curvePointSegments[s].begin() + p);
                        excess -= curLength;
                    }
                }
            }
        }
    }

    // calculate start and end angles for possible repeats
    // (good enough and cheaper than calculating it live every frame)
    if(curve.curvePoints.size() > 1) {
        vec2 c1 = curve.curvePoints[0];
        u64 cnt = 1;
        vec2 c2 = curve.curvePoints[cnt++];
        while(cnt <= nCurve && cnt < curve.curvePoints.size() && vec::length(c2 - c1) < 1) {
            c2 = curve.curvePoints[cnt++];
        }
        curve.fStartAngle = (f32)(std::atan2(c2.y - c1.y, c2.x - c1.x) * 180 / PI);
    }

    if(curve.curvePoints.size() > 1) {
        if(nCurve < curve.curvePoints.size()) {
            vec2 c1 = curve.curvePoints[nCurve];
            i64 cnt = nCurve - 1;
            vec2 c2 = curve.curvePoints[cnt--];
            while(cnt >= 0 && vec::length(c2 - c1) < 1) {
                c2 = curve.curvePoints[cnt--];
            }
            curve.fEndAngle = (f32)(std::atan2(c2.y - c1.y, c2.x - c1.x) * 180 / PI);
        }
    }

    // backup (for dynamic updateStackPosition() recalculation)
    curve.originalCurvePoints = curve.curvePoints;
    curve.originalCurvePointSegments = curve.curvePointSegments;
}

namespace {  // static

// used as a calculation buffer to avoid reallocations on each curve creation
thread_local SliderCurveBuilder g_curveBuilder;

constexpr const SLIDERCURVETYPE CATMULL = 'C';
// constexpr const SLIDERCURVETYPE BEZIER = 'B';
constexpr const SLIDERCURVETYPE LINEAR = 'L';
constexpr const SLIDERCURVETYPE PASSTHROUGH = 'P';

//***********************//
//	 Curve Subclasses	 //
//***********************//

class SliderCurveEqualDistanceMulti : public SliderCurve {
   public:
    SliderCurveEqualDistanceMulti(std::vector<vec2> controlPoints, f32 pixelLength, f32 curvePointsSeparation,
                                  bool line);  // beziers
    SliderCurveEqualDistanceMulti(std::vector<vec2> controlPoints, f32 pixelLength,
                                  f32 curvePointsSeparation);  // catmulls

    SliderCurveEqualDistanceMulti(const SliderCurveEqualDistanceMulti &) = default;
    SliderCurveEqualDistanceMulti &operator=(const SliderCurveEqualDistanceMulti &) = default;
    SliderCurveEqualDistanceMulti(SliderCurveEqualDistanceMulti &&) = default;
    SliderCurveEqualDistanceMulti &operator=(SliderCurveEqualDistanceMulti &&) = default;
    ~SliderCurveEqualDistanceMulti() override = default;

    [[nodiscard]] vec2 pointAt(f32 t) const override;
    [[nodiscard]] vec2 originalPointAt(f32 t) const override;

   private:
    u32 iNCurve;
};

// the only difference is that bezier's constructor takes a "line" parameter as an argument, catmull's doesn't
using SliderCurveBezier = SliderCurveEqualDistanceMulti;
using SliderCurveCatmull = SliderCurveEqualDistanceMulti;

vec2 SliderCurveEqualDistanceMulti::pointAt(f32 t) const {
    if(this->curvePoints.size() < 1) return {0.f, 0.f};

    const f64 indexD = (f64)t * this->iNCurve;
    const u64 index = (u64)indexD;
    if(index >= this->iNCurve) {
        if(this->iNCurve < this->curvePoints.size())
            return this->curvePoints[this->iNCurve];
        else {
            debugLog("SliderCurveEqualDistanceMulti::pointAt() Error: Illegal index {:d}!!!", this->iNCurve);
            return {0.f, 0.f};
        }
    } else {
        if(index + 1 >= this->curvePoints.size()) {
            debugLog("SliderCurveEqualDistanceMulti::pointAt() Error: Illegal index {:d}!!!", index);
            return {0.f, 0.f};
        }

        const vec2 poi = this->curvePoints[index];
        const vec2 poi2 = this->curvePoints[index + 1];

        const f32 t2 = (f32)(indexD - (f64)index);

        return {std::lerp(poi.x, poi2.x, t2), std::lerp(poi.y, poi2.y, t2)};
    }
}

vec2 SliderCurveEqualDistanceMulti::originalPointAt(f32 t) const {
    if(this->originalCurvePoints.size() < 1) return {0.f, 0.f};

    const f64 indexD = (f64)t * this->iNCurve;
    const u64 index = (u64)indexD;
    if(index >= this->iNCurve) {
        if(this->iNCurve < this->originalCurvePoints.size())
            return this->originalCurvePoints[this->iNCurve];
        else {
            debugLog("SliderCurveEqualDistanceMulti::originalPointAt() Error: Illegal index {:d}!!!", this->iNCurve);
            return {0.f, 0.f};
        }
    } else {
        if(index + 1 >= this->originalCurvePoints.size()) {
            debugLog("SliderCurveEqualDistanceMulti::originalPointAt() Error: Illegal index {:d}!!!", index);
            return {0.f, 0.f};
        }

        const vec2 poi = this->originalCurvePoints[index];
        const vec2 poi2 = this->originalCurvePoints[index + 1];

        const f32 t2 = (f32)(indexD - (f64)index);

        return {std::lerp(poi.x, poi2.x, t2), std::lerp(poi.y, poi2.y, t2)};
    }
}

//*******************//
//	 Bezier Curves	 //
//*******************//

SliderCurveEqualDistanceMulti::SliderCurveEqualDistanceMulti(std::vector<vec2> controlPoints_, f32 pixelLength,
                                                             f32 curvePointsSeparation, bool line)
    : SliderCurve(std::move(controlPoints_), pixelLength) {
    const u32 max_points = SLIDER_CURVE_MAX_POINTS;
    this->iNCurve =
        std::min((u32)(this->fPixelLength / std::clamp<f32>(curvePointsSeparation, 1.0f, 100.0f)), max_points);

    const u64 numControlPoints = this->controlPoints.size();

    g_curveBuilder.reset();

    // Beziers: splits points into different Beziers if has the same points (red anchor points)
    // a b c - c d - d e f g
    // Lines: generate a new curve for each sequential pair
    // ab  bc  cd  de  ef  fg
    u64 segmentStart = 0;
    for(u64 i = 1; i < numControlPoints; i++) {
        if(line) {
            g_curveBuilder.addBezierSegment(&this->controlPoints[i - 1], 2);
        } else if(this->controlPoints[i] == this->controlPoints[i - 1]) {
            // red anchor point - end current segment
            if(i - segmentStart >= 2) {
                g_curveBuilder.addBezierSegment(&this->controlPoints[segmentStart], i - segmentStart);
            }
            segmentStart = i;
        }
    }

    // handle final segment (non-line mode only)
    if(!line && numControlPoints - segmentStart >= 2) {
        g_curveBuilder.addBezierSegment(&this->controlPoints[segmentStart], numControlPoints - segmentStart);
    }

    if(g_curveBuilder.hasSegments()) {
        g_curveBuilder.build(*this, this->iNCurve, this->fPixelLength);
    } else {
        debugLog(
            "SliderCurveEqualDistanceMulti ERROR: no segments (line: {} numControlPoints: {} pixelLength: {} "
            "curvePointsSeparation: {})",
            line, numControlPoints, pixelLength, curvePointsSeparation);
    }
}

//********************//
//   Catmull Curves   //
//********************//

SliderCurveEqualDistanceMulti::SliderCurveEqualDistanceMulti(std::vector<vec2> controlPoints_, f32 pixelLength,
                                                             f32 curvePointsSeparation)
    : SliderCurve(std::move(controlPoints_), pixelLength) {
    const u32 max_points = SLIDER_CURVE_MAX_POINTS;
    this->iNCurve =
        std::min((u32)(this->fPixelLength / std::clamp<f32>(curvePointsSeparation, 1.0f, 100.0f)), max_points);

    const u64 numControlPoints = this->controlPoints.size();

    g_curveBuilder.reset();

    // build temporary 4-point windows for catmull-rom
    // repeat the first and last points as control points if they differ from neighbors
    std::array<vec2, 4> catmullPoints;

    // handle first point duplication
    bool duplicateFirst =
        (this->controlPoints[0].x != this->controlPoints[1].x || this->controlPoints[0].y != this->controlPoints[1].y);

    // handle last point duplication
    bool duplicateLast = (this->controlPoints[numControlPoints - 1].x != this->controlPoints[numControlPoints - 2].x ||
                          this->controlPoints[numControlPoints - 1].y != this->controlPoints[numControlPoints - 2].y);

    // calculate effective point count
    u64 effectiveCount = numControlPoints + (duplicateFirst ? 1 : 0) + (duplicateLast ? 1 : 0);

    auto getPoint = [&](u64 idx) -> vec2 {
        if(duplicateFirst) {
            if(idx == 0) return this->controlPoints[0];
            idx--;
        }
        if(idx < numControlPoints) return this->controlPoints[idx];
        return this->controlPoints[numControlPoints - 1];
    };

    for(u64 i = 0; i + 3 < effectiveCount; i++) {
        catmullPoints[0] = getPoint(i);
        catmullPoints[1] = getPoint(i + 1);
        catmullPoints[2] = getPoint(i + 2);
        catmullPoints[3] = getPoint(i + 3);
        g_curveBuilder.addCatmullSegment(catmullPoints);
    }

    // handle final window if we have exactly 4 effective points remaining
    if(effectiveCount >= 4) {
        catmullPoints[0] = getPoint(effectiveCount - 4);
        catmullPoints[1] = getPoint(effectiveCount - 3);
        catmullPoints[2] = getPoint(effectiveCount - 2);
        catmullPoints[3] = getPoint(effectiveCount - 1);

        // only add if not already added in the loop
        if(effectiveCount == 4) {
            g_curveBuilder.addCatmullSegment(catmullPoints);
        }
    }

    if(g_curveBuilder.hasSegments()) {
        g_curveBuilder.build(*this, this->iNCurve, this->fPixelLength);
    } else {
        debugLog("ERROR: catmulls.size() == 0 (numControlPoints: {} pixelLength: {} curvePointsSeparation: {})",
                 numControlPoints, pixelLength, curvePointsSeparation);
    }
}

//**********************//
//	 Circular Curves	//
//**********************//

class SliderCurveCircumscribedCircle final : public SliderCurve {
   public:
    SliderCurveCircumscribedCircle(std::vector<vec2> controlPoints, f32 pixelLength, f32 curvePointsSeparation);

    SliderCurveCircumscribedCircle(const SliderCurveCircumscribedCircle &) = default;
    SliderCurveCircumscribedCircle &operator=(const SliderCurveCircumscribedCircle &) = default;
    SliderCurveCircumscribedCircle(SliderCurveCircumscribedCircle &&) = default;
    SliderCurveCircumscribedCircle &operator=(SliderCurveCircumscribedCircle &&) = default;
    ~SliderCurveCircumscribedCircle() override = default;

    [[nodiscard]] vec2 pointAt(f32 t) const override;
    [[nodiscard]] vec2 originalPointAt(f32 t) const override;

    void updateStackPosition(f32 stackMulStackOffset,
                             bool HR) override;  // must also override this, due to the custom pointAt() function!

   private:
    [[nodiscard]] static vec2 intersect(vec2 a, vec2 ta, vec2 b, vec2 tb);

    [[nodiscard]] static forceinline bool isIn(f32 a, f32 b, f32 c) { return ((b > a && b < c) || (b < a && b > c)); }

    vec2 vCircleCenter{0.f};
    vec2 vOriginalCircleCenter{0.f};
    f32 fRadius;
    f32 fCalculationStartAngle;
    f32 fCalculationEndAngle;
};

SliderCurveCircumscribedCircle::SliderCurveCircumscribedCircle(std::vector<vec2> controlPoints, f32 pixelLength,
                                                               f32 curvePointsSeparation)
    : SliderCurve(std::move(controlPoints), pixelLength) {
    if(this->controlPoints.size() != 3) {
        debugLog("SliderCurveCircumscribedCircle() Error: controlPoints.size() != 3");
        return;
    }

    // construct the three points
    const vec2 start = this->controlPoints[0];
    const vec2 mid = this->controlPoints[1];
    const vec2 end = this->controlPoints[2];

    // find the circle center
    const vec2 mida = start + (mid - start) * 0.5f;
    const vec2 midb = end + (mid - end) * 0.5f;

    vec2 nora = mid - start;
    f32 temp = nora.x;
    nora.x = -nora.y;
    nora.y = temp;
    vec2 norb = mid - end;
    temp = norb.x;
    norb.x = -norb.y;
    norb.y = temp;

    this->vOriginalCircleCenter = this->intersect(mida, nora, midb, norb);
    this->vCircleCenter = this->vOriginalCircleCenter;

    // find the angles relative to the circle center
    vec2 startAngPoint = start - this->vCircleCenter;
    vec2 midAngPoint = mid - this->vCircleCenter;
    vec2 endAngPoint = end - this->vCircleCenter;

    this->fCalculationStartAngle = (f32)std::atan2(startAngPoint.y, startAngPoint.x);
    const auto midAng = (f32)std::atan2(midAngPoint.y, midAngPoint.x);
    this->fCalculationEndAngle = (f32)std::atan2(endAngPoint.y, endAngPoint.x);

    // find the angles that pass through midAng
    if(!this->isIn(this->fCalculationStartAngle, midAng, this->fCalculationEndAngle)) {
        if(std::abs(this->fCalculationStartAngle + 2.f * PI - this->fCalculationEndAngle) < 2.f * PI &&
           this->isIn(this->fCalculationStartAngle + (2.f * PI), midAng, this->fCalculationEndAngle))
            this->fCalculationStartAngle += 2.f * PI;
        else if(std::abs(this->fCalculationStartAngle - (this->fCalculationEndAngle + 2.f * PI)) < 2.f * PI &&
                this->isIn(this->fCalculationStartAngle, midAng, this->fCalculationEndAngle + (2.f * PI)))
            this->fCalculationEndAngle += 2.f * PI;
        else if(std::abs(this->fCalculationStartAngle - 2.f * PI - this->fCalculationEndAngle) < 2.f * PI &&
                this->isIn(this->fCalculationStartAngle - (2.f * PI), midAng, this->fCalculationEndAngle))
            this->fCalculationStartAngle -= 2.f * PI;
        else if(std::abs(this->fCalculationStartAngle - (this->fCalculationEndAngle - 2.f * PI)) < 2.f * PI &&
                this->isIn(this->fCalculationStartAngle, midAng, this->fCalculationEndAngle - (2.f * PI)))
            this->fCalculationEndAngle -= 2.f * PI;
        else {
            debugLog("SliderCurveCircumscribedCircle() Error: Cannot find angles between midAng ({} {} {})",
                     this->fCalculationStartAngle, midAng, this->fCalculationEndAngle);
            return;
        }
    }

    // find an angle with an arc length of pixelLength along this circle
    this->fRadius = vec::length(startAngPoint);
    const f32 arcAng = this->fPixelLength / this->fRadius;  // len = theta * r / theta = len / r

    // now use it for our new end angle
    this->fCalculationEndAngle = (this->fCalculationEndAngle > this->fCalculationStartAngle)
                                     ? this->fCalculationStartAngle + arcAng
                                     : this->fCalculationStartAngle - arcAng;

    // find the angles to draw for repeats
    this->fEndAngle = (f32)((this->fCalculationEndAngle +
                             (this->fCalculationStartAngle > this->fCalculationEndAngle ? PI / 2.0f : -PI / 2.0f)) *
                            180.0f / PI);
    this->fStartAngle = (f32)((this->fCalculationStartAngle +
                               (this->fCalculationStartAngle > this->fCalculationEndAngle ? -PI / 2.0f : PI / 2.0f)) *
                              180.0f / PI);

    // calculate points
    const f32 max_points = SLIDER_CURVE_MAX_POINTS;
    const f32 steps = std::min(this->fPixelLength / (std::clamp<f32>(curvePointsSeparation, 1.0f, 100.0f)), max_points);
    const i32 intSteps = (i32)std::round(steps) + 2;  // must guarantee an int range of 0 to steps!
    for(i32 i = 0; i < intSteps; i++) {
        f32 t = std::clamp<f32>((f32)i / steps, 0.0f, 1.0f);
        this->curvePoints.push_back(this->pointAt(t));

        if(t >= 1.0f)  // early break if we've already reached the end
            break;
    }

    // add the segment (no special logic here for SliderCurveCircumscribedCircle, just add the entire vector)
    this->curvePointSegments.emplace_back(this->curvePoints);  // copy

    // backup (for dynamic updateStackPosition() recalculation)
    this->originalCurvePoints = this->curvePoints;                // copy
    this->originalCurvePointSegments = this->curvePointSegments;  // copy
}

void SliderCurveCircumscribedCircle::updateStackPosition(f32 stackMulStackOffset, bool HR) {
    SliderCurve::updateStackPosition(stackMulStackOffset, HR);

    this->vCircleCenter =
        this->vOriginalCircleCenter - vec2(stackMulStackOffset, stackMulStackOffset * (HR ? -1.0f : 1.0f));
}

vec2 SliderCurveCircumscribedCircle::pointAt(f32 t) const {
    const f32 sanityRange =
        SLIDER_CURVE_MAX_LENGTH;  // NOTE: added to fix some aspire problems (endless drawFollowPoints and star calc etc.)
    const f32 ang = std::lerp(this->fCalculationStartAngle, this->fCalculationEndAngle, t);

    return {std::clamp<f32>(std::cos(ang) * this->fRadius + this->vCircleCenter.x, -sanityRange, sanityRange),
            std::clamp<f32>(std::sin(ang) * this->fRadius + this->vCircleCenter.y, -sanityRange, sanityRange)};
}

vec2 SliderCurveCircumscribedCircle::originalPointAt(f32 t) const {
    const f32 sanityRange =
        SLIDER_CURVE_MAX_LENGTH;  // NOTE: added to fix some aspire problems (endless drawFollowPoints and star calc etc.)
    const f32 ang = std::lerp(this->fCalculationStartAngle, this->fCalculationEndAngle, t);

    return {std::clamp<f32>(std::cos(ang) * this->fRadius + this->vOriginalCircleCenter.x, -sanityRange, sanityRange),
            std::clamp<f32>(std::sin(ang) * this->fRadius + this->vOriginalCircleCenter.y, -sanityRange, sanityRange)};
}

vec2 SliderCurveCircumscribedCircle::intersect(vec2 a, vec2 ta, vec2 b, vec2 tb) {
    const f32 des = (tb.x * ta.y - tb.y * ta.x);
    if(std::abs(des) < 0.0001f) {
        debugLog("SliderCurveCircumscribedCircle::intersect() Error: Vectors are parallel!!!");
        return {0.f, 0.f};
    }

    const f32 u = ((b.y - a.y) * ta.x + (a.x - b.x) * ta.y) / des;
    return (b + vec2(tb.x * u, tb.y * u));
}

}  // namespace

//******************************//
//	 Curve Base Class Factory	//
//******************************//

std::unique_ptr<SliderCurve> SliderCurve::createCurve(SLIDERCURVETYPE type, std::vector<vec2> controlPoints,
                                                      f32 pixelLength) {
    const f32 points_separation = SLIDER_CURVE_POINTS_SEPARATION;
    return createCurve(type, std::move(controlPoints), pixelLength, points_separation);
}

std::unique_ptr<SliderCurve> SliderCurve::createCurve(SLIDERCURVETYPE type, std::vector<vec2> controlPoints,
                                                      f32 pixelLength, f32 curvePointsSeparation) {
    if(type == PASSTHROUGH && controlPoints.size() == 3) {
        vec2 nora = controlPoints[1] - controlPoints[0];
        vec2 norb = controlPoints[1] - controlPoints[2];

        f32 temp = nora.x;
        nora.x = -nora.y;
        nora.y = temp;
        temp = norb.x;
        norb.x = -norb.y;
        norb.y = temp;

        // TODO: to properly support all aspire sliders (e.g. Ping), need to use osu circular arc calc + subdivide line
        // segments if they are too big

        if(std::abs(norb.x * nora.y - norb.y * nora.x) < 0.00001f) {
            return std::make_unique<SliderCurveBezier>(std::move(controlPoints), pixelLength, curvePointsSeparation,
                                                       false);  // vectors parallel, use linear bezier instead
        } else {
            return std::make_unique<SliderCurveCircumscribedCircle>(std::move(controlPoints), pixelLength,
                                                                    curvePointsSeparation);
        }
    } else if(type == CATMULL) {
        return std::make_unique<SliderCurveCatmull>(std::move(controlPoints), pixelLength, curvePointsSeparation);
    } else {
        return std::make_unique<SliderCurveBezier>(std::move(controlPoints), pixelLength, curvePointsSeparation,
                                                   (type == LINEAR));
    }
}

SliderCurve::SliderCurve(std::vector<vec2> controlPoints, f32 pixelLength) {
    this->controlPoints = std::move(controlPoints);
    this->fPixelLength = std::abs(pixelLength);

    this->fStartAngle = 0.0f;
    this->fEndAngle = 0.0f;
}

void SliderCurve::updateStackPosition(f32 stackMulStackOffset, bool HR) {
    for(u64 i = 0; i < this->originalCurvePoints.size() && i < this->curvePoints.size(); i++) {
        this->curvePoints[i] =
            this->originalCurvePoints[i] - vec2(stackMulStackOffset, stackMulStackOffset * (HR ? -1.0f : 1.0f));
    }

    for(u64 s = 0; s < this->originalCurvePointSegments.size() && s < this->curvePointSegments.size(); s++) {
        for(u64 p = 0; p < this->originalCurvePointSegments[s].size() && p < this->curvePointSegments[s].size(); p++) {
            this->curvePointSegments[s][p] = this->originalCurvePointSegments[s][p] -
                                             vec2(stackMulStackOffset, stackMulStackOffset * (HR ? -1.0f : 1.0f));
        }
    }
}
