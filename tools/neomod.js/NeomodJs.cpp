// Copyright (c) 2026, kiwec, All rights reserved.
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "BeatmapPrimitives.h"
#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"
#include "GameplayJudge.h"
#include "LegacyReplay.h"
#include "ModFlags.h"
#include "Replay.h"
#include "score.h"

#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace emscripten;
using namespace neomod;

namespace {

val mods_to_js(const Replay::Mods &mods) {
    val m = val::object();
    m.set("flags", static_cast<u64>(mods.flags));
    m.set("notelock_type", mods.notelock_type);
    m.set("speed", mods.speed);
    m.set("autopilot_lenience", mods.autopilot_lenience);
    m.set("ar_override", mods.ar_override);
    m.set("ar_overridenegative", mods.ar_overridenegative);
    m.set("cs_override", mods.cs_override);
    m.set("cs_overridenegative", mods.cs_overridenegative);
    m.set("hp_override", mods.hp_override);
    m.set("od_override", mods.od_override);
    m.set("timewarp_multiplier", mods.timewarp_multiplier);
    m.set("minimize_multiplier", mods.minimize_multiplier);
    m.set("artimewarp_multiplier", mods.artimewarp_multiplier);
    m.set("arwobble_strength", mods.arwobble_strength);
    m.set("arwobble_interval", mods.arwobble_interval);
    m.set("wobble_strength", mods.wobble_strength);
    m.set("wobble_frequency", mods.wobble_frequency);
    m.set("wobble_rotation_speed", mods.wobble_rotation_speed);
    m.set("jigsaw_followcircle_radius_factor", mods.jigsaw_followcircle_radius_factor);
    m.set("shirone_combo", mods.shirone_combo);
    return m;
}

}  // namespace

struct JsBeatmap {
    JsBeatmap(std::string osu_bytes)
        : data(std::make_shared<const std::vector<u8>>(osu_bytes.begin(), osu_bytes.end())),
          map("", "", DatabaseBeatmap::BeatmapType::NEOMOD_DIFFICULTY) {
        // Load primitive hitobjects (also parses the [Difficulty] settings, incl. AR = OD fallback)
        this->primitives = Primitives::loadPrimitiveObjectsFromData(*this->data, {});
        if(this->primitives.error.errc) {
            this->error_msg = primitives.error.error_string();
            return;
        }

        // the metadata the judge asks the map for (e.g. for the score multiplier)
        if(const auto res = this->map.loadMetadata(false, this->data); res.error.errc) {
            this->error_msg = res.error.error_string();
            return;
        }

        this->numCircles = static_cast<int>(this->primitives.getNumObjects<DBType::HITCIRCLE>());
        this->numSliders = static_cast<int>(this->primitives.getNumObjects<DBType::SLIDER>());
        this->numSpinners = static_cast<int>(this->primitives.getNumObjects<DBType::SPINNER>());

        this->loaded_successfully = true;

        // Force recalc of difficulty attributes to trigger even for nomod
        this->mods_of_current_difficulty_attributes.speed = -1.f;
    }

    double calculate(val obj) { return this->calculate_pp(this->to_score(obj)); }

    double calculate_pp(const FinishedScore &score) {
        if(!this->loaded_successfully) val::global("Error").new_(this->error_msg).throw_();

        if(this->mods_of_current_difficulty_attributes != score.mods) {
            bool success = this->use_mods(score.mods);
            if(!success) val::global("Error").new_(this->error_msg).throw_();
        }

        DiffCalc::PPv2CalcParams params{.attributes = this->difficulty_attributes,
                                        .modFlags = score.mods.flags,
                                        .timescale = score.mods.speed,
                                        .ar = score.mods.get_naive_ar(&this->map),
                                        .od = score.mods.get_naive_od(&this->map),
                                        .numHitObjects = static_cast<int>(this->primitives.getNumObjects()),
                                        .numCircles = this->numCircles,
                                        .numSliders = this->numSliders,
                                        .numSpinners = this->numSpinners,
                                        .maxPossibleCombo = this->maxPossibleCombo,
                                        .combo = score.comboMax,
                                        .misses = score.numMisses,
                                        .c300 = score.num300s,
                                        .c100 = score.num100s,
                                        .c50 = score.num50s,
                                        .legacyTotalScore = (u32)score.score,
                                        .isMcOsuImported = false};
        return DiffCalc::calculatePPv2(params);
    }

    std::shared_ptr<const std::vector<u8>> data;  // the .osu file
    DatabaseBeatmap map;
    bool loaded_successfully = false;
    std::string error_msg = "";

