#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.
#include "noinclude.h"
#include "types.h"

#include <span>

class DatabaseBeatmap;
namespace VolNormalization {

// start a batch calculation over the given maps. replaces any in-flight batch but
// leaves the persistent priority worker and its queue untouched.
void start_calc(std::span<DatabaseBeatmap *const> maps_to_calc);

u32 get_total();
u32 get_computed();

// abort the batch only. the persistent priority worker continues to serve one-off requests.
void abort();

// enqueue a one-off priority request. returns immediately; the result lands in map->loudness
// (real value on success, fallback loudness on failure). deduped against in-flight requests.
// priority work bypasses shouldPauseBGThreads() so user-facing waits are short.
void request_priority(DatabaseBeatmap* map);

// drop pending priority requests without joining the worker. used by Database before tearing
// down beatmap pointers on a reload (the queued raw pointers would dangle otherwise).
void flush_priority();

// full shutdown: abort batch and join the priority worker.
void shutdown();

// convar callback
void loudness_cb(float new_value);
}  // namespace VolNormalization
