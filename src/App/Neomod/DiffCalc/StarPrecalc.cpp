// Copyright (c) 2026, WH, All rights reserved.
#include "StarPrecalc.h"

#include "ModFlags.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace StarPrecalc {

MOD_COMBO_INDEX mod_combo_index(ModFlags flags) {
    using namespace flags::operators;
    using enum ModFlags;

    // extract the 3 relevant bits into a key
    const u8 hr = flags::has<HardRock>(flags) << 0;
    const u8 hd = flags::has<Hidden>(flags) << 1;
    const u8 ez = flags::has<Easy>(flags) << 2;
    const u8 key = hr | hd | ez;

    static constexpr std::array LUT{0,      // 0: None
                                    1,      // 1: HR
                                    2,      // 2: HD
                                    4,      // 3: HR|HD
                                    3,      // 4: EZ
                                    0xFF,   // 5: EZ|HR (disallowed)
                                    5,      // 6: EZ|HD
                                    0xFF};  // 7: all (disallowed)

    return static_cast<MOD_COMBO_INDEX>(LUT[key]);
}

// never fail, return closest
uSz speed_index(f32 speed) {
    auto it = std::ranges::lower_bound(SPEEDS, speed);
    if(it == SPEEDS.begin()) return 0;
    if(it == SPEEDS.end()) return SPEEDS.size() - 1;

    auto prev = std::prev(it);
    return (speed - *prev <= *it - speed) ? std::distance(SPEEDS.begin(), prev) : std::distance(SPEEDS.begin(), it);
}

const char *dbgstr_idx(u8 idx) {
    static thread_local std::array<char, 16> buf{};
    static thread_local u8 last_idx{0xFF};

    if(last_idx == idx) {
        return buf.data();
    }

    last_idx = idx;

    if(idx >= NUM_PRECALC_RATINGS) {
        std::snprintf(buf.data(), buf.size(), "invalid");
        return buf.data();
    }

    std::snprintf(buf.data(), buf.size(), "%s@%sx", MOD_NAMES[idx % NUM_MOD_COMBOS], SPEED_NAMES[idx / NUM_MOD_COMBOS]);
    return buf.data();
}

}  // namespace StarPrecalc
