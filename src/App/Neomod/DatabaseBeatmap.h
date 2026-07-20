#pragma once
// Copyright (c) 2020, PG, All rights reserved.
#if __has_include("config.h")
#include "config.h"
#endif

#include "types.h"
#include "noinclude.h"
#include "Vectors_fwd.h"
#include "FixedSizeArray.h"
#include "DatabaseBeatmapTypes.h"
#include "StrainComputeState.h"

// TODO: make these utilities available without all of these ifdefs (move all diffcalc things to a lightweight separate directory)
#ifndef BUILD_TOOLS_ONLY

#include "StarPrecalc.h"
#include "Overrides.h"
#include "MD5Hash.h"
#include "Color.h"
#include "SyncStoptoken.h"

#else
#include <memory>
#include <stop_token>
namespace Sync {
using std::stop_token;
}

using Color = uint32_t;

#endif

#include <atomic>
#include <string_view>
#include <memory>
#include <functional>

using std::string_view_literals::operator""sv;
using std::string_literals::operator""s;

// purpose:
// 1) contain all infos which are ALWAYS kept in memory for beatmaps
// 2) be the data source for Beatmap when starting a difficulty
// 3) allow async calculations/loaders to work on the contained data (e.g. background image loader)
// 4) be a container for difficulties (all top level DatabaseBeatmap objects are containers)

class AbstractBeatmapInterface;
class HitObject;
namespace neomod::DiffCalc {
class DifficultyHitObject;
}

class Database;

class BGImageHandler;

class DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;
using BeatmapSet = DatabaseBeatmap;
using DiffContainer = std::vector<std::unique_ptr<BeatmapDifficulty>>;

#ifndef BUILD_TOOLS_ONLY
template <typename T>
concept HitObjectContainer = std::is_same_v<T, neomod::DiffCalc::DifficultyHitObject> || std::is_same_v<T, HitObject>;
#else
template <typename T>
concept HitObjectContainer = std::is_same_v<T, neomod::DiffCalc::DifficultyHitObject>;
#endif

namespace DBType = neomod::DatabaseBeatmapTypes;

// DatabaseBeatmap &operator=(DatabaseBeatmap other) already implements these...
// NOLINTNEXTLINE(hicpp-special-member-functions, cppcoreguidelines-special-member-functions)
class DatabaseBeatmap final {
    using DifficultyHitObject = neomod::DiffCalc::DifficultyHitObject;

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

    static const Sync::stop_token alwaysFalseStopPred;

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

        // which parameters the computed (star calc) fields of diffobjects were last fully computed
        // with, invalid on a fresh load. pass to StarCalcParams::strainState to reuse them.
        neomod::DiffCalc::StrainComputeState strainState{};

        u32 playableLength{0};
        u32 totalBreakDuration{0};
        LoadError error;

        [[nodiscard]] u32 getTotalMaxCombo() const { return maxComboAtIndex.back(); }
        [[nodiscard]] u32 getMaxComboAtIndex(uSz diffobjIndex) const;

