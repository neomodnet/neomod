#pragma once
// Copyright (c) 2025, WH, All rights reserved.

#include "CStrView.h"

namespace McThread {
// WARNING: must be called from within the thread itself! otherwise, the main process name/priority will be changed
bool set_current_thread_name(Mc::cstr_view name) noexcept;

enum Priority : unsigned char { NORMAL, HIGH, LOW, REALTIME };
void set_current_thread_prio(Priority prio) noexcept;

const char* get_current_thread_name() noexcept;

bool is_main_thread() noexcept;

// returns at least 1
int get_logical_cpu_count() noexcept;

// not intended to be called manually (run automatically at the start of each thread)
// sets up things like thread-local FPU state if supported
void on_thread_init() noexcept;

// intended to be called once on startup
void debug_disable_thread_init_changes() noexcept;
}  // namespace McThread
