// Copyright (c) 2016, PG, All rights reserved.
#include "Database.h"

#include "AsyncIOHandler.h"
#include "Bancho.h"
#include "ConVarHandler.h"
#include "ContainerRanges.h"
#include "Parsing.h"
#include "SString.h"
#include "MD5Hash.h"
#include "ByteBufferedFile.h"
#include "Collections.h"
#include "OsuConVars.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "DirectoryWatcher.h"
#include "BeatmapInstaller.h"
#include "Engine.h"
#include "File.h"
#include "LegacyReplay.h"
#include "NotificationOverlay.h"
#include "AsyncPool.h"
#include "AsyncPPCalculator.h"
#include "SongBrowser/VolNormalization.h"
#include "DiffCalc/BatchDiffCalc.h"
#include "SongBrowser/SongBrowser.h"
#include "Timing.h"
#include "UI.h"
#include "Logging.h"
#include "crypto.h"
#include "score.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"
#include "Hashing.h"
#include "i18n.h"

#include "fmt/chrono.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>
#include <variant>

using namespace neomod;

Database *db{nullptr};

bool Database::sortScoreByScore(const FinishedScore &a, const FinishedScore &b) {
    if(a.score != b.score) return a.score > b.score;
    if(a.unix_timestamp != b.unix_timestamp) return a.unix_timestamp > b.unix_timestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByCombo(const FinishedScore &a, const FinishedScore &b) {
    if(a.comboMax != b.comboMax) return a.comboMax > b.comboMax;
    if(a.score != b.score) return a.score > b.score;
    if(a.unix_timestamp != b.unix_timestamp) return a.unix_timestamp > b.unix_timestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByDate(const FinishedScore &a, const FinishedScore &b) {
    if(a.unix_timestamp != b.unix_timestamp) return a.unix_timestamp > b.unix_timestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByMisses(const FinishedScore &a, const FinishedScore &b) {
    if(a.numMisses != b.numMisses) return a.numMisses < b.numMisses;
    if(a.score != b.score) return a.score > b.score;
    if(a.unix_timestamp != b.unix_timestamp) return a.unix_timestamp > b.unix_timestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByAccuracy(const FinishedScore &a, const FinishedScore &b) {
    auto a_acc = LiveScore::calculateAccuracy(a.num300s, a.num100s, a.num50s, a.numMisses);
    auto b_acc = LiveScore::calculateAccuracy(b.num300s, b.num100s, b.num50s, b.numMisses);
    if(a_acc != b_acc) return a_acc > b_acc;
    if(a.score != b.score) return a.score > b.score;
    if(a.unix_timestamp != b.unix_timestamp) return a.unix_timestamp > b.unix_timestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByPP(const FinishedScore &a, const FinishedScore &b) {
    auto a_pp = std::max(a.get_pp() * 1000.0, 0.0);
    auto b_pp = std::max(b.get_pp() * 1000.0, 0.0);
    if(a_pp != b_pp) return a_pp > b_pp;
    if(a.score != b.score) return a.score > b.score;
    if(a.unix_timestamp != b.unix_timestamp) return a.unix_timestamp > b.unix_timestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

f32 Database::get_star_rating(const MD5Hash &hash, ModFlags flags, f32 speed) const {
    if(uSz idx = StarPrecalc::index_of(flags, speed); idx != StarPrecalc::INVALID_MODCOMBO) {
        Sync::shared_lock lk(this->star_ratings_mtx);
        if(const auto &it = this->star_ratings.find(hash); it != this->star_ratings.end()) {
            return (*it->second)[idx];
        }
    }
    return 0.f;
}

// static helper
std::string Database::getDBPath(DatabaseType db_type) {
    static_assert(DatabaseType::LAST == DatabaseType::STABLE_MAPS, "add missing case to getDBPath");
    using enum DatabaseType;

    switch(db_type) {
        case INVALID_DB: {
            engine->showMessageError("Database Error",
                                     fmt::format("Invalid database type {}", static_cast<u8>(db_type)));
            return {""};
        }
        case NEOMOD_SCORES:
            return NEOMOD_DB_DIR PACKAGE_NAME "_scores.db";
        case MCNEOMOD_SCORES:
            return NEOMOD_DB_DIR "scores.db";
        case MCNEOMOD_COLLECTIONS:
            return NEOMOD_DB_DIR "collections.db";
        case NEOMOD_MAPS:
            return NEOMOD_DB_DIR PACKAGE_NAME "_maps.db";
        case STABLE_SCORES:
        case STABLE_COLLECTIONS:
        case STABLE_MAPS: {
            std::string osu_folder = cv::osu_folder.getString();
            assert(!osu_folder.empty() && "Database::getDBPath: cv::osu_folder was empty");
            if(osu_folder.back() != '/' && osu_folder.back() != '\\') osu_folder.push_back('/');
            switch(db_type) {
                case STABLE_SCORES:
                    return fmt::format("{}scores.db", osu_folder);
                case STABLE_COLLECTIONS:
                    // note the missing plural...
                    return fmt::format("{}collection.db", osu_folder);
                case STABLE_MAPS:
                    return fmt::format("{}osu!.db", osu_folder);
                default:
                    std::unreachable();
            }
        }
    }
    std::unreachable();
}

// static helper (for figuring out the type of external databases to be imported)
Database::DatabaseType Database::getDBType(std::string_view db_path) {
    std::string db_name = Environment::getFileNameFromFilePath(db_path);

    using enum DatabaseType;
    if(db_name == "collection.db") {
        // osu! collections
        return STABLE_COLLECTIONS;
    }
    if(db_name == "collections.db") {
        // mcosu/neomod collections
        return MCNEOMOD_COLLECTIONS;
    }
    if(db_name == PACKAGE_NAME "_scores.db"sv || db_name == "neosu_scores.db"sv) {
        // neomod!
        return NEOMOD_SCORES;
    }

    if(db_name == "scores.db") {
        ByteBufferedFile::Reader score_db{db_path};
        u32 db_version = score_db.read<u32>();
        if(!score_db.good() || db_version == 0) {
            return INVALID_DB;
        }

        if(db_version == 20210106 || db_version == 20210108 || db_version == 20210110) {
            // McOsu 100%!
            return MCNEOMOD_SCORES;
        } else {
            // We need to do some heuristics to detect whether this is an old neomod or a peppy database.
            MD5Hash dummy_md5;
            u32 nb_beatmaps = score_db.read<u32>();
            for(uSz i = 0; i < nb_beatmaps; i++) {
                (void)score_db.read_hash_chars(dummy_md5);  // TODO: validate
                u32 nb_scores = score_db.read<u32>();
                for(u32 j = 0; j < nb_scores; j++) {
                    /* u8 gamemode = */ score_db.skip<u8>();         // could check for 0xA9, but better method below
                    /* u32 score_version = */ score_db.skip<u32>();  // useless

                    // Here, neomod stores an int64 timestamp. First 32 bits should be 0 (until 2106).
                    // Meanwhile, peppy stores the beatmap hash, which will NEVER be 0, since
                    // it is stored as a string, which starts with an uleb128 (its length).
                    u32 timestamp_check = score_db.read<u32>();
                    if(timestamp_check == 0) {
                        // neomod 100%!
                        return MCNEOMOD_SCORES;
                    } else {
                        // peppy 100%!
                        return STABLE_SCORES;
                    }

                    // unreachable
                }
            }

            // 0 maps or 0 scores
            return INVALID_DB;
        }
    }

    return INVALID_DB;
}

bool Database::isOsuDBReadable(std::string_view peppy_db_path) {
    if(!Environment::fileExists(peppy_db_path)) return false;

    ByteBufferedFile::Reader dbr(peppy_db_path);
    u32 osu_db_version = (dbr.good() && dbr.get_total_size() > 0) ? dbr.read<u32>() : 0;
    return osu_db_version > 0;
}

void Database::onDBLoadComplete() {
    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "(onDBLoadComplete) start");

    // signal that we are done
    this->loading_progress = 1.0f;

    if(this->full_rescan) {
        ui->getNotificationOverlay()->addNotification(
            this->rescan_created > 0 ? tformat("Added {:d} new beatmapset(s).", this->rescan_created)
                                     : std::string{_("No new beatmaps detected.")},
            0xff00ff00);
    }

    // will find maps/scores needing recalc dynamically
    BatchDiffCalc::start_calc();
    VolNormalization::start_calc(this->loudness_to_calc);

    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "(onDBLoadComplete) done");
}

void Database::startLoader() {
    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "start");
    this->destroyLoader();

    this->peppy_root = Database::getOsuSongsFolder();
    this->needs_raw_load = Environment::directoryExists(this->peppy_root) &&
                           (!cv::database_enabled.getBool() || !isOsuDBReadable(getDBPath(DatabaseType::STABLE_MAPS)));
    this->rescan_created = 0;

    this->load_stage.store(LoadStage::ReadingDatabases, std::memory_order_release);
    this->stage_done.store(0, std::memory_order_release);
    this->stage_total.store(0, std::memory_order_release);

    this->loudness_to_calc.clear();
    {
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
        this->beatmap_difficulties.clear();
        this->neomod_folders.clear();
        this->peppy_folders.clear();
        this->sets_by_id.clear();
        this->beatmapsets.clear();
        this->graveyard.clear();
    }

    // append, the copy will only be cleared if loading them succeeded
    Mc::ranges::append(this->extern_db_paths_to_import_async_copy, std::move(this->extern_db_paths_to_import));
    this->extern_db_paths_to_import.clear();

    // reset after destroyLoader() set it to true (subroutines still check it)
    this->load_interrupted.store(false, std::memory_order_release);

    this->db_load_handle = Async::submit_cancellable(
        [this](const Sync::stop_token &tok) {
            logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "(db loader async) start");
            bool cancelled = true;

            Timer load_timer;
            load_timer.start();

            this->findDatabases();

            if(tok.stop_requested()) goto done;

            {
                using enum Database::DatabaseType;
                this->loadScores(this->database_files[NEOMOD_SCORES]);
                if(tok.stop_requested()) goto done;
                this->loadOldMcNeomodScores(this->database_files[MCNEOMOD_SCORES]);
                if(tok.stop_requested()) goto done;
                this->loadPeppyScores(this->database_files[STABLE_SCORES]);
                this->scores_loaded = true;
                if(tok.stop_requested()) goto done;
                this->loadMaps(this->database_files[NEOMOD_MAPS], this->database_files[STABLE_MAPS]);
                if(tok.stop_requested()) goto done;

                // extract + import any loose .osz files dropped into maps/ before handing off to the
                // song browser, so they're part of the initial listing.
                this->importLooseOsz(tok);
                if(tok.stop_requested()) goto done;

                // pick up whatever is on disk that the db doesn't know about yet (and drop what's gone)
                this->rescan_created = this->reconcileRoot(MapRoot::Neomod, tok, this->full_rescan);
                if(tok.stop_requested()) goto done;
                // osu!stable's Songs/ is only walked when osu!.db isn't the source for it; with a readable
                // osu!.db its contents are taken as-is (never a folder scan)
                if(this->needs_raw_load) {
                    this->rescan_created += this->reconcileRoot(MapRoot::Peppy, tok, this->full_rescan);
                    if(tok.stop_requested()) goto done;
                }

                Collections::load_all(this->database_files[MCNEOMOD_COLLECTIONS],
                                      this->database_files[STABLE_COLLECTIONS]);
                if(tok.stop_requested()) goto done;
            }

            // .db files that were dropped on the main window
            for(const auto &db_pair : this->external_databases) {
                this->importDatabase(db_pair);
                if(tok.stop_requested()) goto done;
            }
            // only clear this after we have actually loaded them
            this->extern_db_paths_to_import_async_copy.clear();
            this->external_databases.clear();
            cancelled = false;
        done:
            load_timer.update();
            debugLog("{:s}loading everything took {:f} seconds", cancelled ? "(cancelled) " : "",
                     load_timer.getElapsedTime());
            logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "(db loader async) done");
        },
        Lane::Background);

    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "done");
}

void Database::destroyLoader() {
    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "start");

    // stop threads that rely on database content
    BatchDiffCalc::abort_calc();
    AsyncPPC::set_map(nullptr);
    VolNormalization::abort();
    // queued priority requests hold raw DatabaseBeatmap* pointers; drop them before the
    // beatmap_difficulties wipe in startLoader makes them dangle.
    VolNormalization::flush_priority();

    directoryWatcher->stop_watching(NEOMOD_MAPS_PATH "/");
    this->load_interrupted.store(true, std::memory_order_release);  // for subroutines (loadMaps, etc.)
    this->db_load_handle.cancel();
    if(this->db_load_handle.valid()) this->db_load_handle.wait();
    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "done");
}

bool Database::migrate_neosu_to_neomod() {
    bool scores_migrated = false;
    bool maps_migrated = false;

    {
        const std::string neomod_scores_path = getDBPath(DatabaseType::NEOMOD_SCORES);
        const std::string_view old_neosu_scores_path = NEOMOD_DB_DIR "neosu_scores.db";

        // migrate scores
        if(!Environment::fileExists(neomod_scores_path) && Environment::fileExists(old_neosu_scores_path)) {
            if(!File::copy(old_neosu_scores_path, neomod_scores_path)) {
                debugLog("WARNING: score database migration {}->{} failed!", old_neosu_scores_path, neomod_scores_path);
            } else {
                scores_migrated = true;
            }
        }
    }
    {
        const std::string neomod_maps_path = getDBPath(DatabaseType::NEOMOD_MAPS);
        const std::string_view old_neosu_maps_path = NEOMOD_DB_DIR "neosu_maps.db";

        // migrate maps
        if(!Environment::fileExists(neomod_maps_path) && Environment::fileExists(old_neosu_maps_path)) {
            if(!File::copy(old_neosu_maps_path, neomod_maps_path)) {
                debugLog("WARNING: map database migration {}->{} failed!", old_neosu_maps_path, neomod_maps_path);
            } else {
                maps_migrated = true;
            }
        }
    }

    // TODO?: result unused, could maybe cache it in config or something to avoid the stat syscall
    return scores_migrated || maps_migrated;
}

Database::Database() {
    assert(!db);
    db = this;  // set global db pointer (we are a single instance owned by Osu)
    // convar callback
    cv::cmd::save.setCallback(SA::MakeDelegate<&Database::save>(this));
}

Database::~Database() {
    cv::cmd::save.removeCallback();
    this->destroyLoader();
    if(this->score_save_future.valid()) this->score_save_future.wait();

    BatchDiffCalc::abort_calc();
    AsyncPPC::set_map(nullptr);
    VolNormalization::abort();
    this->loudness_to_calc.clear();

    {
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
        this->beatmap_difficulties.clear();
        this->neomod_folders.clear();
        this->peppy_folders.clear();
        this->sets_by_id.clear();
        this->beatmapsets.clear();
        this->graveyard.clear();
    }

    Collections::unload_all();
}

void Database::update() {
    // check if async db load finished
    if(this->db_load_handle.valid() && this->db_load_handle.is_ready()) {
        this->db_load_handle.get();
        this->onDBLoadComplete();
    }
}

void Database::load(bool full_rescan) {
    this->full_rescan = full_rescan;
    this->load_interrupted = false;
    this->loading_progress = 0.0f;

    this->startLoader();
}

void Database::cancel() {
    // block on async load to cancel if it's in progress
    this->destroyLoader();

    this->loading_progress = 1.0f;  // force finished
}

void Database::save() {
    Collections::save_collections();
    this->saveMaps();
    this->saveScores();
}

