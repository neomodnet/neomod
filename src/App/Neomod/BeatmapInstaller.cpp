// Copyright (c) 2026, WH, All rights reserved.

#include "BeatmapInstaller.h"

#include "Archival.h"
#include "AsyncPool.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "DownloadHandle.h"
#include "Downloader.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "i18n.h"
#include "Logging.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "OsuConfig.h"
#include "Parsing.h"
#include "SongBrowser/SongBrowser.h"
#include "SString.h"
#include "UI.h"

#include <cctype>

namespace {  // internal utils

// one queued import. two kinds share the stage machine, discriminated by is_local():
// - download: set_id known up front, dl_handle drives Queued/Downloading; the fetched bytes
//   then ride extract_future through Extracting like a local import
// - local .osz: osz_path set, extract_future also resolves set_id (-1 until then)
struct Entry {
    u32 uid{0};
    i32 set_id{-1};
    std::string display_name;
    Downloader::DownloadHandle dl_handle;
    std::string osz_path;
    Async::Future<i32> extract_future;  // resolved set_id (>0) on success, <= 0 on failure
    MapInstallStage stage{MapInstallStage::Queued};
    f32 progress{0.f};
    bool auto_select{false};
    bool delete_after{false};
    // download whose Installing attempt sources a pre-existing maps/<set_id>/ folder instead of a
    // fresh extraction; a failed import escalates to a real download instead of failing the entry
    bool from_disk{false};
    f64 finished_time{0.0};

    [[nodiscard]] bool is_local() const { return !this->osz_path.empty(); }
};

// result of try_import(): set/imported are valid only when ready == true. ready == false means the
// db is mid-(re)build and the caller should retry next tick (addBeatmapSet would race the loader).
struct ImportResult {
    BeatmapSet* set{nullptr};
    bool imported{false};
    bool ready{false};
};

// shared Installing-stage tail for downloads and local imports: imports the already-extracted
// maps/<set_id>/ folder once the db is idle.
ImportResult try_import(i32 set_id) {
    // db->isFinished() drops below 1 during a refresh; importing then would race the loader thread
    // appending to beatmapsets, so wait for it to settle (the caller retries next tick).
    if(!db->isFinished() || db->isCancelled()) return {};

    auto [set, added] = db->addBeatmapSet(fmt::format(NEOMOD_MAPS_PATH "/{}/", set_id), set_id);
    return {set, added, true};
}

void on_done(BeatmapSet* set, i32 set_id, bool added, bool auto_select) {
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
        if(ui->getActiveScreen() == ui->getSongBrowserBase()) {
            ui->getSongBrowser()->selectBeatmapset(set);
        } else {
            ui->getSongBrowser()->onDifficultySelected(diffs[0].get(), false);
        }
    }
}

void fail_entry(Entry& e, f64 now) {
    e.stage = MapInstallStage::Failed;
    e.finished_time = now;
    if(e.is_local()) {
        ui->getNotificationOverlay()->addToast(tformat("Failed to import {}", e.display_name), ERROR_TOAST);
    } else {
        ui->getNotificationOverlay()->addToast(tformat("Failed to download beatmapset #{:d} :(", e.set_id),
                                               ERROR_TOAST);
    }
}

// .osz extraction primitives (shared with Database::importLooseOsz via read_and_extract_osz)

i32 get_beatmapset_id_from_osu_file(std::string_view file) {
    i32 set_id = -1;
    bool inMetadata = false;

    for(const auto line : SString::split_newlines(file)) {
        if(line.empty() || SString::is_comment(line)) continue;
        if(line.contains("[Metadata]")) {
            inMetadata = true;
            continue;
        }
        if(line.starts_with('[') && inMetadata) {
            break;
        }
        if(inMetadata) {
            if(Parsing::parse(line, "BeatmapSetID", ':', &set_id)) {
                return set_id;
            }
            continue;
        }
    }

    return -1;
}

