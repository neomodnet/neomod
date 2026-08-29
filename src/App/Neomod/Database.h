#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "AsyncCancellable.h"
#include "LegacyReplay.h"
#include "Overrides.h"
#include "SyncMutex.h"

#include "Hashing.h"
#include "DiffCalc/StarPrecalc.h"

#include <atomic>
#include <set>
#include <span>

namespace Timing {
class Timer;
}
namespace Collections {
class Collection;

extern bool load_peppy(std::string_view peppy_collections_path);
extern bool load_mcneomod(std::string_view neomod_collections_path);
extern bool save_collections();
extern bool save_collections(std::span<const Collection> collections, std::string_view save_path);
extern void save_collections_async();
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
using DiffContainer = std::vector<std::unique_ptr<BeatmapDifficulty>>;

// what Database::reconcileFolder did to one set folder
struct ReconcileResult {
    enum class Outcome : u8 { Unchanged, Created, Updated, Removed, Failed };
    Outcome outcome{Outcome::Unchanged};
    BeatmapSet *set{nullptr};          // live set for the folder afterwards (nullptr: Removed/Failed/no unique diffs)
    BeatmapSet *replaced{nullptr};     // tombstoned predecessor (Updated/Removed), valid until the next load()
    BeatmapSet *dedup_owner{nullptr};  // set owning the first duplicate diff seen (where "already installed" lives)
    u16 added{0}, removed{0}, parsed{0};
    [[nodiscard]] std::string_view outcomeName() const;
};

#define NEOMOD_MAPS_DB_VERSION 20260829
#define NEOMOD_SCORE_DB_VERSION 20240725

class Database;
// global for convenience, created in osu constructor, destroyed in osu constructor
extern Database *db;

// Field ordering matters here
#pragma pack(push, 1)
struct alignas(1) DB_TIMINGPOINT {
    double msPerBeat;
    double offset;
    bool uninherited;
};
#pragma pack(pop)

using HashToScoreMap = Hash::flat::map<MD5Hash, std::vector<FinishedScore>>;

class Database final {
    NOCOPY_NOMOVE(Database)
   public:
    struct PlayerStats {
        std::string name;
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

