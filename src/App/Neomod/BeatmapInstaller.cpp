// Copyright (c) 2026, WH, All rights reserved.

#include "BeatmapInstaller.h"

#include "Archival.h"
#include "AsyncPool.h"
#include "BeatmapInterface.h"
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

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>

namespace {  // internal utils
using namespace std::string_view_literals;
namespace fs = std::filesystem;

// what an extraction worker hands back: the maps/ folder the archive went into (empty on failure) and that
// folder's diffs, parsed right there so the main-thread import only has to match them against the db
struct Extracted {
    std::string folder;
    std::unique_ptr<DiffContainer> diffs{};
};

Extracted parse_extracted(std::string folder) {
    Extracted out{.folder = std::move(folder)};
    if(!out.folder.empty()) out.diffs = Database::parseFolderDiffs(NEOMOD_MAPS_PATH "/" + out.folder + "/", false);
    return out;
}

// one queued import. two kinds share the stage machine, discriminated by is_local():
// - download: set_id known up front, dl_handle drives Queued/Downloading; the fetched bytes
//   then use the extract_handle through Extracting like a local import
// - local .osz: osz_path set; only the archive knows its set id (and it may not have one at all)
struct Entry {
    u32 uid{0};
    i32 set_id{-1};
    std::string display_name;
    Downloader::DownloadHandle dl_handle;
    std::string osz_path;
    std::string folder;                    // maps/ folder the archive was extracted into (relative)
    std::unique_ptr<DiffContainer> diffs;  // its .osu files, parsed by the extraction worker, consumed by the import
    Async::CancellableHandle<Extracted> extract_handle;  // dropping the entry drops a still-queued extraction with it
    MapInstallStage stage{MapInstallStage::Queued};
    f32 progress{0.f};
    bool auto_select{false};
    bool delete_after{false};
    f64 finished_time{0.0};