// uses BeatmapInstaller's static .osz read + extract helper; kept as a separate loader-stage import
// because it runs before the installer and song browser exist.
void Database::importLooseOsz(const Sync::stop_token &tok) {
    // extract any loose .osz files sitting in the maps/ drop-zone (dropped in while the game was closed).
    // runs on the loader thread right before reconcileRoot, which then imports the extracted folders
    // like any other unknown folder, so these maps are part of the initial listing.

    std::vector<std::string> oszs;
    for(auto &file : env->getFilesInFolder(NEOMOD_MAPS_PATH "/")) {
        if(file.size() > 4 && SString::strcase_equal(std::string_view{file.data() + (file.size() - 4), 4}, ".osz"))
            oszs.push_back(std::move(file));
    }
    if(oszs.empty()) return;

    debugLog("Importing {:d} loose .osz file(s) from " NEOMOD_MAPS_PATH "/", oszs.size());

    // the .osz aren't in loadMaps' byte budget, so the percentage stays pinned near 0.99 throughout
    // this loop; the import count surfaced via getStageDone()/getStageTotal() is the user's actual
    // progress signal.
    this->load_stage.store(LoadStage::ImportingOsz, std::memory_order_release);
    this->stage_total.store(static_cast<u32>(oszs.size()), std::memory_order_release);

    // extraction (read + decompress + write) is independent per file, so fan it out across the async pool.
    // a bounded sliding window keeps at most window_size extractions in flight, so a huge drop can't pile
    // up extracted folders on disk faster than this loop deletes the source .osz files.
    // (this loader is itself a pool task, but blocking on these sub-tasks is deadlock-free: the pool has
    // >= 2 threads, fg threads work-steal bg tasks, and read_and_extract_osz never re-enters the pool.)
    const uSz window_size = std::min<uSz>(std::clamp<uSz>(Async::get_thread_count(), 4, 16), oszs.size());

    auto submit_extract = [](const std::string &osz_name) {
        return Async::submit(
            [path = NEOMOD_MAPS_PATH "/" + osz_name]() -> std::string {
                return BeatmapInstaller::read_and_extract_osz(path);
            },
            Lane::Background);
    };

    std::vector<Async::Future<std::string>> window(window_size);
    for(uSz i = 0; i < window_size; i++) window[i] = submit_extract(oszs[i]);

    for(uSz i = 0; i < oszs.size(); i++) {
        if(tok.stop_requested()) return;  // abandons in-flight extracts; harmless, re-imported next run

        // window[i % window_size] holds oszs[i]'s extraction (primed above, then refilled in order)
        const std::string folder = window[i % window_size].get();
        const std::string path = NEOMOD_MAPS_PATH "/" + oszs[i];
        if(!folder.empty()) {
            env->deleteFile(path);
        } else {
            debugLog("Failed to extract loose .osz {}", path);
        }
        this->stage_done.store(static_cast<u32>(i + 1), std::memory_order_release);

        if(const uSz next = i + window_size; next < oszs.size()) window[i % window_size] = submit_extract(oszs[next]);
    }
}

bool Database::addScore(const FinishedScore &score) {
    // if addScoreRaw returns false, it means it wasn't added because we already have the score
    // so just skip everything and return the index
    const bool added = this->addScoreRaw(score);
    if(added) {
        this->sortScores(score.beatmap_hash);

        this->scores_changed = true;

        // wait for any previous save to finish before starting a new one
        if(this->score_save_future.valid()) this->score_save_future.wait();

        std::vector<std::string> saveable_cvars;
        for(auto *cvar : cvars().getConVarArray()) {
            // Very important: do not save passwords in replays
            if(cvar->isFlagSet(cv::HIDDEN)) continue;
            if(cvar->isFlagSet(cv::NOSAVE)) continue;

            saveable_cvars.emplace_back(cvar->getName());
            saveable_cvars.emplace_back(cvar->getString());
        }

        this->score_save_future = Async::submit(
            [this, scorecopy = score, additional_data = std::move(saveable_cvars)] {
                // NOTE: if we ever read cvars out of the replay data, # of cvars would be additional_data.size()/2
                LegacyReplay::save_osr(scorecopy, additional_data);

                if(!engine->isShuttingDown() && cv::scores_save_immediately.getBool()) {
                    this->saveScores();
                }
            },
            Lane::Background);
    }

    return added;
}

int Database::isScoreAlreadyInDB(const MD5Hash &map_hash, i64 unix_timestamp, const std::string &playerName) {
    Sync::shared_lock lock(this->scores_mtx);

    // operator[] might add a new entry
    const auto &scoreit = this->scores.find(map_hash);
    if(scoreit == this->scores.end()) return -1;

    for(int existing_pos = -1; const auto &existing : scoreit->second) {
        existing_pos++;
        if(existing.unix_timestamp == unix_timestamp && existing.playerName == playerName) {
            // Score has already been added
            return existing_pos;
        }
    }

    return -1;
}

bool Database::addScoreRaw(const FinishedScore &score) {
    const bool new_might_have_replay{score.has_possible_replay()};

    int existing_pos{-1};
    bool overwrite{false};

    if((existing_pos = this->isScoreAlreadyInDB(score.beatmap_hash, score.unix_timestamp, score.playerName)) >= 0) {
        // a bit hacky, but allow overwriting mcosu scores with peppy/neomod scores
        // otherwise scores imported to mcosu from stable will be marked as "from mcosu"
        // which we consider to never have a replay available

        // we don't want to overwrite in any case if the new score has no possible replay
        if(!new_might_have_replay) {
            return false;
        }

        {
            Sync::shared_lock lock(this->scores_mtx);
            // otherwise check if the old one doesn't have a replay
            // if it has one, don't overwrite it
            overwrite = !this->scores[score.beatmap_hash][existing_pos].has_possible_replay();
        }

        if(!overwrite) {
            return false;
        }
        // otherwise overwrite it
    }

    Sync::unique_lock lock(this->scores_mtx);

    if(overwrite) {
        this->scores[score.beatmap_hash][existing_pos] = score;
    } else {
        // new score
        this->scores[score.beatmap_hash].push_back(score);
    }

    return true;
}

void Database::deleteScore(const FinishedScore &scoreToDelete) {
    if(scoreToDelete.beatmap_hash.empty()) return;

    Sync::unique_lock lock(this->scores_mtx);
    if(const auto &scoreit = this->scores.find(scoreToDelete.beatmap_hash); scoreit != this->scores.end()) {
        if(std::erase(scoreit->second, scoreToDelete)) {
            this->scores_changed.store(true, std::memory_order_release);
        }
    }
}

void Database::sortScoresInPlace(std::vector<FinishedScore> &scores) {
    if(scores.size() < 2) return;

    int sortIndex = cv::songbrowser_scores_sortingtype.getInt();

    // Fallback: sort by pp
    if(sortIndex < 0 || sortIndex >= ui->getSongBrowser()->SCORE_SORTING_METHODS.size()) {
        sortIndex = ui->getSongBrowser()->DEFAULT_SCORE_SORTING_INDEX;
        cv::songbrowser_scores_sortingtype.setValue(sortIndex);
    }

    std::ranges::sort(scores, ui->getSongBrowser()->SCORE_SORTING_METHODS[sortIndex].comparator);
}

void Database::sortScores(const MD5Hash &beatmapMD5Hash) {
    Sync::unique_lock lk(this->scores_mtx);
    if(auto it = this->scores.find(beatmapMD5Hash); it != this->scores.end()) {
        Database::sortScoresInPlace(it->second);
    }
    return;
}

Database::PlayerPPScores Database::getPlayerPPScores(std::string_view playerName) {
    PlayerPPScores ppScores;
    ppScores.totalScore = 0;
    if(this->getProgress() < 1.0f) return ppScores;

    // hoist out of the loop
    const bool include_autopilot_relax = cv::user_include_relax_and_autopilot_for_stats.getBool();

    u64 totalScore = 0;

    std::vector<FinishedScore *> scores;
    {
        Sync::shared_lock lock(this->scores_mtx);

        for(auto &[hash, scorevec] : this->scores) {
            if(scorevec.empty()) continue;

            FinishedScore *tempScore = &scorevec[0];

            // only add highest pp score per diff
            bool foundValidScore = false;
            f64 prevPP = -1.0;
            for(auto &score : scorevec) {
                // filter out scores set with a different name or if we shouldn't allow relax/autopilot
                if((!include_autopilot_relax &&
                    (u64)score.mods.flags & ((u64)ModFlags::Relax | (u64)ModFlags::Autopilot)) ||
                   (playerName != score.playerName)) {
                    continue;
                }

                foundValidScore = true;
                totalScore += score.score;

                const auto scorePP = score.get_pp();
                if(scorePP > prevPP || prevPP < 0.0) {
                    prevPP = scorePP;
                    tempScore = &score;
                }
            }

            if(foundValidScore) scores.push_back(tempScore);
        }

        // sort by pp (reversed)
        // for some reason this was originally backwards from sortScoreByPP, so negating it here
        std::ranges::sort(scores, [](const FinishedScore *const a, const FinishedScore *const b) -> bool {
            if(a == b) return false;
            return sortScoreByPP(*b, *a);
        });
    }

    ppScores.ppScores = std::move(scores);
    ppScores.totalScore = totalScore;

    return ppScores;
}

std::vector<std::string> Database::getPlayerNamesWithScoresForUserSwitcher() {
    const bool includeLegacyNames = cv::user_switcher_include_legacy_scores_for_names.getBool();

    std::set<std::string> tempNames;
    {
        Sync::shared_lock lock(this->scores_mtx);
        for(const auto &[hash, scorevec] : this->scores) {
            for(const auto &score : scorevec) {
                // skip names that only exist because of scores imported from the peppy client, unless enabled
                if(includeLegacyNames || !score.is_peppy_imported()) tempNames.insert(score.playerName);
            }
        }
    }

    // always add the local user, even without any scores
    tempNames.insert(cv::name.getString());

    std::vector<std::string> names;
    names.reserve(tempNames.size());
    for(const auto &name : tempNames) {
        if(!name.empty()) names.push_back(name);
    }
    std::ranges::sort(names, SString::strcase_comp);

    return names;
}

Database::PlayerStats Database::calculatePlayerStats(std::string_view playerName) {
    // FIXME: returning cached statistics even if we got new scores
    // this is makes this function a "sneaky" API that might return stale stats
    // done for performance to not tank FPS during score recalc where this is done on the main thread
    // every frame (by UserCard::updateUserStats)

    // should be done by the caller but it's more complicated because the prevPlayerStats are
    // cached inside Database...
    const bool scoresChanged = this->scores_changed.load(std::memory_order_acquire);
    const bool returnCached =
        playerName == this->prevPlayerStats.name &&
        (!scoresChanged || (!BatchDiffCalc::scores_finished() && !engine->throttledShouldRun(120)));
    if(returnCached) {
        return this->prevPlayerStats;
    }

    const PlayerPPScores ps = this->getPlayerPPScores(playerName);

    // delay caching until we actually have scores loaded
    if(ps.ppScores.size() > 0 || this->isFinished()) {
        this->scores_changed.store(false, std::memory_order_release);
    }

    // "If n is the amount of scores giving more pp than a given score, then the score's weight is 0.95^n"
    // "Total pp = PP[1] * 0.95^0 + PP[2] * 0.95^1 + PP[3] * 0.95^2 + ... + PP[n] * 0.95^(n-1)"
    // also, total accuracy is apparently weighted the same as pp
    float pp = 0.0f;
    float acc = 0.0f;
    for(uSz i = 0; i < ps.ppScores.size(); i++) {
        const float weight = getWeightForIndex(ps.ppScores.size() - 1 - i);

        pp += (f32)ps.ppScores[i]->get_pp() * weight;
        acc += LiveScore::calculateAccuracy(ps.ppScores[i]->num300s, ps.ppScores[i]->num100s, ps.ppScores[i]->num50s,
                                            ps.ppScores[i]->numMisses) *
               weight;
    }

    // bonus pp
    // https://osu.ppy.sh/wiki/en/Performance_points
    if(cv::scores_bonus_pp.getBool()) pp += getBonusPPForNumScores(ps.ppScores.size());

    // normalize accuracy
    if(ps.ppScores.size() > 0) acc /= (20.0f * (1.0f - getWeightForIndex(ps.ppScores.size())));

    // fill stats
    this->prevPlayerStats.name = playerName;
    this->prevPlayerStats.pp = pp;
    this->prevPlayerStats.accuracy = acc;

    if(ps.totalScore != this->prevPlayerStats.totalScore) {
        this->prevPlayerStats.level = getLevelForScore(ps.totalScore);

        const u64 requiredScoreForCurrentLevel = getRequiredScoreForLevel(this->prevPlayerStats.level);
        const u64 requiredScoreForNextLevel = getRequiredScoreForLevel(this->prevPlayerStats.level + 1);

        if(requiredScoreForNextLevel > requiredScoreForCurrentLevel)
            this->prevPlayerStats.percentToNextLevel =
                (f32)((double)(ps.totalScore - requiredScoreForCurrentLevel) /
                      (double)(requiredScoreForNextLevel - requiredScoreForCurrentLevel));
    }

    this->prevPlayerStats.totalScore = ps.totalScore;

    return this->prevPlayerStats;
}

float Database::getWeightForIndex(uSz i) { return std::pow(0.95f, (f32)i); }

float Database::getBonusPPForNumScores(size_t numScores) {
    return (417.0f - 1.0f / 3.0f) * (f32)(1.0 - pow(0.995, std::min(1000.0, (f64)numScores)));
}

u64 Database::getRequiredScoreForLevel(int level) {
    // https://zxq.co/ripple/ocl/src/branch/master/level.go
    if(level <= 100) {
        if(level > 1)
            return (u64)std::floor(5000. / 3 * (4 * pow(level, 3) - 3 * pow(level, 2) - level) +
                                   std::floor(1.25 * pow(1.8, (double)(level - 60))));

        return 1;
    }

    return (u64)26931190829 + (u64)100000000000 * (u64)(level - 100);
}

int Database::getLevelForScore(u64 score, int maxLevel) {
    // https://zxq.co/ripple/ocl/src/branch/master/level.go
    int i = 0;
    while(true) {
        if(maxLevel > 0 && i >= maxLevel) return i;

        const u64 lScore = getRequiredScoreForLevel(i);

        if(score < lScore) return (i - 1);

        i++;
    }
}

BeatmapDifficulty *Database::getBeatmapDifficulty(const MD5Hash &md5hash) const {
    if(this->isLoading()) {
        debugLog("we are loading, progress {}, not returning a BeatmapDifficulty*", this->getProgress());
        return nullptr;
    }

    Sync::shared_lock lock(this->beatmap_difficulties_mtx);
    auto it = this->beatmap_difficulties.find(md5hash);
    if(it == this->beatmap_difficulties.end()) {
        return nullptr;
    } else {
        return it->second;
    }
}

BeatmapDifficulty *Database::getBeatmapDifficulty(i32 map_id) const {
    if(this->isLoading()) {
        debugLog("we are loading, progress {}, not returning a BeatmapDifficulty*", this->getProgress());
        return nullptr;
    }

    Sync::shared_lock lock(this->beatmap_difficulties_mtx);
    for(const auto &[_, diff] : this->beatmap_difficulties) {
        if(diff->getID() == map_id) {
            return diff;
        }
    }

    return nullptr;
}

BeatmapSet *Database::getBeatmapSet(i32 set_id) const {
    if(set_id <= 0) return nullptr;
    if(this->isLoading()) {
        debugLog("we are loading, progress {}, not returning a BeatmapSet*", this->getProgress());
        return nullptr;
    }

    Sync::shared_lock lock(this->beatmap_difficulties_mtx);
    auto it = this->sets_by_id.find(set_id);
    if(it == this->sets_by_id.end()) return nullptr;

    for(BeatmapSet *set : it->second) {
        if(set->type == DatabaseBeatmap::BeatmapType::NEOMOD_BEATMAPSET) return set;
    }
    return it->second.front();
}

namespace {
std::string rel_folder_of(std::string_view folder_path) {
    // maps/ sets live directly under NEOMOD_MAPS_PATH, so the folder name is the last path component
    while(folder_path.ends_with('/') || folder_path.ends_with('\\')) folder_path.remove_suffix(1);
    const auto sep = folder_path.find_last_of("/\\");
    return std::string{sep == std::string_view::npos ? folder_path : folder_path.substr(sep + 1)};
}
}  // namespace

std::string Database::getBeatmapSetFolder(i32 set_id) const {
    if(set_id <= 0) return {};

    Sync::shared_lock lock(this->beatmap_difficulties_mtx);
    auto it = this->sets_by_id.find(set_id);
    if(it == this->sets_by_id.end()) return {};

    for(const BeatmapSet *set : it->second) {
        if(set->type == DatabaseBeatmap::BeatmapType::NEOMOD_BEATMAPSET) return rel_folder_of(set->getFolder());
    }
    return {};
}

