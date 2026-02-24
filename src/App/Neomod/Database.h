#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "AsyncCancellable.h"
#include "LegacyReplay.h"
#include "Overrides.h"
#include "UString.h"
#include "score.h"
#include "SyncMutex.h"

#include "Hashing.h"
#include "DiffCalc/StarPrecalc.h"

#include <atomic>
#include <set>

namespace Timing {
class Timer;
}
namespace Collections {
extern bool load_all();
extern bool load_peppy(std::string_view peppy_collections_path);
extern bool load_mcneomod(std::string_view neomod_collections_path);
extern bool save_collections();
}  // namespace Collections
namespace LegacyReplay {
extern bool load_from_disk(FinishedScore &score, bool update_db);
}

namespace BatchDiffCalc {
struct internal;
}

class ScoreButton;
class ConVar;

class DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;
using BeatmapSet = DatabaseBeatmap;

#define NEOMOD_MAPS_DB_VERSION 20260202
#define NEOMOD_SCORE_DB_VERSION 20240725

class Database;
// global for convenience, created in osu constructor, destroyed in osu constructor
extern std::unique_ptr<Database> db;

// Field ordering matters here
#pragma pack(push, 1)
struct alignas(1) DB_TIMINGPOINT {
    double msPerBeat;
    double offset;
    bool uninherited;
};
#pragma pack(pop)

using HashToScoreMap = Hash::flat::map<MD5Hash, std::vector<FinishedScore>>;

// // TODO: we are redundantly storing MD5Hashes for each MD5Hash->{data} map
// // with 150k difficulties, we are potentially redundantly storing them:
// // 1. peppy_overrides
// // 2. scores (local)
// // 3. online_scores
// // 4. star_ratings
// // 5. beatmap_difficulties
// // 6. inside each BeatmapDifficulty itself
// // 150,000 diffs * 32 bytes * 6 duplications = 24MB
// // PLUS: FinishedScore itself stores an MD5Hash of the associated beatmap inside of it! so if we have 50k scores, that's another
// // 50,000 * 32 bytes = 1.6MB
// //
// struct DiffHashExtraData {
//     BeatmapDifficulty *diff{nullptr};                                    // the BeatmapDifficulty this MD5Hash points to
//     std::unique_ptr<std::vector<FinishedScore>> local_scores{nullptr};   // all local scores stored for this diff
//     std::unique_ptr<std::vector<FinishedScore>> online_scores{nullptr};  // all online scores stored for this diff
//     std::unique_ptr<StarPrecalc::SRArray> star_ratings{nullptr};         // stored star ratings
//     std::unique_ptr<MapOverrides> overrides{nullptr};  // stored peppy_overrides (only relevant for PEPPY_DIFFICULTYs)
// };

class Database final {
    NOCOPY_NOMOVE(Database)
   public:
    struct PlayerStats {
        UString name;
        float pp;
        float accuracy;
        int level;
        float percentToNextLevel;
        u64 totalScore;
    };

    struct PlayerPPScores {
        std::vector<FinishedScore *> ppScores;
        u64 totalScore;
    };

    struct SCORE_SORTING_METHOD {
        using SCORE_SORTING_COMPARATOR = bool (*)(const FinishedScore &, const FinishedScore &);

        std::string_view name;
        SCORE_SORTING_COMPARATOR comparator;
    };

