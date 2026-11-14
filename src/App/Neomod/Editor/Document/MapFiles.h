#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFile/BeatmapFile.h"
#include "Database.h"
#include "types.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// what the .osu files the editor writes are called and start as, and how other files get into a map folder
namespace MapFiles {
// the name osu! gives a map file after its metadata, "Artist - Title (Creator) [Version].osu" from the romanised names
// (the audio file name without its extension in place of "Artist - Title" without an artist, no " (Creator)" or "
// [Version]" when they're empty), with the characters file names can't have taken out and cut to 200 bytes before
// ".osu"
[[nodiscard]] std::string nameFor(const neomod::BeatmapFile &file);

// a new difficulty in the map set, made from the map content: named `version`, without an online id of its own
// (BeatmapID 0, the set id kept), and without objects and breaks when `empty`; nothing if the game couldn't read it
[[nodiscard]] std::vector<u8> newDifficulty(std::span<const u8> content, std::string_view version, bool empty);

// the first difficulty in a new set whose audio file is `audio` (its name): the fresh map osu! makes, titled after the
// file (the romanised title its ASCII characters), by `creator`, with a timing point of 120 BPM at 0 (the game takes no
// map without one) and no [Editor] section (the editor settings stay as they are until a save writes them)
[[nodiscard]] std::string newSet(std::string_view audio, std::string_view creator);

// the folder the map file at `location` is in, with a slash at its end
[[nodiscard]] std::string folderOf(const Database::MapLocation &location);

// the extensions of the sound files osu! plays, in the order it looks for a sample in
inline constexpr std::array<std::string_view, 3> SOUND_EXTENSIONS{"wav", "mp3", "ogg"};
// whether a file is one of them, by its extension in any case
[[nodiscard]] bool isSound(std::string_view name);
// the filter a file dialog for one takes (see Environment::openFileWindow()), called `name` there
[[nodiscard]] std::string soundFilter(std::string_view name);

// the file at `path` as a file of `folder` (which ends with a slash): the one there already if that's it, else a copy
// under its name, or under a free one ("name (2).ext") when another file there has it. the name it has there; none if
// it couldn't be copied
[[nodiscard]] std::optional<std::string> takeIn(const std::string &folder, const std::string &path);
}  // namespace MapFiles