void Database::updateSetID(DatabaseBeatmap *map, i32 new_id) {
    BeatmapSet *set = map->getParentSet() ? map->getParentSet() : map;

    Sync::unique_lock lock(this->beatmap_difficulties_mtx);
    if(set->iSetID != new_id) {
        // only db-owned sets live in the index (the main menu's preloaded sets don't)
        const bool indexed =
            set->iSetID > 0
                ? this->unindexSet(set)
                : std::ranges::any_of(this->beatmapsets, [set](const auto &owned) { return owned.get() == set; });
        set->iSetID = new_id;
        if(indexed) this->indexSet(set);
    }
    // a set's id is its first difficulty's, so a sibling can still lack it when the set doesn't
    for(auto &diff : set->getDifficulties()) {
        diff->iSetID = new_id;
    }
}

void Database::indexSet(BeatmapSet *set) {
    if(set->iSetID > 0) this->sets_by_id[set->iSetID].push_back(set);
}

bool Database::unindexSet(BeatmapSet *set) {
    if(set->iSetID <= 0) return false;
    auto it = this->sets_by_id.find(set->iSetID);
    if(it == this->sets_by_id.end()) return false;

    const bool found = std::erase(it->second, set) > 0;
    if(it->second.empty()) this->sets_by_id.erase(it);
    return found;
}

void Database::addPathToImport(std::string_view dbPath) { this->extern_db_paths_to_import.emplace_back(dbPath); }

std::string Database::getOsuSongsFolder() {
    std::string songs_dir = cv::songs_folder.getString();
    if(songs_dir.empty()) {
        cv::songs_folder.setValue(cv::songs_folder.getDefaultString());
    }
    if(!songs_dir.ends_with('/') && !songs_dir.ends_with('\\')) {
        songs_dir.push_back('/');
    }

    if(!Environment::isAbsolutePath(songs_dir)) {
        // it's a subfolder (default)
        // cv::osu_folder is already normalized, so just concatenating is fine
        songs_dir = cv::osu_folder.getString() + songs_dir;
    } else {
        // normalize the absolute path
        songs_dir = Environment::normalizeDirectory(songs_dir);
    }

    return songs_dir;
}

MD5Hash Database::recalcMD5(std::string osu_path) {
    // avoid hashmap corruption, we somehow fail to read some entries' md5 hashes
    // TODO: move this somewhere else or put it in peppy overrides or something
    // "temporary" fix (extremely rare edge case so maybe it's not that important)
    MD5Hash md5digest;
    if(Environment::fileExists(osu_path)) {
        if(const auto hash = crypto::hash::md5_file(osu_path)) {
            md5digest = *hash;
            logIfCV(debug_db, "Manually calculated hash {} for {}", md5digest, osu_path);
        } else {
            logIfCV(debug_db, "Failed to recalculate corrupt hash {}", osu_path);
        }
    } else {
        logIfCV(debug_db, "Skipped entry {} with no/corrupt md5", osu_path);
    }

    return md5digest;
}