    // sorting methods
    static bool sortScoreByScore(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByCombo(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByDate(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByMisses(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByAccuracy(const FinishedScore &a, const FinishedScore &b);
    static bool sortScoreByPP(const FinishedScore &a, const FinishedScore &b);

   public:
    Database();
    ~Database();

    void update();

    // full_rescan: stat every .osu of every maps/ folder instead of trusting unchanged folder mtimes (F5)
    void load(bool full_rescan = false);
    void cancel();
    void save();

    // where set folders live: the maps/ drop-zone, or the osu!stable songs folder when it's loaded raw
    // (no osu!.db as its source)
    enum class MapRoot : u8 { Neomod, Peppy };

    enum class ReconcileMode : u8 {
        TrustFolderMtime,  // startup: an unchanged folder mtime means no per-file io at all
        PerFile,           // F5, watcher, installer: check every .osu
    };
    // syncs <root>/<rel_folder>/ against the db (osu!stable's F5, for one folder): new .osu files are parsed,
    // changed ones re-parsed, vanished ones dropped, everything else is kept as-is. preparsed stands in for the
    // listing and the parsing: the folder's diffs as parseFolderDiffs returns them (so a worker thread can do
    // that part); those are matched by content, never by mtime. set_id_override > 0 is stamped onto the result
    // (downloads know their id even if the .osu files don't). dir_mtime is the folder's mtime as the caller's
    // listing of the root reported it, 0 to stat it here. loader thread during load(), main thread afterwards,
    // never both.
    // limits: folder mtimes move on entry add/remove/rename but not on in-place rewrites (PerFile catches those),
    // and without preparsed a rewrite within the same second as the recorded mtime is missed (same as stable)
    ReconcileResult reconcileFolder(MapRoot root, std::string_view rel_folder, ReconcileMode mode, i32 set_id_override,
                                    std::unique_ptr<DiffContainer> preparsed, i64 dir_mtime = 0);

    // returns true if adding succeeded
    bool addScore(const FinishedScore &score);
    void deleteScore(const FinishedScore &scoreToDelete);
    static void sortScoresInPlace(std::vector<FinishedScore> &scores);

    PlayerPPScores getPlayerPPScores(std::string_view playerName);
    PlayerStats calculatePlayerStats(std::string_view playerName);
    std::vector<std::string> getPlayerNamesWithScoresForUserSwitcher();
    static float getWeightForIndex(uSz i);
    static float getBonusPPForNumScores(size_t numScores);
    static u64 getRequiredScoreForLevel(int level);
    static int getLevelForScore(u64 score, int maxLevel = 120);

    // the percentage is byte-based over the database files; the loader's passes over the set folders that
    // follow (loose .osz extraction, then the folder reconcile) count items instead
    enum class LoadStage : u8 { ReadingDatabases, ImportingOsz, ScanningFolders };
    [[nodiscard]] inline float getProgress() const { return this->loading_progress.load(std::memory_order_acquire); }
    [[nodiscard]] inline LoadStage getLoadStage() const { return this->load_stage.load(std::memory_order_acquire); }
    // per-item counters of the current stage for the loading overlay; total stays 0 while there's nothing to count
    [[nodiscard]] inline u32 getStageDone() const { return this->stage_done.load(std::memory_order_acquire); }
    [[nodiscard]] inline u32 getStageTotal() const { return this->stage_total.load(std::memory_order_acquire); }
    [[nodiscard]] inline bool isCancelled() const { return this->load_interrupted.load(std::memory_order_acquire); }
    [[nodiscard]] inline bool isLoading() const {
        float progress = this->getProgress();
        return progress > 0.f && progress < 1.f;
    }
    [[nodiscard]] inline bool isFinished() const { return (this->getProgress() >= 1.0f); }

    BeatmapDifficulty *getBeatmapDifficulty(const MD5Hash &md5hash) const;
    BeatmapDifficulty *getBeatmapDifficulty(i32 map_id) const;
    // nullptr for set_id <= 0 (unsubmitted sets have no usable id); prefers a maps/ set over an osu!stable one
    BeatmapSet *getBeatmapSet(i32 set_id) const;
    // relative maps/ folder of the set with this id ("" if none). unlike the pointer-returning getters this is
    // safe to call from worker threads (copy under the shared lock, no loading gate)
    std::string getBeatmapSetFolder(i32 set_id) const;
    // stamps a (newly learned) online set id onto a map's set (or the set itself) and all of its difficulties,
    // keeping the id index in sync
    void updateSetID(DatabaseBeatmap *map, i32 new_id);
    [[nodiscard]] inline const std::vector<std::unique_ptr<BeatmapSet>> &getBeatmapSets() const {
        return this->beatmapsets;
    }

    // WARNING: Before calling getScores(), you need to lock db->scores_mtx!
    [[nodiscard]] inline const HashToScoreMap &getScores() const { return this->scores; }
    inline HashToScoreMap &getOnlineScores() { return this->online_scores; }

    static std::string getOsuSongsFolder();

    // only used for raw loading without db
    static std::unique_ptr<BeatmapSet> loadRawBeatmap(std::string_view beatmapPath, bool is_peppy = false);
    // parses every .osu directly inside a set folder (what loadRawBeatmap, the maps/ rescan and the installer's
    // extraction worker build sets from); safe on any thread
    static std::unique_ptr<DiffContainer> parseFolderDiffs(std::string_view folder_path, bool is_peppy);

    void addPathToImport(std::string_view dbPath);

    // locks peppy_overrides mutex and updates overrides for loaded-from-stable-db maps which will be stored in the local database
    void update_overrides(const BeatmapDifficulty *diff);

    Sync::shared_mutex peppy_overrides_mtx;
    Sync::shared_mutex scores_mtx;
    std::atomic<bool> scores_changed{true};

    Hash::flat::map<MD5Hash, MapOverrides> peppy_overrides;
    std::vector<BeatmapDifficulty *> loudness_to_calc;

    bool batch_diffcalc_pending{false};

    mutable Sync::shared_mutex star_ratings_mtx;
    Hash::flat::map<MD5Hash, std::unique_ptr<StarPrecalc::SRArray>> star_ratings;
    [[nodiscard]] f32 get_star_rating(const MD5Hash &hash, ModFlags flags, f32 speed) const;

    // this copies neosu_maps.db and neosu_scores.db to
    // neomod_ prefixed equivalents, if neomod_*.db equivalents don't already exist
    static bool migrate_neosu_to_neomod();

   private:
    friend bool Collections::load_peppy(std::string_view peppy_collections_path);
    friend bool Collections::load_mcneomod(std::string_view neomod_collections_path);
    friend bool Collections::save_collections();
    friend bool Collections::save_collections(std::span<const Collections::Collection> collections,
                                              std::string_view save_path);
    friend void Collections::save_collections_async();
    friend class DatabaseBeatmap;

    // diffs a root's folders against its records: parses unknown folders (in parallel), drops vanished ones and
    // reconciles the known ones (all of them when per_file, otherwise only those whose folder mtime moved).
    // returns how many sets were created
    u32 reconcileRoot(MapRoot root, const Sync::stop_token &tok, bool per_file);
    [[nodiscard]] std::string rootPath(MapRoot root) const;  // with trailing slash

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

    // written by the loader thread (importLooseOsz, reconcileRoot), read by the main-thread overlay
    std::atomic<LoadStage> load_stage{LoadStage::ReadingDatabases};
    std::atomic<u32> stage_done{0};
    std::atomic<u32> stage_total{0};

    std::vector<std::string> extern_db_paths_to_import;
    // copy so that more can be added without thread races during loading
    std::vector<std::string> extern_db_paths_to_import_async_copy;

    void onDBLoadComplete();

    void startLoader();
    void destroyLoader();

    void saveMaps();

    void findDatabases();
    bool importDatabase(const std::pair<DatabaseType, std::string> &db_pair);
    void loadMaps(std::string_view neomod_maps_path, std::string_view peppy_db_path);
    // extract + import loose .osz files from the maps/ drop-zone during the loader's run (before buttons build)
    void importLooseOsz();
    void loadScores(std::string_view dbPath);
    void loadOldMcNeomodScores(std::string_view dbPath);
    void loadPeppyScores(std::string_view dbPath);
    void saveScores();
    void sortScores(const MD5Hash &beatmapMD5Hash);
    bool addScoreRaw(const FinishedScore &score);
    // returns position of existing score in the scores[hash] array if found, -1 otherwise
    // this isn't completely accurate but allows skipping importing some duplicate entries early from dbs
    int isScoreAlreadyInDB(const MD5Hash &map_hash, i64 unix_timestamp, const std::string &playerName);

    static MD5Hash recalcMD5(std::string osu_path);

    Async::CancellableHandle<void> db_load_handle;
    Async::Future<void> score_save_future;

    std::unique_ptr<Timing::Timer> importTimer;

    std::atomic<bool> load_interrupted{false};
    // this vector owns all loaded beatmapsets, raw beatmapset pointers are assumed not ownable
    std::vector<std::unique_ptr<BeatmapSet>> beatmapsets;

    // guards beatmap_difficulties, neomod_folders, sets_by_id and the destruction of beatmapsets/graveyard elements
    mutable Sync::shared_mutex beatmap_difficulties_mtx;
    Hash::flat::map<MD5Hash, BeatmapDifficulty *> beatmap_difficulties;

    struct FolderRecord {
        i64 mtime{0};  // st_mtime of the set folder when it was last verified, 0 = never
        // nullptr = known folder without unique diffs (recorded so it isn't re-parsed every load)
        BeatmapSet *set{nullptr};
    };
    // a set is identified by its folder (any name); the online set id is just metadata.
    // key: folder name relative to the root, no trailing slash. peppy_folders only exists while the osu!stable
    // songs folder is loaded raw (osu!.db, when readable, is authoritative for it and isn't persisted here)
    Hash::unstable_stringmap<FolderRecord> neomod_folders;
    Hash::unstable_stringmap<FolderRecord> peppy_folders;
    [[nodiscard]] Hash::unstable_stringmap<FolderRecord> &folderIndex(MapRoot root) {
        return root == MapRoot::Neomod ? this->neomod_folders : this->peppy_folders;
    }
    // online set id -> sets with that id (maps/ and osu!stable), ids > 0 only, in registration order
    Hash::flat::map<i32, std::vector<BeatmapSet *>> sets_by_id;
    // sets replaced/removed at runtime: kept alive so raw pointers held by calc queues/scores/screens stay valid,
    // freed by the next startLoader()
    std::vector<std::unique_ptr<BeatmapSet>> graveyard;

    // sets_by_id registration, caller holds the beatmap_difficulties_mtx unique lock.
    // unindexSet returns whether the set was indexed
    void indexSet(BeatmapSet *set);
    bool unindexSet(BeatmapSet *set);

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

    // the osu!stable songs folder is loaded from its folders (like maps/) when osu!.db isn't the source for it
    bool needs_raw_load{false};
    std::string peppy_root;   // that folder, captured on the main thread for the loader
    bool full_rescan{false};  // set by load(), consumed by the loader
    u32 rescan_created{0};    // sets created by the last load's reconcile passes
};