// write already-decompressed archive entries into map_dir, creating parent directories as needed.
// files that can't be written are skipped (validated later when the beatmap is loaded); returns
// false if nothing at all could be written.
bool write_entries_to_dir(const std::vector<Archive::Entry>& entries, std::string_view map_dir) {
    std::string base{map_dir};
    while(base.size() > 1 && (base.back() == '/' || base.back() == '\\')) base.pop_back();

    const bool existed = env->directoryExists(base);
    if(!existed) env->createDirectory(base);

    bool wrote_any = false;
    for(const auto& entry : entries) {
        if(entry.isDirectory()) continue;

        std::string filename = entry.getFilename();
        File::normalizeSlashes(filename, '\\', '/');

        if(filename.find("../") != std::string::npos) continue;  // path traversal guard

        std::string dir_path{base};
        const auto folders = SString::split(filename, '/');
        for(size_t i = 0; i + 1 < folders.size(); i++) {
            dir_path.append("/").append(folders[i]);
            env->createDirectory(dir_path);
        }

        const std::string extract_to = fmt::format("{}/{}", base, filename);
        if(entry.extractToFile(extract_to)) {
            wrote_any = true;
        } else {
            debugLog("Failed to extract file {:s}", filename);
        }
    }

    // if we created the destination just now but then wrote nothing into it, don't leave an empty
    // maps/<id>/ behind
    if(!wrote_any && !existed) {
        // no-op if it doesnt exist
        env->deletePathsRecursive(base);
    }

    return wrote_any;
}

// osu! always stores .osz entry names as Shift-JIS (CP932)
constexpr std::string_view ARCHIVE_CHARSET{"CP932"};

}  // namespace

// static helpers

bool BeatmapInstaller::extract_beatmapset(std::span<const u8> data, const std::string& map_dir) {
    debugLog("Extracting beatmapset ({:d} bytes)", data.size());

    Archive::Reader archive(data, ARCHIVE_CHARSET);
    if(!archive.isValid()) {
        debugLog("Failed to open .osz file");
        return false;
    }

    auto entries = archive.getAllEntries();
    if(entries.empty()) {
        debugLog(".osz file is empty!");
        return false;
    }

    return write_entries_to_dir(entries, map_dir);
}

i32 BeatmapInstaller::resolve_and_extract_osz(std::span<const u8> data, std::string_view osz_name) {
    debugLog("Reading beatmapset {:s} ({:d} bytes)", osz_name, data.size());

    Archive::Reader archive(data, ARCHIVE_CHARSET);
    if(!archive.isValid()) {
        debugLog("Failed to open .osz file");
        return -1;
    }

    auto entries = archive.getAllEntries();
    if(entries.empty()) {
        debugLog(".osz file is empty!");
        return -1;
    }

    // single decompression pass: the entries are already in memory, so resolve the set id from the
    // first .osu that declares one and then write those same buffers to disk without re-extracting.
    i32 set_id = -1;
    for(const auto& entry : entries) {
        if(entry.isDirectory()) continue;
        if(!entry.getFilename().ends_with(".osu")) continue;

        const auto& osu_data = entry.getUncompressedData();
        if(osu_data.empty()) continue;

        set_id = get_beatmapset_id_from_osu_file(
            std::string_view{reinterpret_cast<const char*>(osu_data.data()), osu_data.size()});
        if(set_id > 0) break;
    }

    // fallback: a leading number in the filename, e.g. "12345 Artist - Title.osz"
    if(set_id <= 0 && !osz_name.empty() && std::isdigit(static_cast<unsigned char>(osz_name.front()))) {
        i32 parsed = -1;
        if(Parsing::parse(osz_name, &parsed)) set_id = parsed;
    }
    // ids must be > 0; without this an id of 0 (e.g. "BeatmapSetID:0" or a filename like "0 foo.osz")
    // would extract into maps/0/ and then be treated as a failure by every caller, orphaning the folder
    if(set_id <= 0) return -1;

    if(!write_entries_to_dir(entries, fmt::format(NEOMOD_MAPS_PATH "/{}/", set_id))) {
        return -1;
    }
    return set_id;
}

i32 BeatmapInstaller::read_and_extract_osz(std::string_view path) {
    std::unique_ptr<u8[]> osz_data;
    uSz filesize = 0;
    {
        File osz(path);
        filesize = osz.getFileSize();
        if(!osz.canRead() || !filesize) return -1;
        osz_data = osz.takeFileBuffer();
        if(!osz_data.get()) return -1;
    }
    return resolve_and_extract_osz({osz_data.get(), filesize}, Environment::getFileNameFromFilePath(path));
}

// public methods implementation below

struct BeatmapInstaller::BMInstallerImpl final {
    std::vector<Entry> entries;  // typically <= 5 entries, so linear scans throughout
    u32 next_uid{1};
};