void Database::loadMaps(std::string_view neomod_maps_path, std::string_view peppy_db_path) {
    Timer importTimer;
    importTimer.start();

    // staging buffer, moved into Database::beatmapsets when async load finishes
    std::vector<std::unique_ptr<BeatmapSet>> temp_loading_beatmapsets;

    u32 nb_neomod_maps = 0;
    u32 nb_peppy_maps = 0;
    u32 nb_overrides = 0;
    bool neomod_maps_loaded = false;
    u32 num_beatmaps_to_load = 0;  // osu!.db entries

    // Load neomod map database
    {
        ByteBufferedFile::Reader neomod_maps(neomod_maps_path);
        bool version_ok = true;
        if(neomod_maps.get_total_size() > 0) {
            u32 version = neomod_maps.read<u32>();
            version_ok = version <= NEOMOD_MAPS_DB_VERSION;
            if(!version_ok) {
                // written by a newer build: don't guess at the layout. neomod_maps_loaded stays false so that
                // saveMaps won't overwrite the file with something that build can't read
                debugLog("{} version {} is newer than this build's {}, ignoring it", neomod_maps_path, version,
                         NEOMOD_MAPS_DB_VERSION);
                ui->getNotificationOverlay()->addToast(
                    tformat("{} was written by a newer version, ignoring it", neomod_maps_path), ERROR_TOAST);
            } else if(version < NEOMOD_MAPS_DB_VERSION) {
                // Reading from older database version: backup just in case
                auto backup_path =
                    fmt::format("{}.{}-{:%F}", neomod_maps_path, version, fmt::gmtime(std::time(nullptr)));
                if(File::copy(neomod_maps_path, backup_path)) {
                    debugLog("older database {} < {}, backed up {} -> {}", version, NEOMOD_MAPS_DB_VERSION,
                             neomod_maps_path, backup_path);
                }
            }

            u32 nb_sets = version_ok ? neomod_maps.read<u32>() : 0;
            for(uSz i = 0; i < nb_sets; i++) {
                if(this->isCancelled()) break;  // cancellation point

                u32 progress_bytes = this->bytes_processed + neomod_maps.get_total_pos();
                f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
                this->loading_progress = (f32)std::clamp(progress_float, 0.01, 0.99);

                std::string rel_folder;
                i64 folder_mtime = 0;
                MapRoot root = MapRoot::Neomod;
                if(version >= 20260829) {
                    neomod_maps.read_string(rel_folder);
                    folder_mtime = neomod_maps.read<i64>();
                    root = neomod_maps.read<u8>() == 1 ? MapRoot::Peppy : MapRoot::Neomod;
                }
                i32 set_id = neomod_maps.read<i32>();
                u16 nb_diffs = neomod_maps.read<u16>();

                // pre-20260829 sets were always stored in maps/<set_id>/, later ones record their folder
                if(version < 20260829) rel_folder = fmt::to_string(set_id);

                // NOTE: Ignoring mapsets with ID -1, since we most likely saved them in the correct folder,
                //       but mistakenly set their ID to -1 (because the ID was missing from the .osu file).
                //       (pre-20260829 files only: their folder is unknown, the startup rescan finds it on disk)
                if(version < 20260829 && set_id == -1) {
                    for(u16 j = 0; j < nb_diffs; j++) {
                        neomod_maps.skip_string();  // osu_filename
                        neomod_maps.skip<i32>();    // iID
                        neomod_maps.skip_string();  // sTitle
                        neomod_maps.skip_string();  // sAudioFileName
                        neomod_maps.skip<i32>();    // iLengthMS
                        neomod_maps.skip<f32>();    // fStackLeniency
                        neomod_maps.skip_string();  // sArtist
                        neomod_maps.skip_string();  // sCreator
                        neomod_maps.skip_string();  // sDifficultyName
                        neomod_maps.skip_string();  // sSource
                        neomod_maps.skip_string();  // sTags
                        MD5Hash md5;
                        if(version >= 20260202) {
                            // storing as the raw digest bytes past this ver
                            (void)neomod_maps.read_hash_digest(md5);  // TODO: validate
                        } else {
                            (void)neomod_maps.read_hash_chars(md5);  // TODO: validate
                        }
                        // other fixed-sized fields in the middle... try adding new stuff at the end so this doesn't break in the future
                        neomod_maps.skip_bytes(sizeof(f32) + sizeof(f32) + sizeof(f32) + sizeof(f32) + sizeof(f64) +
                                               sizeof(u32) + sizeof(u64) + sizeof(i16) + sizeof(i16) + sizeof(u16) +
                                               sizeof(u16) + sizeof(u16) + sizeof(f64) + (sizeof(i32) * 3));
                        if(version < 20240812) {
                            u32 nb_timing_points = neomod_maps.read<u32>();
                            neomod_maps.skip_bytes(sizeof(DB_TIMINGPOINT) * nb_timing_points);
                        }
                        if(version >= 20240703) {  // draw_background
                            neomod_maps.skip<u8>();
                        }
                        if(version >= 20240812) {  // loudness
                            neomod_maps.skip<f32>();
                        }
                        if(version >= 20250801) {  // unicode title+artist
                            neomod_maps.skip_string();
                            neomod_maps.skip_string();
                        }
                        if(version >= 20251009) {  // background image filename
                            neomod_maps.skip_string();
                        }
                        if(version >= 20251225) {  // ppv2 version
                            neomod_maps.skip<u32>();
                        }
                        if(version >= 20260816) {  // last played
                            neomod_maps.skip<i64>();
                        }
                        logIfCV(debug_db, "skipped iSetID==-1 beatmap with hash {} load idx: {},{}", md5, i, j);
                    }
                    continue;
                }

                auto diffs = std::make_unique<DiffContainer>();
                const std::string mapset_path = this->rootPath(root) + rel_folder + "/";
                using enum DatabaseBeatmap::BeatmapType;
                const auto diff_type = root == MapRoot::Peppy ? PEPPY_DIFFICULTY : NEOMOD_DIFFICULTY;
                const auto set_type = root == MapRoot::Peppy ? PEPPY_BEATMAPSET : NEOMOD_BEATMAPSET;

                for(u16 j = 0; j < nb_diffs; j++) {
                    if(this->isCancelled()) {  // cancellation point
                        break;
                    }

                    std::string osu_filename = neomod_maps.read_string();

                    i32 iID = neomod_maps.read<i32>();
                    i32 iSetID = set_id;
                    auto sTitle = neomod_maps.read_string();
                    auto sAudioFileName = neomod_maps.read_string();
                    i32 iLengthMS = neomod_maps.read<i32>();
                    f32 fStackLeniency = neomod_maps.read<f32>();
                    auto sArtist = neomod_maps.read_string();
                    auto sCreator = neomod_maps.read_string();
                    auto sDifficultyName = neomod_maps.read_string();
                    auto sSource = neomod_maps.read_string();
                    auto sTags = neomod_maps.read_string();

                    MD5Hash diff_hash;
                    // TODO: properly validate and skip beatmaps with invalid hashes
                    if(version >= 20260202) {
                        // storing as the raw digest bytes past this ver
                        (void)neomod_maps.read_hash_digest(diff_hash);
                    } else {
                        (void)neomod_maps.read_hash_chars(diff_hash);
                    }

                    f32 fAR = neomod_maps.read<f32>();
                    f32 fCS = neomod_maps.read<f32>();
                    f32 fHP = neomod_maps.read<f32>();
                    f32 fOD = neomod_maps.read<f32>();
                    f64 fSliderMultiplier = neomod_maps.read<f64>();  // TODO: store as float?
                    u32 iPreviewTime = neomod_maps.read<u32>();
                    const i64 maybe_dotnettime = neomod_maps.read<i64>();
                    i64 last_modification_time = maybe_dotnettime;
                    // convert .NET timestamp to unix timestamp (mtime)
                    // we don't really need to update db format for this because it was only used for sorting
                    // so just fix it up here
                    if(maybe_dotnettime > 1'000'000'000'000'000 /* 100% .net timestamp from non-updated db */) {
                        last_modification_time =
                            (maybe_dotnettime - LegacyReplay::UNIX_EPOCH_TICKS) / LegacyReplay::TICKS_PER_SECOND;
                    }
                    i16 iLocalOffset = neomod_maps.read<i16>();
                    i16 iOnlineOffset = neomod_maps.read<i16>();
                    u16 iNumCircles = neomod_maps.read<u16>();
                    u16 iNumSliders = neomod_maps.read<u16>();
                    u16 iNumSpinners = neomod_maps.read<u16>();
                    f64 fStarsNomod = neomod_maps.read<f64>();  // TODO: store as float?

                    i32 iMinBPM{-1}, iMaxBPM{-1}, iMostCommonBPM{-1};
                    if(version >= 20251209) {  // prior versions had a rounding bug, force recalc
                        iMinBPM = neomod_maps.read<i32>();
                        iMaxBPM = neomod_maps.read<i32>();
                        iMostCommonBPM = neomod_maps.read<i32>();
                    } else {
                        neomod_maps.skip_bytes(sizeof(i32) * 3);
                    }

                    if(version < 20240812) {
                        u32 nb_timing_points = neomod_maps.read<u32>();
                        neomod_maps.skip_bytes(sizeof(DB_TIMINGPOINT) * nb_timing_points);
                    }

                    bool draw_background = true;
                    if(version >= 20240703) {
                        draw_background = neomod_maps.read<u8>();
                    }

                    f32 loudness = 0.f;
                    if(version >= 20240812) {
                        loudness = neomod_maps.read<f32>();
                    }

                    std::string sTitleUnicode;
                    std::string sArtistUnicode;
                    if(version >= 20250801) {
                        neomod_maps.read_string(sTitleUnicode);
                        neomod_maps.read_string(sArtistUnicode);
                    }

                    const bool bEmptyTitleUnicode = sTitleUnicode.empty() || SString::is_wspace_only(sTitleUnicode);
                    const bool bEmptyArtistUnicode = sArtistUnicode.empty() || SString::is_wspace_only(sArtistUnicode);

                    // we cache the background image filename in the database past this version
                    std::string sBackgroundImageFileName;
                    if(version >= 20251009) {
                        neomod_maps.read_string(sBackgroundImageFileName);
                    }

                    // prior versions did not store PPv2 version, so there was no way to know if the maps needed pp recalc
                    u32 ppv2Version = 0;
                    if(version >= 20251225) {
                        ppv2Version = neomod_maps.read<u32>();
                    }

                    i64 last_played = 0;
                    if(version >= 20260816) {
                        last_played = neomod_maps.read<i64>();
                    }

                    // Fixup a bug with certain bleeding edge commits ~December 2025,
                    // where the osu_filename was saved as just the folder name itself...
                    if(osu_filename.empty() || osu_filename == mapset_path) {
                        for(const auto &osufile_nameonly : Environment::getFilesInFolder(mapset_path)) {
                            if(Environment::getFileExtensionFromFilePath(osufile_nameonly).compare("osu") != 0) {
                                continue;
                            }
                            const std::string osufile_fullpath = mapset_path + osufile_nameonly;
                            bool inMetadata = false;
                            i32 tempiID = -1;
                            {
                                File file(osufile_fullpath);
                                for(auto line = file.readLine(); !line.empty() || file.canRead();
                                    line = file.readLine()) {
                                    if(line.empty() || SString::is_comment(line)) continue;
                                    if(line.contains("[Metadata]")) {
                                        inMetadata = true;
                                        continue;
                                    }
                                    if(line.starts_with('[') && inMetadata) {
                                        break;
                                    }
                                    if(inMetadata) {
                                        if(Parsing::parse(line, "BeatmapID", ':', &tempiID)) {
                                            break;
                                        }
                                        continue;
                                    }
                                }
                            }
                            if(tempiID != -1 && tempiID == iID) {
                                osu_filename = osufile_nameonly;
                                debugLog("found fixed .osu filename {} iID {} hash {}", osu_filename, iID, diff_hash);
                                break;
                            }
                        }
                    }

                    // force calculate hash if we saved with empty/0 hash
                    if(diff_hash.empty()) {
                        diff_hash = recalcMD5(mapset_path + osu_filename);
                    }

                    auto diff = std::make_unique<BeatmapDifficulty>(mapset_path + osu_filename, mapset_path, diff_type);

                    diff->iID = iID;
                    diff->iSetID = iSetID;
                    diff->sTitle = std::move(sTitle);
                    diff->sAudioFileName = std::move(sAudioFileName);
                    diff->iLengthMS = iLengthMS;
                    diff->fStackLeniency = fStackLeniency;
                    diff->sArtist = std::move(sArtist);
                    diff->sCreator = std::move(sCreator);
                    diff->sDifficultyName = std::move(sDifficultyName);
                    diff->sSource = std::move(sSource);
                    diff->sTags = std::move(sTags);
                    diff->writeMD5(diff_hash);
                    diff->fAR = fAR;
                    diff->fCS = fCS;
                    diff->fHP = fHP;
                    diff->fOD = fOD;
                    diff->fSliderMultiplier = (f32)fSliderMultiplier;
                    diff->iPreviewTime = (i32)iPreviewTime;
                    diff->last_modification_time = last_modification_time;
                    diff->last_play_time = last_played;
                    diff->iLocalOffset = iLocalOffset;
                    diff->iOnlineOffset = iOnlineOffset;
                    diff->iNumCircles = iNumCircles;
                    diff->iNumSliders = iNumSliders;
                    diff->iNumSpinners = iNumSpinners;
                    diff->fStarsNomod = (f32)fStarsNomod;

                    diff->iMinBPM = iMinBPM;
                    diff->iMaxBPM = iMaxBPM;
                    diff->iMostCommonBPM = iMostCommonBPM;

                    diff->draw_background = draw_background;

                    diff->loudness = loudness;

                    if(!bEmptyTitleUnicode) {
                        diff->sTitleUnicode = std::move(sTitleUnicode);
                        diff->has_unicode_title = true;
                    }
                    if(!bEmptyArtistUnicode) {
                        diff->sArtistUnicode = std::move(sArtistUnicode);
                        diff->has_unicode_artist = true;
                    }

                    diff->sBackgroundImageFileName = std::move(sBackgroundImageFileName);

                    diff->ppv2Version = ppv2Version;

                    diffs->push_back(std::move(diff));
                }

                // songs folder records only matter while osu!.db isn't its source; otherwise they're stale and
                // get dropped by the next save
                if(root == MapRoot::Peppy && !this->needs_raw_load) continue;

                BeatmapSet *set_ptr = nullptr;
                if(diffs && !diffs->empty()) {
                    auto set = std::make_unique<BeatmapSet>(std::move(diffs), set_type);
                    set_ptr = set.get();
                    temp_loading_beatmapsets.push_back(std::move(set));
                }
                {
                    Sync::unique_lock lock(this->beatmap_difficulties_mtx);
                    if(set_ptr) {
                        for(const auto &diff : set_ptr->getDifficulties()) {
                            this->beatmap_difficulties[diff->getMD5()] = diff.get();
                            if(diff->loudness.load(std::memory_order_acquire) == 0.f) {
                                this->loudness_to_calc.push_back(diff.get());
                            }
                        }
                        this->indexSet(set_ptr);
                        (root == MapRoot::Peppy ? nb_peppy_maps : nb_neomod_maps) += set_ptr->getDifficulties().size();
                    }
                    this->folderIndex(root)[rel_folder] = {.mtime = folder_mtime, .set = set_ptr};
                }
            }

            if(version_ok && version >= 20240812) {
                nb_overrides = neomod_maps.read<u32>();
                Sync::unique_lock lock(this->peppy_overrides_mtx);
                for(uSz i = 0; i < nb_overrides; i++) {
                    MapOverrides over;
                    MD5Hash map_md5;
                    if(version >= 20260202) {
                        // storing as the raw digest bytes past this ver
                        (void)neomod_maps.read_hash_digest(map_md5);  // TODO: validate
                    } else {
                        (void)neomod_maps.read_hash_chars(map_md5);  // TODO: validate
                    }

                    over.local_offset = neomod_maps.read<i16>();
                    over.online_offset = neomod_maps.read<i16>();
                    over.star_rating = neomod_maps.read<f32>();
                    over.loudness = neomod_maps.read<f32>();
                    if(version >= 20251209) {  // only override if we have accurately calculated values
                        over.min_bpm = neomod_maps.read<i32>();
                        over.max_bpm = neomod_maps.read<i32>();
                        over.avg_bpm = neomod_maps.read<i32>();
                    } else {
                        neomod_maps.skip_bytes(sizeof(i32) * 3);
                        over.min_bpm = -1;  // sentinel values, to be re-calculated when importing the map from peppy db
                        over.max_bpm = -1;
                        over.avg_bpm = -1;
                    }
                    over.draw_background = neomod_maps.read<u8>();
                    if(version >= 20251009) {
                        neomod_maps.read_string(over.background_image_filename);
                    }
                    if(version >= 20251225) {
                        over.ppv2_version = neomod_maps.read<u32>();
                    }
                    if(version >= 20260816) {
                        over.last_played = neomod_maps.read<i64>();
                    }
                    this->peppy_overrides[map_md5] = over;
                }
            }

            // star ratings section
            if(version_ok && version >= 20260202) {
                const uSz stored_speeds = neomod_maps.read<u8>();
                const uSz stored_combos = neomod_maps.read<u8>();
                const u32 nb_star_entries = neomod_maps.read<u32>();
                const uSz stored_entries = stored_speeds * stored_combos;
                const bool layout_matches =
                    (stored_speeds == StarPrecalc::SPEEDS_NUM && stored_combos == StarPrecalc::NUM_MOD_COMBOS);

                if(layout_matches) {
                    Sync::unique_lock lock(this->star_ratings_mtx);
                    this->star_ratings.reserve(nb_star_entries);
                    for(u32 i = 0; i < nb_star_entries; i++) {
                        MD5Hash hash;
                        (void)neomod_maps.read_hash_digest(hash);
                        auto ratings = std::make_unique<StarPrecalc::SRArray>();
                        (void)neomod_maps.read_bytes(reinterpret_cast<u8 *>(ratings->data()),
                                                     sizeof(f32) * StarPrecalc::NUM_PRECALC_RATINGS);
                        this->star_ratings.emplace(hash, std::move(ratings));
                    }
                } else {
                    // layout changed; skip stored data, recalc will be triggered
                    debugLog("star ratings layout changed (stored {}x{}, current {}x{}), skipping", stored_speeds,
                             stored_combos, (u8)StarPrecalc::SPEEDS_NUM, (uSz)StarPrecalc::NUM_MOD_COMBOS);
                    for(u32 i = 0; i < nb_star_entries; i++) {
                        neomod_maps.skip_bytes(sizeof(MD5Hash) + sizeof(f32) * stored_entries);
                    }
                }
            }
        }
        this->bytes_processed += neomod_maps.get_total_size();
        neomod_maps_loaded = version_ok;
    }

    // load peppy maps
    if(!this->needs_raw_load) {
        const std::string peppy_songfolder = Database::getOsuSongsFolder();
        debugLog("Database: osu!stable song folder = {:s}", peppy_songfolder);

        // diffs grouped by beatmapset ID, or by folder for maps with an invalid (-1) set ID
        Hash::flat::map<std::variant<i32, std::string>, std::unique_ptr<DiffContainer>> sid_or_folder_to_diffcont;

        ByteBufferedFile::Reader dbr(peppy_db_path);
        u32 osu_db_version = (dbr.good() && dbr.get_total_size() > 0) ? dbr.read<u32>() : 0;
        bool should_read_peppy_database = osu_db_version > 0;
        if(should_read_peppy_database) {
            // read header
            u32 osu_db_folder_count = dbr.read<u32>();
            dbr.skip<u8>();
            dbr.skip<u64>() /* timestamp */;
            std::string player_name = dbr.read_string();
            num_beatmaps_to_load = dbr.read<u32>();

            debugLog("Database: version = {:d}, folderCount = {:d}, playerName = {:s}, numDiffs = {:d}", osu_db_version,
                     osu_db_folder_count, player_name, num_beatmaps_to_load);

            // hard cap upper db version
            if(osu_db_version > cv::database_version.getVal<u32>() && !cv::database_ignore_version.getBool()) {
                ui->getNotificationOverlay()->addToast(
                    fmt::format("osu!.db version unknown ({:d}), osu!stable maps will not get loaded.", osu_db_version),
                    ERROR_TOAST);
                should_read_peppy_database = false;
            }
        }
        if(!should_read_peppy_database) {
            debugLog("not loading {}, ver: {} size: {} err: {}", peppy_db_path, osu_db_version, dbr.get_total_size(),
                     dbr.error());
        }

        if(should_read_peppy_database) {
            std::vector<BPMCalc::BPMTuple> bpm_calculation_buffer;
            std::vector<DB_TIMINGPOINT> timing_points_buffer;

            for(uSz i = 0; i < num_beatmaps_to_load; i++) {
                if(this->isCancelled()) break;  // cancellation point

                // update progress (another thread checks if progress >= 1.f to know when we're done)
                u32 progress_bytes = this->bytes_processed + dbr.get_total_pos();
                f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
                this->loading_progress = (f32)std::clamp(progress_float, 0.01, 0.99);

                // NOTE: This is documented wrongly in many places.
                //       This int was added in 20160408 and removed in 20191106
                //       https://osu.ppy.sh/home/changelog/stable40/20160408.3
                //       https://osu.ppy.sh/home/changelog/cuttingedge/20191106
                if(osu_db_version >= 20160408 && osu_db_version < 20191106) {
                    // size in bytes of the beatmap entry
                    dbr.skip<u32>();
                }

                std::string artist_name = dbr.read_string();
                SString::trim_inplace(artist_name);
                std::string unicode_artist_name = dbr.read_string();
                std::string song_title = dbr.read_string();
                SString::trim_inplace(song_title);
                std::string unicode_song_title = dbr.read_string();
                std::string creator_name = dbr.read_string();
                SString::trim_inplace(creator_name);
                std::string diff_name = dbr.read_string();
                SString::trim_inplace(diff_name);
                std::string audio_filename = dbr.read_string();

                MD5Hash md5hash;
                (void)dbr.read_hash_chars(md5hash);  // TODO: validate

                logIfCV(debug_db, "Reading osu!.db beatmap {:d}/{:d} md5hash {} ...", (i + 1), num_beatmaps_to_load,
                        md5hash);

                bool overrides_found = false;
                MapOverrides override_;
                {
                    Sync::shared_lock lock(this->peppy_overrides_mtx);
                    auto overrides = this->peppy_overrides.find(md5hash);
                    overrides_found = overrides != this->peppy_overrides.end();
                    if(overrides_found) {
                        override_ = overrides->second;
                    }
                }
                std::string dotosu_filename = dbr.read_string();
                /*unsigned char rankedStatus = */ dbr.skip<u8>();
                u16 nb_circles = dbr.read<u16>();
                u16 nb_sliders = dbr.read<u16>();
                u16 nb_spinners = dbr.read<u16>();

                const i64 modification_time_dotnet_ticks = dbr.read<i64>();
                // convert to unix time (peppy db 100% stores these in .NET timestamp form)
                const i64 last_modification_time =
                    (modification_time_dotnet_ticks - LegacyReplay::UNIX_EPOCH_TICKS) / LegacyReplay::TICKS_PER_SECOND;

                // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
                f32 AR, CS, HP, OD;
                if(osu_db_version < 20140609) {
                    AR = dbr.read<u8>();
                    CS = dbr.read<u8>();
                    HP = dbr.read<u8>();
                    OD = dbr.read<u8>();
                } else {
                    AR = dbr.read<f32>();
                    CS = dbr.read<f32>();
                    HP = dbr.read<f32>();
                    OD = dbr.read<f32>();
                }

                f64 slider_multiplier = dbr.read<f64>();

                f32 nomod_star_rating = 0.0f;
                if(osu_db_version >= 20140609) {
                    static constexpr const auto read_nomod_stars = []<typename T>(ByteBufferedFile::Reader &dbr,
                                                                                  uSz num_std_star_ratings) {
                        f32 ret_sr = 0.f;
                        for(uSz s = 0; s < num_std_star_ratings; s++) {
                            dbr.skip<u8>();  // 0x08 ObjType
                            auto mods = dbr.read<u32>();
                            dbr.skip<u8>();  // 0x0c ObjType
                            if(mods == 0 && ret_sr == 0.f) {
                                ret_sr = static_cast<f32>(dbr.read<T>());
                            } else {
                                dbr.skip<T>();
                            }
                        }
                        return ret_sr;
                    };

                    const auto num_std_star_ratings = dbr.read<u32>();
                    // https://osu.ppy.sh/home/changelog/stable40/20250108.3
                    const u32 sr_field_size = osu_db_version < 20250108 ? sizeof(f64) : sizeof(f32);
                    if(sr_field_size == sizeof(f64)) {  // older format
                        nomod_star_rating = read_nomod_stars.template operator()<f64>(dbr, num_std_star_ratings);
                    } else {
                        nomod_star_rating = read_nomod_stars.template operator()<f32>(dbr, num_std_star_ratings);
                    }

                    // taiko/ctb/mania are here only to skip the correct amount of bytes
                    const u32 minigame_skip_bytes =
                        sizeof(u8) /*ObjType*/ + sizeof(u32) /*mods*/ + sizeof(u8) /*ObjType*/ + sr_field_size;
                    for(auto _ : {1 /*taiko*/, 2 /*ctb*/, 3 /*mania*/}) {
                        const auto num_minigame_star_ratings = dbr.read<u32>();
                        for(u32 s = 0; s < num_minigame_star_ratings; s++) {
                            dbr.skip_bytes(minigame_skip_bytes);
                        }
                    }
                }

                /*unsigned int drainTime = */ dbr.skip<u32>();  // seconds
                int duration = (int)dbr.read<u32>();            // milliseconds
                duration = duration >= 0 ? duration : 0;        // sanity clamp
                int preview_time = (int)dbr.read<u32>();
                preview_time = preview_time >= 0 ? preview_time : 0;  // sanity clamp

                BPMCalc::BPMInfo bpm;
                u32 nb_timing_points = dbr.read<u32>();
                if(overrides_found &&
                   override_.min_bpm != -1) {  // only use cached override bpm if it's not the sentinel -1
                    dbr.skip_bytes(sizeof(DB_TIMINGPOINT) * nb_timing_points);
                    bpm.min = override_.min_bpm;
                    bpm.max = override_.max_bpm;
                    bpm.most_common = override_.avg_bpm;
                } else if(nb_timing_points > 0) {
                    timing_points_buffer.resize(nb_timing_points);
                    if(dbr.read_bytes(reinterpret_cast<u8 *>(timing_points_buffer.data()),
                                      sizeof(DB_TIMINGPOINT) * nb_timing_points) !=
                       sizeof(DB_TIMINGPOINT) * nb_timing_points) {
                        debugLog("WARNING: failed to read timing points from beatmap {:d} !", (i + 1));
                    } else {
                        bpm = BPMCalc::getBPM(timing_points_buffer, bpm_calculation_buffer);
                    }
                }

                i32 beatmap_id =
                    dbr.read<i32>();  // fucking bullshit, this is NOT an unsigned integer as is described on
                                      // the wiki, it can and is -1 sometimes
                i32 beatmapset_id = dbr.read<i32>();  // same here
                /*unsigned int threadID = */ dbr.skip<u32>();

                /*unsigned char osuStandardGrade = */ dbr.skip<u8>();
                /*unsigned char taikoGrade = */ dbr.skip<u8>();
                /*unsigned char ctbGrade = */ dbr.skip<u8>();
                /*unsigned char maniaGrade = */ dbr.skip<u8>();

                i16 offset_local = dbr.read<i16>();
                f32 stack_leniency = dbr.read<f32>();
                u8 mode = dbr.read<u8>();

                std::string song_source = dbr.read_string();
                std::string song_tags = dbr.read_string();
                SString::trim_inplace(song_source);
                SString::trim_inplace(song_tags);

                i16 offset_online = dbr.read<i16>();
                dbr.skip_string();  // song title font
                const bool unplayed = !!dbr.read<u8>();
                i64 last_played = 0;
                if(!unplayed) {
                    const i64 last_played_dotnet_ticks = dbr.read<i64>();
                    last_played =
                        (last_played_dotnet_ticks - LegacyReplay::UNIX_EPOCH_TICKS) / LegacyReplay::TICKS_PER_SECOND;
                } else {
                    dbr.skip<i64>();
                }
                /*bool isOsz2 = */ dbr.skip<u8>();

                // somehow, some beatmaps may have spaces at the start/end of their
                // path, breaking the Windows API (e.g. https://osu.ppy.sh/s/215347)
                std::string beatmap_subfolder = dbr.read_string();
                SString::trim_inplace(beatmap_subfolder);

                /*i64 lastOnlineCheck = */ dbr.skip<u64>();

                /*bool ignoreBeatmapSounds = */ dbr.skip<u8>();
                /*bool ignoreBeatmapSkin = */ dbr.skip<u8>();
                /*bool disableStoryboard = */ dbr.skip<u8>();
                /*bool disableVideo = */ dbr.skip<u8>();
                /*bool visualOverride = */ dbr.skip<u8>();

                if(osu_db_version < 20140609) {
                    // https://github.com/ppy/osu/wiki/Legacy-database-file-structure defines it as "Unknown"
                    dbr.skip<u16>();
                }

                /*int lastEditTime = */ dbr.skip<u32>();
                /*unsigned char maniaScrollSpeed = */ dbr.skip<u8>();

                // skip invalid/corrupt entries
                // the good way would be to check if the .osu file actually exists on disk, but that is slow af, ain't
                // nobody got time for that so, since I've seen some concrete examples of what happens in such cases, we
                // just exclude those
                if(artist_name.length() < 1 && song_title.length() < 1 && creator_name.length() < 1 &&
                   diff_name.length() < 1)
                    continue;

                if(mode != 0) continue;
                // we have read all the bytes from this database entry at this point

                // it can happen that nested beatmaps are stored in the
                // database, and that osu! stores that filepath with a backslash (because windows)
                // so replace them with / to normalize
                std::ranges::replace(beatmap_subfolder, '\\', '/');

                // build beatmap & diffs from all the data
                std::string beatmap_dir = fmt::format("{}{}/", peppy_songfolder, beatmap_subfolder);
                std::string dotosu_fullpath = beatmap_dir + dotosu_filename;

                if(md5hash.empty()) {
                    md5hash = recalcMD5(dotosu_fullpath);
                }

                // special case: legacy fallback behavior for invalid beatmapSetID, try to parse the ID from the path
                if(beatmapset_id < 1 && beatmap_subfolder.length() > 0) {
                    size_t slash = beatmap_subfolder.find('/');
                    std::string candidate =
                        (slash != std::string::npos) ? beatmap_subfolder.substr(0, slash) : beatmap_subfolder;

                    if(!candidate.empty() && std::isdigit(static_cast<unsigned char>(candidate[0]))) {
                        if(!Parsing::parse(candidate, &beatmapset_id)) {
                            beatmapset_id = -1;
                        }
                    }
                }
                if(beatmapset_id < 1) {
                    // use -1 as a sentinel if we still couldn't get one
                    beatmapset_id = -1;
                }

                BeatmapDifficulty *diffp = nullptr;
                {
                    // fill diff with data
                    auto map = std::make_unique<BeatmapDifficulty>(std::move(dotosu_fullpath), std::move(beatmap_dir),
                                                                   DatabaseBeatmap::BeatmapType::PEPPY_DIFFICULTY);

                    map->sTitle = std::move(song_title);
                    if(!SString::is_wspace_only(unicode_song_title)) {
                        map->sTitleUnicode = std::move(unicode_song_title);
                        map->has_unicode_title = true;
                    }
                    map->sAudioFileName = std::move(audio_filename);
                    map->iLengthMS = duration;

                    map->fStackLeniency = stack_leniency;

                    map->sArtist = std::move(artist_name);
                    if(!SString::is_wspace_only(unicode_artist_name)) {
                        map->sArtistUnicode = std::move(unicode_artist_name);
                        map->has_unicode_artist = true;
                    }
                    map->sCreator = std::move(creator_name);
                    map->sDifficultyName = std::move(diff_name);
                    map->sSource = std::move(song_source);
                    map->sTags = std::move(song_tags);
                    map->writeMD5(md5hash);
                    map->iID = beatmap_id;
                    map->iSetID = beatmapset_id;

                    map->fAR = AR;
                    map->fCS = CS;
                    map->fHP = HP;
                    map->fOD = OD;
                    // is downcasting actually safe here...? why is this even stored as a double in osu!.db anyways
                    map->fSliderMultiplier = (f32)slider_multiplier;

                    // map->sBackgroundImageFileName = "";

                    map->iPreviewTime = preview_time;
                    map->last_modification_time = last_modification_time;
                    map->last_play_time = last_played;

                    map->iNumCircles = nb_circles;
                    map->iNumSliders = nb_sliders;
                    map->iNumSpinners = nb_spinners;
                    map->iMinBPM = bpm.min;
                    map->iMaxBPM = bpm.max;
                    map->iMostCommonBPM = bpm.most_common;
                    // (the diff is now fully built)

                    // now, get the container to which this diff would belong (creating it if it doesn't exist yet),
                    // and add the diff to it if one with the same md5hash hasn't already been added there
                    auto &diffcont = (beatmapset_id != -1) ? sid_or_folder_to_diffcont[beatmapset_id]
                                                           : sid_or_folder_to_diffcont[std::move(beatmap_subfolder)];
                    if(!diffcont) diffcont = std::make_unique<DiffContainer>();

                    if(!std::ranges::contains(*diffcont, md5hash, &DatabaseBeatmap::getMD5)) {
                        diffp = map.get();
                        diffcont->push_back(std::move(map));
                    }
                }

                if(diffp != nullptr) {  // if we actually added it
                    bool loudness_found = false;
                    if(overrides_found) {
                        diffp->iLocalOffset = override_.local_offset;
                        diffp->iOnlineOffset = override_.online_offset;
                        diffp->fStarsNomod = override_.star_rating;
                        diffp->ppv2Version = override_.ppv2_version;
                        if(diffp->last_play_time < override_.last_played) {
                            diffp->last_play_time = override_.last_played;
                        }
                        diffp->loudness = override_.loudness;
                        diffp->draw_background = override_.draw_background;
                        diffp->sBackgroundImageFileName = override_.background_image_filename;
                        if(override_.loudness != 0.f) {
                            loudness_found = true;
                        }
                    } else {
                        if(nomod_star_rating <= 0.f) {
                            nomod_star_rating *= -1.f;
                        }

                        diffp->iLocalOffset = offset_local;
                        diffp->iOnlineOffset = offset_online;
                        diffp->fStarsNomod = nomod_star_rating;
                        diffp->draw_background = true;
                    }

                    if(!loudness_found) {
                        this->loudness_to_calc.push_back(diffp);
                    }

                    Sync::unique_lock lock(this->beatmap_difficulties_mtx);
                    this->beatmap_difficulties[md5hash] = diffp;
                }

                nb_peppy_maps++;
            }

            if(!this->isCancelled()) {
                // create BeatmapSets from the collected diff containers
                temp_loading_beatmapsets.reserve(temp_loading_beatmapsets.size() + sid_or_folder_to_diffcont.size());
                Sync::unique_lock lock(this->beatmap_difficulties_mtx);
                for(auto &[_, cont] : sid_or_folder_to_diffcont) {
                    assert(cont && !cont->empty());
                    temp_loading_beatmapsets.emplace_back(
                        new BeatmapSet(std::move(cont), DatabaseBeatmap::BeatmapType::PEPPY_BEATMAPSET));
                    this->indexSet(temp_loading_beatmapsets.back().get());
                }
            }
        }
        this->bytes_processed += dbr.get_total_size();
    }

    // TODO: partial loading? it shouldn't be that difficult, would be more useful for raw loading (used to be supported)
    if(!this->isCancelled()) {
        this->beatmapsets = std::move(temp_loading_beatmapsets);

        // calculate last played tms for each diff
        // (we always load scores before maps, and this is fast enough to process on the fly)
        {
            Sync::unique_lock diff_lock(this->beatmap_difficulties_mtx);
            Sync::shared_lock score_lock(this->scores_mtx);

            for(const auto &[hash, diff] : this->beatmap_difficulties) {
                const auto &map_scores = this->scores.find(hash);
                if(map_scores == this->scores.end()) continue;

                i64 most_recently_played_timestamp = 0;
                for(const auto &score : map_scores->second) {
                    // FIXME: this counts scores coming from players other than the user, if they've downloaded a replay!
                    if(most_recently_played_timestamp < score.unix_timestamp) {
                        most_recently_played_timestamp = score.unix_timestamp;
                    }
                }
                if(diff->getLastPlayTime() < most_recently_played_timestamp) {
                    diff->setLastPlayTime(most_recently_played_timestamp);
                }
            }
        }

        // link each diff's star_ratings pointer to its entry in the star_ratings map
        {
            Sync::shared_lock sr_lock(this->star_ratings_mtx);
            Sync::unique_lock diff_lock(this->beatmap_difficulties_mtx);
            for(const auto &[hash, diff] : this->beatmap_difficulties) {
                if(auto it = this->star_ratings.find(hash); it != this->star_ratings.end()) {
                    diff->star_ratings = it->second.get();
                }
            }
        }
        this->neomod_maps_loaded = neomod_maps_loaded;
    } else {
        this->neomod_maps_loaded = false;
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);

        this->beatmap_difficulties.clear();
        this->neomod_folders.clear();
        this->peppy_folders.clear();
        this->sets_by_id.clear();
        this->loudness_to_calc.clear();
        this->beatmapsets.clear();
    }

    importTimer.update();
    debugLog("peppy+neomod maps: loading took {:f} seconds ({:d} peppy, {:d} neomod, {:d} maps total)",
             importTimer.getElapsedTime(), nb_peppy_maps, nb_neomod_maps, nb_peppy_maps + nb_neomod_maps);
    debugLog("Found {:d} overrides; {:d} maps need loudness recalc", nb_overrides, this->loudness_to_calc.size());
}

