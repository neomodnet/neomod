#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Document.h"
#include "types.h"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

class MapObjects;

// what a mapper would want to know about a map before sharing it. an issue says what was found, where and how bad it
// is; the view puts it into words
namespace Verify {
enum class Severity : u8 { PROBLEM, WARNING, MINOR };

enum class Kind : u8 {
    UNSNAPPED,                  // an object starts `value` ms off the nearest tick
    UNSNAPPED_REPEAT,           // a slider turns around off it
    UNSNAPPED_END,              // a slider or spinner ends off it
    CONCURRENT,                 // two objects at the same time
    ALMOST_CONCURRENT,          // one object starts less than 10 ms after another ends
    OFFSCREEN,                  // an object leaves a 4:3 screen (a slider: at the time)
    SHORT_SPINNER,              // a spinner of `value` ms
    SPINNER_WITHOUT_NEW_COMBO,  //
    QUIET,                      // an object is hit at `value` percent volume
    TIMING_OVERLAP,             // two timing points of a kind at one time
    NO_PREVIEW,                 // no preview point
    NO_BACKGROUND,              // no background image
    MISSING_BACKGROUND,         // the background file isn't in the folder
    MISSING_AUDIO,              // the audio file isn't
    SETTING_DECIMALS,           // a difficulty setting of `value` has more than one decimal place
    SETTING_RANGE,              // one is outside of 0 to 10
    SHORT_DRAIN,                // `value` seconds of play, under 30
};

enum class Setting : u8 { HP, CS, OD, AR };

struct Issue {
    Kind kind;
    Severity severity;
    std::optional<i32> time{};            // where in the song, for those that are somewhere
    std::array<Document::Id, 2> lines{};  // the object or timing point lines it's about (0: none)
    f64 value{0.0};
    Setting setting{};  // from the setting kinds
};

// every issue in the map: whole map issues first, the others by time. `folder` is where its files are looked for
// (empty: they aren't)
[[nodiscard]] std::vector<Issue> check(const Document &document, const MapObjects &objects, std::string_view folder);
}  // namespace Verify
