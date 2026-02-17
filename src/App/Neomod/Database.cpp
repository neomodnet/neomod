// Copyright (c) 2016, PG, All rights reserved.
#include "Database.h"

#include "AsyncIOHandler.h"
#include "Bancho.h"
#include "Parsing.h"
#include "SString.h"
#include "MD5Hash.h"
#include "ByteBufferedFile.h"
#include "Collections.h"
#include "OsuConVars.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "DirectoryWatcher.h"
#include "Engine.h"
#include "File.h"
#include "LegacyReplay.h"
#include "NotificationOverlay.h"
#include "ResourceManager.h"
#include "AsyncPPCalculator.h"
#include "SongBrowser/LoudnessCalcThread.h"
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

#include "fmt/chrono.h"

#include <algorithm>
#include <cstring>
#include <utility>

std::unique_ptr<Database> db = nullptr;

bool Database::sortScoreByScore(const FinishedScore &a, const FinishedScore &b) {
    if(a.score != b.score) return a.score > b.score;
    if(a.unixTimestamp != b.unixTimestamp) return a.unixTimestamp > b.unixTimestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByCombo(const FinishedScore &a, const FinishedScore &b) {
    if(a.comboMax != b.comboMax) return a.comboMax > b.comboMax;
    if(a.score != b.score) return a.score > b.score;
    if(a.unixTimestamp != b.unixTimestamp) return a.unixTimestamp > b.unixTimestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByDate(const FinishedScore &a, const FinishedScore &b) {
    if(a.unixTimestamp != b.unixTimestamp) return a.unixTimestamp > b.unixTimestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByMisses(const FinishedScore &a, const FinishedScore &b) {
    if(a.numMisses != b.numMisses) return a.numMisses < b.numMisses;
    if(a.score != b.score) return a.score > b.score;
    if(a.unixTimestamp != b.unixTimestamp) return a.unixTimestamp > b.unixTimestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByAccuracy(const FinishedScore &a, const FinishedScore &b) {
    auto a_acc = LiveScore::calculateAccuracy(a.num300s, a.num100s, a.num50s, a.numMisses);
    auto b_acc = LiveScore::calculateAccuracy(b.num300s, b.num100s, b.num50s, b.numMisses);
    if(a_acc != b_acc) return a_acc > b_acc;
    if(a.score != b.score) return a.score > b.score;
    if(a.unixTimestamp != b.unixTimestamp) return a.unixTimestamp > b.unixTimestamp;
    if(a.player_id != b.player_id) return a.player_id > b.player_id;
    if(a.play_time_ms != b.play_time_ms) return a.play_time_ms > b.play_time_ms;
    return false;  // equivalent
}

bool Database::sortScoreByPP(const FinishedScore &a, const FinishedScore &b) {
    auto a_pp = std::max(a.get_pp() * 1000.0, 0.0);
    auto b_pp = std::max(b.get_pp() * 1000.0, 0.0);
    if(a_pp != b_pp) return a_pp > b_pp;
    if(a.score != b.score) return a.score > b.score;
    if(a.unixTimestamp != b.unixTimestamp) return a.unixTimestamp > b.unixTimestamp;
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
    u32 osu_db_version = (dbr.good() && dbr.total_size > 0) ? dbr.read<u32>() : 0;
    return osu_db_version > 0;
}

// run after at least one engine frame (due to resourceManager->update() in Engine::onUpdate())
void Database::AsyncDBLoader::init() {
    if(!db) return;  // don't crash when exiting while loading db

    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "(AsyncDBLoader) start");

    if(db->needs_raw_load) {
        db->scheduleLoadRaw();
    } else {
        // signal that we are done
        db->loading_progress = 1.0f;

        // will find maps/scores needing recalc dynamically
        BatchDiffCalc::start_calc();
        VolNormalization::start_calc(db->loudness_to_calc);

        this->setReady(true);
    }

    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "(AsyncDBLoader) done");
}

// run immediately on a separate thread when resourceManager->loadResource() is called
void Database::AsyncDBLoader::initAsync() {
    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "(AsyncDBLoader) start");
    assert(db != nullptr);

    db->findDatabases();

    if(db->load_interrupted.load(std::memory_order_acquire)) goto done;

    using enum Database::DatabaseType;
    db->loadScores(db->database_files[NEOMOD_SCORES]);
    if(db->load_interrupted.load(std::memory_order_acquire)) goto done;
    db->loadOldMcNeomodScores(db->database_files[MCNEOMOD_SCORES]);
    if(db->load_interrupted.load(std::memory_order_acquire)) goto done;
    db->loadPeppyScores(db->database_files[STABLE_SCORES]);
    db->scores_loaded = true;
    if(db->load_interrupted.load(std::memory_order_acquire)) goto done;

    db->loadMaps();
    if(db->load_interrupted.load(std::memory_order_acquire)) goto done;

    if(!db->needs_raw_load) {
        Collections::load_all();
        if(db->load_interrupted.load(std::memory_order_acquire)) goto done;
    }

    // .db files that were dropped on the main window
    for(const auto &db_pair : db->external_databases) {
        db->importDatabase(db_pair);
        if(db->load_interrupted.load(std::memory_order_acquire)) goto done;
    }
    // only clear this after we have actually loaded them
    db->extern_db_paths_to_import_async_copy.clear();
    db->external_databases.clear();

done:

    this->setAsyncReady(true);
    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "(AsyncDBLoader) done");
}

void Database::startLoader() {
    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "start");
    this->destroyLoader();

    // stop threads that rely on database content
    BatchDiffCalc::abort_calc();
    AsyncPPC::set_map(nullptr);
    VolNormalization::abort();

    // only clear diffs/sets for full reloads (only handled for raw re-loading atm)
    // const bool lastLoadWasRaw{this->needs_raw_load};
    // TODO: fix delta load logic
    // TODO: raw loading from other folders
    // TODO: new cvar like "force raw load"
    const bool songsFolderExists = Environment::directoryExists(Database::getOsuSongsFolder());
    this->needs_raw_load = songsFolderExists &&
                           (!cv::database_enabled.getBool() || !isOsuDBReadable(getDBPath(DatabaseType::STABLE_MAPS)));
    // const bool nextLoadIsRaw{this->needs_raw_load};

    this->is_first_load = true;

    this->loudness_to_calc.clear();
    {
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
        this->beatmap_difficulties.clear();
    }
    this->temp_loading_beatmapsets.clear();
    this->beatmapsets.clear();

    // append, the copy will only be cleared if loading them succeeded
    this->extern_db_paths_to_import_async_copy.insert(this->extern_db_paths_to_import_async_copy.end(),
                                                      this->extern_db_paths_to_import.cbegin(),
                                                      this->extern_db_paths_to_import.cend());
    this->extern_db_paths_to_import.clear();

    // create initial loader instance
    if(!this->loader) {
        this->loader = std::make_unique<AsyncDBLoader>();
        resourceManager->requestNextLoadAsync();
        resourceManager->loadResource(this->loader.get());
    } else {
        if(!this->loader->isReady()) {
            resourceManager->destroyResource(this->loader.get(), ResourceDestroyFlags::RDF_NODELETE);
        }
        resourceManager->reloadResource(this->loader.get(), true);
    }

    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "done");
}

