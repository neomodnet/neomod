#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Database.h"
#include "MD5Hash.h"
#include "types.h"

#include <optional>
#include <span>
#include <vector>

// what the editor keeps across reloads and restarts: the open map with its unsaved changes, and where the last maps
// were left. unsaved changes to other maps are draft files. read() takes any bytes and draftOf() any file: what they
// can't read gives a fresh session, or no draft
struct EditorSession {
    // where a map was left (the zoom, the divisor, the spacing and the grid size come from its [Editor] section until
    // the editor showed it)
    struct View {
        Database::MapLocation location;
        i32 time{0};
        std::optional<f32> zoom;
        std::optional<i32> divisor;
        std::optional<f32> spacing;
        std::optional<i32> gridSize;
        bool operator==(const View &) const = default;
    };
    static constexpr uSz MAX_VIEWS{50};

    // unsaved changes to a map: its content, and the md5 the file had when they were made
    struct Draft {
        MD5Hash base;
        std::vector<u8> bytes;
        bool operator==(const Draft &) const = default;
    };

    std::optional<Database::MapLocation> open;
    std::optional<Draft> draft;  // for the open map, until its document has it
    std::vector<View> views;     // most recent first, the open map first
    std::optional<u8> tool;      // the compose tool (Compose::Tool), until the editor shows a map
    u8 tab{0};                   // what the editor showed of it (EditorState::Tab)
    std::vector<u32> selection;  // line numbers for the objects selected in the open map, until its document is read

    [[nodiscard]] static EditorSession read(std::span<const u8> bytes);
    [[nodiscard]] std::vector<u8> write() const;
    // `location` is the open map now: its view goes first (for a map in the songs folder, the one for its copy in
    // maps/), a new one if it has none
    void opened(const Database::MapLocation &location);
    // the open map file is at `location` now (renamed): it stays open there with its view (a draft file left there by a
    // map that was there before goes)
    void moved(const Database::MapLocation &location);

    // a draft for another map, in its file under the editor directory until the map is opened again (or its copy in
    // maps/, for a map in the songs folder); whether it was written
    [[nodiscard]] static bool putDraft(const Database::MapLocation &location, const Draft &draft);
    // the map draft from its file, which stays until dropDraft() (a draft is in one place: the session that took it is
    // written first)
    [[nodiscard]] static std::optional<Draft> draftOf(const Database::MapLocation &location);
    static void dropDraft(const Database::MapLocation &location);
};
