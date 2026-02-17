#pragma once
// Copyright (c) 2024, kiwec & 2025-2026, WH, All rights reserved.
#include "types.h"

// Recalculates outdated/legacy scores and beatmaps imported from databases asynchronously.
namespace BatchDiffCalc {

// Start unified calculation for maps and all scores that need PP recalculation.
// Groups work by beatmap to load each file only once.
void start_calc();

void abort_calc();

// Flush accumulated results to the database. Must be called from the main thread.
// Returns false once when calculation is finished, signaling the caller to call abort_calc().
[[nodiscard]] bool update_mainthread();

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
