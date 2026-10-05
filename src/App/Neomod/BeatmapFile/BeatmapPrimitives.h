#pragma once
// Copyright (c) 2020, PG & 2026, WH, All rights reserved.
#include "types.h"
#include "noinclude.h"
#include "Color.h"
#include "FixedSizeArray.h"
#include "DatabaseBeatmapTypes.h"
#include "SyncStoptoken.h"
#include "OsuConVars/DiffCalcDefaults.h"

#include <array>
#include <span>
#include <string_view>
#include <vector>

// the objects, timing points and settings gameplay and the star calc are built from, read from a .osu file (see
// BeatmapFile) without any of the game's state, so the standalone tools use the same code

namespace neomod {

class BeatmapFile;

struct LoadError {
   public:
    enum code : u8 {
        NONE = 0,
        METADATA = 1,
        FILE_LOAD = 2,
        NO_TIMINGPOINTS = 3,
        NO_OBJECTS = 4,
        TOOMANY_HITOBJECTS = 5,
        LOAD_INTERRUPTED = 6,
        LOADMETADATA_ON_BEATMAPSET = 7,
        NON_STD_GAMEMODE = 8,
        UNKNOWN_VERSION = 9,
        ERRC_COUNT = 10
    };
    code errc{0};

    [[nodiscard]] forceinline std::string_view error_string() const { return reasons[errc]; }

    explicit operator bool() const { return errc != NONE; }

   private:
    static constexpr const std::array<std::string_view, ERRC_COUNT> reasons{"no error",                               //
                                                                            "failed to load file metadata",           //
                                                                            "failed to load file",                    //
                                                                            "no timingpoints in file",                //
                                                                            "no objects in file",                     //
                                                                            "too many objects in file",               //
                                                                            "async load interrupted",                 //
                                                                            "tried to load metadata for beatmapset",  //
                                                                            "cannot load non-standard gamemode",      //
                                                                            "unknown beatmap version"};
};

// guards against maps made to break the game; the game passes its convars, the tools these defaults
struct PrimitiveLimits {
    u32 maxHitObjects{cv::defaults::beatmap_max_num_hitobjects};
    i32 maxSliderScoringTimes{cv::defaults::beatmap_max_num_slider_scoringtimes};
    f32 sliderCurveMaxLength{cv::defaults::slider_curve_max_length};
    i32 sliderEndInsideCheckOffset{cv::defaults::slider_end_inside_check_offset};
    i32 sliderMaxRepeats{cv::defaults::slider_max_repeats};
    i32 sliderMaxTicks{cv::defaults::slider_max_ticks};
};

struct PRIMITIVE_CONTAINER final {
    std::vector<DBType::HITCIRCLE> hitcircles{};
    std::vector<DBType::SLIDER> sliders{};
    std::vector<DBType::SPINNER> spinners{};
    std::vector<DBType::BREAK> breaks{};

    FixedSizeArray<DBType::TIMINGPOINT> timingpoints{};
    std::vector<Color> combocolors{};

    // the [HitObjects] lines no object came from
    std::vector<u32> skippedLines{};

    // what it was read with, for the slider timing calculated from it
    PrimitiveLimits limits{};

    f32 stackLeniency{.7f};
    f32 sliderMultiplier{1.f};
    f32 sliderTickRate{1.f};

    // [Difficulty] settings (old maps without an ApproachRate entry get AR = OD)
    f32 AR{5.f};
    f32 CS{5.f};
    f32 OD{5.f};
    f32 HP{5.f};

    [[nodiscard]] inline u32 getNumObjects() const { return hitcircles.size() + sliders.size() + spinners.size(); }

    u32 totalBreakDuration{0};

    i32 version{14};
    LoadError error;

    // sample set to use if timing point doesn't specify it
    // 1 = normal, 2 = soft, 3 = drum
    u8 defaultSampleSet{1};

    // Set after calculateSliderTimesClicksTicks has populated slider timing data.
    // Allows reuse of the container for multiple loadDifficultyHitObjects calls.
    bool sliderTimesCalculated{false};
};

// the file's timing points in time order
FixedSizeArray<DBType::TIMINGPOINT> readTimingPoints(const BeatmapFile &file);

PRIMITIVE_CONTAINER loadPrimitiveObjectsFromData(std::span<const u8> fileData, const PrimitiveLimits &limits,
                                                 const Sync::stop_token &dead = {});

LoadError calculateSliderTimesClicksTicks(int beatmapVersion, std::vector<DBType::SLIDER> &sliders,
                                          const FixedSizeArray<DBType::TIMINGPOINT> &timingpoints,
                                          float sliderMultiplier, float sliderTickRate, const PrimitiveLimits &limits,
                                          const Sync::stop_token &dead = {});

DBType::TIMING_INFO getTimingInfoForTimeAndTimingPoints(i32 positionMS,
                                                        const FixedSizeArray<DBType::TIMINGPOINT> &timingpoints);

}  // namespace neomod