void Database::destroyLoader() {
    logIf(cv::debug_db.getBool() || cv::debug_async_db.getBool(), "start");
    directoryWatcher->stop_watching(NEOMOD_MAPS_PATH "/");
    if(this->loader) {
        resourceManager->destroyResource(this->loader.get(), ResourceDestroyFlags::RDF_NODELETE);  // force blocking
    }
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

Database::Database() : importTimer(std::make_unique<Timer>()) {
    // convar callback
    cv::cmd::save.setCallback(SA::MakeDelegate<&Database::save>(this));
}

Database::~Database() {
    cv::cmd::save.removeCallback();
    this->destroyLoader();
    if(this->score_saver) {
        resourceManager->destroyResource(this->score_saver.get(),
                                         ResourceDestroyFlags::RDF_NODELETE);  // don't delete a unique_ptr
    }

    BatchDiffCalc::abort_calc();
    AsyncPPC::set_map(nullptr);
    VolNormalization::abort();
    this->loudness_to_calc.clear();

    {
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
        this->beatmap_difficulties.clear();
    }
    this->temp_loading_beatmapsets.clear();
    this->beatmapsets.clear();

    Collections::unload_all();
}

void Database::update() {
    // loadRaw() logic
    if(this->raw_load_scheduled) {
        Timer t;

        while(t.getElapsedTime() < 0.033f) {
            if(this->load_interrupted.load(std::memory_order_acquire)) break;  // cancellation point

            if(this->raw_load_beatmap_folders.size() > 0 &&
               this->cur_raw_load_idx < this->raw_load_beatmap_folders.size()) {
                std::string curBeatmap = this->raw_load_beatmap_folders[this->cur_raw_load_idx++];
                this->raw_loaded_beatmap_folders.push_back(
                    curBeatmap);  // for future incremental loads, so that we know what's been loaded already

                std::string fullBeatmapPath = this->raw_load_osu_song_folder;
                fullBeatmapPath.append(curBeatmap);
                fullBeatmapPath.append("/");

                this->addBeatmapSet(fullBeatmapPath,           //
                                    -1,                        // no set id override
                                    !this->raw_load_is_neomod  // is_peppy
                );
            }

            // update progress
            this->loading_progress = (float)this->cur_raw_load_idx / (float)this->num_beatmaps_to_load;

            // check if we are finished
            if(this->cur_raw_load_idx >= this->num_beatmaps_to_load ||
               std::cmp_greater(this->cur_raw_load_idx, (this->raw_load_beatmap_folders.size() - 1))) {
                this->raw_load_beatmap_folders.clear();
                this->raw_load_scheduled = false;

                this->importTimer->update();

                debugLog("Refresh finished, added {} beatmaps in {:f} seconds.", this->beatmapsets.size(),
                         this->importTimer->getElapsedTime());

                Collections::load_all();

                // clang-format off
                for(auto &diff : this->beatmapsets
                                // for all diffs within the set with fStarsNomod <= 0.f (peppy difficulties needing recalc)
                                | std::views::transform([](const auto &set) -> auto & { return *set->difficulties; })
                                | std::views::join
                                | std::views::filter([](const auto &diff) { return diff->fStarsNomod <= 0.f; })) {
                    diff->fStarsNomod *= -1.f;
                }
                // clang-format on
                this->loading_progress = 1.0f;

                // will find maps/scores needing recalc dynamically
                BatchDiffCalc::start_calc();
                VolNormalization::start_calc(this->loudness_to_calc);

                break;
            }

            t.update();
        }
    }
}

void Database::load() {
    this->load_interrupted = false;
    this->loading_progress = 0.0f;

    // reset scheduled logic
    this->raw_load_scheduled = false;

    this->startLoader();
}

void Database::cancel() {
    this->load_interrupted = true;
    this->loading_progress = 1.0f;  // force finished
    this->raw_found_changes = true;
}

void Database::save() {
    Collections::save_collections();
    this->saveMaps();
    this->saveScores();
}

// NOTE: Should currently only be used for neomod beatmapsets! e.g. from maps/ folder
//       See loadRawBeatmap()
//       (unless is_peppy is specified, in which case we're loading a raw osu folder and not saving the things we loaded)
BeatmapSet *Database::addBeatmapSet(const std::string &beatmapFolderPath, i32 set_id_override, bool is_peppy) {
    std::unique_ptr<BeatmapSet> mapset = this->loadRawBeatmap(beatmapFolderPath, is_peppy);
    if(mapset == nullptr) return nullptr;

    BeatmapSet *raw_mapset = mapset.get();

    {
        // deduplicate diffs
        // TODO: this will disallow adding a neomod beatmapset if we already have a peppy beatmapset that is the same (and vice versa)!
        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
        for(auto diffit = mapset->difficulties->begin(); diffit != mapset->difficulties->end();) {
            const auto &diff = *diffit;
            auto [existingit, inserted] = this->beatmap_difficulties.try_emplace(diff->getMD5(), diff.get());
            if(!inserted) {
                // update set id just in case we had an override, though
                const i32 real_set_id = set_id_override != -1 ? set_id_override : mapset->iSetID;

                BeatmapDifficulty *diffparent = existingit->second->parentSet;
                if(const i32 old_set_id = diffparent->iSetID; old_set_id == -1 && real_set_id > 0) {
                    logIfCV(debug_db, "updating old set {} id {} -> {}", diffparent->getFolder(), old_set_id,
                            real_set_id);
                    diffparent->iSetID = set_id_override;
                    for(auto &existingdiff : diffparent->getDifficulties()) {
                        existingdiff->iSetID = set_id_override;
                    }
                }
                logIfCV(debug_db, "skipping raw {} (already in beatmap_difficulties), current size: {}", diff->getMD5(),
                        this->beatmap_difficulties.size());
                diffit = mapset->difficulties->erase(diffit);
            } else {
                logIfCV(debug_db, "adding raw {} to beatmap_difficulties, current size: {}", diff->getMD5(),
                        this->beatmap_difficulties.size());
                ++diffit;
            }
        }
    }

    if(mapset->difficulties->empty()) {
        logIfCV(debug_db, "WARNING: not adding raw mapset {} id {}, only had duplicate difficulties!",
                mapset->getFolder(), set_id_override != -1 ? set_id_override : mapset->iSetID);
        return nullptr;
    }

    // Some beatmaps don't provide beatmap/beatmapset IDs in the .osu files
    // But we know the beatmapset ID because we just downloaded it!
    if(set_id_override != -1) {
        mapset->iSetID = set_id_override;
        for(auto &diff : mapset->getDifficulties()) {
            diff->iSetID = set_id_override;
        }
    }

    this->beatmapsets.push_back(std::move(mapset));

    // only notify songbrowser if loading is done (it rebuilds from beatmapsets in onDatabaseLoadingFinished)
    if(this->isFinished()) {
        ui->getSongBrowser()->addBeatmapSet(raw_mapset);
        this->bPendingBatchDiffCalc = true;  // picked up by SongBrowser::update
    }

    if(cv::maps_save_immediately.getBool()) {
        db->saveMaps();
    }

    return raw_mapset;
}

void Database::AsyncScoreSaver::initAsync() {
    auto compressed_replay = LegacyReplay::compress_frames(this->scorecopy.replay);
    if(!compressed_replay.empty()) {
        auto replay_path = fmt::format(NEOMOD_REPLAYS_PATH "/{:d}.replay.lzma", this->scorecopy.unixTimestamp);
        debugLog("Saving replay to {}...", replay_path);
        io->write(replay_path, std::move(compressed_replay), [replay_path](bool success) {
            if(success) {
                debugLog("Replay saved.");
            } else {
                debugLog("Failed to save replay to {}!", replay_path);
            }
        });
    }

    if(db && !engine->isShuttingDown() && cv::scores_save_immediately.getBool()) {
        db->saveScores();
    }

    this->setAsyncReady(true);
    // nothing to do in init(), set ready now
    this->setReady(true);
}

bool Database::addScore(const FinishedScore &score) {
    // if addScoreRaw returns false, it means it wasn't added because we already have the score
    // so just skip everything and return the index
    const bool added = this->addScoreRaw(score);
    if(added) {
        this->sortScores(score.beatmap_hash);

        this->scores_changed = true;

        // create initial instance
        if(!this->score_saver) {
            this->score_saver = std::make_unique<AsyncScoreSaver>(score);
            resourceManager->requestNextLoadAsync();
            resourceManager->loadResource(this->score_saver.get());
        } else {
            if(!this->score_saver->isReady()) {  // a previous save attempt is still running
                // NODELETE implies blocking, so this just waits for a previous instance to finish (but does not delete it)
                resourceManager->destroyResource(this->score_saver.get(), ResourceDestroyFlags::RDF_NODELETE);
            }
            // set new score
            this->score_saver->scorecopy = score;
            resourceManager->reloadResource(this->score_saver.get(), true);
        }
    }

    // @PPV3: use new replay format
    return added;
}

int Database::isScoreAlreadyInDB(const MD5Hash &map_hash, u64 unix_timestamp, const std::string &playerName) {
    Sync::shared_lock lock(this->scores_mtx);

    // operator[] might add a new entry
    const auto &scoreit = this->scores.find(map_hash);
    if(scoreit == this->scores.end()) return -1;

    for(int existing_pos = -1; const auto &existing : scoreit->second) {
        existing_pos++;
        if(existing.unixTimestamp == unix_timestamp && existing.playerName == playerName) {
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

    if((existing_pos = this->isScoreAlreadyInDB(score.beatmap_hash, score.unixTimestamp, score.playerName)) >= 0) {
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

    bool found = false;
    const auto &sortTypeString{cv::songbrowser_scores_sortingtype.getString()};
    for(const auto &sortMethod : Database::SCORE_SORTING_METHODS) {
        if(sortTypeString == sortMethod.name) {
            std::ranges::sort(scores, sortMethod.comparator);
            found = true;
            break;
        }
    }

    // Fallback
    if(!found) {
        cv::songbrowser_scores_sortingtype.setValue("By pp");
        std::ranges::sort(scores, sortScoreByPP);
    }
}

void Database::sortScores(const MD5Hash &beatmapMD5Hash) {
    Sync::unique_lock lk(this->scores_mtx);
    if(auto it = this->scores.find(beatmapMD5Hash); it != this->scores.end()) {
        Database::sortScoresInPlace(it->second);
    }
    return;
}

Database::PlayerPPScores Database::getPlayerPPScores(const std::string &playerName) {
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

Database::PlayerStats Database::calculatePlayerStats(const std::string &playerName) {
    // FIXME: returning cached statistics even if we got new scores
    // this is makes this function a "sneaky" API that might return stale stats
    // done for performance to not tank FPS during score recalc where this is done on the main thread
    // every frame (by UserCard::updateUserStats)

    // should be done by the caller but it's more complicated because the prevPlayerStats are
    // cached inside Database...
    const bool scoresChanged = this->scores_changed.load(std::memory_order_acquire);
    const bool returnCached =
        playerName == this->prevPlayerStats.name.utf8View() &&
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

        pp += ps.ppScores[i]->get_pp() * weight;
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
                (double)(ps.totalScore - requiredScoreForCurrentLevel) /
                (double)(requiredScoreForNextLevel - requiredScoreForCurrentLevel);
    }

    this->prevPlayerStats.totalScore = ps.totalScore;

    return this->prevPlayerStats;
}

float Database::getWeightForIndex(int i) { return std::pow(0.95f, (f32)i); }

float Database::getBonusPPForNumScores(size_t numScores) {
    return (417.0 - 1.0 / 3.0) * (1.0 - pow(0.995, std::min(1000.0, (f64)numScores)));
}

u64 Database::getRequiredScoreForLevel(int level) {
    // https://zxq.co/ripple/ocl/src/branch/master/level.go
    if(level <= 100) {
        if(level > 1)
            return (u64)std::floor(5000 / 3 * (4 * pow(level, 3) - 3 * pow(level, 2) - level) +
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

BeatmapDifficulty *Database::getBeatmapDifficulty(const MD5Hash &md5hash) {
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

BeatmapDifficulty *Database::getBeatmapDifficulty(i32 map_id) {
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

BeatmapSet *Database::getBeatmapSet(i32 set_id) {
    if(this->isLoading()) {
        debugLog("we are loading, progress {}, not returning a BeatmapSet*", this->getProgress());
        return nullptr;
    }

    for(const auto &beatmap : this->beatmapsets) {
        if(beatmap->getSetID() == set_id) {
            return beatmap.get();
        }
    }

    return nullptr;
}

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

void Database::scheduleLoadRaw() {
    {
        std::string folderToLoadFrom = Database::getOsuSongsFolder();
        std::vector<std::string> foldersInFolder = Environment::getFoldersInFolder(folderToLoadFrom);

        // TODO
        if(false && (!(File::exists(folderToLoadFrom) == File::FILETYPE::FOLDER) ||  //
                     foldersInFolder.empty())) {
            folderToLoadFrom = NEOMOD_MAPS_PATH "/";
            foldersInFolder = Environment::getFoldersInFolder(folderToLoadFrom);

            debugLog("Loading raw beatmaps from folders in {} (no osu!stable maps found)", folderToLoadFrom);
            this->raw_load_is_neomod = true;
        } else {
            debugLog("Loading raw beatmaps from folders in {}", folderToLoadFrom);
            this->raw_load_is_neomod = false;
        }

        this->raw_load_osu_song_folder = std::move(folderToLoadFrom);
        this->raw_load_beatmap_folders = std::move(foldersInFolder);
    }

    this->num_beatmaps_to_load = this->raw_load_beatmap_folders.size();

    // if this isn't the first load, only load the differences
    if(!this->is_first_load) {
        std::vector<std::string> toLoad;
        for(uSz i = 0; i < this->num_beatmaps_to_load; i++) {
            bool alreadyLoaded = false;
            for(const auto &rawBeatmapFolder : this->raw_loaded_beatmap_folders) {
                if(this->raw_load_beatmap_folders[i] == rawBeatmapFolder) {
                    alreadyLoaded = true;
                    break;
                }
            }

            if(!alreadyLoaded) toLoad.push_back(this->raw_load_beatmap_folders[i]);
        }

        // only load differences
        this->raw_load_beatmap_folders = toLoad;
        this->num_beatmaps_to_load = this->raw_load_beatmap_folders.size();

        debugLog("Database: Found {} new/changed beatmaps.", this->num_beatmaps_to_load);

        this->raw_found_changes = this->num_beatmaps_to_load > 0;
        if(this->raw_found_changes)
            ui->getNotificationOverlay()->addNotification(
                fmt::format("Adding {:d} new beatmap{}.", this->num_beatmaps_to_load,
                            this->num_beatmaps_to_load == 1 ? "" : "s"),
                0xff00ff00);
        else
            ui->getNotificationOverlay()->addNotification(US_("No new beatmaps detected."), 0xff00ff00);
    }

    debugLog("Database: Building beatmap database ...");
    debugLog("Database: Found {} folders to load.", this->raw_load_beatmap_folders.size());

    // only start loading if we have something to load
    if(this->raw_load_beatmap_folders.size() > 0) {
        this->loading_progress = 0.0f;
        this->cur_raw_load_idx = 0;

        this->raw_load_scheduled = true;
        this->importTimer->start();
    } else
        this->loading_progress = 1.0f;

    this->is_first_load = false;
}
namespace {
constexpr i64 TICKS_PER_SECOND = 10'000'000;
constexpr i64 UNIX_EPOCH_TICKS = 621'355'968'000'000'000;  // ticks from 0001-01-01 to 1970-01-01
}  // namespace

MD5Hash Database::recalcMD5(std::string osu_path) {
    // avoid hashmap corruption, we somehow fail to read some entries' md5 hashes
    // TODO: move this somewhere else or put it in peppy overrides or something
    // "temporary" fix (extremely rare edge case so maybe it's not that important)
    MD5Hash md5digest;
    if(Environment::fileExists(osu_path)) {
        crypto::hash::md5_f(osu_path, md5digest.data());
        if(!md5digest.empty()) {
            logIfCV(debug_db, "Manually calculated hash {} for {}", md5digest, osu_path);
        } else {
            logIfCV(debug_db, "Failed to recalculate corrupt hash {}", osu_path);
        }
    } else {
        logIfCV(debug_db, "Skipped entry {} with no/corrupt md5", osu_path);
    }

    return md5digest;
}

void Database::loadMaps() {
    const auto &peppy_db_path = this->database_files[DatabaseType::STABLE_MAPS];
    const auto &neomod_maps_path = this->database_files[DatabaseType::NEOMOD_MAPS];

    const std::string &songFolder = Database::getOsuSongsFolder();
    debugLog("Database: songFolder = {:s}", songFolder);

    this->importTimer->start();

    u32 nb_neomod_maps = 0;
    u32 nb_peppy_maps = 0;
    u32 nb_overrides = 0;

    // Load neomod map database
    {
        ByteBufferedFile::Reader neomod_maps(neomod_maps_path);
        if(neomod_maps.total_size > 0) {
            u32 version = neomod_maps.read<u32>();
            if(version < NEOMOD_MAPS_DB_VERSION) {
                // Reading from older database version: backup just in case
                auto backup_path =
                    fmt::format("{}.{}-{:%F}", neomod_maps_path, version, fmt::gmtime(std::time(nullptr)));
                if(File::copy(neomod_maps_path, backup_path)) {
                    debugLog("older database {} < {}, backed up {} -> {}", version, NEOMOD_MAPS_DB_VERSION,
                             neomod_maps_path, backup_path);
                }
            }

            u32 nb_sets = neomod_maps.read<u32>();
            for(uSz i = 0; i < nb_sets; i++) {
                if(this->load_interrupted.load(std::memory_order_acquire)) break;  // cancellation point

                u32 progress_bytes = this->bytes_processed + neomod_maps.total_pos;
                f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
                this->loading_progress = std::clamp(progress_float, 0.01, 0.99);

                i32 set_id = neomod_maps.read<i32>();
                u16 nb_diffs = neomod_maps.read<u16>();

                // NOTE: Ignoring mapsets with ID -1, since we most likely saved them in the correct folder,
                //       but mistakenly set their ID to -1 (because the ID was missing from the .osu file).
                if(set_id == -1) {
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

                        logIfCV(debug_db, "skipped iSetID==-1 beatmap with hash {} load idx: {},{}", md5, i, j);
                    }
                    continue;
                }

                auto diffs = std::make_unique<DiffContainer>();
                std::string mapset_path = fmt::format(NEOMOD_MAPS_PATH "/{}/", set_id);

                for(u16 j = 0; j < nb_diffs; j++) {
                    if(this->load_interrupted.load(std::memory_order_acquire)) {  // cancellation point
                        // clean up partially loaded diffs in current set
                        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
                        for(const auto &diff : *diffs) {
                            this->beatmap_difficulties.erase(diff->getMD5());

                            // remove from loudness_to_calc
                            std::erase(this->loudness_to_calc, diff.get());
                        }
                        diffs.reset();
                        break;
                    }

                    std::string osu_filename = neomod_maps.read_string();

                    i32 iID = neomod_maps.read<i32>();
                    i32 iSetID = set_id;
                    std::string sTitle = neomod_maps.read_string();
                    std::string sAudioFileName = neomod_maps.read_string();
                    i32 iLengthMS = neomod_maps.read<i32>();
                    f32 fStackLeniency = neomod_maps.read<f32>();
                    std::string sArtist = neomod_maps.read_string();
                    std::string sCreator = neomod_maps.read_string();
                    std::string sDifficultyName = neomod_maps.read_string();
                    std::string sSource = neomod_maps.read_string();
                    std::string sTags = neomod_maps.read_string();

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
                    f64 fSliderMultiplier = neomod_maps.read<f64>();
                    u32 iPreviewTime = neomod_maps.read<u32>();
                    const i64 maybe_dotnettime = neomod_maps.read<i64>();
                    i64 last_modification_time = maybe_dotnettime;
                    // convert .NET timestamp to unix timestamp (mtime)
                    // we don't really need to update db format for this because it was only used for sorting
                    // so just fix it up here
                    if(maybe_dotnettime > 1'000'000'000'000'000 /* 100% .net timestamp from non-updated db */) {
                        last_modification_time = (maybe_dotnettime - UNIX_EPOCH_TICKS) / TICKS_PER_SECOND;
                    }
                    i16 iLocalOffset = neomod_maps.read<i16>();
                    i16 iOnlineOffset = neomod_maps.read<i16>();
                    u16 iNumCircles = neomod_maps.read<u16>();
                    u16 iNumSliders = neomod_maps.read<u16>();
                    u16 iNumSpinners = neomod_maps.read<u16>();
                    f64 fStarsNomod = neomod_maps.read<f64>();

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

                    std::string sTitleUnicode = sTitle;
                    std::string sArtistUnicode = sArtist;
                    if(version >= 20250801) {
                        neomod_maps.read_string(sTitleUnicode);
                        neomod_maps.read_string(sArtistUnicode);
                    }

                    const bool bEmptyTitleUnicode = SString::is_wspace_only(sTitleUnicode);
                    const bool bEmptyArtistUnicode = SString::is_wspace_only(sArtistUnicode);

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

                    auto diff = std::make_unique<BeatmapDifficulty>(mapset_path + osu_filename, mapset_path,
                                                                    DatabaseBeatmap::BeatmapType::NEOMOD_DIFFICULTY);

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
                    diff->fSliderMultiplier = fSliderMultiplier;
                    diff->iPreviewTime = iPreviewTime;
                    diff->last_modification_time = last_modification_time;
                    diff->iLocalOffset = iLocalOffset;
                    diff->iOnlineOffset = iOnlineOffset;
                    diff->iNumCircles = iNumCircles;
                    diff->iNumSliders = iNumSliders;
                    diff->iNumSpinners = iNumSpinners;
                    diff->fStarsNomod = fStarsNomod;

                    diff->iMinBPM = iMinBPM;
                    diff->iMaxBPM = iMaxBPM;
                    diff->iMostCommonBPM = iMostCommonBPM;

                    diff->draw_background = draw_background;

                    if(loudness == 0.f) {
                        this->loudness_to_calc.push_back(diff.get());
                    } else {
                        diff->loudness = loudness;
                    }

                    diff->sTitleUnicode = std::move(sTitleUnicode);
                    diff->sArtistUnicode = std::move(sArtistUnicode);

                    diff->bEmptyTitleUnicode = bEmptyTitleUnicode;
                    diff->bEmptyArtistUnicode = bEmptyArtistUnicode;

                    diff->sBackgroundImageFileName = std::move(sBackgroundImageFileName);

                    diff->ppv2Version = ppv2Version;

                    {
                        Sync::unique_lock lock(this->beatmap_difficulties_mtx);
                        this->beatmap_difficulties[diff_hash] = diff.get();
                    }
                    diffs->push_back(std::move(diff));
                    nb_neomod_maps++;
                }

                if(diffs && !diffs->empty()) {
                    auto set =
                        std::make_unique<BeatmapSet>(std::move(diffs), DatabaseBeatmap::BeatmapType::NEOMOD_BEATMAPSET);
                    this->temp_loading_beatmapsets.push_back(std::move(set));

                    // NOTE: Don't add neomod sets to beatmapSets since they're already processed
                    // Adding them would create duplicate ownership of the diffs vector
                }
            }

            if(version >= 20240812) {
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
                    this->peppy_overrides[map_md5] = over;
                }
            }

            // star ratings section
            if(version >= 20260202) {
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
        this->bytes_processed += neomod_maps.total_size;
        this->neomod_maps_loaded = true;
    }

    // load peppy maps

    // read beatmapInfos, and also build two hashmaps (diff hash -> BeatmapDifficulty, diff hash -> Beatmap)
    struct Beatmap_Set {
        int setID{0};
        std::unique_ptr<DiffContainer> diffs;
    };
    std::vector<Beatmap_Set> beatmapSets;
    Hash::flat::map<int, size_t> setIDToIndex;

    if(!this->needs_raw_load) {
        ByteBufferedFile::Reader dbr(peppy_db_path);
        u32 osu_db_version = (dbr.good() && dbr.total_size > 0) ? dbr.read<u32>() : 0;
        bool should_read_peppy_database = osu_db_version > 0;
        if(should_read_peppy_database) {
            // read header
            u32 osu_db_folder_count = dbr.read<u32>();
            dbr.skip<u8>();
            dbr.skip<u64>() /* timestamp */;
            std::string playerName = dbr.read_string();
            this->num_beatmaps_to_load = dbr.read<u32>();

            debugLog("Database: version = {:d}, folderCount = {:d}, playerName = {:s}, numDiffs = {:d}", osu_db_version,
                     osu_db_folder_count, playerName, this->num_beatmaps_to_load);

            // hard cap upper db version
            if(osu_db_version > cv::database_version.getVal<u32>() && !cv::database_ignore_version.getBool()) {
                ui->getNotificationOverlay()->addToast(
                    fmt::format("osu!.db version unknown ({:d}), osu!stable maps will not get loaded.", osu_db_version),
                    ERROR_TOAST);
                should_read_peppy_database = false;
            }
        }
        if(!should_read_peppy_database) {
            debugLog("not loading {}, ver: {} size: {} err: {}", peppy_db_path, osu_db_version, dbr.total_size,
                     dbr.error());
        }

        if(should_read_peppy_database) {
            std::vector<BPMTuple> bpm_calculation_buffer;
            std::vector<DB_TIMINGPOINT> timing_points_buffer;

            for(uSz i = 0; i < this->num_beatmaps_to_load; i++) {
                if(this->load_interrupted.load(std::memory_order_acquire)) break;  // cancellation point

                // update progress (another thread checks if progress >= 1.f to know when we're done)
                u32 progress_bytes = this->bytes_processed + dbr.total_pos;
                f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
                this->loading_progress = std::clamp(progress_float, 0.01, 0.99);

                // NOTE: This is documented wrongly in many places.
                //       This int was added in 20160408 and removed in 20191106
                //       https://osu.ppy.sh/home/changelog/stable40/20160408.3
                //       https://osu.ppy.sh/home/changelog/cuttingedge/20191106
                if(osu_db_version >= 20160408 && osu_db_version < 20191106) {
                    // size in bytes of the beatmap entry
                    dbr.skip<u32>();
                }

                std::string artistName = dbr.read_string();
                SString::trim_inplace(artistName);
                std::string artistNameUnicode = dbr.read_string();
                std::string songTitle = dbr.read_string();
                SString::trim_inplace(songTitle);
                std::string songTitleUnicode = dbr.read_string();
                std::string creatorName = dbr.read_string();
                SString::trim_inplace(creatorName);
                std::string difficultyName = dbr.read_string();
                SString::trim_inplace(difficultyName);
                std::string audioFileName = dbr.read_string();

                MD5Hash md5hash;
                (void)dbr.read_hash_chars(md5hash);  // TODO: validate

                logIfCV(debug_db, "Reading osu!.db beatmap {:d}/{:d} md5hash {} ...", (i + 1),
                        this->num_beatmaps_to_load, md5hash);

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
                std::string osuFileName = dbr.read_string();
                /*unsigned char rankedStatus = */ dbr.skip<u8>();
                auto numCircles = dbr.read<u16>();
                auto numSliders = dbr.read<u16>();
                auto numSpinners = dbr.read<u16>();
                const i64 modification_time_dotnet_ticks = dbr.read<i64>();
                // convert to unix time (peppy db 100% stores these in .NET timestamp form)
                const i64 last_modification_time =
                    (modification_time_dotnet_ticks - UNIX_EPOCH_TICKS) / TICKS_PER_SECOND;

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

                auto sliderMultiplier = dbr.read<f64>();

                f32 nomod_star_rating = 0.0f;
                if(osu_db_version >= 20140609) {
                    // https://osu.ppy.sh/home/changelog/stable40/20250108.3
                    const u32 sr_field_size = osu_db_version < 20250108 ? sizeof(f64) : sizeof(f32);

                    const auto num_std_star_ratings = dbr.read<u32>();
                    if(sr_field_size == sizeof(f64)) {  // older format
                        for(uSz s = 0; s < num_std_star_ratings; s++) {
                            dbr.skip<u8>();  // 0x08 ObjType
                            auto mods = dbr.read<u32>();
                            dbr.skip<u8>();  // 0x0c ObjType
                            if(mods == 0 && nomod_star_rating == 0.f) {
                                nomod_star_rating = static_cast<f32>(dbr.read<f64>());
                            } else {
                                dbr.skip<f64>();
                            }
                        }
                    } else {
                        for(uSz s = 0; s < num_std_star_ratings; s++) {
                            dbr.skip<u8>();  // 0x08 ObjType
                            auto mods = dbr.read<u32>();
                            dbr.skip<u8>();  // 0x0c ObjType
                            if(mods == 0 && nomod_star_rating == 0.f) {
                                nomod_star_rating = dbr.read<f32>();
                            } else {
                                dbr.skip<f32>();
                            }
                        }
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
                int duration = dbr.read<u32>();                 // milliseconds
                duration = duration >= 0 ? duration : 0;        // sanity clamp
                int previewTime = dbr.read<u32>();

                BPMInfo bpm;
                auto nb_timing_points = dbr.read<u32>();
                if(overrides_found &&
                   override_.min_bpm != -1) {  // only use cached override bpm if it's not the sentinel -1
                    dbr.skip_bytes(sizeof(DB_TIMINGPOINT) * nb_timing_points);
                    bpm.min = override_.min_bpm;
                    bpm.max = override_.max_bpm;
                    bpm.most_common = override_.avg_bpm;
                } else if(nb_timing_points > 0) {
                    timing_points_buffer.resize(nb_timing_points);
                    if(dbr.read_bytes((u8 *)timing_points_buffer.data(), sizeof(DB_TIMINGPOINT) * nb_timing_points) !=
                       sizeof(DB_TIMINGPOINT) * nb_timing_points) {
                        debugLog("WARNING: failed to read timing points from beatmap {:d} !", (i + 1));
                    } else {
                        bpm = getBPM(timing_points_buffer, bpm_calculation_buffer);
                    }
                }

                int beatmapID =
                    dbr.read<i32>();  // fucking bullshit, this is NOT an unsigned integer as is described on
                                      // the wiki, it can and is -1 sometimes
                int beatmapSetID = dbr.read<i32>();  // same here
                /*unsigned int threadID = */ dbr.skip<u32>();

                /*unsigned char osuStandardGrade = */ dbr.skip<u8>();
                /*unsigned char taikoGrade = */ dbr.skip<u8>();
                /*unsigned char ctbGrade = */ dbr.skip<u8>();
                /*unsigned char maniaGrade = */ dbr.skip<u8>();

                auto localOffset = dbr.read<u16>();
                auto stackLeniency = dbr.read<f32>();
                auto mode = dbr.read<u8>();

                std::string songSource = dbr.read_string();
                std::string songTags = dbr.read_string();
                SString::trim_inplace(songSource);
                SString::trim_inplace(songTags);

                auto onlineOffset = dbr.read<u16>();
                dbr.skip_string();  // song title font
                /*bool unplayed = */ dbr.skip<u8>();
                /*i64 lastTimePlayed = */ dbr.skip<u64>();
                /*bool isOsz2 = */ dbr.skip<u8>();

                // somehow, some beatmaps may have spaces at the start/end of their
                // path, breaking the Windows API (e.g. https://osu.ppy.sh/s/215347)
                std::string path = dbr.read_string();
                SString::trim_inplace(path);

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
                if(artistName.length() < 1 && songTitle.length() < 1 && creatorName.length() < 1 &&
                   difficultyName.length() < 1)
                    continue;

                if(mode != 0) continue;

                // it can happen that nested beatmaps are stored in the
                // database, and that osu! stores that filepath with a backslash (because windows)
                // so replace them with / to normalize
                std::ranges::replace(path, '\\', '/');

                // build beatmap & diffs from all the data
                std::string beatmapPath = songFolder;
                beatmapPath.append(path);
                beatmapPath.push_back('/');
                std::string fullFilePath = beatmapPath;
                fullFilePath.append(osuFileName);

                if(md5hash.empty()) {
                    md5hash = recalcMD5(fullFilePath);
                }

                // special case: legacy fallback behavior for invalid beatmapSetID, try to parse the ID from the path
                if(beatmapSetID < 1 && path.length() > 0) {
                    size_t slash = path.find('/');
                    std::string candidate = (slash != std::string::npos) ? path.substr(0, slash) : path;

                    if(!candidate.empty() && std::isdigit(static_cast<unsigned char>(candidate[0]))) {
                        if(!Parsing::parse(candidate, &beatmapSetID)) {
                            beatmapSetID = -1;
                        }
                    }
                }

                BeatmapDifficulty *diffp = nullptr;
                {
                    // fill diff with data
                    auto map = std::make_unique<BeatmapDifficulty>(fullFilePath, beatmapPath,
                                                                   DatabaseBeatmap::BeatmapType::PEPPY_DIFFICULTY);

                    map->sTitle = std::move(songTitle);
                    map->sTitleUnicode = std::move(songTitleUnicode);
                    if(SString::is_wspace_only(map->sTitleUnicode)) {
                        map->bEmptyTitleUnicode = true;
                    }
                    map->sAudioFileName = std::move(audioFileName);
                    map->iLengthMS = duration;

                    map->fStackLeniency = stackLeniency;

                    map->sArtist = std::move(artistName);
                    map->sArtistUnicode = std::move(artistNameUnicode);
                    if(SString::is_wspace_only(map->sArtistUnicode)) {
                        map->bEmptyArtistUnicode = true;
                    }
                    map->sCreator = std::move(creatorName);
                    map->sDifficultyName = std::move(difficultyName);
                    map->sSource = std::move(songSource);
                    map->sTags = std::move(songTags);
                    map->writeMD5(md5hash);
                    map->iID = beatmapID;
                    map->iSetID = beatmapSetID;

                    map->fAR = AR;
                    map->fCS = CS;
                    map->fHP = HP;
                    map->fOD = OD;
                    map->fSliderMultiplier = sliderMultiplier;

                    // map->sBackgroundImageFileName = "";

                    map->iPreviewTime = previewTime;
                    map->last_modification_time = last_modification_time;

                    map->iNumCircles = numCircles;
                    map->iNumSliders = numSliders;
                    map->iNumSpinners = numSpinners;
                    map->iMinBPM = bpm.min;
                    map->iMaxBPM = bpm.max;
                    map->iMostCommonBPM = bpm.most_common;
                    // (the diff is now fully built)

                    // now, search if the current set (to which this diff would belong) already exists and add it there, or
                    // if it doesn't exist then create the set
                    if(const auto &existingit = setIDToIndex.find(beatmapSetID); existingit != setIDToIndex.end()) {
                        const auto &[_, idx] = *existingit;
                        assert(beatmapSets[idx].diffs);
                        DiffContainer &existingDiffs = *beatmapSets[idx].diffs;
                        // if a diff with a the same md5hash hasn't already been added here
                        if(!std::ranges::contains(existingDiffs, md5hash,
                                                  [](const auto &existingdiff) { return existingdiff->getMD5(); })) {
                            diffp = map.get();
                            existingDiffs.push_back(std::move(map));
                        }
                    } else {
                        diffp = map.get();
                        setIDToIndex[beatmapSetID] = beatmapSets.size();

                        Beatmap_Set s;
                        s.setID = beatmapSetID;
                        s.diffs = std::make_unique<DiffContainer>();
                        s.diffs->push_back(std::move(map));
                        beatmapSets.push_back(std::move(s));
                    }
                }

                if(diffp != nullptr) {  // if we actually added it
                    bool loudness_found = false;
                    if(overrides_found) {
                        diffp->iLocalOffset = override_.local_offset;
                        diffp->iOnlineOffset = override_.online_offset;
                        diffp->fStarsNomod = override_.star_rating;
                        diffp->ppv2Version = override_.ppv2_version;
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

                        diffp->iLocalOffset = localOffset;
                        diffp->iOnlineOffset = onlineOffset;
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

            Sync::unique_lock lock(this->beatmap_difficulties_mtx);

            // build beatmap sets
            for(auto &beatmapSet : beatmapSets) {
                if(this->load_interrupted.load(std::memory_order_acquire)) {  // cancellation point
                    // clean up remaining unprocessed diffs2 vectors and their contents
                    for(size_t i = &beatmapSet - &beatmapSets[0]; i < beatmapSets.size(); i++) {
                        if(beatmapSets[i].diffs) {
                            for(const auto &diff : *beatmapSets[i].diffs) {
                                this->beatmap_difficulties.erase(diff->getMD5());

                                // remove from loudness_to_calc
                                std::erase(this->loudness_to_calc, diff.get());
                            }
                            beatmapSets[i].diffs.reset();
                        }
                    }
                    break;
                }

                if(!beatmapSet.diffs || beatmapSet.diffs->empty()) {  // sanity check
                    // clean up empty diffs2 vector
                    beatmapSet.diffs.reset();
                    continue;
                }

                if(beatmapSet.setID > 0) {
                    auto set = std::make_unique<BeatmapSet>(std::move(beatmapSet.diffs),
                                                            DatabaseBeatmap::BeatmapType::PEPPY_BEATMAPSET);
                    this->temp_loading_beatmapsets.push_back(std::move(set));
                    // beatmapSet.diffs2 ownership transferred to BeatmapSet
                } else {
                    // set with invalid ID: treat all its diffs separately. we'll group the diffs by title+artist.
                    Hash::unstable_stringmap<std::unique_ptr<DiffContainer>> titleArtistToBeatmap;
                    for(auto &diff : (*beatmapSet.diffs)) {
                        const std::string titleArtist =
                            fmt::format("{}|{}", diff->getTitleLatin(), diff->getArtistLatin());

                        auto it = titleArtistToBeatmap.find(titleArtist);
                        if(it == titleArtistToBeatmap.end()) {
                            titleArtistToBeatmap[titleArtist] = std::make_unique<DiffContainer>();
                        }

                        titleArtistToBeatmap[titleArtist]->push_back(std::move(diff));
                    }

                    for(auto &[_, diffs] : titleArtistToBeatmap) {
                        auto set = std::make_unique<BeatmapSet>(std::move(diffs),
                                                                DatabaseBeatmap::BeatmapType::PEPPY_BEATMAPSET);
                        this->temp_loading_beatmapsets.push_back(std::move(set));
                    }
                }
            }
        }
        this->bytes_processed += dbr.total_size;
    }
    this->beatmapsets = std::move(this->temp_loading_beatmapsets);
    this->temp_loading_beatmapsets.clear();

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

    this->importTimer->update();
    debugLog("peppy+neomod maps: loading took {:f} seconds ({:d} peppy, {:d} neomod, {:d} maps total)",
             this->importTimer->getElapsedTime(), nb_peppy_maps, nb_neomod_maps, nb_peppy_maps + nb_neomod_maps);
    debugLog("Found {:d} overrides; {:d} maps need loudness recalc", nb_overrides, this->loudness_to_calc.size());
}

void Database::saveMaps() {
    if(this->beatmapsets.empty() || this->isLoading() || this->isCancelled()) {
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

    // collect neomod-only sets here
    std::vector<BeatmapSet *> temp_neomod_sets;
    Hash::flat::set<std::string> folders_already_added;
    for(const auto &mapset : this->beatmapsets) {
        if(mapset->type == DatabaseBeatmap::BeatmapType::NEOMOD_BEATMAPSET) {
            // don't add duplicate entries
            // kind of a hack, we shouldn't have added duplicates to beatmapsets in the first place
            // this happens because addBeatmapSet doesn't check if we already have it
            // TODO: may no longer be necessary since addBeatmapSet now does have deduplication logic
            if(auto [_, newly_inserted] = folders_already_added.insert(mapset->getFolder()); newly_inserted) {
                temp_neomod_sets.push_back(mapset.get());
            }
        }
    }

    maps.write<u32>(NEOMOD_MAPS_DB_VERSION);

    // Save neomod-downloaded maps
    u32 nb_diffs_saved = 0;
    maps.write<u32>(temp_neomod_sets.size());
    for(BeatmapSet *beatmap : temp_neomod_sets) {
        maps.write<i32>(beatmap->getSetID());
        maps.write<u16>(beatmap->getDifficulties().size());

        for(const auto &diff : beatmap->getDifficulties()) {
            maps.write_string(env->getFileNameFromFilePath(diff->sFilePath));
            maps.write<i32>(diff->iID);
            maps.write_string(diff->sTitle);
            maps.write_string(diff->sAudioFileName);
            maps.write<i32>(diff->iLengthMS);
            maps.write<f32>(diff->fStackLeniency);
            maps.write_string(diff->sArtist);
            maps.write_string(diff->sCreator);
            maps.write_string(diff->sDifficultyName);
            maps.write_string(diff->sSource);
            maps.write_string(diff->sTags);
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
            maps.write_string(diff->sTitleUnicode);
            maps.write_string(diff->sArtistUnicode);
            maps.write_string(diff->sBackgroundImageFileName);
            maps.write<u32>(diff->ppv2Version);

            nb_diffs_saved++;
        }
    }

    // We want to save settings we applied on peppy-imported maps
    // When calculating loudness we don't call update_overrides() for performance reasons
    {
        Sync::unique_lock lock(this->peppy_overrides_mtx);
        for(const auto &map : this->loudness_to_calc) {
            if(map->type != DatabaseBeatmap::BeatmapType::PEPPY_DIFFICULTY) continue;
            if(map->loudness.load(std::memory_order_acquire) == 0.f) continue;
            this->peppy_overrides[map->getMD5()] = map->get_overrides();
        }
    }

    u32 nb_overrides = 0;
    Hash::flat::map<MD5Hash, MapOverrides> real_overrides;

    // avoid adding overrides with empty/0/"suspicious" hashes
    {
        // only need read lock here
        Sync::shared_lock lock(this->peppy_overrides_mtx);
        real_overrides.reserve(this->peppy_overrides.size());

        for(const auto &it : this->peppy_overrides) {
            if(it.first.is_suspicious()) continue;
            real_overrides.emplace(it);
        }
    }

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
    if(dbr.total_size == 0) {
        this->bytes_processed += dbr.total_size;
        return;
    }

    u32 nb_neomod_scores = 0;
    u8 magic_bytes[6] = {0};
    if(dbr.read_bytes(magic_bytes, 5) != 5 || memcmp(magic_bytes, "NEOSC", 5) != 0) {
        ui->getNotificationOverlay()->addToast(US_("Failed to load " PACKAGE_NAME "_scores.db!"), ERROR_TOAST);
        this->bytes_processed += dbr.total_size;
        return;
    }

    u32 db_version = dbr.read<u32>();
    if(db_version > NEOMOD_SCORE_DB_VERSION) {
        debugLog(PACKAGE_NAME "_scores.db version is newer than current " PACKAGE_NAME " version!");
        this->bytes_processed += dbr.total_size;
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
            sc.unixTimestamp = dbr.read<u64>();
            sc.player_id = dbr.read<i32>();
            dbr.read_string(sc.playerName);
            sc.grade = (ScoreGrade)dbr.read<u8>();

            dbr.read_string(sc.client);
            dbr.read_string(sc.server);
            sc.bancho_score_id = dbr.read<i64>();
            sc.peppy_replay_tms = dbr.read<u64>();

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
            sc.maxPossibleCombo = dbr.read<u32>();
            sc.numHitObjects = dbr.read<u32>();
            sc.numCircles = dbr.read<u32>();

            sc.beatmap_hash = beatmap_hash;

            this->addScoreRaw(sc);
            nb_neomod_scores++;
        }

        u32 progress_bytes = this->bytes_processed + dbr.total_pos;
        f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
        this->loading_progress = std::clamp(progress_float, 0.01, 0.99);
    }

    if(nb_neomod_scores != nb_scores) {
        debugLog("Inconsistency in " PACKAGE_NAME "_scores.db! Expected {:d} scores, found {:d}!", nb_scores,
                 nb_neomod_scores);
    }

    debugLog("Loaded {:d} " PACKAGE_NAME " scores", nb_neomod_scores);
    this->bytes_processed += dbr.total_size;
}

// import scores from mcosu, or old neomod (before we started saving replays)
void Database::loadOldMcNeomodScores(std::string_view dbPath) {
    ByteBufferedFile::Reader dbr(dbPath);

    u32 db_version = dbr.read<u32>();
    if(dbr.total_size == 0 || db_version == 0) {
        this->bytes_processed += dbr.total_size;
        return;
    }

    u32 nb_imported = 0;
    const bool is_mcosu = (db_version == 20210106 || db_version == 20210108 || db_version == 20210110);
    const bool is_neomod =
        !is_mcosu && db_version > 20210110;  // if it's older, it can't be neomod (too old mcosu database)

    if(is_neomod) {
        u32 nb_beatmaps = dbr.read<u32>();
        for(u32 b = 0; b < nb_beatmaps; b++) {
            u32 progress_bytes = this->bytes_processed + dbr.total_pos;
            f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
            this->loading_progress = std::clamp(progress_float, 0.01, 0.99);
            MD5Hash md5hash;
            (void)dbr.read_hash_chars(md5hash);  // TODO: validate
            u32 nb_scores = dbr.read<u32>();

            for(u32 s = 0; s < nb_scores; s++) {
                dbr.skip<u8>();   // gamemode (always 0)
                dbr.skip<u32>();  // score version

                FinishedScore sc;

                sc.unixTimestamp = dbr.read<u64>();
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
                sc.maxPossibleCombo = dbr.read<u32>();
                sc.numHitObjects = dbr.read<u32>();
                sc.numCircles = dbr.read<u32>();
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
                    else if(mod == "osu_mod_jigsaw1") sc.mods.flags |= ModFlags::Jigsaw1;
                    else if(mod == "osu_mod_jigsaw2") sc.mods.flags |= ModFlags::Jigsaw2;
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
            u32 progress_bytes = this->bytes_processed + dbr.total_pos;
            f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
            this->loading_progress = std::clamp(progress_float, 0.01, 0.99);

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
                const auto unixTimestamp = dbr.read<uint64_t>();
                const std::string playerName{dbr.read_string()};
                if(this->isScoreAlreadyInDB(md5hash, unixTimestamp, playerName) >= 0) {
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

                    sc.unixTimestamp = unixTimestamp;

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
                        else if(mod == "osu_mod_jigsaw1") sc.mods.flags |= ModFlags::Jigsaw1;
                        else if(mod == "osu_mod_jigsaw2") sc.mods.flags |= ModFlags::Jigsaw2;
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

    this->bytes_processed += dbr.total_size;
}

void Database::loadPeppyScores(std::string_view dbPath) {
    ByteBufferedFile::Reader dbr(dbPath);
    int nb_imported = 0;

    u32 db_version = dbr.read<u32>();
    u32 nb_beatmaps = dbr.read<u32>();
    if(dbr.total_size == 0 || db_version == 0) {
        this->bytes_processed += dbr.total_size;
        return;
    }

    debugLog("osu!stable scores.db: version = {:d}, nb_beatmaps = {:d}", db_version, nb_beatmaps);

    char client_str[15] = "peppy-YYYYMMDD";
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
            snprintf(client_str, 14, "peppy-%d", score_version);
            sc.client = client_str;

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

            u64 full_tms = dbr.read<u64>();
            sc.unixTimestamp = (full_tms - UNIX_EPOCH_TICKS) / TICKS_PER_SECOND;
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

        u32 progress_bytes = this->bytes_processed + dbr.total_pos;
        f64 progress_float = (f64)progress_bytes / (f64)this->total_bytes;
        this->loading_progress = std::clamp(progress_float, 0.01, 0.99);
    }

    debugLog("Loaded {:d} osu!stable scores", nb_imported);
    this->bytes_processed += dbr.total_size;
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

    dbr.write_bytes((u8 *)"NEOSC", 5);
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
            dbr.write<u64>(score.unixTimestamp);
            dbr.write<i32>(score.player_id);
            dbr.write_string(score.playerName);
            dbr.write<u8>((u8)score.grade);

            dbr.write_string(score.client);
            dbr.write_string(score.server);
            dbr.write<i64>(score.bancho_score_id);
            dbr.write<u64>(score.peppy_replay_tms);

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

std::unique_ptr<BeatmapSet> Database::loadRawBeatmap(const std::string &beatmapPath, bool is_peppy) {
    logIfCV(debug_db, "beatmap path: {:s}", beatmapPath);

    // try loading all diffs
    DatabaseBeatmap::LoadError lastError;

    auto diffs = std::make_unique<DiffContainer>();

    std::vector<std::string> beatmapFiles = env->getFilesInFolder(beatmapPath);

    for(const auto &beatmapFile : beatmapFiles) {
        std::string ext = env->getFileExtensionFromFilePath(beatmapFile);
        if(ext.compare("osu") != 0) continue;

        std::string fullFilePath = beatmapPath;
        fullFilePath.append(beatmapFile);

        auto map = std::make_unique<BeatmapDifficulty>(fullFilePath, beatmapPath,
                                                       is_peppy ? DatabaseBeatmap::BeatmapType::PEPPY_DIFFICULTY
                                                                : DatabaseBeatmap::BeatmapType::NEOMOD_DIFFICULTY);
        auto res = map->loadMetadata();
        if(!res.error.errc) {
            diffs->push_back(std::move(map));
        } else {
            lastError = res.error;
            logIfCV(debug_db, "Couldn't loadMetadata: {}, deleting object.", res.error.error_string());
        }
    }

    std::unique_ptr<BeatmapSet> set{nullptr};
    if(diffs && !diffs->empty()) {
        set =
            std::make_unique<BeatmapSet>(std::move(diffs), is_peppy ? DatabaseBeatmap::BeatmapType::PEPPY_BEATMAPSET
                                                                    : DatabaseBeatmap::BeatmapType::NEOMOD_BEATMAPSET);
    }

    if(!set && lastError.errc) {
        debugLog("Couldn't load beatmapset {}: {}", beatmapPath, lastError.error_string());
    }

    return set;
}

void Database::update_overrides(BeatmapDifficulty *diff) {
    if(!diff || diff->do_not_store || diff->type != DatabaseBeatmap::BeatmapType::PEPPY_DIFFICULTY) return;

    Sync::unique_lock lock(this->peppy_overrides_mtx);
    this->peppy_overrides[diff->getMD5()] = diff->get_overrides();
}
