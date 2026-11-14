#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Database.h"
#include "MD5Hash.h"
#include "types.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

// the map versions that editor saves made, kept under the editor directory: what each save wrote, and what it overwrote
// when that wasn't a version yet (the map as it was before its first save here, or as another program left it). the
// versions for a difficulty stay together when its file is renamed
namespace EditorHistory {
inline constexpr uSz MAX_VERSIONS{300};

// what differs between two map texts, counted by lines ([Editor] left out: every save writes the view there)
struct Changes {
    u32 objectsAdded{0};
    u32 objectsRemoved{0};
    u32 timingAdded{0};
    u32 timingRemoved{0};
    u32 otherLines{0};  // lines elsewhere that one text has and the other doesn't
    bool operator==(const Changes &) const = default;
};
[[nodiscard]] Changes compare(std::span<const u8> from, std::span<const u8> to);

struct Version {
    std::string path;   // its file
    i64 time{0};        // when it was saved or found (ms since 1970)
    bool saved{false};  // by the editor (else found: what a save overwrote)
    MD5Hash md5;        // the md5 of its content
    std::string file;   // what the map file was called then
    u32 objects{0};     // how many it has
    Changes changes;    // from the version before it
};

// a save at `time`: the file `was` (its name) had the md5 `held` and is `now` with `after`. `before` is what it held,
// kept when that isn't the latest difficulty version (the content a save writes may differ from it in [Editor])
void record(std::string_view was, const MD5Hash &held, std::span<const u8> before, const Database::MapLocation &now,
            std::span<const u8> after, i64 time);
// the versions for the map at `location` whose file holds content with this md5, the latest first
[[nodiscard]] std::vector<Version> versionsOf(const Database::MapLocation &location, const MD5Hash &md5);
// the version content (none: its file is gone or unreadable)
[[nodiscard]] std::optional<std::vector<u8>> read(const Version &version);
}  // namespace EditorHistory
