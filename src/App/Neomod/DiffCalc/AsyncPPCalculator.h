#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.
#include "types.h"
#include "Replay.h"

#include <vector>

class DatabaseBeatmap;
enum class ModFlags : u64;

namespace AsyncPPC {
// computed for request
struct pp_res {
    f64 total_stars{0.0};
    f64 aim_stars{0.0};
    f64 aim_slider_factor{0.0};
    f64 speed_stars{0.0};
    f64 speed_notes{0.0};
    f64 difficult_aim_sliders{0.0};
    f64 difficult_aim_strains{0.0};
    f64 difficult_speed_strains{0.0};
    f64 pp{-1.0};

    std::vector<f64> aimStrains{};
    std::vector<f64> speedStrains{};

    bool operator==(const pp_res&) const = default;
};

struct pp_calc_request {
    ModFlags modFlags{};
    f32 speedOverride{1.f};
    f32 AR{};
    f32 HP{};
    f32 CS{};
    f32 OD{};
    i32 comboMax{-1};
    i32 numMisses{};
    i32 num300s{-1};
    i32 num100s{};
    i32 num50s{};
    u32 legacyTotalScore{};
    bool scoreFromMcOsu{false};

    bool operator==(const pp_calc_request&) const = default;
};

// Set currently selected map. Clears pp cache. Pass NULL to init/reset.
void set_map(const DatabaseBeatmap* map);

// Get pp for given parameters. Returns -1 pp values if not computed yet.
// Second parameter == true forces calculation even during gameplay
pp_res query_result(const pp_calc_request& rqt, bool ignoreBGThreadPause = false);
}  // namespace AsyncPPC