BeatmapInstaller::BeatmapInstaller() : m() {}
BeatmapInstaller::~BeatmapInstaller() = default;

void BeatmapInstaller::enqueue(i32 set_id, bool auto_select, std::string_view display_name) {
    if(set_id <= 0) return;

    // de-dupe against other downloads only: a local import that resolved to the same set coexists
    // harmlessly (try_import/addBeatmapSet are idempotent, the second one just finds a duplicate).
    for(Entry& e : m->entries) {
        if(e.is_local() || e.set_id != set_id) continue;

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
        return;
    }

    Entry e;
    e.uid = m->next_uid++;
    e.set_id = set_id;
    e.auto_select = auto_select;
    if(!display_name.empty()) e.display_name.assign(display_name);
    m->entries.push_back(std::move(e));
}

void BeatmapInstaller::enqueue_local(std::string osz_path, bool auto_select, bool delete_after) {
    if(osz_path.empty()) return;

    // de-dupe by path so the same file scanned/dropped twice doesn't import twice; a later request
    // to navigate to it still takes effect once it lands.
    bool any_local = false;
    for(Entry& existing : m->entries) {
        if(!existing.is_local()) continue;
        any_local = true;
        if(existing.osz_path == osz_path) {
            if(auto_select) existing.auto_select = true;
            return;
        }
    }

    Entry e;
    e.uid = m->next_uid++;
    e.osz_path = std::move(osz_path);
    e.display_name = Environment::getFileNameFromFilePath(e.osz_path);
    e.delete_after = delete_after;

    // auto-select first enqueued map if we get > 1 at a time
    // NOTE: this logic will work "incorrectly" if we ever try to enqueue a local beatmap to import with auto_select is false
    // and the queue already has non-auto_select local entries in it,
    // but that currently never happens, and this is simpler than re-scanning it or adding more bookkeeping
    e.auto_select = auto_select && !any_local;

    m->entries.push_back(std::move(e));
}

void BeatmapInstaller::cancel(i32 set_id) {
    for(auto it = m->entries.begin(); it != m->entries.end(); ++it) {
        if(it->is_local() || it->set_id != set_id) continue;
        // abort the in-flight transfer (if any), then drop the entry
        Downloader::abort_download(it->dl_handle);
        m->entries.erase(it);
        return;
    }
}

void BeatmapInstaller::cancel_entry(u32 uid) {
    for(auto it = m->entries.begin(); it != m->entries.end(); ++it) {
        if(it->uid != uid) continue;
        // aborts the transfer (if still downloading); an in-flight extract future is simply
        // abandoned (a finished extraction leaves maps/<id>/ orphaned, same as quitting mid-extract,
        // and a local source .osz stays put for a later import pass)
        Downloader::abort_download(it->dl_handle);
        m->entries.erase(it);
        return;
    }
}

BeatmapInstaller::State BeatmapInstaller::get_state(i32 set_id) const {
    if(set_id <= 0) return {};
    for(const Entry& e : m->entries) {
        if(e.set_id == set_id) return {e.stage, e.progress};
    }
    return {};
}

void BeatmapInstaller::snapshot(std::vector<BeatmapInstaller::EntryView>& out) const {
    out.clear();
    out.reserve(m->entries.size());
    for(const Entry& e : m->entries) {
        out.push_back({.uid = e.uid,
                       .set_id = e.set_id,
                       .stage = e.stage,
                       .progress = e.progress,
                       .display_name = e.display_name});
    }
}

std::vector<BeatmapInstaller::EntryView> BeatmapInstaller::snapshot() const {
    std::vector<EntryView> out;
    snapshot(out);
    return out;
}

bool BeatmapInstaller::has_pending() const {
    using enum MapInstallStage;
    for(const Entry& e : m->entries) {
        if(e.stage != Done && e.stage != Failed && e.stage != None) return true;
    }
    return false;
}

