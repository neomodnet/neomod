// Copyright (c) 2024, kiwec, All rights reserved.
#include "BanchoSubmitter.h"

#include "Database.h"
#include "Bancho.h"
#include "BanchoAes.h"
#include "BanchoNetworking.h"
#include "OsuConVars.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "NetworkHandler.h"
#include "SongBrowser/SongBrowser.h"
#include "crypto.h"
#include "Logging.h"
#include "Timing.h"
#include "UI.h"
#include "score.h"

#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace BANCHO::Net {
void submit_score(FinishedScore score) {
    debugLog("Submitting score...");
    constexpr auto GRADES = std::array{"XH", "SH", "X", "S", "A", "B", "C", "D", "F", "N"};

    std::array<u8, 32> iv;
    crypto::rng::get_rand(iv);

    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = cv::net_transfer_timeout.getVal<long>(),
        .connect_timeout = 5,
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
        MD5String md5str = score.map->getMD5().to_chars();
        score_data.append(md5str.string());

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
            idiot_check.append(fmt::format("uu{}", md5str.string()));
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

    {
        auto compressed_data = LegacyReplay::compress_frames(score.replay);
        if(compressed_data.size() <= 24) {
            debugLog("Replay too small to submit! Compressed size: {:d} bytes", compressed_data.size());
            return;
        }

        options.mime_parts.push_back({
            .filename = "replay",
            .name = "score",
            .data = compressed_data,
        });
    }

    {
        Packet packet;
        Replay::Mods::pack_and_write(packet, score.mods);
        auto mods_data_b64 = crypto::conv::encode64(std::span{packet.memory, packet.pos});
        free(packet.memory);

        options.mime_parts.push_back({
            .name = PACKAGE_NAME "-mods",
            .data = {mods_data_b64.begin(), mods_data_b64.end()},
        });
    }

    auto url = fmt::format("osu.{}/web/osu-submit-modular-selector.php", BanchoState::endpoint);
    networkHandler->httpRequestAsync(url, std::move(options), [beatmap_hash_str](const Mc::Net::Response &response) {
        if(response.success) {
            // TODO: handle success (pp, etc + error codes)
            debugLog("Score submit result: {}", response.text());

            // Reset leaderboards so new score will appear
            db->getOnlineScores().clear();
            ui->getSongBrowser()->onGotNewLeaderboard(beatmap_hash_str);
        } else {
            // TODO: handle failure
        }
    });
}
}  // namespace BANCHO::Net
