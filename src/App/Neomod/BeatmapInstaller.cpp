// Copyright (c) 2026, WH, All rights reserved.

#include "BeatmapInstaller.h"

#include "AsyncPool.h"
#include "AsyncTypes.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "Downloader.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "FixedSizeArray.h"
#include "i18n.h"
#include "Logging.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "OsuConfig.h"
#include "Parsing.h"
#include "SongBrowser/SongBrowser.h"
#include "UI.h"

#include <cctype>

void BeatmapInstaller::enqueue(i32 set_id, bool auto_select, std::string_view display_name) {
    if(set_id <= 0) return;

    auto [it, inserted] = this->entries.try_emplace(set_id);
    Entry& e = it->second;
    if(inserted) {
        e.stage = MapInstallStage::Queued;
        e.auto_select = auto_select;
        if(!display_name.empty()) e.display_name.assign(display_name);
        return;
    }

    // already tracked: OR in the auto_select intent so re-clicking before completion still navigates on Done
    if(auto_select) e.auto_select = true;

    // upgrade an empty name with whatever the new caller has (don't clobber an existing name)
    if(e.display_name.empty() && !display_name.empty()) e.display_name.assign(display_name);

    // if a previous attempt failed, allow retry
    if(e.stage == MapInstallStage::Failed) {
        e.dl_handle.reset();
        e.progress = 0.f;
        e.finished_time = 0.0;
        e.stage = MapInstallStage::Queued;
    }
}

void BeatmapInstaller::enqueue_local(std::string osz_path, bool auto_select, bool delete_after) {
    if(osz_path.empty()) return;

    // de-dupe by path so the same file scanned/dropped twice doesn't import twice; a later request
    // to navigate to it still takes effect once it lands.
    for(LocalEntry& existing : this->local_imports) {
        if(existing.osz_path == osz_path) {
            if(auto_select) existing.auto_select = true;
            return;
        }
    }

    LocalEntry le;
    le.osz_path = std::move(osz_path);
    le.auto_select = auto_select;
    le.delete_after = delete_after;
    le.stage = MapInstallStage::Queued;
    this->local_imports.push_back(std::move(le));
}

void BeatmapInstaller::cancel(i32 set_id) {
    if(auto it = this->entries.find(set_id); it != this->entries.end()) {
        // abort the in-flight transfer (if any), then drop the entry
        Downloader::abort_download(it->second.dl_handle);
        this->entries.erase(it);
    }
}

BeatmapInstaller::State BeatmapInstaller::get_state(i32 set_id) const {
    auto it = this->entries.find(set_id);
    if(it == this->entries.end()) return {};
    return {it->second.stage, it->second.progress};
}

void BeatmapInstaller::snapshot(std::vector<BeatmapInstaller::EntryView>& out) const {
    out.clear();
    out.reserve(this->entries.size());
    for(const auto& [set_id, e] : this->entries) {
        out.push_back({.set_id = set_id, .stage = e.stage, .progress = e.progress, .display_name = e.display_name});
    }
    return;
}

std::vector<BeatmapInstaller::EntryView> BeatmapInstaller::snapshot() const {
    std::vector<EntryView> out;
    snapshot(out);
    return out;
}

void BeatmapInstaller::clear() { this->entries.clear(); }

