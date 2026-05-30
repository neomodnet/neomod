// Copyright (c) 2024, kiwec, All rights reserved.
#include "LegacyReplay.h"

#ifndef LZMA_API_STATIC
#define LZMA_API_STATIC
#endif
#include <lzma.h>

#include "AsyncIOHandler.h"
#include "ByteBufferedFile.h"
#include "crypto.h"
#include "Bancho.h"
#include "BanchoApi.h"
#include "File.h"
#include "BeatmapInterface.h"
#include "Database.h"
#include "Engine.h"
#include "NetworkHandler.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "SyncStoptoken.h"
#include "OsuConVars.h"
#include "SongBrowser.h"
#include "score.h"
#include "Parsing.h"
#include "Logging.h"
#include "UI.h"

#include <cstdlib>
#include <string>

namespace LegacyReplay {

BEATMAP_VALUES getBeatmapValuesForModsLegacy(LegacyFlags modsLegacy, float legacyAR, float legacyCS, float legacyOD,
                                             float legacyHP) {
    BEATMAP_VALUES v;

    // HACKHACK: code duplication, see Osu::getDifficultyMultiplier()
    v.difficultyMultiplier = 1.0f;
    {
        if(flags::has<LegacyFlags::HardRock>(modsLegacy)) v.difficultyMultiplier = 1.4f;
        if(flags::has<LegacyFlags::Easy>(modsLegacy)) v.difficultyMultiplier = 0.5f;
    }

    // HACKHACK: code duplication, see Osu::getCSDifficultyMultiplier()
    v.csDifficultyMultiplier = 1.0f;
    {
        if(flags::has<LegacyFlags::HardRock>(modsLegacy)) v.csDifficultyMultiplier = 1.3f;  // different!
        if(flags::has<LegacyFlags::Easy>(modsLegacy)) v.csDifficultyMultiplier = 0.5f;
    }

    // apply legacy mods to legacy beatmap values
    v.AR = std::clamp<float>(legacyAR * v.difficultyMultiplier, 0.0f, 10.0f);
    v.CS = std::clamp<float>(legacyCS * v.csDifficultyMultiplier, 0.0f, 10.0f);
    v.OD = std::clamp<float>(legacyOD * v.difficultyMultiplier, 0.0f, 10.0f);
    v.HP = std::clamp<float>(legacyHP * v.difficultyMultiplier, 0.0f, 10.0f);

    return v;
}

std::vector<Frame> get_frames(const u8* replay_data, uSz replay_size) {
    std::vector<Frame> replay_frames;
    if(replay_size <= 0) return replay_frames;

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_alone_decoder(&strm, UINT64_MAX);
    if(ret != LZMA_OK) {
        debugLog("Failed to init lzma library ({:d})", static_cast<unsigned int>(ret));
        return replay_frames;
    }

    i32 cur_music_pos = 0;
    std::array<u8, BUFSIZ> outbuf;
    Packet output;
    strm.next_in = replay_data;
    strm.avail_in = replay_size;
    do {
        strm.next_out = outbuf.data();
        strm.avail_out = outbuf.size();

        ret = lzma_code(&strm, LZMA_FINISH);
        if(ret != LZMA_OK && ret != LZMA_STREAM_END) {
            debugLog("Decompression error ({:d})", static_cast<unsigned int>(ret));
            goto end;
        }

        output.write_bytes(outbuf.data(), outbuf.size() - strm.avail_out);
    } while(strm.avail_out == 0);
    output.write<u8>('\0');

    {
        char* line = (char*)output.memory;
        while(*line) {
            Frame frame;

            char* ms = Parsing::strtok_x('|', &line);
            frame.milliseconds_since_last_frame = Parsing::strto<i32>(ms);

            char* x = Parsing::strtok_x('|', &line);
            frame.x = Parsing::strto<f32>(x);

            char* y = Parsing::strtok_x('|', &line);
            frame.y = Parsing::strto<f32>(y);

            char* flags = Parsing::strtok_x(',', &line);
            frame.key_flags = Parsing::strto<u8>(flags);

            if(frame.milliseconds_since_last_frame != -12345) {
                cur_music_pos += frame.milliseconds_since_last_frame;
                frame.cur_music_pos = cur_music_pos;
                replay_frames.push_back(frame);
            }
        }
    }

end:
    free(output.memory);
    lzma_end(&strm);
    return replay_frames;
}

std::vector<u8> compress_frames(const std::vector<Frame>& frames) {
    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_options_lzma options;
    lzma_lzma_preset(&options, LZMA_PRESET_DEFAULT);
    lzma_ret ret = lzma_alone_encoder(&stream, &options);
    if(ret != LZMA_OK) {
        debugLog("Failed to initialize lzma encoder: error {:d}", static_cast<unsigned int>(ret));
        return {};
    }

    std::string replay_string;
    for(const Frame& frame : frames) {
        replay_string.append(fmt::format("{}|{:.4f}|{:.4f}|{},", frame.milliseconds_since_last_frame, frame.x, frame.y,
                                         frame.key_flags));
    }

    // osu!stable doesn't consider a replay valid unless it ends with this
    replay_string.append("-12345|0.0000|0.0000|0,");

    std::vector<u8> compressed;
    compressed.resize(replay_string.length());

    stream.avail_in = replay_string.length();
    stream.next_in = (const u8*)replay_string.c_str();
    stream.avail_out = compressed.size();
    stream.next_out = compressed.data();
    do {
        ret = lzma_code(&stream, LZMA_FINISH);
        if(ret == LZMA_OK) {
            compressed.resize(compressed.size() * 2);
            stream.avail_out = compressed.size() - stream.total_out;
            stream.next_out = compressed.data() + stream.total_out;
        } else if(ret != LZMA_STREAM_END) {
            debugLog("Error while compressing replay: error {:d}", static_cast<unsigned int>(ret));
            stream.total_out = 0;
            break;
        }
    } while(ret != LZMA_STREAM_END);

    compressed.resize(stream.total_out);
    lzma_end(&stream);
    return compressed;
}

Info from_bytes(const u8* data, uSz s_data) {
    Info info;

    Packet replay;
    replay.memory = (u8*)data;
    replay.size = s_data;

    info.gamemode = replay.read<u8>();
    if(info.gamemode != 0) {
        debugLog("Replay has unexpected gamemode {:d}!", info.gamemode);
        return info;
    }

    info.osu_version = replay.read<u32>();
    info.map_md5 = replay.read_hash_chars();
    info.username = replay.read_stdstring();
    info.replay_md5 = replay.read_hash_chars();
    info.num300s = replay.read<u16>();
    info.num100s = replay.read<u16>();
    info.num50s = replay.read<u16>();
    info.numGekis = replay.read<u16>();
    info.numKatus = replay.read<u16>();
    info.numMisses = replay.read<u16>();
    info.score = replay.read<i32>();
    info.comboMax = replay.read<u16>();
    info.perfect = replay.read<u8>();
    info.mod_flags = replay.read<LegacyFlags>();
    info.life_bar_graph = replay.read_stdstring();
    info.timestamp = (replay.read<i64>() - UNIX_EPOCH_TICKS) / TICKS_PER_SECOND;

    i32 replay_size = replay.read<i32>();
    if(replay_size <= 0) return info;
    auto replay_data = new u8[replay_size];
    replay.read_bytes(replay_data, replay_size);
    info.frames = get_frames(replay_data, replay_size);
    delete[] replay_data;

    // https://github.com/ppy/osu/blob/a0e300c3/osu.Game/Scoring/Legacy/LegacyScoreDecoder.cs
    if(info.osu_version >= 20140721) {
        info.bancho_score_id = replay.read<i64>();
    } else if(info.osu_version >= 20121008) {
        info.bancho_score_id = replay.read<i32>();
    }

    // XXX: handle lazer replay data (versions 30000001 to 30000016)

    // TODO: handle neomod replay data (versions 40000000+)

    return info;
}

bool load_osr(std::string_view osr_path, FinishedScore& score_out) {
    uSz file_size = 0;
    std::unique_ptr<u8[]> buffer;
    {
        File replay_file(osr_path);
        if(!replay_file.canRead() || !(file_size = replay_file.getFileSize())) return false;
        buffer = replay_file.takeFileBuffer();
        if(!buffer) return false;
    }

    auto info = from_bytes(buffer.get(), file_size);
    if(info.frames.empty()) return false;

    score_out.replay = info.frames;
    score_out.mods = Replay::Mods::from_legacy(info.mod_flags);
    score_out.num300s = info.num300s;
    score_out.num100s = info.num100s;
    score_out.num50s = info.num50s;
    score_out.numGekis = info.numGekis;
    score_out.numKatus = info.numKatus;
    score_out.numMisses = info.numMisses;
    score_out.score = info.score;
    score_out.playerName = info.username;
    score_out.beatmap_hash = info.map_md5;
    score_out.perfect = info.perfect;
    score_out.comboMax = info.comboMax;
    score_out.unixTimestamp = info.timestamp;
    score_out.bancho_score_id = info.bancho_score_id;

    // Prevent saving score to db
    score_out.is_online_score = true;
    score_out.is_online_replay_available = true;

    return true;
}

bool save_osr(const FinishedScore& score, std::span<const std::string> additional_data) {
    if(score.replay.empty()) {
        debugLog("Cannot save empty replay!");
        return false;
    }

    const std::string osr_path =
        fmt::format(NEOMOD_REPLAYS_PATH "/{}/{}-{}.osr", score.server, score.player_id, score.unixTimestamp);
    ByteBufferedFile::Writer osr(osr_path);
    if(!osr.good()) {
        debugLog("Cannot save replay to {}: {}", osr_path, osr.error());
        return false;
    }

    auto compressed_replay = LegacyReplay::compress_frames(score.replay);
    auto compressed_replay_hash = crypto::hash::md5_hex(compressed_replay.data(), compressed_replay.size());

    osr.write<u8>(0);          // ruleset
    osr.write<u32>(40000000);  // osu_version (30m+ is lazer-specific, 40m+ is neomod-specific)
    osr.write_string(score.beatmap_hash.to_chars().string());
    osr.write_string(score.playerName);
    osr.write_string(compressed_replay_hash.string());
    osr.write<u16>(score.num300s);
    osr.write<u16>(score.num100s);
    osr.write<u16>(score.num50s);
    osr.write<u16>(score.numGekis);
    osr.write<u16>(score.numKatus);
    osr.write<u16>(score.numMisses);
    osr.write<u32>(score.score);
    osr.write<u16>(score.comboMax);
    osr.write<u8>(score.perfect);
    osr.write<u32>((u32)score.mods.to_legacy());
    osr.write_string(""sv);  // life_bar_graph
    osr.write<i64>((i64)score.unixTimestamp * TICKS_PER_SECOND + UNIX_EPOCH_TICKS);
    osr.write<i32>((i32)compressed_replay.size());
    osr.write_bytes(compressed_replay.data(), compressed_replay.size());
    osr.write<i64>(score.bancho_score_id);

    // neomod-specific
    Replay::Mods::pack_and_write(osr, score.mods);

    if(!additional_data.empty()) {
        osr.write<u32>(additional_data.size());
        for(const auto& str : additional_data) {
            osr.write_string(str);
        }
    }

    return true;
}

namespace {
// armed per online replay download so starting another supersedes the previous one
Sync::stop_source replay_dl_cancel;

bool load_raw(std::string_view lzma_path, FinishedScore& score_out) {
    uSz file_size = 0;
    std::unique_ptr<u8[]> buffer;
    {
        File replay_file(lzma_path);
        if(!replay_file.canRead() || !(file_size = replay_file.getFileSize())) return false;
        buffer = replay_file.takeFileBuffer();
        if(!buffer) return false;
    }

    score_out.replay = get_frames(buffer.get(), file_size);
    return !score_out.replay.empty();
}

void watch(const FinishedScore& score) {
    assert(!score.replay.empty());

    auto* map = db->getBeatmapDifficulty(score.beatmap_hash);
    if(map == nullptr) {
        // XXX: Auto-download beatmap
        ui->getNotificationOverlay()->addToast("Missing beatmap for this replay", ERROR_TOAST);
    } else {
        auto* sb = ui->getSongBrowser();
        sb->onDifficultySelected(map, false);
        sb->selectSelectedBeatmapSongButton();
        osu->getMapInterface()->watch(score, 0);
    }
}
}  // namespace

bool load_from_disk(FinishedScore& score, bool update_db) {
    bool succeeded = false;
    for(const auto& osr : std::array{
            // peppy replay
            (score.peppy_replay_tms > 0 ? fmt::format("{}/Data/r/{}-{}.osr", cv::osu_folder.getString(),
                                                      score.beatmap_hash, score.peppy_replay_tms)
                                        : ""s),
            // neomod 43.09+
            (score.server.empty() ? fmt::format(NEOMOD_REPLAYS_PATH "/{}-{}.osr", score.player_id, score.unixTimestamp)
                                  : fmt::format(NEOMOD_REPLAYS_PATH "/{}/{}-{}.osr", score.server, score.player_id,
                                                score.unixTimestamp))}) {
        if(osr.empty()) continue;
        if((succeeded = load_osr(osr, score))) break;
    }

    if(!succeeded) {
        // neomod (legacy)
        for(const auto& raw : std::array{(fmt::format(NEOMOD_REPLAYS_PATH "/{}.replay.lzma", score.unixTimestamp)),
                                         (score.server.empty() ? ""s
                                                               : fmt::format(NEOMOD_REPLAYS_PATH "/{}/{}.replay.lzma",
                                                                             score.server, score.unixTimestamp))}) {
            if(raw.empty()) continue;
            if((succeeded = load_raw(raw, score))) break;
        }
    }

    if(succeeded && update_db) {
        Sync::unique_lock lk(db->scores_mtx);
        if(const auto& it = db->getScoresMutable().find(score.beatmap_hash); it != db->getScoresMutable().end()) {
            if(auto scorevecIt = std::ranges::find(it->second, score); scorevecIt != it->second.end()) {
                scorevecIt->replay = score.replay;
            }
        }
    }

    return succeeded;
}

void load_and_watch(FinishedScore score) {
    if(!score.replay.empty()) {
        // Replay already loaded
        watch(score);
        return;
    }

    if(load_from_disk(score, true)) {
        // Score was successfully loaded from disk
        watch(score);
        return;
    }

    // We don't have the replay, try loading it from the server
    if(score.server != BanchoState::endpoint) {
        ui->getNotificationOverlay()->addToast(fmt::format("Please connect to {:s} to view this replay!", score.server),
                                               ERROR_TOAST);
        return;
    }

    ui->getNotificationOverlay()->addNotification("Downloading replay...");

    std::string url =
        fmt::format("osu.{:s}/web/osu-getreplay.php?m=0&c={:d}", BanchoState::endpoint, score.bancho_score_id);
    BANCHO::Api::append_auth_params(url);
    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = 5,
        .connect_timeout = 5,
    };

    // cancel any previous replay download; only one replay is watched at a time
    replay_dl_cancel.request_stop();
    replay_dl_cancel = {};
    options.cancel_token = replay_dl_cancel.get_token();

    networkHandler->httpRequestAsync(url, std::move(options), [score](const Mc::Net::Response& response) mutable {
        if(!response.success) {
            // Most likely, 404
            ui->getNotificationOverlay()->addToast("Failed to download replay", ERROR_TOAST);
            return;
        }

        // Unzip replay frames from server response
        score.replay = get_frames((const u8*)response.body.data(), response.body.size());
        if(!score.replay.empty()) {
            // Save it to disk (XXX: blocking main thread)
            save_osr(score);

            // Watch it
            watch(score);
        } else {
            ui->getNotificationOverlay()->addToast("Failed to load replay", ERROR_TOAST);
        }
    });
}

}  // namespace LegacyReplay
