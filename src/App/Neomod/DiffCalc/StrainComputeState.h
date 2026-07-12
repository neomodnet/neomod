// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#if __has_include("config.h")
#include "config.h"
#endif

#include "types.h"

namespace neomod::DiffCalc {

// tracks which parameters the per-object computed fields (distances/angles/strains) of a
// DifficultyHitObject vector were last fully computed with, so repeat star calc calls over the
// same (unmodified) vector can skip the whole preprocessing+strain pass.
// these four inputs are exactly what the computed fields depend on beyond the vector contents;
// everything else (AR/stacking/clock rate, baked into object times/positions) is covered by
// "freshly loaded => invalid".
struct StrainComputeState {
    f32 CS{0.f};
    f32 OD{0.f};
    f32 speedMultiplier{0.f};
    bool autopilot{false};
    bool valid{false};

    [[nodiscard]] bool matches(f32 cs, f32 od, f32 speed, bool ap) const {
        return valid && CS == cs && OD == od && speedMultiplier == speed && autopilot == ap;
    }
};

}  // namespace neomod::DiffCalc