   private:
    // Embind doesn't support default values in structs, we have to handle it ourselves...
    FinishedScore to_score(val obj) {
        FinishedScore score;
        if(obj.isUndefined() || obj.isNull()) return score;

        if(obj.hasOwnProperty("comboMax")) score.comboMax = obj["comboMax"].as<int>();
        if(obj.hasOwnProperty("numMisses")) score.numMisses = obj["numMisses"].as<int>();
        if(obj.hasOwnProperty("num300s")) score.num300s = obj["num300s"].as<int>();
        if(obj.hasOwnProperty("num100s")) score.num100s = obj["num100s"].as<int>();
        if(obj.hasOwnProperty("num50s")) score.num50s = obj["num50s"].as<int>();
        if(obj.hasOwnProperty("score")) score.score = obj["score"].as<u64>();

        if(!obj.hasOwnProperty("mods")) return score;
        auto m = obj["mods"];
        if(m.isUndefined() || m.isNull()) return score;

        if(m.hasOwnProperty("flags")) score.mods.flags = static_cast<ModFlags>(m["flags"].as<u64>());
        if(m.hasOwnProperty("notelock_type")) score.mods.notelock_type = m["notelock_type"].as<i32>();

        if(m.hasOwnProperty("speed")) score.mods.speed = m["speed"].as<f32>();
        if(m.hasOwnProperty("autopilot_lenience")) score.mods.autopilot_lenience = m["autopilot_lenience"].as<f32>();
        if(m.hasOwnProperty("ar_override")) score.mods.ar_override = m["ar_override"].as<f32>();
        if(m.hasOwnProperty("ar_overridenegative")) score.mods.ar_overridenegative = m["ar_overridenegative"].as<f32>();
        if(m.hasOwnProperty("cs_override")) score.mods.cs_override = m["cs_override"].as<f32>();
        if(m.hasOwnProperty("cs_overridenegative")) score.mods.cs_overridenegative = m["cs_overridenegative"].as<f32>();
        if(m.hasOwnProperty("hp_override")) score.mods.hp_override = m["hp_override"].as<f32>();
        if(m.hasOwnProperty("od_override")) score.mods.od_override = m["od_override"].as<f32>();
        if(m.hasOwnProperty("timewarp_multiplier")) score.mods.timewarp_multiplier = m["timewarp_multiplier"].as<f32>();
        if(m.hasOwnProperty("minimize_multiplier")) score.mods.minimize_multiplier = m["minimize_multiplier"].as<f32>();
        if(m.hasOwnProperty("artimewarp_multiplier"))
            score.mods.artimewarp_multiplier = m["artimewarp_multiplier"].as<f32>();
        if(m.hasOwnProperty("arwobble_strength")) score.mods.arwobble_strength = m["arwobble_strength"].as<f32>();
        if(m.hasOwnProperty("arwobble_interval")) score.mods.arwobble_interval = m["arwobble_interval"].as<f32>();
        if(m.hasOwnProperty("wobble_strength")) score.mods.wobble_strength = m["wobble_strength"].as<f32>();
        if(m.hasOwnProperty("wobble_frequency")) score.mods.wobble_frequency = m["wobble_frequency"].as<f32>();
        if(m.hasOwnProperty("wobble_rotation_speed"))
            score.mods.wobble_rotation_speed = m["wobble_rotation_speed"].as<f32>();
        if(m.hasOwnProperty("jigsaw_followcircle_radius_factor"))
            score.mods.jigsaw_followcircle_radius_factor = m["jigsaw_followcircle_radius_factor"].as<f32>();
        if(m.hasOwnProperty("shirone_combo")) score.mods.shirone_combo = m["shirone_combo"].as<f32>();

        return score;
    }

    bool use_mods(Replay::Mods mods) {
        auto diffResult = DiffCalc::loadDifficultyHitObjects(this->primitives, mods.get_naive_ar(&this->map),
                                                             mods.get_naive_cs(&this->map), mods.speed,
                                                             flags::has<ModFlags::HardRock>(mods.flags));
        if(diffResult.error.errc) {
            this->loaded_successfully = false;
            this->error_msg = diffResult.error.error_string();
            return false;
        }

        DiffCalc::BeatmapDiffcalcData diffcalcData{.sortedHitObjects = diffResult.diffobjects,
                                                   .CS = mods.get_naive_cs(&this->map),
                                                   .HP = mods.get_naive_hp(&this->map),
                                                   .AR = mods.get_naive_ar(&this->map),
                                                   .OD = mods.get_naive_od(&this->map),
                                                   .fileCS = diffResult.fileCS,
                                                   .fileHP = diffResult.fileHP,
                                                   .fileOD = diffResult.fileOD,
                                                   .hidden = flags::has<ModFlags::Hidden>(mods.flags),
                                                   .relax = flags::has<ModFlags::Relax>(mods.flags),
                                                   .autopilot = flags::has<ModFlags::Autopilot>(mods.flags),
                                                   .touchDevice = flags::has<ModFlags::TouchDevice>(mods.flags),
                                                   .flashlight = flags::has<ModFlags::Flashlight>(mods.flags),
                                                   .speedMultiplier = mods.speed,
                                                   .breakDuration = diffResult.totalBreakDuration};

        this->difficulty_attributes = DiffCalc::DifficultyAttributes{};
        DiffCalc::StarCalcParams starParams{
            .outAttributes = this->difficulty_attributes,
            .beatmapData = diffcalcData,
            .outAimStrains = nullptr,
            .outSpeedStrains = nullptr,
            .upToObjectIndex = -1,
            .cancelCheck = {},
        };

        this->star_rating = DiffCalc::calculateStarDiffForHitObjects(starParams);
        this->mods_of_current_difficulty_attributes = mods;
        this->maxPossibleCombo = static_cast<int>(diffResult.getTotalMaxCombo());
        return true;
    }

