#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include <string>
#include "types.h"

struct MapOverrides {
    std::string background_image_filename;
    u32 ppv2_version = 0;
    f32 star_rating = 0.0;
    f32 loudness = 0.f;
    i32 min_bpm = 0;
    i32 max_bpm = 0;
    i32 avg_bpm = 0;
    i16 local_offset = 0;
    i16 online_offset = 0;
    u16 nb_circles = 0;
    u16 nb_sliders = 0;
    u16 nb_spinners = 0;
    u8 draw_background = 1;
};