    // sorting methods
    static bool sortScoreByScore(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByCombo(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByDate(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByMisses(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByAccuracy(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByPP(const FinishedScore &a, const FinishedScore &b);

   public:
    static constexpr std::array<SCORE_SORTING_METHOD, 6> SCORE_SORTING_METHODS{{{"By accuracy", sortScoreByAccuracy},
                                                                                {"By combo", sortScoreByCombo},
                                                                                {"By date", sortScoreByDate},
                                                                                {"By misses", sortScoreByMisses},
                                                                                {"By score", sortScoreByScore},
                                                                                {"By pp", sortScoreByPP}}};

   public:
    Database();
    ~Database();

    void update();

    void load();
    void cancel();
    void save();

    BeatmapSet *addBeatmapSet(const std::string &beatmapFolderPath, i32 set_id_override = -1, bool is_peppy = false);

    // returns true if adding succeeded
    bool addScore(const FinishedScore &score);
    void deleteScore(const FinishedScore &scoreToDelete);
    static void sortScoresInPlace(std::vector<FinishedScore> &scores);

    PlayerPPScores getPlayerPPScores(const std::string &playerName);
    PlayerStats calculatePlayerStats(const std::string &playerName);
    static float getWeightForIndex(int i);
    static float getBonusPPForNumScores(size_t numScores);
    static u64 getRequiredScoreForLevel(int level);
    static int getLevelForScore(u64 score, int maxLevel = 120);

    [[nodiscard]] inline float getProgress() const { return this->loading_progress.load(std::memory_order_acquire); }
    [[nodiscard]] inline bool isCancelled() const { return this->load_interrupted.load(std::memory_order_acquire); }
    [[nodiscard]] inline bool isLoading() const {
        float progress = this->getProgress();
        return progress > 0.f && progress < 1.f;
    }
    [[nodiscard]] inline bool isFinished() const { return (this->getProgress() >= 1.0f); }
    [[nodiscard]] inline bool foundChanges() const { return this->raw_found_changes; }

    BeatmapDifficulty *getBeatmapDifficulty(const MD5Hash &md5hash);
    BeatmapDifficulty *getBeatmapDifficulty(i32 map_id);
    BeatmapSet *getBeatmapSet(i32 set_id);
    [[nodiscard]] inline const std::vector<std::unique_ptr<BeatmapSet>> &getBeatmapSets() const {
        return this->beatmapsets;
    }

    // WARNING: Before calling getScores(), you need to lock db->scores_mtx!
    [[nodiscard]] inline const HashToScoreMap &getScores() const { return this->scores; }
    inline HashToScoreMap &getOnlineScores() { return this->online_scores; }

    static std::string getOsuSongsFolder();

    // only used for raw loading without db
    std::unique_ptr<BeatmapSet> loadRawBeatmap(const std::string &beatmapPath, bool is_peppy = false);

    inline void addPathToImport(const std::string &dbPath) { this->extern_db_paths_to_import.push_back(dbPath); }

    // locks peppy_overrides mutex and updates overrides for loaded-from-stable-db maps which will be stored in the local database
    void update_overrides(BeatmapDifficulty *diff);

    Sync::shared_mutex peppy_overrides_mtx;
    Sync::shared_mutex scores_mtx;
    std::atomic<bool> scores_changed{true};

    Hash::flat::map<MD5Hash, MapOverrides> peppy_overrides;
    std::vector<BeatmapDifficulty *> loudness_to_calc;

    bool bPendingBatchDiffCalc{false};

    mutable Sync::shared_mutex star_ratings_mtx;
    Hash::flat::map<MD5Hash, std::unique_ptr<StarPrecalc::SRArray>> star_ratings;
    [[nodiscard]] f32 get_star_rating(const MD5Hash &hash, ModFlags flags, f32 speed) const;

    // this copies neosu_maps.db and neosu_scores.db to
    // neomod_ prefixed equivalents, if neomod_*.db equivalents don't already exist
    static bool migrate_neosu_to_neomod();

   private:
    friend bool Collections::load_all();
    friend bool Collections::load_peppy(std::string_view peppy_collections_path);
    friend bool Collections::load_mcneomod(std::string_view neomod_collections_path);
    friend bool Collections::save_collections();
    friend class DatabaseBeatmap;

    void scheduleLoadRaw();

    // for updating scores externally
    friend struct BatchDiffCalc::internal;
    friend class ScoreButton;  // HACKHACK: why are we updating database scores from a BUTTON???
    friend bool LegacyReplay::load_from_disk(FinishedScore &score, bool update_db);
    inline HashToScoreMap &getScoresMutable() { return this->scores; }

    HashToScoreMap scores;
    HashToScoreMap online_scores;

    enum class DatabaseType : u8 {
        INVALID_DB = 0,
        NEOMOD_SCORES = 1,
        MCNEOMOD_SCORES = 2,
        MCNEOMOD_COLLECTIONS = 3,  // mcosu/neomod both use same collection format
        NEOMOD_MAPS = 4,
        STABLE_SCORES = 5,
        STABLE_COLLECTIONS = 6,
        STABLE_MAPS = 7,
        LAST = STABLE_MAPS
    };

    static std::string getDBPath(DatabaseType db_type);
    static DatabaseType getDBType(std::string_view db_path);
    static bool isOsuDBReadable(std::string_view db_path);  // basic check for size and version > 0

    // should only be accessed from database loader thread!
    Hash::flat::map<DatabaseType, std::string> database_files;
    std::set<std::pair<DatabaseType, std::string>> external_databases;

    u64 bytes_processed{0};
    u64 total_bytes{0};
    std::atomic<float> loading_progress{0.f};

    std::vector<std::string> extern_db_paths_to_import;
    // copy so that more can be added without thread races during loading
    std::vector<std::string> extern_db_paths_to_import_async_copy;

    void onDBLoadComplete();

    void startLoader();
    void destroyLoader();

    void saveMaps();

    void findDatabases();
    bool importDatabase(const std::pair<DatabaseType, std::string> &db_pair);
    void loadMaps();
    void loadScores(std::string_view dbPath);
    void loadOldMcNeomodScores(std::string_view dbPath);
    void loadPeppyScores(std::string_view dbPath);
    void saveScores();
    void sortScores(const MD5Hash &beatmapMD5Hash);
    bool addScoreRaw(const FinishedScore &score);
    // returns position of existing score in the scores[hash] array if found, -1 otherwise
    // this isn't completely accurate but allows skipping importing some duplicate entries early from dbs
    int isScoreAlreadyInDB(const MD5Hash &map_hash, u64 unix_timestamp, const std::string &playerName);

    static MD5Hash recalcMD5(std::string osu_path);

    Async::CancellableHandle<void> db_load_handle;
    Async::Future<void> score_save_future;

    std::unique_ptr<Timing::Timer> importTimer;
    bool is_first_load{true};      // only load differences after first raw load
    bool raw_found_changes{true};  // for total refresh detection of raw loading

    // global
    u32 num_beatmaps_to_load{0};
    std::atomic<bool> load_interrupted{false};
    // this vector owns all loaded beatmapsets, raw beatmapset pointers are assumed not ownable
    std::vector<std::unique_ptr<BeatmapSet>> beatmapsets;
    std::vector<std::unique_ptr<BeatmapSet>>
        temp_loading_beatmapsets;  // staging buffer for async loadMaps() thread only; moved into beatmapsets when async load finishes

    Sync::shared_mutex beatmap_difficulties_mtx;
    Hash::flat::map<MD5Hash, BeatmapDifficulty *> beatmap_difficulties;

    bool neomod_maps_loaded{false};

    // scores.db (legacy and custom)
    bool scores_loaded{false};

    PlayerStats prevPlayerStats{
        .name = "",
        .pp = 0.0f,
        .accuracy = 0.0f,
        .level = 0,
        .percentToNextLevel = 0.0f,
        .totalScore = 0,
    };

    std::string raw_load_osu_song_folder;
    std::vector<std::string> raw_loaded_beatmap_folders;
    std::vector<std::string> raw_load_beatmap_folders;

    // raw load
    u32 cur_raw_load_idx{0};
    bool needs_raw_load{false};
    bool raw_load_scheduled{false};
    bool raw_load_is_neomod{
        false};  // if we're raw loading from the local neomod folder instead of the osu!stable song folder
};