void Database::saveMaps() {
    if((this->beatmapsets.empty() && this->neomod_folders.empty() && this->peppy_folders.empty()) ||
       this->isLoading() || this->isCancelled()) {
        return;
    }

    debugLog("Osu: Saving maps ...");
    if(!this->neomod_maps_loaded) {
        debugLog("Cannot save maps since they weren't loaded properly first!");
        return;
    }

    Timer t;
    t.start();

    const auto neomod_maps_db = getDBPath(DatabaseType::NEOMOD_MAPS);

    ByteBufferedFile::Writer maps(neomod_maps_db);
    if(!maps.good()) {
        debugLog("Cannot save maps to {}: {}", neomod_maps_db, maps.error());
        return;
    }

    maps.write<u32>(NEOMOD_MAPS_DB_VERSION);

    // one record per known folder, maps/ always and the raw-loaded songs folder while it's the source for it (a
    // folder without a set is still recorded, so that a folder full of duplicates isn't re-parsed on every load)
    u32 nb_diffs_saved = 0;
    Sync::shared_lock diff_lock(this->beatmap_difficulties_mtx);
    maps.write<u32>(this->neomod_folders.size() + this->peppy_folders.size());
    auto write_records = [&](const Hash::unstable_stringmap<FolderRecord> &records, MapRoot root) {
        for(const auto &[rel_folder, rec] : records) {
            maps.write_string(rel_folder);
            maps.write<i64>(rec.mtime);
            maps.write<u8>(root == MapRoot::Peppy ? 1 : 0);
            maps.write<i32>(rec.set ? rec.set->getSetID() : -1);
            maps.write<u16>(rec.set ? rec.set->getDifficulties().size() : 0);
            if(!rec.set) continue;

            for(const auto &diff : rec.set->getDifficulties()) {
                maps.write_string(env->getFileNameFromFilePath(diff->getFilePath()));
                maps.write<i32>(diff->iID);
                maps.write_string(diff->getTitleLatin());
                maps.write_string(diff->getAudioFileName());
                maps.write<i32>((i32)diff->iLengthMS);  // TODO: weird casting
                maps.write<f32>(diff->fStackLeniency);
                maps.write_string(diff->getArtistLatin());
                maps.write_string(diff->getCreator());
                maps.write_string(diff->getDifficultyName());
                maps.write_string(diff->getSource());
                maps.write_string(diff->getTags());
                maps.write_hash_digest(diff->getMD5());
                maps.write<f32>(diff->fAR);
                maps.write<f32>(diff->fCS);
                maps.write<f32>(diff->fHP);
                maps.write<f32>(diff->fOD);
                maps.write<f64>(diff->fSliderMultiplier);
                maps.write<u32>(diff->iPreviewTime);
                maps.write<i64>(diff->last_modification_time);
                maps.write<i16>(diff->iLocalOffset);
                maps.write<i16>(diff->iOnlineOffset);
                maps.write<u16>(diff->iNumCircles);
                maps.write<u16>(diff->iNumSliders);
                maps.write<u16>(diff->iNumSpinners);
                maps.write<f64>(diff->fStarsNomod);
                maps.write<i32>(diff->iMinBPM);
                maps.write<i32>(diff->iMaxBPM);
                maps.write<i32>(diff->iMostCommonBPM);
                maps.write<u8>(diff->draw_background);
                maps.write<f32>(diff->loudness.load(std::memory_order_acquire));
                maps.write_string(diff->getTitleUnicode());
                maps.write_string(diff->getArtistUnicode());
                maps.write_string(diff->getBackgroundImageFileName());
                maps.write<u32>(diff->ppv2Version);
                maps.write<i64>(diff->last_play_time);

                nb_diffs_saved++;
            }
        }
    };
    write_records(this->neomod_folders, MapRoot::Neomod);
    write_records(this->peppy_folders, MapRoot::Peppy);  // empty unless the songs folder is loaded raw
    diff_lock.unlock();

    Hash::flat::map<MD5Hash, MapOverrides> real_overrides;

    // We want to save settings we applied on peppy-imported maps
    // When calculating loudness we don't call update_overrides() for performance reasons
    {
        Sync::unique_lock lock(this->peppy_overrides_mtx);
        for(const auto *map : this->loudness_to_calc) {
            if(map->type != DatabaseBeatmap::BeatmapType::PEPPY_DIFFICULTY) continue;
            if(map->loudness.load(std::memory_order_acquire) == 0.f) continue;
            this->peppy_overrides[map->getMD5()] = map->get_overrides();
        }

        // avoid adding overrides with empty/0/"suspicious" hashes
        real_overrides.reserve(this->peppy_overrides.size());

        for(const auto &it : this->peppy_overrides) {
            if(it.first.is_suspicious()) continue;
            real_overrides.emplace(it);
        }
    }

    u32 nb_overrides = 0;

    maps.write<u32>(real_overrides.size());
    for(const auto &[hash, override_] : real_overrides) {
        maps.write_hash_digest(hash);
        maps.write<i16>(override_.local_offset);
        maps.write<i16>(override_.online_offset);
        maps.write<f32>(override_.star_rating);
        maps.write<f32>(override_.loudness);
        maps.write<i32>(override_.min_bpm);
        maps.write<i32>(override_.max_bpm);
        maps.write<i32>(override_.avg_bpm);
        maps.write<u8>(override_.draw_background);
        maps.write_string(override_.background_image_filename);
        maps.write<u32>(override_.ppv2_version);
        maps.write<i64>(override_.last_played);

        nb_overrides++;
    }

    // star ratings section
    // header: num_speeds, num_combos so we can detect layout changes without bumping db version
    u32 nb_star_entries = 0;
    {
        Sync::shared_lock lock(this->star_ratings_mtx);
        maps.write<u8>(StarPrecalc::SPEEDS_NUM);
        maps.write<u8>(StarPrecalc::NUM_MOD_COMBOS);
        maps.write<u32>(this->star_ratings.size());
        for(const auto &[hash, ratings] : this->star_ratings) {
            maps.write_hash_digest(hash);
            maps.write_bytes(reinterpret_cast<const u8 *>(ratings->data()),
                             sizeof(f32) * StarPrecalc::NUM_PRECALC_RATINGS);
            nb_star_entries++;
        }
    }

    t.update();
    debugLog("Saved {:d} maps (+ {:d} overrides, {:d} star ratings) in {:f} seconds.", nb_diffs_saved, nb_overrides,
             nb_star_entries, t.getElapsedTime());
}

void Database::findDatabases() {
    this->bytes_processed = 0;
    this->total_bytes = 0;
    this->database_files.clear();
    this->external_databases.clear();

    using enum DatabaseType;
    this->database_files.emplace(STABLE_SCORES, getDBPath(STABLE_SCORES));
    this->database_files.emplace(NEOMOD_SCORES, getDBPath(NEOMOD_SCORES));
    this->database_files.emplace(MCNEOMOD_SCORES, getDBPath(MCNEOMOD_SCORES));  // mcneomod database

    // ignore if explicitly disabled
    if(cv::database_enabled.getBool()) {
        this->database_files.emplace(STABLE_MAPS, getDBPath(STABLE_MAPS));
    }

    this->database_files.emplace(NEOMOD_MAPS, getDBPath(NEOMOD_MAPS));

    this->database_files.emplace(STABLE_COLLECTIONS, getDBPath(STABLE_COLLECTIONS));
    this->database_files.emplace(MCNEOMOD_COLLECTIONS, getDBPath(MCNEOMOD_COLLECTIONS));

    for(const auto &db_path : this->extern_db_paths_to_import_async_copy) {
        auto db_type = getDBType(db_path);
        if(db_type != INVALID_DB) {
            debugLog("adding external DB {} (type {}) for import", db_path, static_cast<u8>(db_type));
            const auto &[_, added] = this->external_databases.emplace(db_type, db_path);
            if(!added) {
                debugLog("NOTE: ignored duplicate database {}", db_path);
            }
        } else {
            debugLog("invalid external database: {}", db_path);
        }
    }

    for(const auto &[type, pathstr] : this->database_files) {
        std::error_code ec;
        auto db_filesize = std::filesystem::file_size(File::getFsPath(pathstr), ec);
        if(!ec && db_filesize > 0) {
            this->total_bytes += db_filesize;
        }
    }

    for(const auto &[type, pathstr] : this->external_databases) {
        std::error_code ec;
        auto db_filesize = std::filesystem::file_size(File::getFsPath(pathstr), ec);
        if(!ec && db_filesize > 0) {
            this->total_bytes += db_filesize;
        }
    }
}