void BeatmapInstaller::update() {
    if(this->entries.empty() && this->local_imports.empty()) return;

    const f64 now = engine->getTime();

    for(auto it = this->entries.begin(); it != this->entries.end();) {
        const i32 set_id = it->first;
        Entry& e = it->second;

        switch(e.stage) {
            using enum MapInstallStage;
            case Queued:
            case Downloading: {
                // download_beatmapset lazily creates the handle on first call (when e.dl_handle is null),
                // then on each subsequent call polls completion and performs extraction once bytes arrive.
                const bool ready = Downloader::download_beatmapset(static_cast<u32>(set_id), e.dl_handle);
                if(ready) {
                    // either freshly extracted, or the directory already existed on disk
                    e.progress = 1.f;
                    e.stage = Installing;
                } else if(e.dl_handle.failed()) {
                    e.stage = Failed;
                    e.finished_time = now;
                    this->on_failed(set_id);
                } else {
                    e.progress = e.dl_handle.progress();
                    e.stage = Downloading;
                }
                break;
            }

            case Installing: {
                // defensive: only import while db isn't being rebuilt. db->isFinished() flips back to <1
                // during a refresh, and addBeatmapSet would race the loader.
                if(!db->isFinished() || db->isCancelled()) break;

                const std::string mapset_path = fmt::format(NEOMOD_MAPS_PATH "/{}/", set_id);
                auto [set, imported] = db->addBeatmapSet(mapset_path, set_id);
                if(set == nullptr) {
                    e.stage = Failed;
                    e.finished_time = now;
                    this->on_failed(set_id);
                } else {
                    e.stage = Done;
                    e.finished_time = now;
                    ui->getNotificationOverlay()->addToast(tformat("Downloaded beatmapset #{:d}", set_id),
                                                           SUCCESS_TOAST);
                    this->on_done(set, set_id, imported, e.auto_select);
                }
                break;
            }

            case Done:
            case Failed:
            case None:
            case Extracting:  // downloads never enter Extracting; that stage is local-import only
                break;
        }

        // housekeeping: drop Done entries (db is now authoritative; listings query db->getBeatmapSet directly),
        // and expire Failed entries after a TTL so retries are possible without manual reset.
        if((e.stage == MapInstallStage::Done) ||
           (e.stage == MapInstallStage::Failed && (now - e.finished_time) > FAILED_ENTRY_TTL_S)) {
            it = this->entries.erase(it);
        } else {
            ++it;
        }
    }

    // local .osz imports (drag-drop, file association, maps/ watcher). these have no set_id until the
    // archive is cracked open, so they live here rather than in the set_id-keyed download map. nothing
    // else mutates local_imports during this loop, so iterators stay valid.
    for(auto it = this->local_imports.begin(); it != this->local_imports.end();) {
        LocalEntry& le = *it;

        switch(le.stage) {
            using enum MapInstallStage;
            case Queued: {
                // read + decompress + extract on a worker so the main thread never blocks on it.
                le.extract_future = Async::submit(
                    [path = le.osz_path]() -> i32 {
                        FixedSizeArray<u8> osz_data;
                        {
                            File osz(path);
                            const uSz filesize = osz.getFileSize();
                            osz_data = FixedSizeArray{osz.takeFileBuffer(), filesize};
                            if(!osz.canRead() || !filesize || !osz_data.data()) return -1;
                        }

                        // fallback id: a leading number in the filename, e.g. "12345 Artist - Title.osz"
                        i32 fallback_id = -1;
                        std::string name = Environment::getFileNameFromFilePath(path);
                        if(!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
                            if(!Parsing::parse(name, &fallback_id)) fallback_id = -1;
                        }

                        return Downloader::resolve_and_extract_osz(osz_data, name, fallback_id);
                    },
                    Lane::Background);
                le.stage = Extracting;
                break;
            }

            case Extracting: {
                if(!le.extract_future.is_ready()) break;
                le.set_id = le.extract_future.get();
                if(le.set_id <= 0) {
                    le.stage = Failed;
                    le.finished_time = now;
                    ui->getNotificationOverlay()->addToast(
                        fmt::format("Failed to import {}", Environment::getFileNameFromFilePath(le.osz_path)),
                        ERROR_TOAST);
                } else {
                    le.stage = Installing;
                }
                break;
            }

            case Installing: {
                // same guard as downloads: don't import while the database is being (re)built.
                // TODO: dedup
                if(!db->isFinished() || db->isCancelled()) break;

                const std::string mapset_path = fmt::format(NEOMOD_MAPS_PATH "/{}/", le.set_id);
                auto [set, imported] = db->addBeatmapSet(mapset_path, le.set_id);
                if(set == nullptr) {
                    le.stage = Failed;
                    le.finished_time = now;
                    ui->getNotificationOverlay()->addToast(
                        fmt::format("Failed to import {}", Environment::getFileNameFromFilePath(le.osz_path)),
                        ERROR_TOAST);
                } else {
                    le.stage = Done;
                    le.finished_time = now;
                    if(le.delete_after) env->deleteFile(le.osz_path);
                    this->on_done(set, le.set_id, imported, le.auto_select);
                }
                break;
            }

            case Downloading:
            case Done:
            case Failed:
            case None:
                break;
        }

        if(le.stage == MapInstallStage::Done ||
           (le.stage == MapInstallStage::Failed && (now - le.finished_time) > FAILED_ENTRY_TTL_S)) {
            it = this->local_imports.erase(it);
        } else {
            ++it;
        }
    }
}

void BeatmapInstaller::on_done(BeatmapSet* set, i32 set_id, bool added, bool auto_select) {
    // we may have added a duplicate
    debugLog("Finished installing beatmapset {:d}{:s}", set_id, !added ? " (duplicate)" : "");
    if(added) {
        ui->getSongBrowser()->addBeatmapSet(set);
    }

    if(auto_select) {
        const auto& diffs = set->getDifficulties();
        assert(!diffs.empty());  // if we successfully added it, we must have difficulties!

        // TODO: spaghetti
        // (onDifficultySelected just plays music, i.e. we can call it when we are still in online beatmaps screen)
        // otherwise actually select it
        // ALSO TODO: if we are in songbrowser and importing a lot of maps, it will cause us to jump around...
        // (existing issue)
        if(ui->getActiveScreen() == ui->getSongBrowserBase()) {
            ui->getSongBrowser()->selectBeatmapset(set);
        } else {
            ui->getSongBrowser()->onDifficultySelected(diffs[0].get(), false);
        }
    }
}

void BeatmapInstaller::on_failed(i32 set_id) {
    ui->getNotificationOverlay()->addToast(tformat("Failed to download beatmapset #{:d} :(", set_id), ERROR_TOAST);
}
