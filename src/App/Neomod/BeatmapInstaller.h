// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"

#include "types.h"
#include "StaticPImpl.h"

#include <span>
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
    Extracting = (1 << 6),  // decompress the .osz on a worker before Installing
};
MAKE_FLAG_ENUM(MapInstallStage)

// drives beatmapset import (downloaded sets and local .osz files) as a single async unit, decoupled
// from any UI element. ticked from Osu::update().
class BeatmapInstaller final {
    NOCOPY_NOMOVE(BeatmapInstaller)
   public:
    BeatmapInstaller();
    ~BeatmapInstaller();

    ////////////
    // Utils: //
    ////////////

    // extract a .osz already in memory into maps/<folder>/ and return that folder name (relative to maps/,
    // "" on failure). the folder is the one of the already-installed set with the same id (which is how new
    // or updated difficulties end up next to the existing ones), else a new osu!stable-style
    // "<setid> Artist - Title" (the .osz stem for local files). a set id isn't required: it comes from
    // set_id_override (downloads), else the archive's .osu files, else a leading number in osz_name.
    static std::string resolve_and_extract_osz(std::span<const u8> data, std::string_view osz_name,
                                               i32 set_id_override = -1);

    // same as resolve_and_extract_osz but reading from a path on disk instead of directly from memory
    static std::string read_and_extract_osz(std::string_view path);

    // how long to keep Failed entries around so listings can render the red state
    static constexpr f64 FAILED_ENTRY_TTL_S = 60.0;

    ////////////

    struct State {
        MapInstallStage stage{MapInstallStage::None};
        f32 progress{0.f};  // 0..1, only meaningful while Downloading
    };

    // snapshot row used by the global install overlay (and any future consumers).
    // Done entries are erased immediately, so they don't appear; Failed entries linger
    // for FAILED_ENTRY_TTL_S so listings can render the red state.
    struct EntryView {
        u32 uid{0};  // stable per-entry identity (set_id is -1 for unresolved local imports)
        i32 set_id{-1};
        MapInstallStage stage{MapInstallStage::None};
        f32 progress{0.f};
        std::string display_name;  // filename for local imports; empty if caller didn't supply one
    };

    // main thread; tick from Osu::update()
    void update();

    // idempotent. if the set is already tracked as a download, only the auto_select bit is OR'd in
    // and display_name is filled iff currently empty (so a later caller with a name
    // upgrades an entry queued earlier without one).
    void enqueue(i32 set_id, bool auto_select, std::string_view display_name = {});

    // import a local .osz file (drag-drop, file association, maps/ watcher). the set id is unknown
    // until the archive is cracked open on a worker thread. delete_after removes the source file once
    // imported (used for the maps/ drop-zone, never for files the user passed in by path).
    // NOTE: auto_select has some quirks you might want to read about if you set it to false (see enqueue_local function body)
    void enqueue_local(std::string osz_path, bool auto_select, bool delete_after);

    // aborts the in-flight transfer (if any) and drops the entry. downloads only; screens
    // key their cancel requests by set id.
    void cancel(i32 set_id);

    // like cancel(), but by entry identity (install overlay X button), so it also reaches local
    // imports and not-yet-resolved entries.
    void cancel_entry(u32 uid);

    [[nodiscard]] State get_state(i32 set_id) const;

    // copy of all current entries (typically <= 5), one row each.
    void snapshot(std::vector<EntryView>& out) const;  // "out" is immediately cleared
    [[nodiscard]] std::vector<EntryView> snapshot() const;

    // true while any entry is still working toward an import (not yet Done/Failed). callers poll this
    // to tell "still installing, keep waiting" apart from "nothing left that could land in the db".
    [[nodiscard]] bool has_pending() const;
    // how a maps/<folder> change the directory watcher reports relates to the installer's own writes: InFlight
    // while an import is between extracting into the folder and registering it (leave the event to the installer),
    // Settled when the folder is exactly as the last import left it (the event was that extraction, there's
    // nothing to do; the claim is forgotten once asked), else Unclaimed
    enum class FolderClaim : u8 { Unclaimed, InFlight, Settled };
    [[nodiscard]] FolderClaim claim(std::string_view folder);

   private:
    struct BMInstallerImpl;
    StaticPImpl<BMInstallerImpl, 104> m;
};
