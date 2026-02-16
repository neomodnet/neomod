// Copyright (c) 2023, kiwec, All rights reserved.
#include "BanchoLeaderboard.h"

#include "NetworkHandler.h"
#include "Osu.h"
#include "Bancho.h"
#include "BanchoApi.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "OsuConVars.h"
#include "Environment.h"
#include "Database.h"
#include "Engine.h"
#include "ModSelector.h"
#include "Parsing.h"
#include "SongBrowser/SongBrowser.h"
#include "UI.h"
#include "crypto.h"
#include "Logging.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace {  // static namespace
FinishedScore parse_score(const char *score_line) {
    FinishedScore score;
    score.client = "peppy-unknown";
    score.server = BanchoState::endpoint;
    score.is_online_score = true;

    const std::vector<std::string_view> tokens = SString::split(score_line, '|');
    if(tokens.size() < 16) return score;

    score.bancho_score_id = Parsing::strto<i64>(tokens[0]);
    score.playerName = tokens[1];
    score.score = Parsing::strto<u64>(tokens[2]);
    score.comboMax = Parsing::strto<i32>(tokens[3]);
    score.num50s = Parsing::strto<i32>(tokens[4]);
    score.num100s = Parsing::strto<i32>(tokens[5]);
    score.num300s = Parsing::strto<i32>(tokens[6]);
    score.numMisses = Parsing::strto<i32>(tokens[7]);
    score.numKatus = Parsing::strto<i32>(tokens[8]);
    score.numGekis = Parsing::strto<i32>(tokens[9]);
    score.perfect = Parsing::strto<bool>(tokens[10]);
    score.mods = Replay::Mods::from_legacy(static_cast<LegacyFlags>(Parsing::strto<u32>(tokens[11])));
    score.player_id = Parsing::strto<i32>(tokens[12]);
    score.unixTimestamp = Parsing::strto<u64>(tokens[14]);
    score.is_online_replay_available = Parsing::strto<bool>(tokens[15]);

    if(tokens.size() > 16) {
        std::vector<u8> mod_bytes = crypto::conv::decode64(tokens[16]);
        Packet mod_packet{
            .memory = mod_bytes.data(),
            .size = mod_bytes.size(),
        };
        score.mods = Replay::Mods::unpack(mod_packet);
    }

    // @PPV3: score can only be ppv2, AND we need to recompute ppv2 on it
    // might also be missing some important fields here, double check

    // Set username for given user id, since we now know both
    UserInfo *user = BANCHO::User::get_user_info(score.player_id);
    user->name = score.playerName;

    // Mark as a player. Setting this also makes the has_user_info check pass,
    // which unlocks context menu actions such as sending private messages.
    user->privileges |= 1;

    return score;
}

}  // namespace

namespace BANCHO::Leaderboard {
void fetch_online_scores(const DatabaseBeatmap *beatmap) {
    std::string url = "/web/osu-osz2-getscores.php?m=0&s=0&vv=4&a=0";

    // TODO: b.py calls this "map_package_hash", could be useful for storyboard-specific LBs
    //       (assuming it's some hash that includes all relevant map files)
    url.append("&h=");

    char lb_type = '1';  // Global / default
    const char filter_first_letter{cv::songbrowser_scores_filteringtype.getString()[0]};
    switch(filter_first_letter) {
        case 'S':  // Selected mods
            lb_type = '2';
            break;
        case 'F':  // Friends
            lb_type = '3';
            break;
        case 'C':  // Country
            lb_type = '4';
            break;
        case 'T':  // Team
            lb_type = '5';
            break;
        default:  // Global / default
            break;
    }

    // leaderboard type filter
    url.append("&v=");
    url.push_back(lb_type);

    // Map info
    std::string map_filename = env->getFileNameFromFilePath(beatmap->getFilePath());
    url.append(fmt::format("&f={}", Mc::Net::urlEncode(map_filename)));
    url.append(fmt::format("&c={}", beatmap->getMD5()));
    url.append(fmt::format("&i={}", beatmap->getSetID()));

    // Some servers use mod flags, even without any leaderboard filter active (e.g. for relax)
    url.append(fmt::format("&mods={}", static_cast<u32>(ui->getModSelector()->getModFlags())));

    // Auth (uses different params than default)
    BANCHO::Api::append_auth_params(url, "us", "ha");

    BANCHO::Api::Request request;
    request.type = BANCHO::Api::GET_MAP_LEADERBOARD;
    request.path = url;
    request.extra = (u8 *)strdup(std::string{beatmap->getMD5().to_chars().string()}.c_str());  // lol...

    BANCHO::Api::send_request(request);
}

void process_leaderboard_response(const Packet &response) {
    // Don't update the leaderboard while playing, that's weird
    if(osu->isInPlayMode()) return;

    // NOTE: We're not doing anything with the "info" struct.
    //       Server can return partial responses in some cases, so make sure
    //       you actually received the data if you plan on using it.
    OnlineMapInfo info{};
    MD5Hash beatmap_hash{(char *)response.extra};
    std::vector<FinishedScore> scores;
    char *body = (char *)response.memory;

    char *ranked_status = Parsing::strtok_x('|', &body);
    info.ranked_status = Parsing::strto<i32>(ranked_status);

    char *server_has_osz2 = Parsing::strtok_x('|', &body);
    info.server_has_osz2 = !strcmp(server_has_osz2, "true");

    char *beatmap_id = Parsing::strtok_x('|', &body);
    info.beatmap_id = Parsing::strto<u32>(beatmap_id);

    char *beatmap_set_id = Parsing::strtok_x('|', &body);
    info.beatmap_set_id = Parsing::strto<u32>(beatmap_set_id);

    char *nb_scores = Parsing::strtok_x('|', &body);
    info.nb_scores = Parsing::strto<i32>(nb_scores);

    char *fa_track_id = Parsing::strtok_x('|', &body);
    (void)fa_track_id;

    char *fa_license_text = Parsing::strtok_x('\n', &body);
    (void)fa_license_text;

    char *online_offset = Parsing::strtok_x('\n', &body);
    info.online_offset = Parsing::strto<i32>(online_offset);

    char *map_name = Parsing::strtok_x('\n', &body);
    (void)map_name;

    char *user_ratings = Parsing::strtok_x('\n', &body);
    (void)user_ratings;  // no longer used

    char *pb_score = Parsing::strtok_x('\n', &body);
    (void)pb_score;

    // XXX: We should also separately display either the "personal best" the server sent us,
    //      or the local best, depending on which score is better.
    debugLog("Received online leaderboard for Beatmap ID {:d}", info.beatmap_id);
    auto map = db->getBeatmapDifficulty(beatmap_hash);
    if(map) {
        const i16 previous_offset = map->getOnlineOffset();
        map->setOnlineOffset(info.online_offset);
        if(previous_offset != info.online_offset) {
            db->update_overrides(map);
        }
    }

    char *score_line = nullptr;
    while((score_line = Parsing::strtok_x('\n', &body))[0] != '\0') {
        FinishedScore score = parse_score(score_line);
        score.beatmap_hash = beatmap_hash;
        score.map = map;
        scores.push_back(std::move(score));
    }

    db->getOnlineScores()[beatmap_hash] = std::move(scores);
    ui->getSongBrowser()->onGotNewLeaderboard(beatmap_hash);
}
}  // namespace BANCHO::Leaderboard