    int numCircles = 0;
    int numSliders = 0;
    int numSpinners = 0;
    int maxPossibleCombo = 0;
    Primitives::PRIMITIVE_CONTAINER primitives;
    DiffCalc::DifficultyAttributes difficulty_attributes;
    Replay::Mods mods_of_current_difficulty_attributes;

    // maybe expose these later
    double star_rating = 0.0;
    // aim_pp == this->difficulty_attributes.AimDifficulty
    // speed_pp == this->difficulty_attributes.SpeedDifficulty
};

struct JsReplay {
    JsReplay(std::string osr_bytes) {
        this->loaded_successfully = LegacyReplay::load_osr(
            std::span{reinterpret_cast<const u8 *>(osr_bytes.data()), osr_bytes.size()}, this->replay);
    }

    [[nodiscard]] val info() const {
        if(!this->loaded_successfully) val::global("Error").new_(std::string{"failed to parse replay"}).throw_();

        val frames = val::array();
        for(const auto &frame : this->replay.replay) {
            val f = val::object();
            f.set("time", frame.cur_music_pos);
            f.set("delta", frame.milliseconds_since_last_frame);
            f.set("x", frame.x);
            f.set("y", frame.y);
            f.set("keys", frame.key_flags);
            frames.call<void>("push", f);
        }

        val obj = val::object();
        obj.set("beatmapMd5", std::string{this->replay.beatmap_hash.to_chars().string()});
        obj.set("playerName", this->replay.playerName);
        obj.set("num300s", this->replay.num300s);
        obj.set("num100s", this->replay.num100s);
        obj.set("num50s", this->replay.num50s);
        obj.set("numGekis", this->replay.numGekis);
        obj.set("numKatus", this->replay.numKatus);
        obj.set("numMisses", this->replay.numMisses);
        obj.set("score", static_cast<double>(this->replay.score));
        obj.set("comboMax", this->replay.comboMax);
        obj.set("perfect", this->replay.perfect);
        obj.set("mods", mods_to_js(this->replay.mods));
        obj.set("timestamp", static_cast<double>(this->replay.unix_timestamp));
        obj.set("banchoScoreId", static_cast<double>(this->replay.bancho_score_id));
        obj.set("frames", frames);
        return obj;
    }

    // judge the replay frames on the beatmap like the game does when watching a replay
    [[nodiscard]] val simulate(JsBeatmap &beatmap) const {
        if(!this->loaded_successfully) val::global("Error").new_(std::string{"failed to parse replay"}).throw_();
        if(!beatmap.loaded_successfully) val::global("Error").new_(beatmap.error_msg).throw_();

        LiveScore score(true);
        score.mods = this->replay.mods;

        GameplayJudge judge(&beatmap.map, score);
        if(const auto err = judge.load(beatmap.data))
            val::global("Error").new_(std::string{err.error_string()}).throw_();

        judge.replay = this->replay.replay;
        judge.simulate_to(std::numeric_limits<i32>::max());

        FinishedScore result;
        result.mods = this->replay.mods;
        result.num300s = score.getNum300s();
        result.num100s = score.getNum100s();
        result.num50s = score.getNum50s();
        result.numMisses = score.getNumMisses();
        result.comboMax = score.getComboMax();
        result.score = score.getScore();

        val obj = val::object();
        obj.set("num300s", result.num300s);
        obj.set("num100s", result.num100s);
        obj.set("num50s", result.num50s);
        obj.set("numGekis", score.getNum300gs());
        obj.set("numKatus", score.getNum100ks());
        obj.set("numMisses", result.numMisses);
        obj.set("numSliderBreaks", score.getNumSliderBreaks());
        obj.set("score", static_cast<double>(result.score));
        obj.set("comboMax", result.comboMax);
        obj.set("maxPossibleCombo", judge.iMaxPossibleCombo);
        obj.set("accuracy", score.getAccuracy());
        obj.set("unstableRate", score.getUnstableRate());
        obj.set("failed", judge.hasFailed());
        obj.set("mods", mods_to_js(result.mods));
        obj.set("pp", beatmap.calculate_pp(result));
        return obj;
    }

   private:
    FinishedScore replay;
    bool loaded_successfully = false;
};

EMSCRIPTEN_BINDINGS(neomod_diffcalc) {
    class_<JsBeatmap>("Beatmap").constructor<std::string>().function("calculate", &Beatmap::calculate);
    class_<JsReplay>("Replay")
        .constructor<std::string>()
        .property("info", &JsReplay::info)
        .function("simulate", &JsReplay::simulate);
    constant("PP_ALGORITHM_VERSION", DiffCalc::PP_ALGORITHM_VERSION);
}