// Detects what type of database it is, then imports it
bool Database::importDatabase(const std::pair<DatabaseType, std::string> &db_pair) {
    using enum DatabaseType;
    auto db_type = db_pair.first;
    auto db_path = db_pair.second;
    switch(db_type) {
        case INVALID_DB:
            return false;
        case NEOMOD_SCORES: {
            this->loadScores(db_path);
            return true;
        }
        case MCNEOMOD_SCORES: {
            this->loadOldMcNeomodScores(db_path);
            return true;
        }
        case MCNEOMOD_COLLECTIONS:
            return Collections::load_mcneomod(db_path);
        case NEOMOD_MAPS: {
            debugLog("tried to import external " PACKAGE_NAME "_maps.db {}, not supported", db_path);
            return false;
        }
        case STABLE_SCORES: {
            this->loadPeppyScores(db_path);
            return true;
        }
        case STABLE_COLLECTIONS:
            return Collections::load_peppy(db_path);
        case STABLE_MAPS: {
            debugLog("tried to import external stable maps db {}, not supported", db_path);
            return false;
        }
    }

    std::unreachable();
}

void Database::loadScores(std::string_view dbPath) {
    ByteBufferedFile::Reader dbr(dbPath);
    if(dbr.get_total_size() == 0) {
        this->bytes_processed += dbr.get_total_size();
        return;
    }

    u32 nb_neomod_scores = 0;
    std::array<u8, 6> magic_bytes{};
    if(dbr.read_bytes(magic_bytes.data(), 5) != 5 || memcmp(magic_bytes.data(), "NEOSC", 5) != 0) {
        ui->getNotificationOverlay()->addToast("Failed to load " PACKAGE_NAME "_scores.db!", ERROR_TOAST);
        this->bytes_processed += dbr.get_total_size();
        return;
    }

    u32 db_version = dbr.read<u32>();
    if(db_version > NEOMOD_SCORE_DB_VERSION) {
        debugLog(PACKAGE_NAME "_scores.db version is newer than current " PACKAGE_NAME " version!");
        this->bytes_processed += dbr.get_total_size();
        return;
    } else if(db_version < NEOMOD_SCORE_DB_VERSION) {
        // Reading from older database version: backup just in case
        auto backup_path = fmt::format("{}.{}-{:%F}", dbPath, db_version, fmt::gmtime(std::time(nullptr)));
        if(File::copy(dbPath, backup_path)) {
            debugLog("older database {} < {}, backed up {} -> {}", db_version, NEOMOD_SCORE_DB_VERSION, dbPath,
                     backup_path);
        }
    }

    u32 nb_beatmaps = dbr.read<u32>();
    u32 nb_scores = dbr.read<u32>();
    this->scores.reserve(nb_beatmaps);

    for(u32 b = 0; b < nb_beatmaps; b++) {
        MD5Hash beatmap_hash;
        (void)dbr.read_hash_chars(beatmap_hash);  // TODO: validate

        u32 nb_beatmap_scores = dbr.read<u32>();

        for(u32 s = 0; s < nb_beatmap_scores; s++) {
            FinishedScore sc;

            sc.mods = Replay::Mods::unpack(dbr);
            sc.score = dbr.read<u64>();
            sc.spinner_bonus = dbr.read<u64>();
            sc.unix_timestamp = dbr.read<i64>();
            sc.player_id = dbr.read<i32>();
            dbr.read_string(sc.playerName);
            sc.grade = (ScoreGrade)dbr.read<u8>();

            dbr.read_string(sc.client);
            dbr.read_string(sc.server);
            sc.bancho_score_id = dbr.read<i64>();
            sc.peppy_replay_tms = dbr.read<i64>();

            sc.num300s = dbr.read<u16>();
            sc.num100s = dbr.read<u16>();
            sc.num50s = dbr.read<u16>();
            sc.numGekis = dbr.read<u16>();
            sc.numKatus = dbr.read<u16>();
            sc.numMisses = dbr.read<u16>();
            sc.comboMax = dbr.read<u16>();

            sc.ppv2_version = dbr.read<u32>();
            sc.ppv2_score = dbr.read<f32>();
            sc.ppv2_total_stars = dbr.read<f32>();
            sc.ppv2_aim_stars = dbr.read<f32>();
            sc.ppv2_speed_stars = dbr.read<f32>();

            sc.numSliderBreaks = dbr.read<u16>();
            sc.unstableRate = dbr.read<f32>();
            sc.hitErrorAvgMin = dbr.read<f32>();
            sc.hitErrorAvgMax = dbr.read<f32>();
            // TODO: look into casts...
            sc.maxPossibleCombo = (int)dbr.read<u32>();
            sc.numHitObjects = (int)dbr.read<u32>();
            sc.numCircles = (int)dbr.read<u32>();

            sc.beatmap_hash = beatmap_hash;

            this->addScoreRaw(sc);
            nb_neomod_scores++;
        }

        u32 progress_bytes = this->bytes_processed + dbr.get_total_pos();
        f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
        this->loading_progress = (f32)std::clamp(progress_float, 0.01, 0.99);
    }

    if(nb_neomod_scores != nb_scores) {
        debugLog("Inconsistency in " PACKAGE_NAME "_scores.db! Expected {:d} scores, found {:d}!", nb_scores,
                 nb_neomod_scores);
    }

    debugLog("Loaded {:d} " PACKAGE_NAME " scores", nb_neomod_scores);
    this->bytes_processed += dbr.get_total_size();
}

// import scores from mcosu, or old neomod (before we started saving replays)
void Database::loadOldMcNeomodScores(std::string_view dbPath) {
    ByteBufferedFile::Reader dbr(dbPath);

    u32 db_version = dbr.read<u32>();
    if(dbr.get_total_size() == 0 || db_version == 0) {
        this->bytes_processed += dbr.get_total_size();
        return;
    }

    u32 nb_imported = 0;
    const bool is_mcosu = (db_version == 20210106 || db_version == 20210108 || db_version == 20210110);
    const bool is_neomod =
        !is_mcosu && db_version > 20210110;  // if it's older, it can't be neomod (too old mcosu database)

    if(is_neomod) {
        u32 nb_beatmaps = dbr.read<u32>();
        for(u32 b = 0; b < nb_beatmaps; b++) {
            u32 progress_bytes = this->bytes_processed + dbr.get_total_pos();
            f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
            this->loading_progress = (f32)std::clamp(progress_float, 0.01, 0.99);
            MD5Hash md5hash;
            (void)dbr.read_hash_chars(md5hash);  // TODO: validate
            u32 nb_scores = dbr.read<u32>();

            for(u32 s = 0; s < nb_scores; s++) {
                dbr.skip<u8>();   // gamemode (always 0)
                dbr.skip<u32>();  // score version

                FinishedScore sc;

                sc.unix_timestamp = dbr.read<i64>();
                dbr.read_string(sc.playerName);
                sc.num300s = dbr.read<u16>();
                sc.num100s = dbr.read<u16>();
                sc.num50s = dbr.read<u16>();
                sc.numGekis = dbr.read<u16>();
                sc.numKatus = dbr.read<u16>();
                sc.numMisses = dbr.read<u16>();
                sc.score = dbr.read<u64>();
                sc.comboMax = dbr.read<u16>();
                sc.mods = Replay::Mods::from_legacy(dbr.read<LegacyFlags>());
                sc.numSliderBreaks = dbr.read<u16>();
                sc.ppv2_version = 20220902;
                sc.ppv2_score = dbr.read<f32>();
                sc.unstableRate = dbr.read<f32>();
                sc.hitErrorAvgMin = dbr.read<f32>();
                sc.hitErrorAvgMax = dbr.read<f32>();
                sc.ppv2_total_stars = dbr.read<f32>();
                sc.ppv2_aim_stars = dbr.read<f32>();
                sc.ppv2_speed_stars = dbr.read<f32>();
                sc.mods.speed = dbr.read<f32>();
                sc.mods.cs_override = dbr.read<f32>();
                sc.mods.ar_override = dbr.read<f32>();
                sc.mods.od_override = dbr.read<f32>();
                sc.mods.hp_override = dbr.read<f32>();
                // TODO: look into casts...
                sc.maxPossibleCombo = (i32)dbr.read<u32>();
                sc.numHitObjects = (i32)dbr.read<u32>();
                sc.numCircles = (i32)dbr.read<u32>();
                sc.bancho_score_id = dbr.read<u32>();
                sc.client = PACKAGE_NAME "-win64-release-35.10";  // we don't know the actual version
                dbr.read_string(sc.server);

                std::string experimentalModsConVars = dbr.read_string();
                auto experimentalMods = SString::split(experimentalModsConVars, ';');
                for(const auto mod : experimentalMods) {
                    using namespace flags::operators;
                    // clang-format off
                    if(mod == "") continue;
                    else if(mod == "fposu_mod_strafing") sc.mods.flags |= ModFlags::FPoSu_Strafing;
                    else if(mod == "osu_mod_wobble") sc.mods.flags |= ModFlags::Wobble1;
                    else if(mod == "osu_mod_wobble2") sc.mods.flags |= ModFlags::Wobble2;
                    else if(mod == "osu_mod_arwobble") sc.mods.flags |= ModFlags::ARWobble;
                    else if(mod == "osu_mod_timewarp") sc.mods.flags |= ModFlags::Timewarp;
                    else if(mod == "osu_mod_artimewarp") sc.mods.flags |= ModFlags::ARTimewarp;
                    else if(mod == "osu_mod_minimize") sc.mods.flags |= ModFlags::Minimize;
                    else if(mod == "osu_mod_fadingcursor") sc.mods.flags |= ModFlags::FadingCursor;
                    else if(mod == "osu_mod_fps") sc.mods.flags |= ModFlags::FPS;
                    else if(mod == "osu_mod_jigsaw1") sc.mods.flags |= ModFlags::StrictClicks;
                    else if(mod == "osu_mod_jigsaw2") sc.mods.flags |= ModFlags::PreciseSliders;
                    else if(mod == "osu_mod_fullalternate") sc.mods.flags |= ModFlags::FullAlternate;
                    else if(mod == "osu_mod_reverse_sliders") sc.mods.flags |= ModFlags::ReverseSliders;
                    else if(mod == "osu_mod_no50s") sc.mods.flags |= ModFlags::No50s;
                    else if(mod == "osu_mod_no100s") sc.mods.flags |= ModFlags::No100s;
                    else if(mod == "osu_mod_ming3012") sc.mods.flags |= ModFlags::Ming3012;
                    else if(mod == "osu_mod_halfwindow") sc.mods.flags |= ModFlags::HalfWindow;
                    else if(mod == "osu_mod_millhioref") sc.mods.flags |= ModFlags::Millhioref;
                    else if(mod == "osu_mod_mafham") sc.mods.flags |= ModFlags::Mafham;
                    else if(mod == "osu_mod_strict_tracking") sc.mods.flags |= ModFlags::StrictTracking;
                    else if(mod == "osu_playfield_mirror_horizontal") sc.mods.flags |= ModFlags::MirrorHorizontal;
                    else if(mod == "osu_playfield_mirror_vertical") sc.mods.flags |= ModFlags::MirrorVertical;
                    else if(mod == "osu_mod_shirone") sc.mods.flags |= ModFlags::Shirone;
                    else if(mod == "osu_mod_approach_different") sc.mods.flags |= ModFlags::ApproachDifferent;
                    else if(mod == "osu_mod_no_spinners") sc.mods.flags |= ModFlags::SpunOut;
                    // clang-format on
                }

                sc.beatmap_hash = md5hash;
                sc.perfect = sc.comboMax >= sc.maxPossibleCombo;
                sc.grade = sc.calculate_grade();

                if(this->addScoreRaw(sc)) {
                    nb_imported++;
                }
            }
        }

        debugLog("Loaded {} old-" PACKAGE_NAME " scores", nb_imported);
    } else {  // mcosu (this is copy-pasted from mcosu-ng)
        const int numBeatmaps = dbr.read<int32_t>();
        debugLog("McOsu scores: version = {}, numBeatmaps = {}", db_version, numBeatmaps);

        for(int b = 0; b < numBeatmaps; b++) {
            u32 progress_bytes = this->bytes_processed + dbr.get_total_pos();
            f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
            this->loading_progress = (f32)std::clamp(progress_float, 0.01, 0.99);

            const std::string md5hash_str = dbr.read_string();
            if(md5hash_str.length() < 32) {
                debugLog("WARNING: Invalid score on beatmap {:d} with md5hash_str.length() = {:d}!", b,
                         md5hash_str.length());
                continue;
            } else if(md5hash_str.length() > 32) {
                debugLog("ERROR: Corrupt score database/entry detected, stopping.");
                break;
            }

            MD5Hash md5hash{md5hash_str.c_str()};

            const int numScores = dbr.read<int32_t>();
            logIfCV(debug_db, "Beatmap[{}]: md5hash = {:s}, numScores = {}", b, md5hash, numScores);

            for(u32 s = 0; s < numScores; s++) {
                const auto gamemode = dbr.read<uint8_t>();  // NOTE: abused as isImportedLegacyScore flag (because I
                                                            // forgot to add a version cap to old builds)
                const int scoreVersion = dbr.read<int32_t>();
                if(db_version == 20210103 && scoreVersion > 20190103) {
                    /* isImportedLegacyScore = */ dbr.skip<uint8_t>();  // too lazy to handle this logic
                }
                auto unixTimestamp = dbr.read<uint64_t>();
                const std::string playerName{dbr.read_string()};
                if(this->isScoreAlreadyInDB(md5hash, static_cast<int64_t>(unixTimestamp), playerName) >= 0) {
                    u32 bytesToSkipUntilNextScore = 0;
                    bytesToSkipUntilNextScore +=
                        (sizeof(uint16_t) * 8) + (sizeof(int64_t)) + (sizeof(int32_t)) + (sizeof(f32) * 12);
                    if(scoreVersion > 20180722) {
                        // maxPossibleCombos
                        bytesToSkipUntilNextScore += sizeof(int32_t) * 3;
                    }
                    dbr.skip_bytes(bytesToSkipUntilNextScore);
                    dbr.skip_string();  // experimentalMods
                    logIfCV(debug_db, "skipped score {} (already loaded from " PACKAGE_NAME "_scores.db)", md5hash);
                    continue;
                }

                // default
                const auto num300s = dbr.read<uint16_t>();
                const auto num100s = dbr.read<uint16_t>();
                const auto num50s = dbr.read<uint16_t>();
                const auto numGekis = dbr.read<uint16_t>();
                const auto numKatus = dbr.read<uint16_t>();
                const auto numMisses = dbr.read<uint16_t>();

                const auto score = dbr.read<int64_t>();
                const auto maxCombo = dbr.read<uint16_t>();
                const auto mods = Replay::Mods::from_legacy(dbr.read<LegacyFlags>());

                // custom
                const auto numSliderBreaks = dbr.read<uint16_t>();
                const auto pp = dbr.read<f32>();
                const auto unstableRate = dbr.read<f32>();
                const auto hitErrorAvgMin = dbr.read<f32>();
                const auto hitErrorAvgMax = dbr.read<f32>();
                const auto starsTomTotal = dbr.read<f32>();
                const auto starsTomAim = dbr.read<f32>();
                const auto starsTomSpeed = dbr.read<f32>();
                const auto speedMultiplier = dbr.read<f32>();
                const auto CS = dbr.read<f32>();
                const auto AR = dbr.read<f32>();
                const auto OD = dbr.read<f32>();
                const auto HP = dbr.read<f32>();

                int maxPossibleCombo = -1;
                int numHitObjects = -1;
                int numCircles = -1;
                if(scoreVersion > 20180722) {
                    maxPossibleCombo = dbr.read<int32_t>();
                    numHitObjects = dbr.read<int32_t>();
                    numCircles = dbr.read<int32_t>();
                }

                std::string experimentalModsConVars = dbr.read_string();
                auto experimentalMods = SString::split(experimentalModsConVars, ';');

                if(gamemode == 0x0 || (db_version > 20210103 &&
                                       scoreVersion > 20190103))  // gamemode filter (osu!standard) // HACKHACK: for
                                                                  // explanation see hackIsImportedLegacyScoreFlag
                {
                    FinishedScore sc;

                    sc.unix_timestamp = static_cast<int64_t>(unixTimestamp);

                    // default
                    sc.playerName = playerName;

                    sc.num300s = num300s;
                    sc.num100s = num100s;
                    sc.num50s = num50s;
                    sc.numGekis = numGekis;
                    sc.numKatus = numKatus;
                    sc.numMisses = numMisses;
                    sc.score = score;
                    sc.comboMax = maxCombo;
                    sc.perfect = (maxPossibleCombo > 0 && sc.comboMax > 0 && sc.comboMax >= maxPossibleCombo);
                    sc.mods = mods;

                    // custom
                    sc.numSliderBreaks = numSliderBreaks;
                    sc.ppv2_version = 20220902;
                    sc.ppv2_score = pp;
                    sc.unstableRate = unstableRate;
                    sc.hitErrorAvgMin = hitErrorAvgMin;
                    sc.hitErrorAvgMax = hitErrorAvgMax;
                    sc.ppv2_total_stars = starsTomTotal;
                    sc.ppv2_aim_stars = starsTomAim;
                    sc.ppv2_speed_stars = starsTomSpeed;
                    sc.mods.speed = speedMultiplier;
                    sc.mods.cs_override = CS;
                    sc.mods.ar_override = AR;
                    sc.mods.od_override = OD;
                    sc.mods.hp_override = HP;
                    sc.maxPossibleCombo = maxPossibleCombo;
                    sc.numHitObjects = numHitObjects;
                    sc.numCircles = numCircles;
                    for(const auto mod : experimentalMods) {
                        using namespace flags::operators;
                        // clang-format off
                        if(mod == "") continue;
                        else if(mod == "fposu_mod_strafing") sc.mods.flags |= ModFlags::FPoSu_Strafing;
                        else if(mod == "osu_mod_wobble") sc.mods.flags |= ModFlags::Wobble1;
                        else if(mod == "osu_mod_wobble2") sc.mods.flags |= ModFlags::Wobble2;
                        else if(mod == "osu_mod_arwobble") sc.mods.flags |= ModFlags::ARWobble;
                        else if(mod == "osu_mod_timewarp") sc.mods.flags |= ModFlags::Timewarp;
                        else if(mod == "osu_mod_artimewarp") sc.mods.flags |= ModFlags::ARTimewarp;
                        else if(mod == "osu_mod_minimize") sc.mods.flags |= ModFlags::Minimize;
                        else if(mod == "osu_mod_fadingcursor") sc.mods.flags |= ModFlags::FadingCursor;
                        else if(mod == "osu_mod_fps") sc.mods.flags |= ModFlags::FPS;
                        else if(mod == "osu_mod_jigsaw1") sc.mods.flags |= ModFlags::StrictClicks;
                        else if(mod == "osu_mod_jigsaw2") sc.mods.flags |= ModFlags::PreciseSliders;
                        else if(mod == "osu_mod_fullalternate") sc.mods.flags |= ModFlags::FullAlternate;
                        else if(mod == "osu_mod_reverse_sliders") sc.mods.flags |= ModFlags::ReverseSliders;
                        else if(mod == "osu_mod_no50s") sc.mods.flags |= ModFlags::No50s;
                        else if(mod == "osu_mod_no100s") sc.mods.flags |= ModFlags::No100s;
                        else if(mod == "osu_mod_ming3012") sc.mods.flags |= ModFlags::Ming3012;
                        else if(mod == "osu_mod_halfwindow") sc.mods.flags |= ModFlags::HalfWindow;
                        else if(mod == "osu_mod_millhioref") sc.mods.flags |= ModFlags::Millhioref;
                        else if(mod == "osu_mod_mafham") sc.mods.flags |= ModFlags::Mafham;
                        else if(mod == "osu_mod_strict_tracking") sc.mods.flags |= ModFlags::StrictTracking;
                        else if(mod == "osu_playfield_mirror_horizontal") sc.mods.flags |= ModFlags::MirrorHorizontal;
                        else if(mod == "osu_playfield_mirror_vertical") sc.mods.flags |= ModFlags::MirrorVertical;
                        else if(mod == "osu_mod_shirone") sc.mods.flags |= ModFlags::Shirone;
                        else if(mod == "osu_mod_approach_different") sc.mods.flags |= ModFlags::ApproachDifferent;
                        else if(mod == "osu_mod_no_spinners") sc.mods.flags |= ModFlags::SpunOut;
                        // clang-format on
                    }

                    sc.beatmap_hash = md5hash;
                    sc.perfect = sc.comboMax >= sc.maxPossibleCombo;
                    sc.grade = sc.calculate_grade();
                    sc.client = fmt::format("mcosu-{}", scoreVersion);

                    if(this->addScoreRaw(sc)) {
                        nb_imported++;
                    }
                }
            }
        }
        debugLog("Loaded {} McOsu scores", nb_imported);
    }

    this->bytes_processed += dbr.get_total_size();
}

