#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.

#include "DatabaseBeatmap.h"

struct Packet;

namespace BANCHO::Leaderboard {
struct OnlineMapInfo {
    i32 ranked_status;
    u32 beatmap_id;
    u32 beatmap_set_id;
    i32 online_offset;
    i32 nb_scores;
    bool server_has_osz2;
};

void process_leaderboard_response(const Packet &response);

void fetch_online_scores(const DatabaseBeatmap *beatmap);
}  // namespace BANCHO::Leaderboard
