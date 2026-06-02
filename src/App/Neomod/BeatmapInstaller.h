// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"

#include "types.h"
#include "AsyncFuture.h"
#include "DownloadHandle.h"
#include "Hashing.h"

#include <string>
#include <string_view>
#include <vector>

class DatabaseBeatmap;
using BeatmapSet = DatabaseBeatmap;

enum class MapInstallStage : u8 {
    None = (1 << 0),
    Queued = (1 << 1),
    Downloading = (1 << 2),
    Installing = (1 << 3),
    Done = (1 << 4),
    Failed = (1 << 5),
    Extracting = (1 << 6),  // local .osz imports only: decompress on a worker before Installing
};

// drives beatmapset import (downloaded sets and local .osz files) as a single async unit, decoupled
// from any UI element. ticked from Osu::update(). downloads are keyed by set_id; local files have no
// id until cracked open, so they ride a separate queue and resolve theirs on a worker thread.
class BeatmapInstaller final {
    NOCOPY_NOMOVE(BeatmapInstaller)
   public:
    struct State {
        MapInstallStage stage{MapInstallStage::None};
        f32 progress{0.f};  // 0..1, only meaningful while Downloading
    };

    // snapshot row used by the global install overlay (and any future consumers).
    // Done entries are erased immediately, so they don't appear; Failed entries linger
    // for FAILED_ENTRY_TTL_S so listings can render the red state.
    struct EntryView {
        i32 set_id{0};
        MapInstallStage stage{MapInstallStage::None};
        f32 progress{0.f};
        std::string display_name;  // empty if caller didn't supply one
    };

    BeatmapInstaller() = default;
    ~BeatmapInstaller() = default;

    // main thread; tick from Osu::update()
    void update();

    // idempotent. if the set is already tracked, only the auto_select bit is OR'd in
    // and display_name is filled iff currently empty (so a later caller with a name
    // upgrades an entry queued earlier without one).
    void enqueue(i32 set_id, bool auto_select, std::string_view display_name = {});

    // import a local .osz file (drag-drop, file association, maps/ watcher). the set id is unknown
    // until the archive is cracked open on a worker thread. delete_after removes the source file once
    // imported (used for the maps/ drop-zone, never for files the user passed in by path).
    void enqueue_local(std::string osz_path, bool auto_select, bool delete_after);

    // aborts the in-flight transfer (if any) and drops the entry.
    void cancel(i32 set_id);

    [[nodiscard]] State get_state(i32 set_id) const;

    // copy of all current entries (typically <= 5), one row each.
    void snapshot(std::vector<EntryView>& out) const;  // "out" is immediately cleared
    [[nodiscard]] std::vector<EntryView> snapshot() const;

    // drop everything (e.g. on bancho disconnect)
    void clear();

   private:
    struct Entry {
        Downloader::DownloadHandle dl_handle;
        std::string display_name;
        MapInstallStage stage{MapInstallStage::Queued};
        f32 progress{0.f};
        bool auto_select{false};
        f64 finished_time{0.0};
    };

    // a local .osz import in flight. has no set_id until Extracting resolves it from the archive.
    struct LocalEntry {
        std::string osz_path;
        Async::Future<i32> extract_future;  // resolved set_id (>0) on success, <= 0 on failure
        i32 set_id{-1};
        MapInstallStage stage{MapInstallStage::Queued};
        bool auto_select{false};
        bool delete_after{false};
        f64 finished_time{0.0};
    };

    void on_done(BeatmapSet* set, i32 set_id, bool added, bool auto_select);
    void on_failed(i32 set_id);

    Hash::flat::map<i32, Entry> entries;
    std::vector<LocalEntry> local_imports;

    // how long to keep Failed entries around so listings can render the red state
    static constexpr f64 FAILED_ENTRY_TTL_S = 60.0;
};

MAKE_FLAG_ENUM(MapInstallStage)
