#pragma once
// Copyright (c) 2020, PG, All rights reserved.
#if __has_include("config.h")
#include "config.h"
#endif

#include "types.h"
#include "noinclude.h"
#include "Vectors.h"
#include "FixedSizeArray.h"
#include "StarPrecalc.h"

// TODO: make these utilities available without all of these ifdefs (move all diffcalc things to a lightweight separate directory)
#ifndef BUILD_TOOLS_ONLY

#include "Overrides.h"
#include "MD5Hash.h"
#include "Color.h"
#include "HitSounds.h"
#include "SyncStoptoken.h"

#else

using Color = uint32_t;

// re-defining these to avoid needing to compile HitSounds.cpp (only need the definitions for diffcalc)
namespace SampleSetType {
enum {
    NORMAL = 1,
    SOFT = 2,
    DRUM = 3,
};
}

namespace HitSoundType {
enum {
    NORMAL = (1 << 0),
    WHISTLE = (1 << 1),
    FINISH = (1 << 2),
    CLAP = (1 << 3),

    VALID_HITSOUNDS = NORMAL | WHISTLE | FINISH | CLAP,
    VALID_SLIDER_HITSOUNDS = NORMAL | WHISTLE,
};
}

struct HitSamples final {
    u8 hitSounds = 0;
    u8 normalSet = 0;
    u8 additionSet = 0;
    u8 volume = 0;
    i32 index = 0;
    std::string filename = "";
};

#include <stop_token>
namespace Sync {
using std::stop_token;
}
#endif

#include <atomic>
#include <string_view>
#include <memory>
#include <functional>

// purpose:
// 1) contain all infos which are ALWAYS kept in memory for beatmaps
// 2) be the data source for Beatmap when starting a difficulty
// 3) allow async calculations/loaders to work on the contained data (e.g. background image loader)
// 4) be a container for difficulties (all top level DatabaseBeatmap objects are containers)

class AbstractBeatmapInterface;
class HitObject;
class DifficultyHitObject;

class Database;

class BGImageHandler;

class DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;
using BeatmapSet = DatabaseBeatmap;
using DiffContainer = std::vector<std::unique_ptr<BeatmapDifficulty>>;

struct SLIDER_SCORING_TIME;  // for diffcalc things