void Database::loadPeppyScores(std::string_view dbPath) {
    ByteBufferedFile::Reader dbr(dbPath);
    int nb_imported = 0;

    u32 db_version = dbr.read<u32>();
    u32 nb_beatmaps = dbr.read<u32>();
    if(dbr.get_total_size() == 0 || db_version == 0) {
        this->bytes_processed += dbr.get_total_size();
        return;
    }

    debugLog("osu!stable scores.db: version = {:d}, nb_beatmaps = {:d}", db_version, nb_beatmaps);

    for(u32 b = 0; b < nb_beatmaps; b++) {
        const std::string md5hash_str = dbr.read_string();
        if(md5hash_str.length() < 32) {
            debugLog("WARNING: Invalid score on beatmap {:d} with md5hash_str.length() = {:d}!", b,
                     md5hash_str.length());
            continue;
        } else if(md5hash_str.length() > 32) {
            debugLog("ERROR: Corrupt score database/entry detected, stopping.");
            break;
        }

        MD5Hash md5hash{md5hash_str.c_str()};

        u32 nb_scores = dbr.read<u32>();

        for(u32 s = 0; s < nb_scores; s++) {
            FinishedScore sc;

            u8 gamemode = dbr.read<u8>();

            u32 score_version = dbr.read<u32>();
            sc.client = fmt::format("peppy-{}", score_version);

            sc.server = "ppy.sh";
            dbr.skip_string();  // beatmap hash (already have it)
            dbr.read_string(sc.playerName);
            dbr.skip_string();  // replay hash (unused)

            sc.num300s = dbr.read<u16>();
            sc.num100s = dbr.read<u16>();
            sc.num50s = dbr.read<u16>();
            sc.numGekis = dbr.read<u16>();
            sc.numKatus = dbr.read<u16>();
            sc.numMisses = dbr.read<u16>();

            i32 score = dbr.read<i32>();
            sc.score = (score < 0 ? 0 : score);

            sc.comboMax = dbr.read<u16>();
            sc.perfect = dbr.read<u8>();
            sc.mods = Replay::Mods::from_legacy(dbr.read<LegacyFlags>());

            dbr.skip_string();  // hp graph

            const i64 full_tms = dbr.read<i64>();
            sc.unix_timestamp = (full_tms - LegacyReplay::UNIX_EPOCH_TICKS) / LegacyReplay::TICKS_PER_SECOND;
            sc.peppy_replay_tms = full_tms - 504911232000000000;

            // Always -1, but let's skip it properly just in case
            i32 old_replay_size = dbr.read<i32>();
            if(old_replay_size > 0) {
                dbr.skip_bytes(old_replay_size);
            }

            if(score_version >= 20131110) {
                sc.bancho_score_id = dbr.read<i64>();
            } else if(score_version >= 20121008) {
                sc.bancho_score_id = dbr.read<i32>();
            } else {
                sc.bancho_score_id = 0;
            }

            if(sc.mods.has(ModFlags::Target)) {
                dbr.skip<f64>();  // total accuracy
            }

            if(gamemode == 0 && sc.bancho_score_id != 0) {
                sc.beatmap_hash = md5hash;
                sc.grade = sc.calculate_grade();

                if(this->addScoreRaw(sc)) {
                    nb_imported++;
                }
            }
        }

        u32 progress_bytes = this->bytes_processed + dbr.get_total_pos();
        f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
        this->loading_progress = (f32)std::clamp(progress_float, 0.01, 0.99);
    }

    debugLog("Loaded {:d} osu!stable scores", nb_imported);
    this->bytes_processed += dbr.get_total_size();
}

void Database::saveScores() {
    debugLog("Osu: Saving scores ...");
    if(!this->scores_loaded) {
        debugLog("Cannot save scores since they weren't loaded properly first!");
        return;
    }

    const double startTime = Timing::getTimeReal();

    const auto neomod_scores_db = getDBPath(DatabaseType::NEOMOD_SCORES);

    ByteBufferedFile::Writer dbr(neomod_scores_db);

    if(!dbr.good()) {
        debugLog("Cannot save scores to {}: {}", neomod_scores_db, dbr.error());
        return;
    }

    dbr.write_bytes(reinterpret_cast<const u8 *>("NEOSC"), 5);
    dbr.write<u32>(NEOMOD_SCORE_DB_VERSION);

    u32 nb_beatmaps = 0;
    u32 nb_scores = 0;

    Sync::shared_lock lock(this->scores_mtx);  // only need read lock here
    for(const auto &[_, scorevec] : this->scores) {
        u32 beatmap_scores = scorevec.size();
        if(beatmap_scores > 0) {
            nb_beatmaps++;
            nb_scores += beatmap_scores;
        }
    }
    dbr.write<u32>(nb_beatmaps);
    dbr.write<u32>(nb_scores);

    for(const auto &[hash, scorevec] : this->scores) {
        if(scorevec.empty()) continue;
        if(!dbr.good()) {
            break;
        }

        // TODO: should store as digest directly, need score db version bump
        dbr.write_hash_chars(hash);
        dbr.write<u32>(scorevec.size());

        for(const auto &score : scorevec) {
            assert(!score.is_online_score);
            if(!dbr.good()) {
                break;
            }

            Replay::Mods::pack_and_write(dbr, score.mods);
            dbr.write<u64>(score.score);
            dbr.write<u64>(score.spinner_bonus);
            dbr.write<i64>(score.unix_timestamp);
            dbr.write<i32>(score.player_id);
            dbr.write_string(score.playerName);
            dbr.write<u8>((u8)score.grade);

            dbr.write_string(score.client);
            dbr.write_string(score.server);
            dbr.write<i64>(score.bancho_score_id);
            dbr.write<i64>(score.peppy_replay_tms);

            dbr.write<u16>(score.num300s);
            dbr.write<u16>(score.num100s);
            dbr.write<u16>(score.num50s);
            dbr.write<u16>(score.numGekis);
            dbr.write<u16>(score.numKatus);
            dbr.write<u16>(score.numMisses);
            dbr.write<u16>(score.comboMax);

            dbr.write<u32>(score.ppv2_version);
            dbr.write<f32>(score.ppv2_score);
            dbr.write<f32>(score.ppv2_total_stars);
            dbr.write<f32>(score.ppv2_aim_stars);
            dbr.write<f32>(score.ppv2_speed_stars);

            dbr.write<u16>(score.numSliderBreaks);
            dbr.write<f32>(score.unstableRate);
            dbr.write<f32>(score.hitErrorAvgMin);
            dbr.write<f32>(score.hitErrorAvgMax);
            dbr.write<u32>(score.maxPossibleCombo);
            dbr.write<u32>(score.numHitObjects);
            dbr.write<u32>(score.numCircles);
        }
    }

    debugLog("Saved {:d} scores in {:f} seconds.", nb_scores, (Timing::getTimeReal() - startTime));
}

