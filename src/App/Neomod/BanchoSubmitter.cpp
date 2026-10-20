// Copyright (c) 2024, kiwec, All rights reserved.
#include "BanchoSubmitter.h"

#include "ContainerRanges.h"
#include "Database.h"
#include "Bancho.h"
#include "BanchoAes.h"
#include "BanchoNetworking.h"
#include "OsuConVars.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "NetworkHandler.h"
#include "NotificationOverlay.h"
#include "Parsing.h"
#include "RankingScreen.h"
#include "SongBrowser/SongBrowser.h"
#include "SString.h"
#include "SyncStoptoken.h"
#include "crypto.h"
#include "Logging.h"
#include "Timing.h"
#include "UI.h"
#include "i18n.h"
#include "score.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {  // static namespace

struct PendingSubmission {
    FinishedScore score;  // without replay frames, those are only sent compressed
    std::vector<u8> compressed_replay;
    u64 id;
    f64 next_attempt_time{0.0};
    u32 failed_attempts{0};
    bool in_flight{false};
};

// scores the server has neither accepted nor rejected yet
std::vector<PendingSubmission> pending;
u64 next_submission_id{0};

// aborts the requests in flight when the queue is dropped
Sync::stop_source submit_cancel;

// the answer is "key:value" entries separated by '|'. the entries after "chartId:<name>" belong to that chart,
// and its stats come as "<stat>Before" and "<stat>After" pairs.
BANCHO::Net::SubmissionResult parse_charts(std::string_view body) {
    using Result = BANCHO::Net::SubmissionResult;
    static constexpr std::array<std::pair<std::string_view, Result::Stat Result::Chart::*>, 6> STATS{{
        {"rank", &Result::Chart::rank},
        {"rankedScore", &Result::Chart::ranked_score},
        {"totalScore", &Result::Chart::total_score},
        {"maxCombo", &Result::Chart::max_combo},
        {"accuracy", &Result::Chart::accuracy},
        {"pp", &Result::Chart::pp},
    }};

    Result result;
    Result::Chart *chart = nullptr;
    for(std::string_view entry : SString::split(body, '|')) {
        entry = SString::trimmed(entry);
        const size_t colon = entry.find(':');
        if(colon == std::string_view::npos) continue;
        const std::string_view key = entry.substr(0, colon);
        const std::string_view value = entry.substr(colon + 1);

        if(key == "chartId") {
            chart = value == "beatmap" ? &result.beatmap : value == "overall" ? &result.overall : nullptr;
            result.has_charts |= chart != nullptr;
        } else if(key == "onlineScoreId") {
            result.online_score_id = Parsing::strto<i64>(value);
        } else if(chart) {
            for(const auto &[name, stat] : STATS) {
                if(!key.starts_with(name)) continue;
                if(const std::string_view suffix = key.substr(name.size()); suffix == "Before") {
                    (chart->*stat).before = Parsing::strto<f64>(value);
                } else if(suffix == "After") {
                    (chart->*stat).after = Parsing::strto<f64>(value);
                }
            }
        }
    }
    return result;
}

void on_submission_response(u64 id, const Mc::Net::Response &response) {
    const auto it = Mc::ranges::find(pending, id, &PendingSubmission::id);
    if(it == pending.end()) return;
    it->in_flight = false;

    const long code = response.response_code;
    const bool answered = response.success && code >= 200 && code < 300;
    const std::string_view body = answered ? SString::trimmed(response.text()) : std::string_view{};

    // an empty answer means the server doesn't see the player online yet.
    // other client errors than 408 and 429 mean the server will never take this request.
    if(answered ? body.empty() : (code < 300 || code >= 500 || code == 408 || code == 429)) {
        const std::string_view reason =
            answered ? std::string_view{_("not online on the server")} : std::string_view{response.error_msg};
        if(it->failed_attempts == 0) {
            ui->getNotificationOverlay()->addToast(tformat("Score submission failed ({}), retrying...", reason),
                                                   ERROR_TOAST);
        }

        // 5s, 10s, 20s, 40s, then every minute
        const f64 delay = std::min(5.0 * (1 << std::min(it->failed_attempts, 4u)), 60.0);
        it->failed_attempts++;
        it->next_attempt_time = engine->getTime() + delay;
        debugLog("Score submission failed ({}), retrying in {}s", reason, delay);
        return;
    }

    BANCHO::Net::SubmissionResult result;
    if(!answered) {
        result.rejection_reason = response.error_msg;
    } else if(body.starts_with("error:")) {
        // "no" is a duplicate: an earlier attempt already got through
        if(const std::string_view reason = SString::trimmed(body.substr(6)); reason != "no") {
            result.rejection_reason = reason;
        }
    } else {
        debugLog("Score submit result: {}", body);
        result = parse_charts(body);
    }

    const FinishedScore score = std::move(it->score);
    pending.erase(it);

    if(!result.rejection_reason.empty()) {
        debugLog("Score submission rejected: {}", result.rejection_reason);
        ui->getNotificationOverlay()->addToast(tformat("Score submission rejected: {}", result.rejection_reason),
                                               ERROR_TOAST);
    } else if(result.online_score_id != 0) {
        db->setBanchoScoreId(score, result.online_score_id);
    }

    // the leaderboard and the score buttons show which scores are still pending
    db->getOnlineScores().erase(score.beatmap_hash);
    ui->getSongBrowser()->rebuildScoreButtons();
    ui->getRankingScreen()->onScoreSubmitted(score, result);
}

