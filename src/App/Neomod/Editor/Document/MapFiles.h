#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFile/BeatmapFile.h"
#include "types.h"

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

// the file at `path` as a file of `folder` (which ends with a slash): the one there already if that's it, else a copy
// under its name, or under a free one ("name (2).ext") when another file there has it. the name it has there; none if
// it couldn't be copied
[[nodiscard]] std::optional<std::string> takeIn(const std::string &folder, const std::string &path);
}  // namespace MapFiles
