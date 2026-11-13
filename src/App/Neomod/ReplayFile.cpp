// Copyright (c) 2024, kiwec, All rights reserved.
#include "ReplayFile.h"

#ifndef LZMA_API_STATIC
#define LZMA_API_STATIC
#endif
#include <lzma.h>

#include <rapidjson/document.h>

#include "AsyncIOHandler.h"
#include "ByteBufferedFile.h"
#include "ContainerRanges.h"
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
#include "Paths.h"
#include "SyncStoptoken.h"
#include "OsuConVars.h"
#include "SongBrowser.h"
#include "score.h"
#include "Parsing.h"
#include "SString.h"
#include "Logging.h"
#include "UI.h"
#include "Replay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <span>
#include <string_view>

namespace Replay {

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

namespace {
// streams the decompressed lzma data to on_chunk. returns false on a decompression error.
template <typename F>
bool decompress(std::span<const u8> data, F&& on_chunk) {
    if(data.empty()) return false;

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_alone_decoder(&strm, UINT64_MAX);
    if(ret != LZMA_OK) {
        debugLog("Failed to init lzma library ({:d})", static_cast<unsigned int>(ret));
        return false;
    }

    std::array<u8, BUFSIZ> outbuf;
    strm.next_in = data.data();
    strm.avail_in = data.size();
    do {
        strm.next_out = outbuf.data();
        strm.avail_out = outbuf.size();

        ret = lzma_code(&strm, LZMA_FINISH);
        if(ret != LZMA_OK && ret != LZMA_STREAM_END) {
            debugLog("Decompression error ({:d})", static_cast<unsigned int>(ret));
            break;
        }

        on_chunk(std::string_view{(const char*)outbuf.data(), outbuf.size() - strm.avail_out});
    } while(ret != LZMA_STREAM_END && strm.avail_out == 0);

    lzma_end(&strm);
    return ret == LZMA_OK || ret == LZMA_STREAM_END;
}

std::vector<Frame> get_frames(std::span<const u8> replay_data) {
    std::vector<Frame> replay_frames;

    i32 cur_music_pos = 0;
    std::string pending;  // partial frame entry carried across chunk boundaries
    const auto parse_entry = [&cur_music_pos, &replay_frames](std::string_view frame_str) {
        Frame frame;
        if(!Parsing::parse(frame_str, &frame.milliseconds_since_last_frame, '|', &frame.x, '|', &frame.y, '|',
                           &frame.key_flags))
            return;

        if(frame.milliseconds_since_last_frame != -12345) {
            cur_music_pos += frame.milliseconds_since_last_frame;
            frame.cur_music_pos = cur_music_pos;
            replay_frames.push_back(frame);
        }
    };

    // parse the complete (comma-terminated) frame entries of each chunk as they arrive
    const bool decompressed = decompress(replay_data, [&pending, &parse_entry](std::string_view chunk) {
        for(size_t comma = chunk.find(','); comma != std::string_view::npos; comma = chunk.find(',')) {
            std::string_view entry = chunk.substr(0, comma);
            if(!pending.empty()) {
                pending.append(entry);
                entry = pending;
            }
            parse_entry(entry);
            pending.clear();
            chunk.remove_prefix(comma + 1);
        }
        pending.append(chunk);
    });
    if(!decompressed) return {};

    // tolerate a final entry without the trailing comma
    if(!pending.empty()) parse_entry(pending);

    return replay_frames;
}
}  // namespace

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
        // the shortest exact representation, unless rounding to 4 decimals is shorter and this isn't a key frame
        const auto coord = [&frame](f32 value) {
            std::string exact = fmt::format("{}", value);
            if(frame.key_frame) return exact;
            std::string rounded = fmt::format("{:.4f}", value);
            return rounded.length() < exact.length() ? rounded : exact;
        };
        replay_string.append(fmt::format("{}|{}|{}|{},", frame.milliseconds_since_last_frame, coord(frame.x),
                                         coord(frame.y), frame.key_flags));
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

namespace {
struct Info {
    u8 gamemode;
    u32 osu_version;
    MD5Hash map_md5;
    std::string username;
    MD5Hash replay_md5;
    int num300s;
    int num100s;
    int num50s;
    int numGekis;
    int numKatus;
    int numMisses;
    u64 score;
    int comboMax;
    bool perfect;
    std::string life_bar_graph;
    i64 timestamp;
    std::vector<Frame> frames;
    i64 score_id = 0;
    i64 legacy_score_id = 0;
    Replay::Mods mods;
};

// rapidjson's getters only assert the value's type and replay files are untrusted:
// a missing member reads as null, a mistyped value as 0/empty
const rapidjson::Value& json_get(const rapidjson::Value& obj, const char* key) {
    static const rapidjson::Value null;
    if(!obj.IsObject()) return null;
    const auto member = obj.FindMember(key);
    return member != obj.MemberEnd() ? member->value : null;
}

f64 json_num(const rapidjson::Value& value) { return value.IsNumber() ? value.GetDouble() : 0.; }

std::string_view json_str(const rapidjson::Value& value) {
    return value.IsString() ? std::string_view{value.GetString(), value.GetStringLength()} : std::string_view{};
}

// converts the mods of a lazer score info json, skipping mods and mod settings neomod doesn't have
Replay::Mods parse_lazer_mods(const rapidjson::Value& lazer_mods_json) {
    using namespace flags::operators;

    struct LazerMod {
        std::string_view acronym;
        ModFlags flags{};
        f32 speed = 1.f;
        std::array<std::string_view, 2> settings{};
    };
    static constexpr std::array<LazerMod, 17> lazer_mods{{
        {.acronym = "NF", .flags = ModFlags::NoFail},
        {.acronym = "EZ", .flags = ModFlags::Easy},
        {.acronym = "HD", .flags = ModFlags::Hidden},
        {.acronym = "HR", .flags = ModFlags::HardRock},
        {.acronym = "SD", .flags = ModFlags::SuddenDeath},
        {.acronym = "PF", .flags = ModFlags::Perfect},
        {.acronym = "FL", .flags = ModFlags::Flashlight},
        {.acronym = "RX", .flags = ModFlags::Relax},
        {.acronym = "AP", .flags = ModFlags::Autopilot},
        {.acronym = "SO", .flags = ModFlags::SpunOut},
        {.acronym = "TD", .flags = ModFlags::TouchDevice},
        {.acronym = "CL"},
        {.acronym = "DT", .speed = 1.5f, .settings = {"speed_change", "adjust_pitch"}},
        {.acronym = "NC", .flags = ModFlags::NoPitchCorrection, .speed = 1.5f, .settings = {"speed_change"}},
        {.acronym = "HT", .speed = 0.75f, .settings = {"speed_change", "adjust_pitch"}},
        {.acronym = "DC", .flags = ModFlags::NoPitchCorrection, .speed = 0.75f, .settings = {"speed_change"}},
        {.acronym = "MR", .settings = {"reflection"}},
    }};

    Replay::Mods mods;
    for(const auto& mod : lazer_mods_json.GetArray()) {
        const auto& acronym_json = json_get(mod, "acronym");
        if(!acronym_json.IsString()) continue;
        const std::string_view acronym = json_str(acronym_json);
        const auto lazer_mod = Mc::ranges::find(lazer_mods, acronym, &LazerMod::acronym);
        if(lazer_mod == lazer_mods.end()) {
            debugLog("Ignoring unsupported lazer mod {}", acronym);
            continue;
        }

        mods.flags |= lazer_mod->flags;
        if(lazer_mod->speed != 1.f) mods.speed = lazer_mod->speed;

        const auto& settings = json_get(mod, "settings");
        if(settings.IsObject()) {
            for(const auto& setting : settings.GetObj()) {
                const std::string_view key = json_str(setting.name);
                if(!Mc::ranges::contains(lazer_mod->settings, key)) {
                    debugLog("Ignoring unsupported lazer mod setting {}.{}", acronym, key);
                    continue;
                }

                if(key == "speed_change") {
                    // otherwise keeps the mod's default rate
                    if(const f32 speed = (f32)json_num(setting.value); Replay::Mods::is_playable_speed(speed)) {
                        mods.speed = speed;
                    }
                } else if(key == "adjust_pitch") {
                    if(setting.value.IsTrue()) mods.flags |= ModFlags::NoPitchCorrection;
                }
            }
        }

        // horizontal by default
        if(acronym == "MR") {
            const auto& reflection = json_get(settings, "reflection");
            const std::string_view reflection_str = json_str(reflection);
            if(json_num(reflection) == 1 || SString::strcase_equal(reflection_str, "vertical")) {
                mods.flags |= ModFlags::MirrorVertical;
            } else if(json_num(reflection) == 2 || SString::strcase_equal(reflection_str, "both")) {
                mods.flags |= ModFlags::MirrorHorizontal | ModFlags::MirrorVertical;
            } else {
                mods.flags |= ModFlags::MirrorHorizontal;
            }
        }
    }

    return mods;
}

// reads the score info json at the end of lazer replays: the online score id, the mods and the score
void read_lazer_score_info(std::string_view json, Info& info) {
    // iterative since the default recursive parser overflows the stack on deeply nested json
    rapidjson::Document score_info;
    score_info.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(json.data(), json.size());
    if(!score_info.IsObject()) {
        debugLog("Invalid lazer score info");
        return;
    }

    // -1 when the score wasn't submitted
    if(const auto& online_id = json_get(score_info, "online_id"); online_id.IsInt64()) {
        if(const i64 id = online_id.GetInt64(); id != -1) info.score_id = id;
    }

    const auto& lazer_mods = json_get(score_info, "mods");
    if(lazer_mods.IsArray()) info.mods = parse_lazer_mods(lazer_mods);

    // lazer's "classic scoring" unfairly nerfs CL, and fucks up multipliers in general.
    // so we recompute the score with correct mod multipliers.
    f64 score_without_mods = info.score;
    if(lazer_mods.IsArray() && !lazer_mods.Empty()) {
        const auto& total_score_without_mods = json_get(score_info, "total_score_without_mods");
        if(!total_score_without_mods.IsNumber()) return;
        score_without_mods = total_score_without_mods.GetDouble();
    }

    const auto& statistics = json_get(score_info, "statistics");
    const auto& maximum_statistics = json_get(score_info, "maximum_statistics");
    const auto stat = [](const rapidjson::Value& stats, const char* key) { return json_num(json_get(stats, key)); };

    const f64 objects = stat(maximum_statistics, "great") + stat(maximum_statistics, "ok") +
                        stat(maximum_statistics, "meh") + stat(maximum_statistics, "miss");
    const f64 classic = std::round((objects * objects * 32.57 + 100000) * score_without_mods / 1000000);

    const f64 hits = 300 * stat(statistics, "great") + 100 * stat(statistics, "ok") + 50 * stat(statistics, "meh") +
                     30 * stat(statistics, "slider_tail_hit") + 10 * stat(statistics, "large_tick_hit") +
                     100 * stat(statistics, "small_bonus") + 1000 * stat(statistics, "large_bonus");
    const f64 multiplier = info.mods.get_scorev1_multiplier();
    info.score = std::llround(hits + std::max(classic - hits, 0.) * multiplier);
}

Info from_bytes(std::span<const u8> data) {
    Info info{};

    PacketReader replay{data};

    info.gamemode = replay.read<u8>();
    if(info.gamemode != 0) {
        debugLog("Replay has unexpected gamemode {:d}!", info.gamemode);
        return info;
    }

    info.osu_version = replay.read<u32>();
    info.map_md5 = replay.read_hash_chars();
    info.username = replay.read_string();
    info.replay_md5 = replay.read_hash_chars();
    info.num300s = replay.read<u16>();
    info.num100s = replay.read<u16>();
    info.num50s = replay.read<u16>();
    info.numGekis = replay.read<u16>();
    info.numKatus = replay.read<u16>();
    info.numMisses = replay.read<u16>();
    info.score = (u32)replay.read<i32>();
    info.comboMax = replay.read<u16>();
    info.perfect = replay.read<u8>();
    info.mods = Replay::Mods::from_legacy(replay.read<LegacyFlags>());
    info.life_bar_graph = replay.read_string();
    info.timestamp = (replay.read<i64>() - UNIX_EPOCH_TICKS) / TICKS_PER_SECOND;

    i32 replay_size = replay.read<i32>();
    if(replay_size <= 0) return info;
    const auto lzma_frames = replay.read_span((uSz)replay_size);
    if(!replay.good()) return info;
    info.frames = get_frames(lzma_frames);

    // https://github.com/ppy/osu/blob/a0e300c3/osu.Game/Scoring/Legacy/LegacyScoreDecoder.cs
    i64 score_id = 0;
    if(info.osu_version >= 20140721) {
        score_id = replay.read<i64>();
    } else if(info.osu_version >= 20121008) {
        score_id = replay.read<i32>();
    }

    if(info.osu_version >= 40000000) {
        // score_id = score_id, if only things could always be this simple
        info.score_id = score_id;
    } else {
        // thanks peppy for the yummy spaghetti!
        // this field is legacy_score_id when it's an osu!stable score
        // but it's -1 when it's a lazer score (we then have to get "actual" id from json below)
        if(score_id != -1) info.legacy_score_id = score_id;
    }

    // lazer replays append their score info as lzma-compressed json
    if(info.osu_version >= 30000001 && info.osu_version < 40000000) {
        const i32 score_info_size = replay.read<i32>();
        const auto compressed_score_info = replay.read_span(score_info_size > 0 ? (uSz)score_info_size : 0);

        std::string score_info;
        if(replay.good() &&
           decompress(compressed_score_info, [&score_info](std::string_view chunk) { score_info.append(chunk); })) {
            read_lazer_score_info(score_info, info);
        }
    }

    // handle neomod mods
    if(info.osu_version >= 40000000) {
        auto mods = Replay::Mods::unpack(replay);
        // a truncated/absent block fails the reader
        if(replay.good()) info.mods = mods;
        // cvar snapshot (u32 count + strings) not currently read (see NOTE in Database::addScore)
    }

    return info;
}
}  // namespace

bool load_osr(std::string_view osr_path, FinishedScore& score_out) {
    uSz file_size = 0;
    std::unique_ptr<u8[]> buffer;
    {
        File replay_file(osr_path);
        if(!replay_file.canRead() || !(file_size = replay_file.getFileSize())) return false;
        buffer = replay_file.takeFileBuffer();
        if(!buffer) return false;
    }

    return load_osr(std::span<const u8>{buffer.get(), file_size}, score_out);
}

bool load_osr(std::span<const u8> osr_data, FinishedScore& score_out) {
    auto info = from_bytes(osr_data);
    if(info.frames.empty()) return false;

    score_out.replay = info.frames;
    score_out.mods = info.mods;
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
    score_out.unix_timestamp = info.timestamp;
    score_out.bancho_score_id = info.score_id != 0 ? info.score_id : info.legacy_score_id;
    score_out.is_legacy_score_id = info.score_id == 0 && info.legacy_score_id != 0;

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
        fmt::format("{}/{}/{}-{}.osr", Mc::Paths::replays(), score.server, score.player_id, score.unix_timestamp);
    ByteBufferedFile::Writer osr(osr_path);
    if(!osr.good()) {
        debugLog("Cannot save replay to {}: {}", osr_path, osr.error());
        return false;
    }

    auto compressed_replay = Replay::compress_frames(score.replay);
    const MD5String compressed_replay_hash{crypto::hash::md5(compressed_replay)};

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
    osr.write<i64>((i64)score.unix_timestamp * TICKS_PER_SECOND + UNIX_EPOCH_TICKS);
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

    if(!osr.commit()) {
        debugLog("Cannot save replay to {}: {}", osr_path, osr.error());
        return false;
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

    score_out.replay = get_frames({buffer.get(), file_size});
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
    for(const auto& osr :
        std::array{// peppy replay
                   (score.peppy_replay_tms > 0 ? fmt::format("{}/Data/r/{}-{}.osr", cv::osu_folder.getString(),
                                                             score.beatmap_hash, score.peppy_replay_tms)
                                               : ""s),
                   // neomod 43.09+
                   (score.server.empty()
                        ? fmt::format("{}/{}-{}.osr", Mc::Paths::replays(), score.player_id, score.unix_timestamp)
                        : fmt::format("{}/{}/{}-{}.osr", Mc::Paths::replays(), score.server, score.player_id,
                                      score.unix_timestamp))}) {
        if(osr.empty()) continue;
        if((succeeded = load_osr(osr, score))) break;
    }

    if(!succeeded) {
        // neomod (legacy)
        for(const auto& raw :
            std::array{(fmt::format("{}/{}.replay.lzma", Mc::Paths::replays(), score.unix_timestamp)),
                       (score.server.empty() ? ""s
                                             : fmt::format("{}/{}/{}.replay.lzma", Mc::Paths::replays(), score.server,
                                                           score.unix_timestamp))}) {
            if(raw.empty()) continue;
            if((succeeded = load_raw(raw, score))) break;
        }
    }

    if(succeeded && update_db) {
        Sync::unique_lock lk(db->scores_mtx);
        if(const auto& it = db->getScoresMutable().find(score.beatmap_hash); it != db->getScoresMutable().end()) {
            if(auto scorevecIt = Mc::ranges::find(it->second, score); scorevecIt != it->second.end()) {
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
        score.replay = get_frames({response.body.data(), response.body.size()});
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

}  // namespace Replay
