#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "noinclude.h"
#include "types.h"
#include "Vectors.h"

class AbstractBeatmapInterface;

namespace GameRules {

inline constexpr const int OSU_COORD_WIDTH{512};
inline constexpr const int OSU_COORD_HEIGHT{384};

//********************//
//  Positional Audio  //
//********************//

constexpr inline float osuCoords2Pan(float x) { return (x / (float)OSU_COORD_WIDTH - 0.5f) * 0.8f; }

//************************//
//	Hitobject Animations  //
//************************//

// this scales the fadeout duration with the current speed multiplier
float getFadeOutTime(float animationSpeedMultiplier = 1.f);
i32 getFadeInTime();

//********************//
//	Hitobject Timing  //
//********************//

inline constexpr const float MIN_HITWINDOW_300{80.f};
inline constexpr const float MID_HITWINDOW_300{50.f};
inline constexpr const float MAX_HITWINDOW_300{20.f};

inline constexpr const float MIN_HITWINDOW_100{140.f};
inline constexpr const float MID_HITWINDOW_100{100.f};
inline constexpr const float MAX_HITWINDOW_100{60.f};

inline constexpr const float MIN_HITWINDOW_50{200.f};
inline constexpr const float MID_HITWINDOW_50{150.f};
inline constexpr const float MAX_HITWINDOW_50{100.f};

inline constexpr const float HITWINDOW_MISS{400.f};

// respect mods and overrides
float getMinApproachTime();
float getMidApproachTime();
float getMaxApproachTime();

// AR 5 -> 1200 ms
template <typename T>
    requires(std::is_same_v<T, float> || std::is_same_v<T, double>)
constexpr inline T mapDifficultyRange(T scaledDiff, T min, T mid, T max) {
    constexpr const T MIDDLE{5};
    if(scaledDiff == MIDDLE)
        return mid;
    else if(scaledDiff > MIDDLE)
        return mid + (max - mid) * (scaledDiff - MIDDLE) / MIDDLE;
    else
        return mid - (mid - min) * (MIDDLE - scaledDiff) / MIDDLE;
}

float arToMilliseconds(float AR);

constexpr inline float odTo50HitWindowMS(float OD) {
    return mapDifficultyRange(OD, MIN_HITWINDOW_50, MID_HITWINDOW_50, MAX_HITWINDOW_50);
}
constexpr inline float odTo100HitWindowMS(float OD) {
    return mapDifficultyRange(OD, MIN_HITWINDOW_100, MID_HITWINDOW_100, MAX_HITWINDOW_100);
}
constexpr inline float odTo300HitWindowMS(float OD) {
    return mapDifficultyRange(OD, MIN_HITWINDOW_300, MID_HITWINDOW_300, MAX_HITWINDOW_300);
}

// 1200 ms -> AR 5
template <typename T>
    requires(std::is_same_v<T, float> || std::is_same_v<T, double>)
constexpr inline float mapDifficultyRangeInv(T val, T min, T mid, T max) {
    constexpr const T MIDDLE{5};
    if(val == mid)
        return MIDDLE;
    else if(val < mid)  // > 5.0f (inverted)
        return ((val * MIDDLE - mid * MIDDLE) / (max - mid)) + MIDDLE;
    else  // < 5.0f (inverted)
        return MIDDLE - ((mid * MIDDLE - val * MIDDLE) / (mid - min));
}

// AR 9, speed 1.5 -> AR 10.3
float arWithSpeed(float AR, float speed);

// OD 9, speed 1.5 -> OD 10.4
constexpr inline float odWithSpeed(float OD, float speed) {
    float hittableTime = odTo300HitWindowMS(OD);
    return mapDifficultyRangeInv(hittableTime / speed, MIN_HITWINDOW_300, MID_HITWINDOW_300, MAX_HITWINDOW_300);
}

constexpr inline float getApproachTimeForStacking(float AR) {
    return mapDifficultyRange(AR, getMinApproachTime(), getMidApproachTime(), getMaxApproachTime());
}

// raw spins required per second
float getSpinnerSpinsPerSecond(const AbstractBeatmapInterface *beatmap);

inline float getSpinnerRotationsForSpeedMultiplier(const AbstractBeatmapInterface *beatmap, i32 spinnerDuration,
                                                   float speedMultiplier) {
    /// return (int)((float)spinnerDuration / 1000.0f * getSpinnerSpinsPerSecond(beatmap)); // actual
    return (int)((((float)spinnerDuration / 1000.0f * getSpinnerSpinsPerSecond(beatmap)) * 0.5f) *
                 (std::min(1.0f / speedMultiplier, 1.0f)));  // Mc
}

// spinner length compensated rotations
// respect all mods and overrides
float getSpinnerRotationsForSpeedMultiplier(const AbstractBeatmapInterface *beatmap, i32 spinnerDuration);

//*************//
//	Playfield  //
//*************//

float getPlayfieldScaleFactor();

inline vec2 getPlayfieldSize() {
    const float scaleFactor = getPlayfieldScaleFactor();
    return {(float)OSU_COORD_WIDTH * scaleFactor, (float)OSU_COORD_HEIGHT * scaleFactor};
}

vec2 getPlayfieldOffset();

inline vec2 getPlayfieldCenter() {
    const float scaleFactor = getPlayfieldScaleFactor();
    const vec2 playfieldOffset = getPlayfieldOffset();

    return {(OSU_COORD_WIDTH / 2.f) * scaleFactor + playfieldOffset.x,
            (OSU_COORD_HEIGHT / 2.f) * scaleFactor + playfieldOffset.y};
}

//*********************//
//	Hitobject Scaling  //
//*********************//

// "Builds of osu! up to 2013-05-04 had the gamefield being rounded down, which caused incorrect radius calculations
// in widescreen cases. This ratio adjusts to allow for old replays to work post-fix, which in turn increases the
// lenience for all plays, but by an amount so small it should only be effective in replays."
inline constexpr const float broken_gamefield_rounding_allowance{1.00041f};

inline f32 getRawHitCircleScale(f32 CS) {
    return std::max(0.0f, ((1.0f - 0.7f * (CS - 5.0f) / 5.0f) / 2.0f) * broken_gamefield_rounding_allowance);
}

// gives the circle diameter in osu!pixels, goes negative above CS 12.1429
inline f32 getRawHitCircleDiameter(f32 CS) { return getRawHitCircleScale(CS) * 128.0f; }

// scales osu!pixels to the actual playfield size
inline f32 getHitCircleXMultiplier() { return getPlayfieldSize().x / OSU_COORD_WIDTH; }

}  // namespace GameRules