void BeatmapInstaller::update() {
    if(m->entries.empty()) return;

    const f64 now = engine->getTime();

    for(auto it = m->entries.begin(); it != m->entries.end();) {
        Entry& e = *it;
        bool severed = false;

        switch(e.stage) {
            using enum MapInstallStage;
            case Queued:
                if(e.is_local()) {
                    // read + decompress + extract on a worker so the main thread never blocks on it.
                    e.extract_future = Async::submit(
                        [path = e.osz_path]() -> i32 { return read_and_extract_osz(path); }, Lane::Background);
                    e.stage = Extracting;
                    break;
                } else {  // trying to download, check disk first
                    // if the db doesn't know this set but maps/<set_id>/ already exists
                    // (crash before a db save, deleted db, ...), try importing it as-is before spending
                    // a transfer; "Installing"/try_import is the oracle for whether the folder is actually usable,
                    // which will upgrade it back to a real download if it isn't (empty, partial, garbage).
                    // if the db DOES have the set, the enqueuer wants bytes the db doesn't have (e.g.
                    // an updated version (TODO!)), so always download. db busy => can't know, just download.
                    if(db->isFinished() && !db->isCancelled() && db->getBeatmapSet(e.set_id) == nullptr &&
                       env->directoryExists(fmt::format(NEOMOD_MAPS_PATH "/{}/", e.set_id))) {
                        e.from_disk = true;
                        e.stage = Installing;
                        break;
                    }
                }
                [[fallthrough]];
            case Downloading: {
                // download_beatmapset lazily creates the handle on first call (when e.dl_handle is null),
                // then on each subsequent call polls completion.
                const bool ready = Downloader::download_beatmapset(static_cast<u32>(e.set_id), e.dl_handle);
                if(ready) {
                    // bytes arrived: from here on a download is just a local import whose .osz is
                    // already in memory. decompress on a worker, into the folder whose id we know.
                    e.extract_future = Async::submit(
                        [data = e.dl_handle.take_data(), set_id = e.set_id]() -> i32 {
                            return extract_beatmapset(data, fmt::format(NEOMOD_MAPS_PATH "/{}/", set_id)) ? set_id : -1;
                        },
                        Lane::Background);
                    e.dl_handle.reset();
                    e.progress = 1.f;
                    e.stage = Extracting;
                } else if(e.dl_handle.failed()) {
                    fail_entry(e, now);
                } else if(e.dl_handle.cancelled()) {
                    // transfer aborted externally (Downloader::abort_downloads() on bancho disconnect):
                    // it will never complete or fail, so drop the entry silently.
                    severed = true;
                } else {
                    e.progress = e.dl_handle.progress();
                    e.stage = Downloading;
                }
                break;
            }

            case Extracting: {
                if(!e.extract_future.is_ready()) break;
                // <= 0 means extraction failed; leave set_id alone (a download knows its real id,
                // which fail() puts in the toast)
                if(const i32 resolved = e.extract_future.get(); resolved <= 0) {
                    fail_entry(e, now);
                } else {
                    e.set_id = resolved;
                    e.stage = Installing;
                }
                break;
            }

            case Installing: {
                auto [set, imported, ready] = try_import(e.set_id);
                if(!ready) break;  // db busy/rebuilding; retry next tick

                if(set == nullptr) {
                    if(e.from_disk) {
                        // the re-extraction will overwrite the potentially corrupt folder we had
                        debugLog("maps/{:d}/ exists but isn't importable, downloading instead", e.set_id);
                        e.from_disk = false;
                        e.stage = Downloading;
                        break;
                    }
                    fail_entry(e, now);
                } else {
                    e.stage = Done;
                    e.finished_time = now;
                    if(e.is_local()) {
                        if(e.delete_after) env->deleteFile(e.osz_path);
                    } else {
                        ui->getNotificationOverlay()->addToast(e.from_disk
                                                                   ? tformat("Imported beatmapset #{:d}", e.set_id)
                                                                   : tformat("Downloaded beatmapset #{:d}", e.set_id),
                                                               SUCCESS_TOAST);
                    }
                    on_done(set, e.set_id, imported, e.auto_select);
                }
                break;
            }

            case Done:
            case Failed:
            case None:
                break;
        }

        // housekeeping: drop Done entries (db is now authoritative; listings query db->getBeatmapSet directly)
        // and severed downloads, and expire Failed entries after a TTL so retries are possible without manual reset.
        if(severed || (e.stage == MapInstallStage::Done) ||
           (e.stage == MapInstallStage::Failed && (now - e.finished_time) > FAILED_ENTRY_TTL_S)) {
            it = m->entries.erase(it);
        } else {
            ++it;
        }
    }
}
