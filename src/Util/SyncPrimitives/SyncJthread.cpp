// Copyright (c) 2025-2026, WH, All rights reserved.
#include "SyncJthread.h"
// jthreads (+ stop token support)

#ifdef USE_NSYNC
#include "nsync_note.h"

#include <cassert>
#endif

#ifndef MCENGINE_PLATFORM_WASM

#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_cpuinfo.h>

#include <cassert>

namespace Sync {

SDL_Thread *sdl_jthread::create_thread_internal(void *ctx, void *entry) noexcept {
    auto props = SDL_CreateProperties();
    assert(props);

    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, entry);
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, ctx);
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 8LL * 1024 * 1024); /* 8MB */

    SDL_Thread *ret = SDL_CreateThreadWithProperties(props);

    SDL_DestroyProperties(props);

    return ret;
}

bool sdl_jthread::joinable() const noexcept {
    SDL_ThreadState state = SDL_THREAD_UNKNOWN;
    return m_thread && ((state = SDL_GetThreadState(m_thread)) == SDL_THREAD_ALIVE || state == SDL_THREAD_COMPLETE);
}

void sdl_jthread::join() {
    assert(joinable());
    SDL_WaitThread(m_thread, nullptr);
    m_thread = nullptr;
}

void sdl_jthread::detach() {
    SDL_DetachThread(m_thread);
    m_thread = nullptr;
}

sdl_jthread::id sdl_jthread::get_id() const noexcept { return SDL_GetThreadID(m_thread); }

unsigned int sdl_jthread::hardware_concurrency() noexcept { return SDL_GetNumLogicalCPUCores(); }

}  // namespace Sync

#endif  // MCENGINE_PLATFORM_WASM

#ifdef USE_NSYNC
namespace Sync::detail {

stop_state::stop_state()
    : m_value(ssrc_counter_inc),  // starts with 1 source
      m_note(nsync_note_new(nullptr, nsync_time_no_deadline)) {
    assert(m_note && "stop_state: nsync_note_new failed");
}

stop_state::~stop_state() {
    if(m_note) nsync_note_free(m_note);
}

void stop_state::notify() noexcept { nsync_note_notify(m_note); }

}  // namespace Sync::detail
#endif
