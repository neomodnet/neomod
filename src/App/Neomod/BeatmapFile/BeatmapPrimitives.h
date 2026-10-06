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

namespace neomod {
class BeatmapFile;
}

// the objects, timing points and settings gameplay and the star calc are built from, read from a .osu file (see
// BeatmapFile) without any of the game's state, so the standalone tools use the same code
namespace neomod::Primitives {

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
struct Limits {
    u32 maxHitObjects{cv::defaults::beatmap_max_num_hitobjects};
    i32 maxSliderScoringTimes{cv::defaults::beatmap_max_num_slider_scoringtimes};
    f32 sliderCurveMaxLength{cv::defaults::slider_curve_max_length};
    i32 sliderEndInsideCheckOffset{cv::defaults::slider_end_inside_check_offset};
    i32 sliderMaxRepeats{cv::defaults::slider_max_repeats};
    i32 sliderMaxTicks{cv::defaults::slider_max_ticks};
};

// a map's timing points in time order, and what applies at a given time
class TimingPoints {
    struct Entry {
        DBType::TIMINGPOINT point;
        // the last uninherited and the last inherited point up to this one (the first point if there's none)
        u32 lastUninherited;
        u32 lastInherited;
    };

   public:
    class Iterator {
       public:
        using value_type = DBType::TIMINGPOINT;
        using difference_type = std::ptrdiff_t;

        Iterator() = default;
        explicit Iterator(const Entry *entry) : entry(entry) {}

        const DBType::TIMINGPOINT &operator*() const { return this->entry->point; }
        const DBType::TIMINGPOINT *operator->() const { return &this->entry->point; }
        Iterator &operator++() {
            ++this->entry;
            return *this;
        }
        Iterator operator++(int) {
            const Iterator old = *this;
            ++this->entry;
            return old;
        }
        bool operator==(const Iterator &) const = default;

       private:
        const Entry *entry{nullptr};
    };

    TimingPoints() = default;
    explicit TimingPoints(std::vector<DBType::TIMINGPOINT> points);

    // the beat length and samples at a time, from the points at or before it (or the first point)
    [[nodiscard]] DBType::TIMING_INFO getTimingInfo(i32 positionMS) const;
    // the beats since the uninherited point that applies at a time, with their fraction: for what pulses with the music
    // (0 without a beat length)
    [[nodiscard]] f64 getBeat(i32 positionMS) const;

    [[nodiscard]] uSz size() const { return this->entries.size(); }
    [[nodiscard]] bool empty() const { return this->entries.empty(); }
    [[nodiscard]] const DBType::TIMINGPOINT &operator[](uSz i) const { return this->entries[i].point; }
    [[nodiscard]] const DBType::TIMINGPOINT &back() const { return this->entries.back().point; }
    [[nodiscard]] Iterator begin() const { return Iterator{this->entries.data()}; }
    [[nodiscard]] Iterator end() const { return Iterator{this->entries.data() + this->entries.size()}; }

   private:
    // the last point at or before the time (the first point before them all)
    [[nodiscard]] uSz entryAt(i32 positionMS) const;

    FixedSizeArray<Entry> entries;
};

struct PRIMITIVE_CONTAINER final {
    std::vector<DBType::HITCIRCLE> hitcircles{};
    std::vector<DBType::SLIDER> sliders{};
    std::vector<DBType::SPINNER> spinners{};
    std::vector<DBType::BREAK> breaks{};

    TimingPoints timingpoints{};
    std::vector<Color> combocolors{};

    // the [HitObjects] lines no object came from
    std::vector<u32> skippedLines{};

    // what it was read with, for the slider timing calculated from it
    Limits limits{};

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

TimingPoints readTimingPoints(const BeatmapFile &file);

PRIMITIVE_CONTAINER loadPrimitiveObjectsFromData(std::span<const u8> fileData, const Limits &limits,
                                                 const Sync::stop_token &dead = {});

LoadError calculateSliderTimesClicksTicks(int beatmapVersion, std::vector<DBType::SLIDER> &sliders,
                                          const TimingPoints &timingpoints, float sliderMultiplier,
                                          float sliderTickRate, const Limits &limits,
                                          const Sync::stop_token &dead = {});

}  // namespace neomod::Primitives
