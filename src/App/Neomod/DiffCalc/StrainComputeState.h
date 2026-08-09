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
// these inputs are exactly what the computed fields depend on beyond the vector contents:
// mods act per-note inside the skills now (RX/AP/TD), AR feeds the reading skill via preempt,
// and the flashlight flag gates the flashlight strain series. hidden is deliberately NOT part
// of the key: the hidden-dependent series (reading, flashlight) are computed for both hidden
// states in one pass. everything else (stacking, clock rate, baked into object
// times/positions) is covered by "freshly loaded => invalid".
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
};

}  // namespace neomod::DiffCalc