// builds the request from the current session, so a retry after a reconnect uses the new credentials
void send_submission(PendingSubmission &sub) {
    const FinishedScore &score = sub.score;
    debugLog("Submitting score... (attempt {})", sub.failed_attempts + 1);
    constexpr auto GRADES = std::array{"XH", "SH", "X", "S", "A", "B", "C", "D", "F", "N"};

    std::array<u8, 32> iv;
    crypto::rng::get_rand(iv);

    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = cv::net_transfer_timeout.getVal<long>(),
        .connect_timeout = 5,
        .cancel_token = submit_cancel.get_token(),
    };

    options.headers["token"] = BanchoState::cho_token;

    auto quit = fmt::format("{}", score.ragequit ? 1 : 0);
    options.mime_parts.push_back({
        .name = "x",
        .data = {quit.begin(), quit.end()},
    });

    auto fail_time = fmt::format("{}", score.passed ? 0 : score.play_time_ms);
    options.mime_parts.push_back({
        .name = "ft",
        .data = {fail_time.begin(), fail_time.end()},
    });

    auto score_time = fmt::format("{}", score.passed ? score.play_time_ms : 0);
    options.mime_parts.push_back({
        .name = "st",
        .data = {score_time.begin(), score_time.end()},
    });

    std::string visual_settings_b64 = "0";  // TODO: not used by bancho.py
    options.mime_parts.push_back({
        .name = "fs",
        .data = {visual_settings_b64.begin(), visual_settings_b64.end()},
    });

    MD5String beatmap_hash_str = score.beatmap_hash.to_chars();
    options.mime_parts.push_back({
        .name = "bmk",
        .data = {beatmap_hash_str.begin(), beatmap_hash_str.end()},
    });

    MD5String storyboard_hash_str = score.storyboard_hash.to_chars();
    options.mime_parts.push_back({
        .name = "sbk",
        .data = {storyboard_hash_str.begin(), storyboard_hash_str.end()},
    });

    auto unique_ids = fmt::format("{}|{}", BanchoState::get_install_id(), BanchoState::get_disk_uuid());
    options.mime_parts.push_back({
        .name = "c1",
        .data = {unique_ids.begin(), unique_ids.end()},
    });

    std::string_view password = BanchoState::is_oauth ? BanchoState::cho_token : BanchoState::pw_md5.string();
    options.mime_parts.push_back({
        .name = "pass",
        .data = {password.begin(), password.end()},
    });

    constexpr std::string_view osu_version = MC_STRINGIZE(OSU_VERSION_DATEONLY);
    options.mime_parts.push_back({
        .name = "osuver",
        .data = {osu_version.begin(), osu_version.end()},
    });

    auto iv_b64 = crypto::conv::encode64(iv);
    options.mime_parts.push_back({
        .name = "iv",
        .data = {iv_b64.begin(), iv_b64.end()},
    });

    {
        auto client_hashes_b64 = crypto::conv::encode64(BANCHO::AES::encrypt(iv, BanchoState::client_hashes));
        options.mime_parts.push_back({
            .name = "s",
            .data = {client_hashes_b64.begin(), client_hashes_b64.end()},
        });
    }

    {
        std::string score_data;
        score_data.append(beatmap_hash_str.string());
        assert(!score.map || score.map->getMD5().to_chars() == beatmap_hash_str);

        if(BanchoState::is_oauth) {
            score_data.append(":$token");
        } else {
            score_data.append(fmt::format(":{}", BanchoState::get_username()));
        }

        struct tm timeinfo;
        std::time_t timestamp = score.unix_timestamp;
        localtime_x(&timestamp, &timeinfo);

        std::array<char, 80> score_time{};
        strftime(score_time.data(), score_time.size(), "%y%m%d%H%M%S", &timeinfo);

        {
            auto idiot_check = fmt::format("chickenmcnuggets{}", score.num300s + score.num100s);
            idiot_check.append(fmt::format("o15{}{}", score.num50s, score.numGekis));
            idiot_check.append(fmt::format("smustard{}{}", score.numKatus, score.numMisses));
            idiot_check.append(fmt::format("uu{}", beatmap_hash_str.string()));
            idiot_check.append(fmt::format("{}{}", score.comboMax, score.perfect ? "True" : "False"));

            if(BanchoState::is_oauth) {
                idiot_check.append("$token");
            } else {
                idiot_check.append(BanchoState::get_username());
            }

            idiot_check.append(fmt::format("{}{}", score.score, GRADES[(int)score.grade]));
            idiot_check.append(
                fmt::format("{}Q{}", static_cast<u32>(score.mods.to_legacy()), score.passed ? "True" : "False"));
            idiot_check.append(fmt::format("0{}{}", OSU_VERSION_DATEONLY, score_time.data()));
            idiot_check.append(BanchoState::client_hashes);

            const MD5String idiot_hash{crypto::hash::md5(idiot_check)};
            score_data.append(":");
            score_data.append(idiot_hash.string());
        }
        score_data.append(fmt::format(":{}", score.num300s));
        score_data.append(fmt::format(":{}", score.num100s));
        score_data.append(fmt::format(":{}", score.num50s));
        score_data.append(fmt::format(":{}", score.numGekis));
        score_data.append(fmt::format(":{}", score.numKatus));
        score_data.append(fmt::format(":{}", score.numMisses));
        score_data.append(fmt::format(":{}", score.score));
        score_data.append(fmt::format(":{}", score.comboMax));
        score_data.append(fmt::format(":{}", score.perfect ? "True" : "False"));
        score_data.append(fmt::format(":{}", GRADES[(int)score.grade]));
        score_data.append(fmt::format(":{}", static_cast<u32>(score.mods.to_legacy())));
        score_data.append(fmt::format(":{}", score.passed ? "True" : "False"));
        score_data.append(":0");  // gamemode, always std
        score_data.append(fmt::format(":{}", score_time.data()));
        score_data.append(":mcosu");  // anticheat flags

        auto score_data_b64 = crypto::conv::encode64(BANCHO::AES::encrypt(iv, score_data));
        options.mime_parts.push_back({
            .name = "score",
            .data = {score_data_b64.begin(), score_data_b64.end()},
        });
    }

    options.mime_parts.push_back({
        .filename = "replay",
        .name = "score",
        .data = sub.compressed_replay,
    });

    {
        Packet packet;
        Replay::Mods::pack_and_write(packet, score.mods);
        auto mods_data_b64 = crypto::conv::encode64(packet.data);

        options.mime_parts.push_back({
            .name = PACKAGE_NAME "-mods",
            .data = {mods_data_b64.begin(), mods_data_b64.end()},
        });
    }

    auto url = fmt::format("osu.{}/web/osu-submit-modular-selector.php", BanchoState::endpoint);
    sub.in_flight = true;
    networkHandler->httpRequestAsync(url, std::move(options), [id = sub.id](const Mc::Net::Response &response) {
        on_submission_response(id, response);
    });
}

}  // namespace

