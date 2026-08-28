// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "types.h"
#include "MD5Hash.h"

class DatabaseBeatmap;

// drives "make this beatmap available locally" to a terminal verdict, for screens that wait on a
// specific map or set (multi room host pick, spectated player, chat link auto-download).
// owns the resolve -> install -> re-check loop, and knows when to
// give up: a completed download that still doesn't produce the wanted map means the server has a
// different version of the set, so retrying forever is pointless.
//
// main thread only; tick() from the owning screen's update(), state() from draw().
class MapFetcher final {
   public:
    enum class Status : u8 {
        Idle,      // no target
        Working,   // resolving / downloading / installing
        Found,     // target is in the db; result() is valid
        NotFound,  // gave up; see why. latched until the target changes (but a target that
                   // appears in the db some other way still flips this back to Found)
    };

    enum class Why : u8 {
        None,
        Resolve,   // couldn't resolve the map to a beatmapset id
        Install,   // download or import failed (the installer already toasted about it)
        Mismatch,  // an install completed but the wanted map isn't in the set (version skew)
    };

    struct State {
        Status status{Status::Idle};
        Why why{Why::None};
        f32 progress{0.f};  // 0..1, only meaningful while Working
    };

    // want a specific difficulty: md5 (if non-empty) is the success criterion, map_id otherwise.
    // a set_id_hint > 0 skips the metadata query. retargeting resets the machinery; calling every
    // tick with the same target is free.
    void target_map(i32 map_id, const MD5Hash &md5 = {}, i32 set_id_hint = 0);

    // want a whole set.
    void target_set(i32 set_id);

    void clear();

    // drive resolution/install and return the current state. call once per frame.
    const State &tick();

    // last tick()'s state, for draw()
    [[nodiscard]] const State &state() const { return this->st; }

    // the found difficulty (map targets) or set (set targets); valid after tick() returned Found
    [[nodiscard]] DatabaseBeatmap *result() const { return this->found; }

   private:
    [[nodiscard]] DatabaseBeatmap *lookup() const;

    MD5Hash md5{};
    i32 map_id{0};
    i32 set_id{0};  // the target itself for set targets, 0 until resolved for map targets
    i32 set_id_hint{0};
    DatabaseBeatmap *found{nullptr};
    State st{};
    bool in_flight{false};  // an installer entry for set_id existed on a previous tick
};
