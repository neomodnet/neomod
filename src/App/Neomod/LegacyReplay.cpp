// Copyright (c) 2016, PG, All rights reserved.
#include "LegacyReplay.h"

#ifndef LZMA_API_STATIC
#define LZMA_API_STATIC
#endif
#include <lzma.h>

#include "OsuConVars.h"
#include "Bancho.h"
#include "BanchoApi.h"
#include "File.h"
#include "BeatmapInterface.h"
#include "Database.h"
#include "Engine.h"
#include "NotificationOverlay.h"
#include "Osu.h"
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

std::vector<Frame> get_frames(u8* replay_data, uSz replay_size) {
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
    for(Frame frame : frames) {
        std::string frame_str =
            fmt::format("{}|{:.4f}|{:.4f}|{},", frame.milliseconds_since_last_frame, frame.x, frame.y, frame.key_flags);
        replay_string.append(frame_str.c_str(), frame_str.length());
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

Info from_bytes(u8* data, uSz s_data) {
    Info info;

    Packet replay;
    replay.memory = data;
    replay.size = s_data;

    info.gamemode = replay.read<u8>();
    if(info.gamemode != 0) {
        debugLog("Replay has unexpected gamemode {:d}!", info.gamemode);
        return info;
    }

    info.osu_version = replay.read<u32>();
    info.map_md5 = replay.read_ustring();
    info.username = replay.read_ustring();
    info.replay_md5 = replay.read_ustring();
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
    info.life_bar_graph = replay.read_ustring();
    info.timestamp = replay.read<i64>() / 10LL;

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
    // XXX: handle neomod replay data (versions 40000000+?)

    return info;
}

bool load_from_disk(FinishedScore& score, bool update_db) {
    bool success = false;

    const bool is_peppy = score.peppy_replay_tms > 0;
    const auto path = is_peppy
                          ? fmt::format("{:s}/Data/r/{:s}-{:d}.osr", cv::osu_folder.getString(), score.beatmap_hash,
                                        score.peppy_replay_tms)
                          : fmt::format(NEOMOD_REPLAYS_PATH "/{:s}/{:d}.replay.lzma", score.server, score.unixTimestamp);
    do {
        uSz file_size = 0;
        std::unique_ptr<u8[]> buffer;
        {
            File replay_file(path);
            if(!replay_file.canRead() || !(file_size = replay_file.getFileSize())) break;
            buffer = replay_file.takeFileBuffer();
            if(!buffer) break;
        }

        if(is_peppy) {
            auto info = from_bytes(buffer.get(), file_size);
            score.replay = info.frames;
            score.mods = Replay::Mods::from_legacy(info.mod_flags);  // update mods just in case
        } else {
            score.replay = get_frames(buffer.get(), file_size);
        }

        if(score.replay.empty()) break;

        success = true;  // FIXME/TODO (?): we're just assuming from_bytes/get_frames will have worked?

        if(update_db) {
            Sync::unique_lock lk(db->scores_mtx);
            if(const auto& it = db->getScoresMutable().find(score.beatmap_hash); it != db->getScoresMutable().end()) {
                if(auto scorevecIt = std::ranges::find(it->second, score); scorevecIt != it->second.end()) {
                    scorevecIt->replay = score.replay;
                }
            }
        }
    } while(false);

    return success;
}

void load_and_watch(FinishedScore score) {
    // Check if replay is loaded
    if(score.replay.empty()) {
        if(!load_from_disk(score, true)) {
            if(score.server.c_str() != BanchoState::endpoint) {
                auto msg = fmt::format("Please connect to {} to view this replay!", score.server);
                ui->getNotificationOverlay()->addToast(msg, ERROR_TOAST);
            }

            // Need to allocate with calloc since BANCHO::Api::Requests free() the .extra
            void* mem = calloc(1, sizeof(FinishedScore));
            auto* score_cpy = new(mem) FinishedScore;
            *score_cpy = score;

            std::string url = fmt::format("/web/osu-getreplay.php?m=0&c={}", score.bancho_score_id);
            BANCHO::Api::append_auth_params(url);

            BANCHO::Api::Request request;
            request.type = BANCHO::Api::GET_REPLAY;
            request.path = url;
            request.extra = (u8*)score_cpy;
            BANCHO::Api::send_request(request);

            ui->getNotificationOverlay()->addNotification(u"Downloading replay...");
            return;
        }
    }

    // We tried loading from memory, we tried loading from file, we tried loading from server... RIP
    if(score.replay.empty()) {
        ui->getNotificationOverlay()->addToast(US_("Failed to load replay"), ERROR_TOAST);
        return;
    }

    auto map = db->getBeatmapDifficulty(score.beatmap_hash);
    if(map == nullptr) {
        // XXX: Auto-download beatmap
        ui->getNotificationOverlay()->addToast(US_("Missing beatmap for this replay"), ERROR_TOAST);
    } else {
        ui->getSongBrowser()->onDifficultySelected(map, false);
        ui->getSongBrowser()->selectSelectedBeatmapSongButton();
        osu->getMapInterface()->watch(score, 0);
    }
}

}  // namespace LegacyReplay
