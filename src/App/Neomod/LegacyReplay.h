#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "ModFlags.h"
#include "UString.h"

struct FinishedScore;

namespace LegacyReplay {
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

struct Info {
    u8 gamemode;
    u32 osu_version;
    UString map_md5;
    UString username;
    UString replay_md5;
    int num300s;
    int num100s;
    int num50s;
    int numGekis;
    int numKatus;
    int numMisses;
    i32 score;
    int comboMax;
    bool perfect;
    LegacyFlags mod_flags;
    UString life_bar_graph;
    i64 timestamp;
    std::vector<Frame> frames;
    i64 bancho_score_id = 0;
};

BEATMAP_VALUES getBeatmapValuesForModsLegacy(LegacyFlags modsLegacy, float legacyAR, float legacyCS, float legacyOD,
                                             float legacyHP);

Info from_bytes(u8* data, uSz s_data);
std::vector<Frame> get_frames(u8* replay_data, uSz replay_size);
std::vector<u8> compress_frames(const std::vector<Frame>& frames);
bool load_from_disk(FinishedScore& score, bool update_db);
void load_and_watch(FinishedScore score);

}  // namespace LegacyReplay

using GameplayKeys = LegacyReplay::KeyFlags;
