// Copyright (c) 2026, WH, All rights reserved.
#pragma once
// pre-calculated star ratings for common mod combinations

#include "types.h"

#include <array>

enum class ModFlags : u64;

namespace StarPrecalc {

enum SPEEDS_ENUM : u8 { SPEEDS_MIN, _0_75 = SPEEDS_MIN, _0_8, _0_9, _1_0, _1_1, _1_2, _1_3, _1_4, _1_5, SPEEDS_NUM };
inline constexpr std::array SPEEDS{0.75f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f};
inline constexpr std::array SPEED_NAMES{"0.75", "0.8", "0.9", "1.0", "1.1", "1.2", "1.3", "1.4", "1.5"};

static_assert(SPEEDS_NUM == SPEEDS.size());
static_assert(SPEED_NAMES.size() == SPEEDS.size());

// mod combo indices:
//  0 = None
//  1 = HR
//  2 = HD
//  3 = EZ
//  4 = HD|HR
//  5 = HD|EZ
inline constexpr uSz NUM_MOD_COMBOS = 6;
inline constexpr std::array MOD_NAMES{"NM", "HR", "HD", "EZ", "HDHR", "HDEZ"};
static_assert(MOD_NAMES.size() == NUM_MOD_COMBOS);

inline constexpr uSz NUM_PRECALC_RATINGS = SPEEDS_NUM * NUM_MOD_COMBOS;  // 54
inline constexpr uSz NOMOD_1X_INDEX = _1_0 * NUM_MOD_COMBOS;             // speed_idx=3 (1.0) * 6 + combo=0 (None) = 18

using SRArray = std::array<f32, NUM_PRECALC_RATINGS>;

enum MOD_COMBO_INDEX : u8 { INVALID_MODCOMBO = 0xFF };
// if flags are an invalid combination this returns INVALID_MODCOMBO
MOD_COMBO_INDEX mod_combo_index(ModFlags flags);

// never fails, return closest speed index
uSz speed_index(f32 speed);

// if flags are an invalid combination this returns INVALID_MODCOMBO, otherwise it returns an index into an SRArray
inline MOD_COMBO_INDEX index_of(ModFlags flags, f32 speed) {
    const uSz si = speed_index(speed);
    const uSz mi = mod_combo_index(flags);
    if(mi == INVALID_MODCOMBO) return INVALID_MODCOMBO;

    return static_cast<MOD_COMBO_INDEX>(si * NUM_MOD_COMBOS + mi);
}

const char *dbgstr_idx(u8 idx);

// currently active mod combination index, updated by Osu::updateMods()
inline u8 active_idx = NOMOD_1X_INDEX;

}  // namespace StarPrecalc
