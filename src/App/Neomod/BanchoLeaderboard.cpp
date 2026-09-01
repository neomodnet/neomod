// Copyright (c) 2023, kiwec, All rights reserved.
#include "BanchoLeaderboard.h"

#include "NetworkHandler.h"
#include "SyncStoptoken.h"
#include "Osu.h"
#include "Bancho.h"
#include "BanchoApi.h"
#include "BanchoNetworking.h"
#include "BanchoUsers.h"
#include "OsuConVars.h"
#include "Environment.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "score.h"
#include "Engine.h"
#include "ModSelector.h"
#include "Parsing.h"
#include "SString.h"
#include "SongBrowser/SongBrowser.h"
#include "UI.h"
#include "crypto.h"
#include "Logging.h"
#include "i18n.h"

#include <string_view>
#include <vector>

namespace {  // static namespace
// armed per fetch so selecting a different map cancels the stale leaderboard request
Sync::stop_source fetch_cancel;

FinishedScore parse_score(std::string_view score_line) {
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
    score.unix_timestamp = Parsing::strto<i64>(tokens[14]);
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
    user->privileges |= (u8)Privileges::PLAYER;

    return score;
}

// NOTE: also updates local beatmap ID and beatmapset ID if they were missing in our local beatmap
void process_leaderboard_response(const MD5Hash &beatmap_hash, std::string_view body) {
    // Don't update the leaderboard while playing, that's weird
    if(osu->isInPlayMode()) return;

    // NOTE: We're not doing anything with the "info" struct.
    //       Server can return partial responses in some cases, so make sure
    //       you actually received the data if you plan on using it.
    BANCHO::Leaderboard::OnlineMapInfo info{};
    std::vector<FinishedScore> scores;

    // line 0: ranked_status|server_has_osz2|beatmap_id|beatmap_set_id|nb_scores|fa_track_id|fa_license_text
    // line 1: online_offset
    // line 2: map_name
    // line 3: user_ratings (no longer used)
    // line 4: pb_score
    // lines 5+: leaderboard scores
    const std::vector<std::string_view> lines = SString::split(body, '\n');
    const auto line_at = [&lines](size_t i) { return i < lines.size() ? lines[i] : std::string_view{}; };

    const std::vector<std::string_view> info_tokens = SString::split(line_at(0), '|');
    const auto info_at = [&info_tokens](size_t i) {
        return i < info_tokens.size() ? info_tokens[i] : std::string_view{};
    };

    info.ranked_status = Parsing::strto<i32>(info_at(0));
    info.server_has_osz2 = info_at(1) == "true";
    info.beatmap_id = Parsing::strto<u32>(info_at(2));
    info.beatmap_set_id = Parsing::strto<u32>(info_at(3));
    info.nb_scores = Parsing::strto<i32>(info_at(4));
    info.online_offset = Parsing::strto<i32>(line_at(1));

    // XXX: We should also separately display either the "personal best" the server sent us,
    //      or the local best, depending on which score is better.
    debugLog("Received online leaderboard for Beatmap ID {:d}", info.beatmap_id);
    auto map = db->getBeatmapDifficulty(beatmap_hash);
    if(map) {
        const i16 previous_offset = (i16)map->getOnlineOffset();
        map->setOnlineOffset((i16)info.online_offset);
        if(previous_offset != info.online_offset) {
            db->update_overrides(map);
        }

        // for now, only override local state if we didn't already have something valid
        if(info.beatmap_id > 0 && map->getID() <= 0) {
            map->setMapID((i32)info.beatmap_id);
        }
        if(info.beatmap_set_id > 0 && map->getSetID() <= 0) {
            db->updateSetID(map, (i32)info.beatmap_set_id);
        }
    }

    for(size_t i = 5; i < lines.size(); i++) {
        if(lines[i].empty()) continue;
        FinishedScore score = parse_score(lines[i]);
        score.beatmap_hash = beatmap_hash;
        score.map = map;
        scores.push_back(std::move(score));
    }

    db->getOnlineScores()[beatmap_hash] = std::move(scores);
    ui->getSongBrowser()->onGotNewLeaderboard(beatmap_hash);
}
}  // namespace

namespace BANCHO::Leaderboard {
void fetch_online_scores(const DatabaseBeatmap *beatmap) {
    std::string url = "osu." + BanchoState::endpoint;
    url.append("/web/osu-osz2-getscores.php?m=0&s=0&vv=4&a=0");

    // TODO: b.py calls this "map_package_hash", could be useful for storyboard-specific LBs
    //       (assuming it's some hash that includes all relevant map files)
    url.append("&h=");

    // TODO: avoid needing to pull in translations here (use numeric id)
    const std::string &user_type = cv::songbrowser_scores_filteringtype.getString();
    char lb_type = '1';  // Global / default
    if(user_type == _("Global")) {
        // (already set)
    } else if(user_type == _("Selected mods")) {
        lb_type = '2';
    } else if(user_type == _("Friends")) {
        lb_type = '3';
    } else if(user_type == _("Country")) {
        lb_type = '4';
    } else if(user_type == _("Team")) {
        lb_type = '5';
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

    const auto map_md5 = beatmap->getMD5();
    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = 5,
        .connect_timeout = 5,
    };

    // cancel any previous in-flight fetch; selecting a new map supersedes the old leaderboard
    fetch_cancel.request_stop();
    fetch_cancel = {};
    options.cancel_token = fetch_cancel.get_token();

    networkHandler->httpRequestAsync(url, std::move(options), [map_md5](const Mc::Net::Response &response) {
        if(response.success) {
            process_leaderboard_response(map_md5, response.text());
        } else {
            debugLog("Leaderboard request failed: {}", response.error_msg);
            db->getOnlineScores()[map_md5] = std::vector<FinishedScore>();
            ui->getSongBrowser()->onGotNewLeaderboard(map_md5);
        }
    });
}
}  // namespace BANCHO::Leaderboard
