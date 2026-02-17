#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.
#include <string>
#include <vector>
#include "types.h"

namespace SampleSetType {
enum {
    NORMAL = 1,
    SOFT = 2,
    DRUM = 3,
};
}

namespace HitSoundType {
enum {
    NORMAL = (1 << 0),
    WHISTLE = (1 << 1),
    FINISH = (1 << 2),
    CLAP = (1 << 3),

    VALID_HITSOUNDS = NORMAL | WHISTLE | FINISH | CLAP,
    VALID_SLIDER_HITSOUNDS = NORMAL | WHISTLE,
};
}

struct HitSamples final {
    u8 hitSounds = 0;           // bitfield of HitSoundTypes to play
    u8 normalSet = 0;           // SampleSetType of the normal sound
    u8 additionSet = 0;         // SampleSetType of the whistle, finish and clap sounds
    u8 volume = 0;              // volume of the sample, 1-100. if 0, use timing point volume instead
    i32 index = 0;              // index of the sample (for custom map sounds). if 0, use skin sound instead
    std::string filename = "";  // when not empty, ignore all the above mess (except volume) and just play that file

    struct Set_Slider_Hit {
        i32 set;
        i32 slider;
        i32 hit;
    };

    std::vector<Set_Slider_Hit> play(f32 pan, i32 delta, i32 play_time = -1, bool is_sliderslide = false);
    void stop(const std::vector<Set_Slider_Hit> &specific_sets = {});

    i32 getAdditionSet(i32 play_time = -1);
    i32 getNormalSet(i32 play_time = -1);
    f32 getVolume(i32 hitSoundType, bool is_sliderslide, i32 play_time = -1);
};
