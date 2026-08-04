#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.
#include "ModFlags.h"
#include "MD5Hash.h"

#include <span>

// TODO: we should rename this to something else than "LegacyReplay"
//       it's no longer just "legacy", as we export in this format too...

struct FinishedScore;

namespace LegacyReplay {

inline constexpr i64 TICKS_PER_SECOND = 10'000'000;
inline constexpr i64 UNIX_EPOCH_TICKS = 621'355'968'000'000'000;  // ticks from 0001-01-01 to 1970-01-01

struct Frame {
    i32 cur_music_pos;
    i32 milliseconds_since_last_frame;

    f32 x;  // 0 - 512
    f32 y;  // 0 - 384

    u8 key_flags;
};

enum KeyFlags : uint8_t {
    M1 = 1,
    M2 = 2,
    K1 = 4,
    K2 = 8,
    Smoke = 16,
};

struct BEATMAP_VALUES {
    float AR;
    float CS;
    float OD;
    float HP;

    float difficultyMultiplier;
    float csDifficultyMultiplier;
};

BEATMAP_VALUES getBeatmapValuesForModsLegacy(LegacyFlags modsLegacy, float legacyAR, float legacyCS, float legacyOD,
                                             float legacyHP);

std::vector<Frame> get_frames(const u8* replay_data, uSz replay_size);
std::vector<u8> compress_frames(const std::vector<Frame>& frames);
bool load_from_disk(FinishedScore& score, bool update_db);
bool load_osr(std::string_view osr_path, FinishedScore& score_out);
bool save_osr(const FinishedScore& score, std::span<const std::string> additional_data = {});
void load_and_watch(FinishedScore score);

}  // namespace LegacyReplay

using GameplayKeys = LegacyReplay::KeyFlags;