// DatabaseBeatmap &operator=(DatabaseBeatmap other) already implements these...
// NOLINTNEXTLINE(hicpp-special-member-functions, cppcoreguidelines-special-member-functions)
class DatabaseBeatmap final {
   public:
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
        static constexpr const std::array<std::string_view, ERRC_COUNT> reasons{
            "no error",                               //
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

    enum class BlockId : i8 {
        Sentinel = -2,  // for skipping the first string scan, header must come first
        Header = -1,
        General = 0,
        Metadata = 1,
        Difficulty = 2,
        Events = 3,
        TimingPoints = 4,
        Colours = 5,
        HitObjects = 6,
    };

    struct MetadataBlock {
        std::string_view str;
        BlockId id;
    };

    static constexpr const std::array<MetadataBlock, 7> metadataBlocks{
        MetadataBlock{.str = "[General]", .id = BlockId::General},
        MetadataBlock{.str = "[Metadata]", .id = BlockId::Metadata},
        MetadataBlock{.str = "[Difficulty]", .id = BlockId::Difficulty},
        MetadataBlock{.str = "[Events]", .id = BlockId::Events},
        MetadataBlock{.str = "[TimingPoints]", .id = BlockId::TimingPoints},
        MetadataBlock{.str = "[Colours]", .id = BlockId::Colours},
        MetadataBlock{.str = "[HitObjects]", .id = BlockId::HitObjects}};
    static inline const auto alwaysFalseStopPred = Sync::stop_token{};

    // raw structs (not editable, we're following db format directly)
    struct TIMINGPOINT final {
        f64 offset;
        f64 msPerBeat;

        i32 sampleSet;
        i32 sampleIndex;
        i32 volume;

        bool uninherited;  // <=> timingChange
        bool kiai;

        bool operator==(const TIMINGPOINT &) const = default;
    };

    struct BREAK final {
        i64 startTime;
        i64 endTime;
    };

    // custom structs
    struct LOAD_DIFFOBJ_RESULT final {
        LOAD_DIFFOBJ_RESULT();
        ~LOAD_DIFFOBJ_RESULT();

        LOAD_DIFFOBJ_RESULT(const LOAD_DIFFOBJ_RESULT &) = delete;
        LOAD_DIFFOBJ_RESULT &operator=(const LOAD_DIFFOBJ_RESULT &) = delete;
        LOAD_DIFFOBJ_RESULT(LOAD_DIFFOBJ_RESULT &&) noexcept;
        LOAD_DIFFOBJ_RESULT &operator=(LOAD_DIFFOBJ_RESULT &&) noexcept;

        // DifficultyHitObject defined in DifficultyCalculator.h
        std::vector<DifficultyHitObject> diffobjects;

        u32 playableLength{0};
        u32 totalBreakDuration{0};
        LoadError error;

        [[nodiscard]] u32 getTotalMaxCombo() const { return maxComboAtIndex.back(); }
        [[nodiscard]] u32 getMaxComboAtIndex(uSz diffobjIndex) const;

       private:
        friend class DatabaseBeatmap;
        std::vector<u32> maxComboAtIndex{0};
    };

    struct TIMING_INFO {
        i32 offset{0};

        f32 beatLengthBase{0.f};
        f32 beatLength{0.f};

        i32 sampleSet{0};
        i32 sampleIndex{0};
        i32 volume{0};

        bool isNaN{false};

        bool operator==(const TIMING_INFO &) const = default;
    };

    // primitive objects

    struct HITCIRCLE final {
        int x, y;
        i32 time;
        int number;
        int colorCounter;
        int colorOffset;
        bool clicked;
        HitSamples samples;
    };

    struct SLIDER final {
        SLIDER() noexcept;  // needs extern ctors/dtors due to SLIDER_SCORING_TIME being externally defined
        ~SLIDER() noexcept;

        SLIDER(const SLIDER &) noexcept;
        SLIDER &operator=(const SLIDER &) noexcept;
        SLIDER(SLIDER &&) noexcept;
        SLIDER &operator=(SLIDER &&) noexcept;

        int x, y;
        char type;
        int repeat;
        float pixelLength;
        i32 time;
        int number;
        int colorCounter;
        int colorOffset;
        std::vector<vec2> points;
        HitSamples hoverSamples;
        std::vector<HitSamples> edgeSamples;

        float sliderTime;
        float sliderTimeWithoutRepeats;
        std::vector<float> ticks;

        std::vector<SLIDER_SCORING_TIME> scoringTimesForStarCalc;
    };

    struct SPINNER final {
        int x, y;
        i32 time;
        i32 endTime;
        HitSamples samples;
    };

    struct PRIMITIVE_CONTAINER final {
        std::vector<HITCIRCLE> hitcircles{};
        std::vector<SLIDER> sliders{};
        std::vector<SPINNER> spinners{};
        std::vector<BREAK> breaks{};

        FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> timingpoints{};
        std::vector<Color> combocolors{};

        f32 stackLeniency{.7f};
        f32 sliderMultiplier{1.f};
        f32 sliderTickRate{1.f};

        [[nodiscard]] inline u32 getNumObjects() const { return hitcircles.size() + sliders.size() + spinners.size(); }

        u32 totalBreakDuration{0};

        // sample set to use if timing point doesn't specify it
        // 1 = normal, 2 = soft, 3 = drum
        i32 defaultSampleSet{1};

        i32 version{14};
        LoadError error;

        // Set after calculateSliderTimesClicksTicks has populated slider timing data.
        // Allows reuse of the container for multiple loadDifficultyHitObjects calls.
        bool sliderTimesCalculated{false};
    };

#ifndef BUILD_TOOLS_ONLY  // pass data/primitives directly for tools build
    static LOAD_DIFFOBJ_RESULT loadDifficultyHitObjects(std::string_view osuFilePath, float AR, float CS,
                                                        float speedMultiplier, bool calculateStarsInaccurately = false,
                                                        const Sync::stop_token &dead = alwaysFalseStopPred);

    static PRIMITIVE_CONTAINER loadPrimitiveObjects(std::string_view osuFilePath,
                                                    const Sync::stop_token &dead = alwaysFalseStopPred);
#endif

    static LOAD_DIFFOBJ_RESULT loadDifficultyHitObjects(PRIMITIVE_CONTAINER &c, float AR, float CS,
                                                        float speedMultiplier, bool calculateStarsInaccurately,
                                                        const Sync::stop_token &dead = alwaysFalseStopPred);

    static PRIMITIVE_CONTAINER loadPrimitiveObjectsFromData(const std::vector<u8> &fileData,
                                                            std::string_view osuFilePath,
                                                            const Sync::stop_token &dead = alwaysFalseStopPred);
    static LoadError calculateSliderTimesClicksTicks(int beatmapVersion, std::vector<SLIDER> &sliders,
                                                     FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> &timingpoints,
                                                     float sliderMultiplier, float sliderTickRate);
    static LoadError calculateSliderTimesClicksTicks(int beatmapVersion, std::vector<SLIDER> &sliders,
                                                     FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> &timingpoints,
                                                     float sliderMultiplier, float sliderTickRate,
                                                     const Sync::stop_token &dead);

    static TIMING_INFO getTimingInfoForTimeAndTimingPoints(
        i32 positionMS, const FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> &timingpoints);

#ifndef BUILD_TOOLS_ONLY

   public:
    enum class BeatmapType : uint8_t {
        NEOMOD_BEATMAPSET,
        PEPPY_BEATMAPSET,
        NEOMOD_DIFFICULTY,
        PEPPY_DIFFICULTY,
    };

    DatabaseBeatmap();
    ~DatabaseBeatmap();

    DatabaseBeatmap(std::string filePath, std::string folder, BeatmapType type);  // beatmap difficulty
    DatabaseBeatmap(std::unique_ptr<DiffContainer> &&difficulties,
                    BeatmapType type);  // beatmapset

    DatabaseBeatmap(const DatabaseBeatmap &);
    DatabaseBeatmap(DatabaseBeatmap &&) noexcept;
    DatabaseBeatmap &operator=(DatabaseBeatmap other) noexcept {
        swap(*this, other);
        return *this;
    }

    // for difficulties, compares MD5 hash for equality
    // if both are mapsets, recursively compare their contained difficulties' MD5 hashes
    bool operator==(const DatabaseBeatmap &other) const;

    friend void swap(DatabaseBeatmap &a, DatabaseBeatmap &b) noexcept;

    // if we are a beatmapset, update values from difficulties
    void updateRepresentativeValues() noexcept;

    struct LOAD_META_RESULT {
        std::vector<u8> fileData{};
        LoadError error{DatabaseBeatmap::LoadError::NONE};

        explicit operator bool() const { return error.errc != 0; }
    };

    LOAD_META_RESULT loadMetadata(bool compute_md5 = true);

    struct LOAD_GAMEPLAY_RESULT final {
        LOAD_GAMEPLAY_RESULT();
        ~LOAD_GAMEPLAY_RESULT();

        LOAD_GAMEPLAY_RESULT(const LOAD_GAMEPLAY_RESULT &) = delete;
        LOAD_GAMEPLAY_RESULT &operator=(const LOAD_GAMEPLAY_RESULT &) = delete;
        LOAD_GAMEPLAY_RESULT(LOAD_GAMEPLAY_RESULT &&) noexcept;
        LOAD_GAMEPLAY_RESULT &operator=(LOAD_GAMEPLAY_RESULT &&) noexcept;

        std::vector<std::unique_ptr<HitObject>> hitobjects;
        std::vector<BREAK> breaks;
        std::vector<Color> combocolors;

        i32 defaultSampleSet{1};
        LoadError error;
    };

    static LOAD_GAMEPLAY_RESULT loadGameplay(BeatmapDifficulty *databaseBeatmap, AbstractBeatmapInterface *beatmap,
                                             LOAD_META_RESULT preloadedMetadata = {{},
                                                                                   {DatabaseBeatmap::LoadError::NONE}},
                                             PRIMITIVE_CONTAINER *outPrimitivesCopy = nullptr);
    inline LOAD_GAMEPLAY_RESULT loadGameplay(AbstractBeatmapInterface *beatmap,
                                             LOAD_META_RESULT preloadedMetadata = {{},
                                                                                   {DatabaseBeatmap::LoadError::NONE}},
                                             PRIMITIVE_CONTAINER *outPrimitivesCopy = nullptr) {
        return loadGameplay(this, beatmap, std::move(preloadedMetadata), outPrimitivesCopy);
    }

    [[nodiscard]] MapOverrides get_overrides() const;

    inline void setLocalOffset(i16 localOffset) { this->iLocalOffset = localOffset; }
    inline void setOnlineOffset(i16 onlineOffset) { this->iOnlineOffset = onlineOffset; }

    [[nodiscard]] inline const std::string &getFolder() const { return this->sFolder; }
    [[nodiscard]] inline const std::string &getFilePath() const { return this->sFilePath; }

    template <typename T = BeatmapDifficulty>
    [[nodiscard]] inline const std::vector<std::unique_ptr<T>> &getDifficulties() const
        requires(std::is_same_v<std::remove_cv_t<T>, BeatmapDifficulty>)
    {
        static std::vector<std::unique_ptr<T>> empty;
        return this->difficulties == nullptr
                   ? empty
                   : reinterpret_cast<const std::vector<std::unique_ptr<T>> &>(*this->difficulties);
    }

    [[nodiscard]] inline BeatmapSet *getParentSet() const { return this->parentSet; }

    [[nodiscard]] TIMING_INFO getTimingInfoForTime(i32 positionMS) const;

    static bool prefer_cjk_names();

    // raw metadata

    [[nodiscard]] inline int getVersion() const { return this->iVersion; }
    // [[nodiscard]] inline int getGameMode() const { return this->iGameMode; }
    [[nodiscard]] inline int getID() const { return this->iID; }
    [[nodiscard]] inline int getSetID() const { return this->iSetID; }

    [[nodiscard]] inline const std::string &getTitle() const {
        if(!this->bEmptyTitleUnicode && prefer_cjk_names()) {
            return this->sTitleUnicode;
        } else {
            return this->sTitle;
        }
    }
    [[nodiscard]] inline const std::string &getTitleLatin() const { return this->sTitle; }
    [[nodiscard]] inline const std::string &getTitleUnicode() const { return this->sTitleUnicode; }

    [[nodiscard]] inline const std::string &getArtist() const {
        if(!this->bEmptyArtistUnicode && prefer_cjk_names()) {
            return this->sArtistUnicode;
        } else {
            return this->sArtist;
        }
    }
    [[nodiscard]] inline const std::string &getArtistLatin() const { return this->sArtist; }
    [[nodiscard]] inline const std::string &getArtistUnicode() const { return this->sArtistUnicode; }

    [[nodiscard]] inline const std::string &getCreator() const { return this->sCreator; }
    [[nodiscard]] inline const std::string &getDifficultyName() const { return this->sDifficultyName; }
    [[nodiscard]] inline const std::string &getSource() const { return this->sSource; }
    [[nodiscard]] inline const std::string &getTags() const { return this->sTags; }
    [[nodiscard]] inline const std::string &getBackgroundImageFileName() const {
        return this->sBackgroundImageFileName;
    }
    [[nodiscard]] inline const std::string &getAudioFileName() const { return this->sAudioFileName; }

    [[nodiscard]] inline u32 getLengthMS() const { return this->iLengthMS; }
    [[nodiscard]] inline int getPreviewTime() const { return this->iPreviewTime; }

    [[nodiscard]] inline float getAR() const { return this->fAR; }
    [[nodiscard]] inline float getCS() const { return this->fCS; }
    [[nodiscard]] inline float getHP() const { return this->fHP; }
    [[nodiscard]] inline float getOD() const { return this->fOD; }

    [[nodiscard]] inline float getStackLeniency() const { return this->fStackLeniency; }
    [[nodiscard]] inline float getSliderTickRate() const { return this->fSliderTickRate; }
    [[nodiscard]] inline float getSliderMultiplier() const { return this->fSliderMultiplier; }

    [[nodiscard]] inline const FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> &getTimingpoints() const {
        return this->timingpoints;
    }

    using MapFileReadDoneCallback = std::function<void(std::vector<u8>)>;  // == AsyncIOHandler::ReadCallback
    bool getMapFileAsync(MapFileReadDoneCallback data_callback);
    [[nodiscard]] inline std::string getFullSoundFilePath() const { return this->sFolder + this->sAudioFileName; }

    // redundant data
    [[nodiscard]] inline std::string getFullBackgroundImageFilePath() const {
        return this->sFolder + this->sBackgroundImageFileName;
    }

    // precomputed data

    // TODO: return "closest computed" SR for queries while calculating
    // falls back to nomod stars ATM
    [[nodiscard]] f32 getStarRating(u8 idx) const;

    [[nodiscard]] inline f32 getStarsNomod() const { return this->getStarRating(StarPrecalc::NOMOD_1X_INDEX); }

    [[nodiscard]] inline int getMinBPM() const { return this->iMinBPM; }
    [[nodiscard]] inline int getMaxBPM() const { return this->iMaxBPM; }
    [[nodiscard]] inline int getMostCommonBPM() const { return this->iMostCommonBPM; }

    [[nodiscard]] inline int getNumObjects() const {
        return this->iNumCircles + this->iNumSliders + this->iNumSpinners;
    }
    [[nodiscard]] inline int getNumCircles() const { return this->iNumCircles; }
    [[nodiscard]] inline int getNumSliders() const { return this->iNumSliders; }
    [[nodiscard]] inline int getNumSpinners() const { return this->iNumSpinners; }

    [[nodiscard]] inline i32 getLocalOffset() const { return this->iLocalOffset; }
    [[nodiscard]] inline i32 getOnlineOffset() const { return this->iOnlineOffset; }

    inline void writeMD5(const MD5Hash &hash) {
        if(this->md5_init.load(std::memory_order_relaxed) || this->md5_init.load(std::memory_order_acquire)) return;

        this->sMD5Hash = hash;
        this->md5_init.store(true, std::memory_order_release);
    }

    inline const MD5Hash &getMD5() const {
        if(this->md5_init.load(std::memory_order_relaxed) || this->md5_init.load(std::memory_order_acquire))
            return this->sMD5Hash;

        return MD5Hash::sentinel;  // DEADBEEFDEADBEEFDEADBEEFDEADBEEF
    }

   private:
    // may be lazy-computed by loadMetadata, or precomputed and loaded off disk from database
    MD5Hash sMD5Hash;

    // if this is NULL: we are a BeatmapDifficulty, not a BeatmapSet
    // if this is non-NULL: it MUST contain at least 1 entry (a BeatmapSet cannot have 0 difficulties)
    // NOTE: this class has ownership of the individual beatmap difficulties, Database owns the top-level beatmapsets
    std::unique_ptr<DiffContainer> difficulties;

    // this is XOR difficulties, if we are a difficulty, this points to our parent container beatmapset
    BeatmapSet *parentSet{nullptr};

   public:
    FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> timingpoints;  // necessary for main menu anim

    // redundant data (technically contained in metadata, but precomputed anyway)

    std::string sFolder;    // path to folder containing .osu file (e.g. "/path/to/beatmapfolder/")
    std::string sFilePath;  // path to .osu file (e.g. "/path/to/beatmapfolder/beatmap.osu")

   public:
    // raw metadata
    i64 last_modification_time{0};

    std::string sTitle;
    std::string sTitleUnicode;
    std::string sArtist;
    std::string sArtistUnicode;
    std::string sCreator;
    std::string sDifficultyName;  // difficulty name ("Version")
    std::string sSource;          // only used by search
    std::string sTags;            // only used by search
    std::string sBackgroundImageFileName;
    std::string sAudioFileName;

    int iID{0};  // online ID, if uploaded
    u32 iLengthMS{0};

    i16 iLocalOffset{0};
    i16 iOnlineOffset{0};

    int iSetID{-1};  // online set ID, if uploaded
    int iPreviewTime{-1};

    float fAR{5.f};
    float fCS{5.f};
    float fHP{5.f};
    float fOD{5.f};

    float fStackLeniency{.7f};
    float fSliderTickRate{1.f};
    float fSliderMultiplier{1.f};

    // precomputed data (can-run-without-but-nice-to-have data)
    u32 ppv2Version{0};  // necessary for knowing if stars are up to date
    float fStarsNomod{0.f};
    StarPrecalc::SRArray *star_ratings{nullptr};  // points into Database::star_ratings map (stable via unique_ptr)

    int iMinBPM{0};
    int iMaxBPM{0};
    int iMostCommonBPM{0};

    int iNumCircles{0};
    int iNumSliders{0};
    int iNumSpinners{0};

    // custom data (not necessary, not part of the beatmap file, and not precomputed)
    std::atomic<f32> loudness{0.f};

    // cache for SR queries to avoid array lookup and a bunch of conditionals
    mutable f32 last_queried_sr{0.f};
    mutable u8 last_queried_sr_idx{0xFF};

    // this is from metadata but put here for struct layout purposes
    u8 iVersion{128};  // e.g. "osu file format v12" -> 12
    // u8 iGameMode;  // 0 = osu!standard, 1 = Taiko, 2 = Catch the Beat, 3 = osu!mania

    mutable std::atomic<bool> md5_init{false};

    BeatmapType type{BeatmapType::NEOMOD_DIFFICULTY};

    bool bEmptyArtistUnicode{false};
    bool bEmptyTitleUnicode{false};
    bool do_not_store{false};
    bool draw_background{true};

    // class internal data (custom)

    friend class Database;
    friend class BGImageHandler;
};

struct BPMInfo {
    i32 min{0};
    i32 max{0};
    i32 most_common{0};
};

struct BPMTuple {
    i32 bpm;
    double duration;
};

struct DB_TIMINGPOINT;

template <typename T>
BPMInfo getBPM(const T &timing_points, std::vector<BPMTuple> &bpm_buffer)
    requires((std::is_same_v<T, std::vector<DB_TIMINGPOINT>> ||
              std::is_same_v<T, std::vector<DatabaseBeatmap::TIMINGPOINT>>) ||
             (std::is_same_v<T, FixedSizeArray<DB_TIMINGPOINT>> ||
              std::is_same_v<T, FixedSizeArray<DatabaseBeatmap::TIMINGPOINT>>))
{
    if(timing_points.empty()) {
        return {};
    }

    bpm_buffer.clear();  // reuse existing buffer
    bpm_buffer.reserve(timing_points.size());

    double lastTime = timing_points.back().offset;
    for(size_t i = 0; i < timing_points.size(); i++) {
        const auto &t = timing_points[i];
        if(t.offset > lastTime) continue;
        if(t.msPerBeat <= 0.0 || std::isnan(t.msPerBeat)) continue;

        // "osu-stable forced the first control point to start at 0."
        // "This is reproduced here to maintain compatibility around osu!mania scroll speed and song
        // select display."
        double currentTime = (i == 0 ? 0 : t.offset);
        double nextTime = (i == timing_points.size() - 1 ? lastTime : timing_points[i + 1].offset);

        i32 bpm = (i32)std::round(std::min(60000.0 / t.msPerBeat, 9001.0));
        double duration = std::max(nextTime - currentTime, 0.0);

        bool found = false;
        for(auto &tuple : bpm_buffer) {
            if(tuple.bpm == bpm) {
                tuple.duration += duration;
                found = true;
                break;
            }
        }

        if(!found) {
            bpm_buffer.push_back(BPMTuple{
                .bpm = bpm,
                .duration = duration,
            });
        }
    }

    i32 min = 9001;
    i32 max = 0;
    i32 mostCommonBPM = 0;
    double longestDuration = 0;
    for(const auto &tuple : bpm_buffer) {
        if(tuple.bpm > max) max = tuple.bpm;
        if(tuple.bpm < min) min = tuple.bpm;
        if(tuple.duration > longestDuration || (tuple.duration == longestDuration && tuple.bpm > mostCommonBPM)) {
            longestDuration = tuple.duration;
            mostCommonBPM = tuple.bpm;
        }
    }
    if(min > max) min = max;

    return BPMInfo{
        .min = min,
        .max = max,
        .most_common = mostCommonBPM,
    };
}

#else
};

#endif  // BUILD_TOOLS_ONLY
