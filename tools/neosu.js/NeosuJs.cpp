// Copyright (c) 2026, kiwec, All rights reserved.
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"
#include "ModFlags.h"
#include "Parsing.h"
#include "Replay.h"
#include "score.h"
#include "SString.h"

using namespace emscripten;

struct Beatmap {
    Beatmap(std::string osu_bytes) {
        auto lines = SString::split_newlines(osu_bytes);

        // Simplified parsing of difficulty settings (from DiffCalcTool.cpp)
        bool foundAR = false;
        bool inDifficulty = false;
        for(auto line : lines) {
            if(line.empty() || SString::is_comment(line)) continue;

            if(line.contains("[Difficulty]")) {
                inDifficulty = true;
                continue;
            }

            if(line.starts_with('[') && inDifficulty) {
                break;
            }

            if(inDifficulty) {
                Parsing::parse(line, "CircleSize", ':', &this->CS);
                if(Parsing::parse(line, "ApproachRate", ':', &this->AR)) {
                    foundAR = true;
                }
                Parsing::parse(line, "HPDrainRate", ':', &this->HP);
                Parsing::parse(line, "OverallDifficulty", ':', &this->OD);
            }
        }

        // old beatmaps: AR = OD
        if(!foundAR) {
            this->AR = this->OD;
        }

        // Load primitive hitobjects
        std::vector<uint8_t> osu_file(osu_bytes.begin(), osu_bytes.end());
        this->primitives = DatabaseBeatmap::loadPrimitiveObjectsFromData(osu_file, "memory.osu");
        if(this->primitives.error.errc) {
            this->error_msg = primitives.error.error_string();
            return;
        }

        this->loaded_successfully = true;

        // Force recalc of difficulty attributes to trigger even for nomod
        this->mods_of_current_difficulty_attributes.speed = -1.f;
    }

    double calculate(val obj) {
        if(!this->loaded_successfully) {
            val::global("Error").new_(this->error_msg).throw_();
        }

        auto score = this->to_score(obj);
        if(this->mods_of_current_difficulty_attributes != score.mods) {
            bool success = this->use_mods(score.mods);
            if(!success) {
                val::global("Error").new_(this->error_msg).throw_();
            }
        }

        DifficultyCalculator::PPv2CalcParams params{.attributes = this->difficulty_attributes,
                                                    .modFlags = score.mods.flags,
                                                    .timescale = score.mods.speed,
                                                    .ar = score.mods.get_naive_ar(this->AR),
                                                    .od = score.mods.get_naive_od(this->OD),
                                                    .numHitObjects = static_cast<int>(this->primitives.getNumObjects()),
                                                    .numCircles = static_cast<int>(this->primitives.hitcircles.size()),
                                                    .numSliders = static_cast<int>(this->primitives.sliders.size()),
                                                    .numSpinners = static_cast<int>(this->primitives.spinners.size()),
                                                    .maxPossibleCombo = this->maxPossibleCombo,
                                                    .combo = score.comboMax,
                                                    .misses = score.numMisses,
                                                    .c300 = score.num300s,
                                                    .c100 = score.num100s,
                                                    .c50 = score.num50s,
                                                    .legacyTotalScore = (u32)score.score};
        return DifficultyCalculator::calculatePPv2(params);
    }

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
        auto diffResult = DatabaseBeatmap::loadDifficultyHitObjects(this->primitives, mods.get_naive_ar(this->AR),
                                                                    mods.get_naive_cs(this->CS), mods.speed, false);
        if(diffResult.error.errc) {
            this->loaded_successfully = false;
            this->error_msg = diffResult.error.error_string();
            return false;
        }

        DifficultyCalculator::BeatmapDiffcalcData diffcalcData{
            .sortedHitObjects = diffResult.diffobjects,
            .CS = mods.get_naive_cs(this->CS),
            .HP = mods.get_naive_hp(this->HP),
            .AR = mods.get_naive_ar(this->AR),
            .OD = mods.get_naive_od(this->OD),
            .hidden = flags::has<ModFlags::Hidden>(mods.flags),
            .relax = flags::has<ModFlags::Relax>(mods.flags),
            .autopilot = flags::has<ModFlags::Autopilot>(mods.flags),
            .touchDevice = flags::has<ModFlags::TouchDevice>(mods.flags),
            .speedMultiplier = mods.speed,
            .breakDuration = diffResult.totalBreakDuration,
            .playableLength = diffResult.playableLength};

        this->difficulty_attributes = DifficultyCalculator::DifficultyAttributes{};
        DifficultyCalculator::StarCalcParams starParams{
            .cachedDiffObjects = {},
            .outAttributes = this->difficulty_attributes,
            .beatmapData = diffcalcData,
            .outAimStrains = nullptr,
            .outSpeedStrains = nullptr,
            .incremental = nullptr,
            .upToObjectIndex = -1,
            .cancelCheck = {},
        };

        this->star_rating = DifficultyCalculator::calculateStarDiffForHitObjects(starParams);
        this->mods_of_current_difficulty_attributes = mods;
        this->maxPossibleCombo = static_cast<int>(diffResult.getTotalMaxCombo());
        return true;
    }

    float AR = 5.0f;
    float CS = 5.0f;
    float OD = 5.0f;
    float HP = 5.0f;
    int maxPossibleCombo = 0;
    DatabaseBeatmap::PRIMITIVE_CONTAINER primitives;
    DifficultyCalculator::DifficultyAttributes difficulty_attributes;
    Replay::Mods mods_of_current_difficulty_attributes;

    // maybe expose these later
    bool loaded_successfully = false;
    std::string error_msg = "";
    double star_rating = 0.0;
    // aim_pp == this->difficulty_attributes.AimDifficulty
    // speed_pp == this->difficulty_attributes.SpeedDifficulty
};

EMSCRIPTEN_BINDINGS(neomod_diffcalc) {
    class_<Beatmap>("Beatmap").constructor<std::string>().function("calculate", &Beatmap::calculate);
    constant("PP_ALGORITHM_VERSION", DiffCalc::PP_ALGORITHM_VERSION);
}
