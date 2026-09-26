// Copyright (c) 2025-2026, WH, All rights reserved.
#include "SyncJthread.h"
// jthreads (+ stop token support)

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
