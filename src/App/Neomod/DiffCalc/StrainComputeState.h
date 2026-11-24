// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#if __has_include("config.h")
#include "config.h"
#endif

#include "types.h"
#include "Vectors.h"

#include <vector>

namespace neomod::DiffCalc {

// the per-object values (distances/angles/strains) computed by the star calc for a
// DifficultyHitObject vector, one array entry per object, and which parameters they were last fully
// computed with, so repeat star calc calls over the same (unmodified) vector can skip the whole
// preprocessing+strain pass.
// these inputs are exactly what the computed values depend on beyond the vector contents:
// mods act per-note inside the skills now (RX/AP/TD), AR feeds the reading skill via preempt,
// and the flashlight flag gates the flashlight strain series. hidden is deliberately NOT part
// of the key: the hidden-dependent series (reading, flashlight) are computed for both hidden
// states in one pass. everything else (stacking, clock rate, baked into object
// times/positions) is covered by "freshly loaded => invalid".
// NOTE: unlike lazer, the first hitobject is included (the lazer difficulty hit objects start at
// the second one), so the lazer Index is the array position minus 1.
struct StrainComputeState {
    f32 CS{0.f};
    f32 OD{0.f};
    f32 AR{0.f};
    f32 speedMultiplier{0.f};
    bool relax{false};
    bool autopilot{false};
    bool touchDevice{false};
    bool flashlight{false};
    bool valid{false};

    [[nodiscard]] bool matches(f32 cs, f32 od, f32 ar, f32 speed, bool rx, bool ap, bool td, bool fl) const {
        return valid && CS == cs && OD == od && AR == ar && speedMultiplier == speed && relax == rx &&
               autopilot == ap && touchDevice == td && flashlight == fl;
    }

    // time since the previous object start (raw, and min 25ms == lazer AdjustedDeltaTime)
    std::vector<f64> deltaTime;
    std::vector<f64> strainTime;

    // time between the previous object end and this object start, min 25ms
    // (lazer LastObjectEndDeltaTime; falls back to the start->start delta for the first pair)
    std::vector<f64> lastObjectEndDeltaTime;

    std::vector<f64> jumpDistance;  // start->start distance (lazer JumpDistance)
    std::vector<f64> lazyJumpDistance;
    std::vector<f64> minimumJumpTime;
    std::vector<f64> travelDistance;
    std::vector<f64> travelTime;

    // angle at the previous object, NaN when lazer would have null
    std::vector<f64> angle;
    // angle of the current-1 -> current vector, normalized so symmetrical vectors in any axis
    // compare equal (lazer NormalisedVectorAngle); NaN when lazer would have null
    std::vector<f64> normalisedVectorAngle;

    // slider cursor path (lazer LazyEndPosition/LazyTravelDistance/LazyTravelTime)
    std::vector<vec2> lazyEndPos;
    std::vector<f64> lazyTravelDist;
    std::vector<f64> lazyTravelTime;
    std::vector<b8> lazyCalcFinished;

    // recorded per-object difficulties (lazer Skill.ObjectDifficulties). for speed the rhythm
    // multiplier is part of the recorded value. the reading/flashlight series track both hidden
    // variants in one pass so the hidden flag stays out of the key. flashlight is only filled when
    // the flashlight flag is set, zero otherwise.
    std::vector<f64> aimStrain;
    std::vector<f64> aimNoSlidersStrain;
    std::vector<f64> speedDifficulty;
    std::vector<f64> readingStrainNoHidden;
    std::vector<f64> readingStrainHidden;
    std::vector<f64> flashlightStrainNoHidden;
    std::vector<f64> flashlightStrainHidden;

    // the harmonic sum index decay, which only depends on the index (kept across computations)
    std::vector<f64> harmonicIndexDecay;
};

}  // namespace neomod::DiffCalc