    [[nodiscard]] bool is_local() const { return !this->osz_path.empty(); }
};

// shared Installing-stage tail for downloads and local imports: imports the already-extracted folder
// once it's safe to. nullopt means the caller should retry next tick: the db is mid-(re)build
// (reconciling then would race the loader thread), or a map is being played (an updated/removed
// selection would unload it).
std::optional<ReconcileResult> try_import(Entry& e) {
    if(!db->isFinished() || db->isCancelled() || osu->isInPlayMode()) return std::nullopt;

    // the worker's parse stands in for the listing, so the files are matched by content (their mtimes could
    // share a stale record's second). a download stamps its id onto diffs that don't declare one
    return db->reconcileFolder(Database::MapRoot::Neomod, e.folder, Database::ReconcileMode::PerFile,
                               e.is_local() ? -1 : e.set_id, std::move(e.diffs));
}

// toasts + auto-select for a finished import (the song browser has already been synced with r)
void on_done(const ReconcileResult& r, const Entry& e) {
    using enum ReconcileResult::Outcome;
    auto* sb = ui->getSongBrowser();
    auto* toasts = ui->getNotificationOverlay();

    // the set to navigate to: the folder's, or the one that already owns these diffs
    BeatmapSet* set = r.set ? r.set : r.dedup_owner;
    debugLog("Finished installing {} into maps/{}/: {} (+{:d} -{:d})",
             e.is_local() ? e.display_name : fmt::format("beatmapset #{:d}", e.set_id), e.folder, r.outcomeName(),
             r.added, r.removed);

    if(r.outcome != Created && r.outcome != Updated) {  // nothing the db didn't already have
        toasts->addToast(e.is_local() ? tformat("{} is already installed", e.display_name)
                                      : tformat("Beatmapset #{:d} is already installed", e.set_id),
                         INFO_TOAST);
    }
    if(!e.is_local() && r.outcome != Unchanged) {
        toasts->addToast(tformat("Downloaded beatmapset #{:d}", e.set_id), SUCCESS_TOAST);
    }

    if(e.auto_select && set) {
        const auto& diffs = set->getDifficulties();
        assert(!diffs.empty());

        // TODO: spaghetti
        // (onDifficultySelected just plays music, i.e. we can call it when we are still in online beatmaps screen)
        // otherwise actually select it
        if(ui->getActiveScreen() == ui->getSongBrowserBase()) {
            sb->selectBeatmapset(set);
        } else {
            sb->onDifficultySelected(diffs[0].get(), false);
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

struct OszMeta {
    i32 set_id{-1};
    std::string artist;
    std::string title;
};

// the [Metadata] of one .osu: enough to know the set and to name its folder
OszMeta parse_osz_meta(std::string_view file) {
    OszMeta meta;
    bool inMetadata = false;

    for(const auto line : SString::split_newlines(file)) {
        if(line.empty() || SString::is_comment(line)) continue;
        if(line.contains("[Metadata]")) {
            inMetadata = true;
            continue;
        }
        if(!inMetadata) continue;
        if(line.starts_with('[')) break;

        if(Parsing::parse(line, "Artist", ':', &meta.artist)) continue;
        if(Parsing::parse(line, "Title", ':', &meta.title)) continue;
        Parsing::parse(line, "BeatmapSetID", ':', &meta.set_id);
    }

    return meta;
}

// something every filesystem the maps/ folder could end up on accepts
std::string sanitize_folder_name(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for(const char c : name) {
        const bool bad = "\\/:*?\"<>|"sv.contains(c) || static_cast<unsigned char>(c) < 0x20 || c == 0x7f;
        out.push_back(bad ? '_' : c);
    }
    auto trim_ends = [&out] {
        while(!out.empty() && out.front() == ' ') out.erase(0, 1);
        while(!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();  // windows
    };
    trim_ends();
    if(out.size() > 200) {  // cap the length without splitting a utf-8 sequence
        uSz cut = 200;
        while(cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;
        out.resize(cut);
        trim_ends();
    }
    return out;
}

// where a set lives under maps/: the only place that decides folder names. an installed set (by id) keeps
// its folder, which is how new or updated difficulties end up next to the existing ones; everything else
// gets osu!stable's "<setid> Artist - Title" (a local file's own stem already has that shape if it came
// from the website, and is whatever the user named it otherwise). "" if nothing usable is known
std::string target_folder_name(i32 set_id, std::string_view osz_stem, const OszMeta& meta) {
    if(set_id > 0) {
        if(std::string existing = db->getBeatmapSetFolder(set_id); !existing.empty()) return existing;
    }

    std::string name = sanitize_folder_name(osz_stem);
    if(name.empty() && (!meta.artist.empty() || !meta.title.empty())) {
        name = sanitize_folder_name(set_id > 0 ? fmt::format("{} {} - {}", set_id, meta.artist, meta.title)
                                               : fmt::format("{} - {}", meta.artist, meta.title));
    }
    if(name.empty() && set_id > 0) name = fmt::to_string(set_id);
    return name;
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
    // folder behind
    if(!wrote_any && !existed) {
        // no-op if it doesnt exist
        env->deletePathsRecursive(base);
    }

    return wrote_any;
}

// osu! always stores .osz entry names as Shift-JIS (CP932)
constexpr std::string_view ARCHIVE_CHARSET{"CP932"};

// maps/<folder> as the directory watcher would see it right now, for telling its events apart from the
// installer's own writes (see claim()): the mtime, or a sentinel for a folder that isn't there (an uninstall)
fs::file_time_type folder_state(std::string_view folder) {
    std::error_code ec;
    const auto mtime = fs::last_write_time(File::getFsPath(NEOMOD_MAPS_PATH "/" + std::string{folder}), ec);
    return ec ? fs::file_time_type::min() : mtime;
}

}  // namespace

// static helpers

std::string BeatmapInstaller::resolve_and_extract_osz(std::span<const u8> data, std::string_view osz_name,
                                                      i32 set_id_override) {
    debugLog("Reading beatmapset {:s} ({:d} bytes)", osz_name, data.size());

    Archive::Reader archive(data, ARCHIVE_CHARSET);
    if(!archive.isValid()) {
        debugLog("Failed to open .osz file");
        return {};
    }

    auto entries = archive.getAllEntries();
    if(entries.empty()) {
        debugLog(".osz file is empty!");
        return {};
    }

    // single decompression pass: the entries are already in memory, so read the metadata off the first .osu
    // (and the set id off the first one that declares it) and then write those same buffers to disk
    OszMeta meta;
    for(const auto& entry : entries) {
        if(entry.isDirectory()) continue;
        const std::string_view name = entry.getFilename();
        if(name.size() <= 4 || !SString::strcase_equal(name.substr(name.size() - 4), ".osu")) continue;

        const auto& osu_data = entry.getUncompressedData();
        if(osu_data.empty()) continue;

        OszMeta parsed =
            parse_osz_meta(std::string_view{reinterpret_cast<const char*>(osu_data.data()), osu_data.size()});
        if(meta.artist.empty() && meta.title.empty()) {
            meta.artist = std::move(parsed.artist);
            meta.title = std::move(parsed.title);
        }
        if(parsed.set_id > 0) {
            meta.set_id = parsed.set_id;
            break;
        }
    }

    i32 set_id = set_id_override > 0 ? set_id_override : meta.set_id;
    // fallback: a leading number in the filename, e.g. "12345 Artist - Title.osz"
    if(set_id <= 0 && !osz_name.empty() && std::isdigit(static_cast<unsigned char>(osz_name.front()))) {
        i32 parsed = -1;
        if(Parsing::parse(osz_name, &parsed) && parsed > 0) set_id = parsed;
    }

    std::string_view stem = osz_name;
    if(stem.size() > 4 && SString::strcase_equal(stem.substr(stem.size() - 4), ".osz")) stem.remove_suffix(4);

    std::string folder = target_folder_name(set_id, stem, meta);
    if(folder.empty()) {
        debugLog("No usable name for the beatmapset folder of {:s}", osz_name);
        return {};
    }
    if(!write_entries_to_dir(entries, fmt::format(NEOMOD_MAPS_PATH "/{}/", folder))) return {};
    return folder;
}

std::string BeatmapInstaller::read_and_extract_osz(std::string_view path) {
    std::unique_ptr<u8[]> osz_data;
    uSz filesize = 0;
    {
        File osz(path);
        filesize = osz.getFileSize();
        if(!osz.canRead() || !filesize) return {};
        osz_data = osz.takeFileBuffer();
        if(!osz_data.get()) return {};
    }
    return resolve_and_extract_osz({osz_data.get(), filesize}, Environment::getFileNameFromFilePath(path));
}

// public methods implementation below

struct BeatmapInstaller::BMInstallerImpl final {
    std::vector<Entry> entries;  // typically <= 5 entries, so linear scans throughout
    // maps/ folders as the last import or uninstall left them, so the directory watcher's event for that write
    // can be told from a later change (see claim())
    Hash::unstable_stringmap<fs::file_time_type> settled;
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
        // aborts the transfer (if still downloading); a still-queued extraction is dropped with the entry, a
        // running one finishes and leaves its folder for the next db rescan to pick up (same as quitting
        // mid-extract), and a local source .osz stays put for a later import pass
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

BeatmapInstaller::FolderClaim BeatmapInstaller::claim(std::string_view folder) {
    // an extraction's folder isn't known until the worker is done with it, so every event waits for those
    if(std::ranges::any_of(m->entries, [folder](const Entry& e) {
           return e.stage == MapInstallStage::Extracting ||
                  (e.stage == MapInstallStage::Installing && e.folder == folder);
       })) {
        return FolderClaim::InFlight;
    }
    auto it = m->settled.find(folder);
    if(it == m->settled.end()) return FolderClaim::Unclaimed;

    const bool untouched = folder_state(folder) == it->second;
    m->settled.erase(it);  // whatever happens to the folder next is a change to what the installer left
    return untouched ? FolderClaim::Settled : FolderClaim::Unclaimed;
}

void BeatmapInstaller::uninstall(const DatabaseBeatmap* map, bool whole_set) {
    const BeatmapSet* set = map->getParentSet() ? map->getParentSet() : map;
    // the song browser greys the menu items out for osu!stable sets, and nothing here touches that folder either
    if(set->type != DatabaseBeatmap::BeatmapType::NEOMOD_BEATMAPSET) return;
    if(!db->isFinished() || db->isCancelled() || osu->isInPlayMode()) return;  // (see try_import)

    // maps/ sets live directly under NEOMOD_MAPS_PATH, the only place this ever deletes from
    std::string_view rel_view = set->getFolder();
    if(!rel_view.starts_with(NEOMOD_MAPS_PATH "/")) return;
    rel_view.remove_prefix(std::string_view{NEOMOD_MAPS_PATH "/"}.size());
    while(rel_view.ends_with('/')) rel_view.remove_suffix(1);
    if(rel_view.empty() || rel_view.contains('/')) return;
    const std::string rel{rel_view};

    // the last difficulty takes the folder with it (as in osu!stable; a folder of nothing but audio and
    // backgrounds would only be litter)
    whole_set = whole_set || map == set || set->getDifficulties().size() <= 1;
    const std::string name =
        whole_set ? fmt::format("{} - {}", set->getArtist(), set->getTitle())
                  : fmt::format("{} - {} [{}]", map->getArtist(), map->getTitle(), map->getDifficultyName());
    const std::string path = whole_set ? NEOMOD_MAPS_PATH "/" + rel : std::string{map->getFilePath()};

    // the preview music streams from the folder, and an open file can't be deleted on windows
    auto* iface = osu->getMapInterface();
    if(whole_set && iface->getBeatmap() && (iface->getBeatmap() == set || iface->getBeatmap()->getParentSet() == set)) {
        iface->unloadMusic();
    }

    if(whole_set) {
        env->deletePathsRecursive(path, 8);  // storyboards nest their assets a few folders deep
    } else {
        env->deleteFile(path);
    }
    const bool gone = whole_set ? !env->directoryExists(path) : !env->fileExists(path);

    // the db and the carousel follow whatever is left on disk, and the directory watcher's event for this
    // write is recognized by claim()
    const auto r = db->reconcileFolder(Database::MapRoot::Neomod, rel, Database::ReconcileMode::PerFile, -1, nullptr);
    ui->getSongBrowser()->applyReconcile(r);
    m->settled[rel] = folder_state(rel);

    debugLog("Uninstalled {} from maps/{}/: {} (-{:d})", name, rel, r.outcomeName(), r.removed);
    ui->getNotificationOverlay()->addToast(gone ? tformat("Deleted {}", name) : tformat("Couldn't delete {}", name),
                                           gone ? SUCCESS_TOAST : ERROR_TOAST);
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

    // extraction is bounded: a bulk drop (or a burst of downloads) would otherwise fill the pool's bg queue
    // ahead of everything else submitted to it
    const size_t max_extracting = std::clamp<size_t>(Async::get_thread_count(), 2, 16);
    size_t extracting = 0;
    for(const Entry& e : m->entries) extracting += e.stage == MapInstallStage::Extracting;

    for(auto it = m->entries.begin(); it != m->entries.end();) {
        Entry& e = *it;
        bool severed = false;

        switch(e.stage) {
            using enum MapInstallStage;
            case Queued:
                if(e.is_local()) {
                    // read + decompress + extract on a worker so the main thread never blocks on it.
                    // the target folder depends on what the db knows, so wait until it's loaded
                    if(!db->isFinished() || db->isCancelled() || extracting >= max_extracting) break;
                    e.extract_handle = Async::submit_cancellable(
                        [path = e.osz_path](const Sync::stop_token&) {
                            return parse_extracted(read_and_extract_osz(path));
                        },
                        Lane::Background);
                    e.stage = Extracting;
                    extracting++;
                    break;
                }
                // a download always transfers: if the db has the set, the enqueuer wants bytes it doesn't have
                // (an updated version), which then land in the set's existing folder
                [[fallthrough]];
            case Downloading: {
                // download_beatmapset lazily creates the handle on first call (when e.dl_handle is null),
                // then on each subsequent call polls completion.
                const bool ready = Downloader::download_beatmapset(static_cast<u32>(e.set_id), e.dl_handle);
                if(ready) {
                    // the target folder depends on what the db knows, so hold the bytes until it's loaded
                    // (and until an extraction slot is free)
                    if(!db->isFinished() || db->isCancelled() || extracting >= max_extracting) {
                        e.progress = 1.f;
                        e.stage = Downloading;
                        break;
                    }
                    // bytes arrived: from here on a download is just a local import whose .osz is
                    // already in memory. decompress on a worker, into the folder of the id we know.
                    e.extract_handle = Async::submit_cancellable(
                        [data = e.dl_handle.take_data(), set_id = e.set_id](const Sync::stop_token&) {
                            return parse_extracted(resolve_and_extract_osz(data, "", set_id));
                        },
                        Lane::Background);
                    e.dl_handle.reset();
                    e.progress = 1.f;
                    e.stage = Extracting;
                    extracting++;
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
                if(!e.extract_handle.is_ready()) break;
                extracting--;
                if(Extracted x = e.extract_handle.get(); x.folder.empty()) {
                    fail_entry(e, now);
                } else {
                    e.folder = std::move(x.folder);
                    e.diffs = std::move(x.diffs);
                    e.stage = Installing;
                }
                break;
            }

            case Installing: {
                const auto r = try_import(e);
                if(!r) break;  // db busy/rebuilding; retry next tick

                // the carousel follows the db whatever the outcome (an archive can overwrite an installed set's
                // files with nothing loadable, which removes the set)
                ui->getSongBrowser()->applyReconcile(*r);
                if(!r->set && !r->dedup_owner) {
                    fail_entry(e, now);
                } else {
                    e.stage = Done;
                    e.finished_time = now;
                    if(e.is_local() && e.delete_after) env->deleteFile(e.osz_path);
                    m->settled[e.folder] = folder_state(e.folder);
                    on_done(*r, e);
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
