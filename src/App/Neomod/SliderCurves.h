#pragma once
// Copyright (c) 2015, PG & Jeffrey Han (opsu!), All rights reserved.
#if __has_include("config.h")
#include "config.h"
#endif

#include "Vectors.h"

#include <vector>
#include <memory>

using SLIDERCURVETYPE = char;

class SliderCurveBuilder;

//**********************//
//	 Curve Base Class	//
//**********************//

class SliderCurve {
   public:
    static std::unique_ptr<SliderCurve> createCurve(SLIDERCURVETYPE type, std::vector<vec2> controlPoints,
                                                    float pixelLength);
    static std::unique_ptr<SliderCurve> createCurve(SLIDERCURVETYPE type, std::vector<vec2> controlPoints,
                                                    float pixelLength, float curvePointsSeparation);

   public:
    SliderCurve() = delete;

    SliderCurve(const SliderCurve &) = default;
    SliderCurve &operator=(const SliderCurve &) = default;
    SliderCurve(SliderCurve &&) = default;
    SliderCurve &operator=(SliderCurve &&) = default;
    virtual ~SliderCurve() = default;

    virtual void updateStackPosition(float stackMulStackOffset, bool HR);

    [[nodiscard]] virtual vec2 pointAt(float t) const = 0;          // with stacking
    [[nodiscard]] virtual vec2 originalPointAt(float t) const = 0;  // without stacking

    [[nodiscard]] inline float getStartAngle() const { return this->fStartAngle; }
    [[nodiscard]] inline float getEndAngle() const { return this->fEndAngle; }

    [[nodiscard]] inline const std::vector<vec2> &getPoints() const { return this->curvePoints; }
    [[nodiscard]] inline const std::vector<std::vector<vec2>> &getPointSegments() const {
        return this->curvePointSegments;
    }

    [[nodiscard]] inline float getPixelLength() const { return this->fPixelLength; }

   protected:
    friend class SliderCurveBuilder;

    SliderCurve(std::vector<vec2> controlPoints, float pixelLength);

    // original input values
    std::vector<vec2> controlPoints;

    // these must be explicitly calculated/set in one of the subclasses
    std::vector<std::vector<vec2>> curvePointSegments;
    std::vector<std::vector<vec2>> originalCurvePointSegments;
    std::vector<vec2> curvePoints;
    std::vector<vec2> originalCurvePoints;
    float fStartAngle;
    float fEndAngle;
    float fPixelLength;
};