       private:
        friend class DatabaseBeatmap;
        // starts with a single 0 sentinel so getTotalMaxCombo() works pre-fill
        std::vector<u32> maxComboAtIndex;
    };

    struct PRIMITIVE_CONTAINER final {
        std::vector<DBType::HITCIRCLE> hitcircles{};
        std::vector<DBType::SLIDER> sliders{};
        std::vector<DBType::SPINNER> spinners{};
        std::vector<DBType::BREAK> breaks{};

        FixedSizeArray<DBType::TIMINGPOINT> timingpoints{};
        std::vector<Color> combocolors{};

        f32 stackLeniency{.7f};
        f32 sliderMultiplier{1.f};
        f32 sliderTickRate{1.f};

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

#ifndef BUILD_TOOLS_ONLY  // pass data/primitives directly for tools build
    static LOAD_DIFFOBJ_RESULT loadDifficultyHitObjects(std::string_view osuFilePath, float AR, float CS,
                                                        float speedMultiplier,
                                                        const Sync::stop_token &dead = alwaysFalseStopPred);

    static PRIMITIVE_CONTAINER loadPrimitiveObjects(std::string_view osuFilePath,
                                                    const Sync::stop_token &dead = alwaysFalseStopPred);
#endif

    template <HitObjectContainer C>
    using ObjectGetter = std::function<C *(uSz)>;

    template <HitObjectContainer C>
    static void calculateStacks(const ObjectGetter<C> &getObj, uSz numObjects, float AR, int beatmapVersion,
                                float stackLeniency);

    static LOAD_DIFFOBJ_RESULT loadDifficultyHitObjects(PRIMITIVE_CONTAINER &c, float AR, float CS,
                                                        float speedMultiplier,
                                                        const Sync::stop_token &dead = alwaysFalseStopPred);

    static PRIMITIVE_CONTAINER loadPrimitiveObjectsFromData(const std::vector<u8> &fileData,
                                                            std::string_view osuFilePath,
                                                            const Sync::stop_token &dead = alwaysFalseStopPred);
    static LoadError calculateSliderTimesClicksTicks(int beatmapVersion, std::vector<DBType::SLIDER> &sliders,
                                                     FixedSizeArray<DBType::TIMINGPOINT> &timingpoints,
                                                     float sliderMultiplier, float sliderTickRate);
    static LoadError calculateSliderTimesClicksTicks(int beatmapVersion, std::vector<DBType::SLIDER> &sliders,
                                                     FixedSizeArray<DBType::TIMINGPOINT> &timingpoints,
                                                     float sliderMultiplier, float sliderTickRate,
                                                     const Sync::stop_token &dead);

    static DBType::TIMING_INFO getTimingInfoForTimeAndTimingPoints(
        i32 positionMS, const FixedSizeArray<DBType::TIMINGPOINT> &timingpoints);

#ifndef BUILD_TOOLS_ONLY
    NOCOPY_NOMOVE(DatabaseBeatmap)
   public:
    enum class BeatmapType : uint8_t {
        NEOMOD_BEATMAPSET,
        PEPPY_BEATMAPSET,
        NEOMOD_DIFFICULTY,
        PEPPY_DIFFICULTY,
    };

    DatabaseBeatmap() = delete;
    ~DatabaseBeatmap() = default;

    DatabaseBeatmap(std::string filePath, std::string folder,
                    BeatmapType type);  // beatmap difficulty
    DatabaseBeatmap(std::unique_ptr<DiffContainer> &&difficulties,
                    BeatmapType type);  // beatmapset

    // for difficulties, compares MD5 hash for equality
    // if both are mapsets, recursively compare their contained difficulties' MD5 hashes
    bool operator==(const DatabaseBeatmap &other) const;

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
        std::vector<DBType::BREAK> breaks;
        std::vector<Color> combocolors;

        LoadError error;

        u8 defaultSampleSet{1};
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

    [[nodiscard]] inline std::string_view getFolder() const { return this->sFolder; }
    [[nodiscard]] inline std::string_view getFilePath() const { return this->sFilePath; }

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

    [[nodiscard]] DBType::TIMING_INFO getTimingInfoForTime(i32 positionMS) const;

    static bool prefer_cjk_names();

    // raw metadata

    [[nodiscard]] inline int getVersion() const { return this->iVersion; }
    // [[nodiscard]] inline int getGameMode() const { return this->iGameMode; }
    [[nodiscard]] inline int getID() const { return this->iID; }
    [[nodiscard]] inline int getSetID() const { return this->iSetID; }

    [[nodiscard]] inline std::string_view getTitle() const {
        if(this->has_unicode_title && prefer_cjk_names()) {
            return this->sTitleUnicode;
        } else {
            return this->getTitleLatin();
        }
    }
    [[nodiscard]] inline std::string_view getTitleLatin() const { return this->sTitle; }
    [[nodiscard]] inline std::string_view getTitleUnicode() const { return this->sTitleUnicode; }

    [[nodiscard]] inline std::string_view getArtist() const {
        if(this->has_unicode_artist && prefer_cjk_names()) {
            return this->sArtistUnicode;
        } else {
            return this->getArtistLatin();
        }
    }
    [[nodiscard]] inline std::string_view getArtistLatin() const { return this->sArtist; }
    [[nodiscard]] inline std::string_view getArtistUnicode() const { return this->sArtistUnicode; }

    [[nodiscard]] inline std::string_view getCreator() const { return this->sCreator; }
    [[nodiscard]] inline std::string_view getDifficultyName() const { return this->sDifficultyName; }
    [[nodiscard]] inline std::string_view getSource() const { return this->sSource; }
    [[nodiscard]] inline std::string_view getTags() const { return this->sTags; }
    [[nodiscard]] inline std::string_view getBackgroundImageFileName() const { return this->sBackgroundImageFileName; }
    [[nodiscard]] inline std::string_view getAudioFileName() const { return this->sAudioFileName; }

    [[nodiscard]] inline u32 getLengthMS() const { return this->iLengthMS; }
    [[nodiscard]] inline int getPreviewTime() const { return this->iPreviewTime; }

    [[nodiscard]] inline float getAR() const { return this->fAR; }
    [[nodiscard]] inline float getCS() const { return this->fCS; }
    [[nodiscard]] inline float getHP() const { return this->fHP; }
    [[nodiscard]] inline float getOD() const { return this->fOD; }

    [[nodiscard]] inline float getStackLeniency() const { return this->fStackLeniency; }
    [[nodiscard]] inline float getSliderTickRate() const { return this->fSliderTickRate; }
    [[nodiscard]] inline float getSliderMultiplier() const { return this->fSliderMultiplier; }

    [[nodiscard]] inline const FixedSizeArray<DBType::TIMINGPOINT> &getTimingpoints() const {
        return this->timingpoints;
    }

    using MapFileReadDoneCallback = std::function<void(std::vector<u8>)>;  // == AsyncIOHandler::ReadCallback
    [[nodiscard]] bool getMapFileAsync(MapFileReadDoneCallback data_callback) const;

    [[nodiscard]] std::string getFullSoundFilePath() const;
    [[nodiscard]] std::string getFullBackgroundImageFilePath() const;

    // redundant data

    // precomputed data

    // TODO: return "closest computed" SR for queries while calculating
    // falls back to nomod stars ATM
    [[nodiscard]] f32 getStarRating(u8 idx) const;

    [[nodiscard]] inline f32 getStarsNomod() const { return this->getStarRating(StarPrecalc::NOMOD_1X_INDEX); }

    [[nodiscard]] inline i32 getMinBPM() const { return this->iMinBPM; }
    [[nodiscard]] inline i32 getMaxBPM() const { return this->iMaxBPM; }
    [[nodiscard]] inline i32 getMostCommonBPM() const { return this->iMostCommonBPM; }

    [[nodiscard]] inline i32 getNumObjects() const {
        return this->iNumCircles + this->iNumSliders + this->iNumSpinners;
    }
    [[nodiscard]] inline i32 getNumCircles() const { return this->iNumCircles; }
    [[nodiscard]] inline i32 getNumSliders() const { return this->iNumSliders; }
    [[nodiscard]] inline i32 getNumSpinners() const { return this->iNumSpinners; }

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
    FixedSizeArray<DBType::TIMINGPOINT> timingpoints;  // necessary for main menu anim

    // redundant data (technically contained in metadata, but precomputed anyway)

    std::string sFolder;    // path to folder containing .osu file (e.g. "/path/to/beatmapfolder/")
    std::string sFilePath;  // path to .osu file (e.g. "/path/to/beatmapfolder/beatmap.osu")

   public:
    // raw metadata
    i64 last_modification_time{0};

   private:
    // if there is no unicode representation, they remain NULL
    std::string sTitle;
    std::string sTitleUnicode;
    std::string sArtist;
    std::string sArtistUnicode;

   public:
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
    // points into Database::star_ratings map (stable via unique_ptr)
    StarPrecalc::SRArray *star_ratings{nullptr};

    int iMinBPM{0};
    int iMaxBPM{0};
    int iMostCommonBPM{0};

    u16 iNumCircles{0};
    u16 iNumSliders{0};
    u16 iNumSpinners{0};

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

    bool do_not_store{false};
    bool draw_background{true};

   private:
    bool has_unicode_title{false};
    bool has_unicode_artist{false};

    // class internal data (custom)

    friend class Database;
    friend class BGImageHandler;
};

struct DB_TIMINGPOINT;

namespace neomod::BPMCalc {

struct BPMInfo {
    i32 min{0};
    i32 max{0};
    i32 most_common{0};
};

struct BPMTuple {
    i32 bpm;
    double duration;
};

// defined in DatabaseBeatmap.cpp with explicit instantiations for the constrained set
template <typename T>
BPMInfo getBPM(const T &timing_points, std::vector<BPMTuple> &bpm_buffer)
    requires((std::is_same_v<T, std::vector<DB_TIMINGPOINT>> || std::is_same_v<T, std::vector<DBType::TIMINGPOINT>>) ||
             (std::is_same_v<T, FixedSizeArray<DB_TIMINGPOINT>> ||
              std::is_same_v<T, FixedSizeArray<DBType::TIMINGPOINT>>));
}  // namespace neomod::BPMCalc

#else
};

#endif  // BUILD_TOOLS_ONLY