namespace BANCHO::Net {

void submit_score(FinishedScore score) {
    auto compressed_replay = LegacyReplay::compress_frames(score.replay);
    if(compressed_replay.size() <= 24) {
        debugLog("Replay too small to submit! Compressed size: {:d} bytes", compressed_replay.size());
        return;
    }

    score.replay.clear();
    pending.push_back(PendingSubmission{
        .score = std::move(score),
        .compressed_replay = std::move(compressed_replay),
        .id = next_submission_id++,
    });
    update_submissions();
}

void update_submissions() {
    const f64 now = engine->getTime();
    for(PendingSubmission &sub : pending) {
        if(sub.in_flight || now < sub.next_attempt_time) continue;

        // only the account that set the score can submit it
        if(!BanchoState::is_online() || BanchoState::endpoint != sub.score.server ||
           BanchoState::get_uid() != sub.score.player_id) {
            continue;
        }

        send_submission(sub);
    }
}

void cleanup_submissions() {
    submit_cancel.request_stop();
    submit_cancel = {};
    pending.clear();
}

std::vector<FinishedScore> get_pending_scores(const MD5Hash &beatmap_hash) {
    std::vector<FinishedScore> scores;
    for(const PendingSubmission &sub : pending) {
        if(sub.score.beatmap_hash == beatmap_hash) scores.push_back(sub.score);
    }
    return scores;
}

bool is_score_pending(const FinishedScore &score) {
    return Mc::ranges::any_of(pending, [&score](const PendingSubmission &sub) {
        return sub.score.beatmap_hash == score.beatmap_hash && sub.score.unix_timestamp == score.unix_timestamp &&
               sub.score.playerName == score.playerName;
    });
}

}  // namespace BANCHO::Net
