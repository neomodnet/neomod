#pragma once
// Copyright (c) 2024, kiwec & 2025-2026, WH, All rights reserved.
#include "types.h"
#include "StarPrecalc.h"

#include <span>

class DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;

// Recalculates outdated/legacy scores and beatmaps imported from databases asynchronously.
namespace BatchDiffCalc {

// what the calculation fills in for a difficulty
struct MapResult {
    BeatmapDifficulty* map{};
    u32 length_ms{};
    u32 nb_circles{};
    u32 nb_sliders{};
    u32 nb_spinners{};
    StarPrecalc::SRArray star_ratings{};
    i32 min_bpm{};
    i32 max_bpm{};
    i32 avg_bpm{};
};

// Start unified calculation for maps and all scores that need PP recalculation.
// Groups work by beatmap to load each file only once.
void start_calc();

void abort_calc();

// Flush accumulated results to the database. Must be called from the main thread.
// Returns false once when calculation is finished, signaling the caller to call abort_calc().
[[nodiscard]] bool update_mainthread();

// Calculates one difficulty from its file, on any thread: for difficulties that aren't in the database yet (imports),
// whose results are applied before they're registered.
[[nodiscard]] MapResult calc_map(BeatmapDifficulty* map);

// Stores results into their difficulties and the database's star rating cache. Must be called from the main thread.
void apply_results(std::span<const MapResult> results);

[[nodiscard]] u32 get_maps_total();
[[nodiscard]] u32 get_maps_processed();

[[nodiscard]] u32 get_scores_total();
[[nodiscard]] u32 get_scores_processed();

[[nodiscard]] bool running();          // is the thread still running?
[[nodiscard]] bool scores_finished();  // are score recalculations done?
[[nodiscard]] bool is_finished();      // is everything done?
[[nodiscard]] bool did_actual_work();  // did anything actually happen?

struct internal;
}  // namespace BatchDiffCalc