namespace {
// osu!stable accepts any casing of the extension
bool is_osu_file(std::string_view name) {
    return name.size() > 4 && SString::strcase_equal(name.substr(name.size() - 4), ".osu");
}

// 0 if the folder doesn't exist (without the trailing separator, not every stat accepts one on directories)
i64 folder_mtime(std::string_view folder) {
    std::string path{folder};
    while(path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back();
    struct stat64 st{};
    return File::stat_c(path.c_str(), &st) == 0 ? static_cast<i64>(st.st_mtime) : 0;
}

}  // namespace

std::string_view ReconcileResult::outcomeName() const {
    switch(this->outcome) {
        using enum Outcome;
        case Unchanged:
            return "unchanged";
        case Created:
            return "created";
        case Updated:
            return "updated";
        case Removed:
            return "removed";
        case Failed:
            return "failed";
    }
    std::unreachable();
    return "?";
}

std::unique_ptr<DiffContainer> Database::parseFolderDiffs(std::string_view folder_path, bool is_peppy) {
    // (lots of things expect the folder to end with a path separator, if it doesn't, things would blow up in weird ways)
    const std::string beatmapPath{folder_path.ends_with('/') ? folder_path : fmt::format("{:s}/", folder_path)};
    logIfCV(debug_db, "beatmap path: {:s}", beatmapPath);

    using enum DatabaseBeatmap::BeatmapType;
    const auto difficultyType = is_peppy ? PEPPY_DIFFICULTY : NEOMOD_DIFFICULTY;

    auto diffs = std::make_unique<DiffContainer>();
    DatabaseBeatmap::LoadError lastError;
    for(const auto &beatmapFile : env->getFilesInFolder(beatmapPath)) {
        if(!is_osu_file(beatmapFile)) continue;

        auto map = std::make_unique<BeatmapDifficulty>(beatmapPath + beatmapFile, beatmapPath, difficultyType);
        auto res = map->loadMetadata();
        if(!res.error.errc) {
            diffs->push_back(std::move(map));
        } else {
            lastError = res.error;
            logIfCV(debug_db, "Couldn't loadMetadata: {}, deleting object.", res.error.error_string());
        }
    }

    if(diffs->empty() && lastError.errc) {
        debugLog("Couldn't load beatmapset {}: {}", beatmapPath, lastError.error_string());
    }
    return diffs;
}

std::unique_ptr<BeatmapSet> Database::loadRawBeatmap(std::string_view beatmapPath, bool is_peppy) {
    auto diffs = parseFolderDiffs(beatmapPath, is_peppy);
    if(diffs->empty()) return nullptr;

    using enum DatabaseBeatmap::BeatmapType;
    return std::make_unique<BeatmapSet>(std::move(diffs), is_peppy ? PEPPY_BEATMAPSET : NEOMOD_BEATMAPSET);
}

ReconcileResult Database::reconcileFolder(MapRoot root, std::string_view rel_folder, ReconcileMode mode,
                                          i32 set_id_override, std::unique_ptr<DiffContainer> preparsed,
                                          i64 dir_mtime) {
    using enum ReconcileResult::Outcome;
    using enum DatabaseBeatmap::BeatmapType;
    ReconcileResult res;

    const std::string abs_folder = this->rootPath(root) + std::string{rel_folder} + "/";
    auto &folders = this->folderIndex(root);
    if(dir_mtime == 0) dir_mtime = folder_mtime(abs_folder);

    // snapshot the record (the map may rehash while this folder is being worked on)
    bool known = false;
    FolderRecord rec;
    {
        Sync::shared_lock lock(this->beatmap_difficulties_mtx);
        if(auto it = folders.find(rel_folder); it != folders.end()) {
            known = true;
            rec = it->second;
        }
    }
    BeatmapSet *old = rec.set;

    // retires a set: its remaining diffs leave the md5 index and the set itself moves from beatmapsets into the
    // graveyard (raw pointers to it and its diffs stay valid until the next load). caller holds the unique lock
    auto tombstone = [this](BeatmapSet *set) {
        for(const auto &diff : set->getDifficulties()) {
            if(auto it = this->beatmap_difficulties.find(diff->getMD5());
               it != this->beatmap_difficulties.end() && it->second == diff.get()) {
                this->beatmap_difficulties.erase(it);
            }
        }
        this->unindexSet(set);
        if(auto it = std::ranges::find_if(this->beatmapsets, [set](const auto &owned) { return owned.get() == set; });
           it != this->beatmapsets.end()) {
            this->graveyard.push_back(std::move(*it));
            this->beatmapsets.erase(it);
        }
    };

    if(dir_mtime == 0) {  // the folder is gone
        if(!known) {
            res.outcome = Failed;
            return res;
        }
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
        folders.erase(folders.find(rel_folder));
        if(old) {  // (a known folder without a set leaves nothing behind: Unchanged)
            res.removed = static_cast<u16>(old->getDifficulties().size());
            tombstone(old);
            res.outcome = Removed;
            res.replaced = old;
            logIfCV(debug_db, "reconcile {}: removed (-{})", rel_folder, res.removed);
        }
        return res;
    }

    if(known && mode == ReconcileMode::TrustFolderMtime && rec.mtime != 0 && rec.mtime == dir_mtime) {
        res.set = old;
        return res;  // unchanged without touching a single file
    }

    // one per .osu file in the folder: what represents it in the rebuilt set is either an old object of this set
    // that survives, or a fresh parse. neither = unloadable, or a copy of a map that's already there
    struct Slot {
        std::string path;
        BeatmapDifficulty *kept{nullptr};
        std::unique_ptr<BeatmapDifficulty> parsed{};
        BeatmapDifficulty *owner{nullptr};  // what the md5 index has under parsed's hash
    };
    std::vector<Slot> slots;

    if(preparsed) {
        // the caller listed and parsed the folder already (on a worker thread, say): every file is matched by
        // content below
        res.parsed = static_cast<u16>(preparsed->size());
        for(auto &diff : *preparsed) {
            slots.push_back({.path = std::string{diff->getFilePath()}, .parsed = std::move(diff)});
        }
    } else {
        // 1. an old object keeps its file without it being opened if the mtime didn't move since it was verified
        //    (a record from before mtimes were verified is trusted and stamped)
        std::vector<File::DirEntry> files;
        File::getDirectoryEntries(abs_folder, File::DirContents::FILES, files);
        for(auto &file : files) {
            if(!is_osu_file(file.name)) continue;
            Slot slot{.path = abs_folder + file.name};
            if(old) {
                const auto &old_diffs = old->getDifficulties();
                if(auto it = std::ranges::find(old_diffs, std::string_view{slot.path}, &DatabaseBeatmap::getFilePath);
                   it != old_diffs.end()) {
                    if((*it)->last_modification_time <= 0) (*it)->last_modification_time = file.mtime;
                    if((*it)->last_modification_time == file.mtime) slot.kept = it->get();
                }
            }
            slots.push_back(std::move(slot));
        }
        // 2. everything else is parsed
        for(auto &slot : slots) {
            if(slot.kept) continue;
            slot.parsed = std::make_unique<BeatmapDifficulty>(
                slot.path, abs_folder, root == MapRoot::Peppy ? PEPPY_DIFFICULTY : NEOMOD_DIFFICULTY);
            res.parsed++;
            if(auto r = slot.parsed->loadMetadata(); r.error.errc) {
                logIfCV(debug_db, "reconcile {}: couldn't load {}: {}", rel_folder,
                        Environment::getFileNameFromFilePath(slot.path), r.error.error_string());
                slot.parsed.reset();  // a stale record for it (if any) is dropped below
            }
        }
    }
    std::ranges::sort(slots, {}, &Slot::path);

    // 3. the hash decides what a parsed file is: one index lookup each under the lock, then the decisions (which
    //    may stat other folders) without it
    {
        Sync::shared_lock lock(this->beatmap_difficulties_mtx);
        for(auto &slot : slots) {
            if(!slot.parsed) continue;
            if(auto it = this->beatmap_difficulties.find(slot.parsed->getMD5());
               it != this->beatmap_difficulties.end()) {
                slot.owner = it->second;
            }
        }
    }
    for(uSz i = 0; i < slots.size(); i++) {
        Slot &slot = slots[i];
        if(!slot.parsed) continue;
        BeatmapDifficulty *owner = slot.owner;
        auto name = [&slot] { return Environment::getFileNameFromFilePath(slot.path); };

        if(owner && old && owner->getParentSet() == old) {
            if(std::ranges::any_of(slots, [owner](const Slot &s) { return s.kept == owner; })) {
                // a second copy of a map this folder still has: the first one wins
                logIfCV(debug_db, "reconcile {}: {} duplicates {}", rel_folder, name(), owner->getFilePath());
            } else {
                // the set's own content under a new name (or with a new mtime): the old object follows the file
                owner->sFilePath = slot.path;
                owner->last_modification_time = slot.parsed->last_modification_time;
                slot.kept = owner;
            }
            slot.parsed.reset();
        } else if(owner && Environment::fileExists(owner->getFilePath())) {
            // a copy of a map that still exists elsewhere: the first one wins
            if(!res.dedup_owner) res.dedup_owner = owner->getParentSet();
            logIfCV(debug_db, "reconcile {}: {} duplicates {}", rel_folder, name(), owner->getFilePath());
            slot.parsed.reset();
        } else if(std::ranges::any_of(slots.begin(), slots.begin() + static_cast<sSz>(i), [&slot](const Slot &s) {
                      return s.parsed && s.parsed->getMD5() == slot.parsed->getMD5();
                  })) {
            logIfCV(debug_db, "reconcile {}: {} duplicates another new file in the folder", rel_folder, name());
            slot.parsed.reset();
        }
        // otherwise it's new here: nothing has this content, or the owner's file is gone (the map moved here; the
        // index entry is retargeted below, the stale object stays in its own set until that folder is reconciled)
    }

    const uSz n_old = old ? old->getDifficulties().size() : 0;
    const uSz n_kept = std::ranges::count_if(slots, [](const Slot &s) { return s.kept != nullptr; });
    res.added = static_cast<u16>(std::ranges::count_if(slots, [](const Slot &s) { return s.parsed != nullptr; }));
    res.removed = static_cast<u16>(n_old - n_kept);

    if(res.added == 0 && res.removed == 0) {
        {
            Sync::unique_lock lock(this->beatmap_difficulties_mtx);
            folders[std::string{rel_folder}] = {.mtime = dir_mtime, .set = old};
        }
        if(old) old->updateRepresentativeValues();  // mtimes may have moved
        res.set = old;
        // a new folder with nothing but duplicates (or nothing loadable at all) is recorded so it isn't re-parsed
        res.outcome = (old || res.dedup_owner) ? Unchanged : Failed;
        if(res.parsed > 0 || !old) {
            logIfCV(debug_db, "reconcile {}: {} (parsed {})", rel_folder, res.outcomeName(), res.parsed);
        }
        return res;
    }

    // 4. the rebuilt set: survivors move over as-is (same address, so every raw pointer to them stays valid), the
    //    tombstone keeps only what was dropped (compacted: nothing may ever see a null diff in a set)
    auto container = std::make_unique<DiffContainer>();
    std::vector<BeatmapDifficulty *> fresh;
    for(auto &slot : slots) {
        if(slot.parsed) {
            fresh.push_back(slot.parsed.get());
            container->push_back(std::move(slot.parsed));
        } else if(slot.kept) {
            auto &old_diffs = *old->difficulties;
            auto it = std::ranges::find_if(old_diffs, [&slot](const auto &p) { return p.get() == slot.kept; });
            assert(it != old_diffs.end());
            container->push_back(std::move(*it));
        }
    }
    if(old) std::erase_if(*old->difficulties, [](const auto &p) { return p == nullptr; });

    if(container->empty()) {  // nothing kept and nothing new past the unchanged early-out: the old set lost everything
        assert(old);
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
        tombstone(old);
        folders[std::string{rel_folder}] = {.mtime = dir_mtime, .set = nullptr};
        res.outcome = Removed;
        res.replaced = old;
        logIfCV(debug_db, "reconcile {}: {} (-{}, parsed {})", rel_folder, res.outcomeName(), res.removed, res.parsed);
        return res;
    }

    auto new_set = std::make_unique<BeatmapSet>(std::move(container),
                                                root == MapRoot::Peppy ? PEPPY_BEATMAPSET : NEOMOD_BEATMAPSET);
    BeatmapSet *set = new_set.get();

    // set id: the download's, else what the old set already knew, else whatever a diff declares, else a leading
    // number in the folder name (osu!stable's "<setid> Artist - Title" convention)
    i32 set_id = set_id_override;
    if(set_id <= 0 && old) set_id = old->getSetID();
    if(set_id <= 0) {
        for(const auto &diff : set->getDifficulties()) {
            if(diff->iSetID > 0) {
                set_id = diff->iSetID;
                break;
            }
        }
    }
    if(set_id <= 0 && !rel_folder.empty() && std::isdigit(static_cast<unsigned char>(rel_folder.front()))) {
        i32 parsed_id = -1;
        if(Parsing::parse(rel_folder, &parsed_id) && parsed_id > 0) set_id = parsed_id;
    }
    if(set_id > 0) {
        set->iSetID = set_id;
        for(const auto &diff : set->getDifficulties()) {
            if(diff->iSetID <= 0) diff->iSetID = set_id;
        }
    }

    {
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
        for(BeatmapDifficulty *diff : fresh) this->beatmap_difficulties[diff->getMD5()] = diff;
        if(old) tombstone(old);  // only the dropped diffs are left in it
        this->indexSet(set);
        this->beatmapsets.push_back(std::move(new_set));
        folders[std::string{rel_folder}] = {.mtime = dir_mtime, .set = set};
    }

    // what loadMaps does for db-loaded diffs after the fact
    {
        Sync::shared_lock sr_lock(this->star_ratings_mtx);
        for(BeatmapDifficulty *diff : fresh) {
            if(auto it = this->star_ratings.find(diff->getMD5()); it != this->star_ratings.end()) {
                diff->star_ratings = it->second.get();
            }
        }
    }
    {
        Sync::shared_lock score_lock(this->scores_mtx);
        for(BeatmapDifficulty *diff : fresh) {
            auto it = this->scores.find(diff->getMD5());
            if(it == this->scores.end()) continue;
            for(const auto &score : it->second) {
                if(diff->getLastPlayTime() < score.unix_timestamp) diff->setLastPlayTime(score.unix_timestamp);
            }
        }
    }
    if(this->isFinished()) {
        // post-load import: the batch passes already ran, so request the calcs explicitly
        this->batch_diffcalc_pending = true;  // picked up by SongBrowser::update
        for(BeatmapDifficulty *diff : fresh) VolNormalization::request_priority(diff);
    } else {
        for(BeatmapDifficulty *diff : fresh) this->loudness_to_calc.push_back(diff);
    }

    res.set = set;
    res.replaced = old;
    res.outcome = old ? Updated : Created;
    logIfCV(debug_db, "reconcile {}: {} (+{} -{}, parsed {})", rel_folder, res.outcomeName(), res.added, res.removed,
            res.parsed);
    return res;
}

u32 Database::reconcileRoot(MapRoot root, const Sync::stop_token &tok, bool per_file) {
    using enum ReconcileResult::Outcome;
    Timer t;
    t.start();

    // the listing's mtimes are what the records get: a folder that changes between this listing and its
    // reconcile is verified again next time (the other way around could record a change the parse didn't see)
    const std::string root_path = this->rootPath(root);
    std::vector<File::DirEntry> on_disk;
    File::getDirectoryEntries(root_path, File::DirContents::DIRECTORIES, on_disk);
    Hash::flat::set<std::string_view> on_disk_set;
    on_disk_set.reserve(on_disk.size());
    for(const auto &e : on_disk) on_disk_set.insert(e.name);

    // snapshot the known folders (reconcileFolder mutates the map)
    std::vector<std::string> known;
    {
        Sync::shared_lock lock(this->beatmap_difficulties_mtx);
        const auto &folders = this->folderIndex(root);
        known.reserve(folders.size());
        for(const auto &[rel, _] : folders) known.push_back(rel);
    }
    Hash::flat::set<std::string_view> known_set(known.begin(), known.end());

    u32 n_removed = 0, n_updated = 0, n_created = 0, n_parsed = 0;

    // this pass isn't in the loading percentage's byte budget: the overlay counts folders instead (the vanished,
    // the unknown and the known ones, once each)
    u32 n_gone = 0;
    for(const auto &rel : known) n_gone += !on_disk_set.contains(rel);
    u32 done = 0;
    this->load_stage.store(LoadStage::ScanningFolders, std::memory_order_release);
    this->stage_done.store(0, std::memory_order_release);
    this->stage_total.store(n_gone + static_cast<u32>(on_disk.size()), std::memory_order_release);

    // vanished folders first, so a renamed folder is a plain remove + create
    for(const auto &rel : known) {
        if(on_disk_set.contains(rel)) continue;
        if(tok.stop_requested()) return n_created;
        n_removed += this->reconcileFolder(root, rel, ReconcileMode::TrustFolderMtime, -1, nullptr).outcome == Removed;
        this->stage_done.store(++done, std::memory_order_release);
    }

    // unknown folders are parsed in parallel (same bounded window and deadlock-freedom argument as importLooseOsz)
    std::vector<const File::DirEntry *> unknown;
    for(const auto &e : on_disk) {
        if(!known_set.contains(e.name)) unknown.push_back(&e);
    }
    if(!unknown.empty()) {
        const uSz window_size = std::min<uSz>(std::clamp<uSz>(Async::get_thread_count(), 4, 16), unknown.size());
        auto submit_parse = [&root_path, root](const File::DirEntry &e) {
            return Async::submit(
                [path = root_path + e.name + "/", root] { return parseFolderDiffs(path, root == MapRoot::Peppy); },
                Lane::Background);
        };
        std::vector<Async::Future<std::unique_ptr<DiffContainer>>> window(window_size);
        for(uSz i = 0; i < window_size; i++) window[i] = submit_parse(*unknown[i]);

        for(uSz i = 0; i < unknown.size(); i++) {
            if(tok.stop_requested())
                return n_created;  // in-flight parses are abandoned, their folders are picked up next time

            const auto r = this->reconcileFolder(root, unknown[i]->name, ReconcileMode::TrustFolderMtime, -1,
                                                 window[i % window_size].get(), unknown[i]->mtime);
            n_created += r.outcome == Created;
            n_parsed += r.parsed;
            this->stage_done.store(++done, std::memory_order_release);

            if(const uSz next = i + window_size; next < unknown.size()) {
                window[i % window_size] = submit_parse(*unknown[next]);
            }
        }
    }

    for(const auto &e : on_disk) {
        if(!known_set.contains(e.name)) continue;
        if(tok.stop_requested()) return n_created;
        const auto r = this->reconcileFolder(
            root, e.name, per_file ? ReconcileMode::PerFile : ReconcileMode::TrustFolderMtime, -1, nullptr, e.mtime);
        n_updated += r.outcome == Updated || r.outcome == Removed;
        n_parsed += r.parsed;
        this->stage_done.store(++done, std::memory_order_release);
    }

    t.update();
    debugLog(
        "{} {}: {} folders on disk, {} known, {} removed, {} updated, {} new ({} created), {} diffs parsed in "
        "{:.3f}s",
        root_path, per_file ? "rescan" : "startup pass", on_disk.size(), known.size(), n_removed, n_updated,
        unknown.size(), n_created, n_parsed, t.getElapsedTime());
    return n_created;
}

std::string Database::rootPath(MapRoot root) const {
    return root == MapRoot::Neomod ? std::string{NEOMOD_MAPS_PATH "/"} : this->peppy_root;
}

void Database::update_overrides(const BeatmapDifficulty *diff) {
    if(!diff || diff->do_not_store || diff->type != DatabaseBeatmap::BeatmapType::PEPPY_DIFFICULTY) return;

    Sync::unique_lock lock(this->peppy_overrides_mtx);
    this->peppy_overrides[diff->getMD5()] = diff->get_overrides();
}
