// Copyright (c) 2025, WH, All rights reserved.

#include "Thread.h"
#include "Logging.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_cpuinfo.h>

#if defined(_WIN32)
#include "WinDebloatDefs.h"
#include <windows.h>
#include <processthreadsapi.h>
#include <libloaderapi.h>
#include "dynutils.h"
#include "UniString.h"

namespace {
using SetThreadDescription_t = HRESULT WINAPI(HANDLE hThread, PCWSTR lpThreadDescription);
using GetThreadDescription_t = HRESULT WINAPI(HANDLE hThread, PWSTR *ppszThreadDescription);
SetThreadDescription_t *pset_thread_desc{nullptr};
GetThreadDescription_t *pget_thread_desc{nullptr};

thread_local char thread_name_buffer[256];

void try_load_funcs() noexcept {
    static dynutils::lib_obj *kernel32_handle{nullptr};
    static bool load_attempted{false};
    if(!load_attempted) {
        load_attempted = true;
        kernel32_handle = reinterpret_cast<dynutils::lib_obj *>(GetModuleHandle(TEXT("kernel32.dll")));
        if(kernel32_handle) {
            pset_thread_desc = load_func<SetThreadDescription_t>(kernel32_handle, "SetThreadDescription");
            pget_thread_desc = load_func<GetThreadDescription_t>(kernel32_handle, "GetThreadDescription");
        }
    }
}
}  // namespace

#else
#include <pthread.h>
namespace {
#if defined(__linux__) || defined(__APPLE__)
thread_local char thread_name_buffer[16];
#elif defined(__FreeBSD__)
thread_local char thread_name_buffer[256];
#endif
// note: other platforms (like WASM) don't use thread_name_buffer
}  // namespace
#endif

#if defined(__SSE__) || (defined(_M_IX86_FP) && (_M_IX86_FP > 0))
// check if x86 for sse include
#include <xmmintrin.h>
#endif

#include <cfloat>  // for _controlfp

namespace {
bool on_thread_init_disabled{false};
}

namespace McThread {

void debug_disable_thread_init_changes() noexcept { on_thread_init_disabled = true; }

void on_thread_init() noexcept {
    // debug disabled
    if(on_thread_init_disabled) return;

#ifdef _MCW_DN
    // flush denormals
    _controlfp(_DN_FLUSH, _MCW_DN);
#endif

#if defined(__SSE__) || (defined(_M_IX86_FP) && (_M_IX86_FP > 0))
    // denorm clear to zero (CTZ) and denorms are zero (DAZ) for x86 sse
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif !(defined(_MSC_VER) && !defined(__clang__))  // idk how to do this with msvc and no inline assembly
// flush-to-zero for arm
// arm32
#if defined(__arm__) || defined(_ARM_)
    __asm__ __volatile__("vmsr fpscr,%0" ::"r"(1 << 24));
#endif
// arm64
#if defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__)
    uint64_t fpcr;  // NOLINT
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);  // FZ
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif

#endif  // !(defined(_MSC_VER) && !defined(__clang__))
}

// WARNING: must be called from within the thread itself! otherwise, the main process name will be changed
bool set_current_thread_name(const char *name) noexcept {
    (void)name;  // may be unused on some platforms
#if defined(_WIN32)
    try_load_funcs();
    if(pset_thread_desc) {
        HANDLE handle = GetCurrentThread();
        std::wstring wide_name = UniString::to_wide(std::string_view{name});
        HRESULT hr = pset_thread_desc(handle, wide_name.c_str());
        return SUCCEEDED(hr);
    }
#elif defined(__linux__)
    std::string truncated_name{name};
    if(truncated_name.length() > 15) {
        truncated_name = truncated_name.substr(0, 15);
    }
    return pthread_setname_np(pthread_self(), truncated_name.c_str()) == 0;
#elif defined(__APPLE__)
    return pthread_setname_np(name) == 0;
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    pthread_set_name_np(pthread_self(), name.c_str());
    return true;
#endif
    return false;
}

const char *get_current_thread_name() noexcept {
#if defined(_WIN32)
    try_load_funcs();
    if(pget_thread_desc) {
        HANDLE handle = GetCurrentThread();
        PWSTR thread_desc;
        HRESULT hr = pget_thread_desc(handle, &thread_desc);
        if(SUCCEEDED(hr) && thread_desc) {
            std::wstring name{thread_desc};
            LocalFree(thread_desc);
            auto utf8_name = UniString::to_utf8(name);
            strncpy_s(thread_name_buffer, sizeof(thread_name_buffer), utf8_name.c_str(), _TRUNCATE);
            return thread_name_buffer;
        }
    }
#elif defined(__linux__) || defined(__APPLE__)
    if(pthread_getname_np(pthread_self(), &thread_name_buffer[0], sizeof(thread_name_buffer)) == 0) {
        return thread_name_buffer[0] ? &thread_name_buffer[0] : PACKAGE_NAME;
    }
#elif defined(__FreeBSD__)
    pthread_get_name_np(pthread_self(), &thread_name_buffer[0], sizeof(thread_name_buffer));
    return thread_name_buffer[0] ? &thread_name_buffer[0] : PACKAGE_NAME;
#endif
    return PACKAGE_NAME;
}

bool is_main_thread() noexcept { return SDL_IsMainThread(); }

int get_logical_cpu_count() noexcept { return SDL_GetNumLogicalCPUCores(); }

void set_current_thread_prio(Priority prio) noexcept {
    SDL_ThreadPriority sdlprio{SDL_THREAD_PRIORITY_NORMAL};
    const char *priostring{"normal"};
    if(prio < NORMAL || prio > REALTIME) prio = NORMAL;  // sanity
    switch(prio) {
        case NORMAL:
            sdlprio = SDL_THREAD_PRIORITY_NORMAL;
            priostring = "normal";
            break;
        case HIGH:
            sdlprio = SDL_THREAD_PRIORITY_HIGH;
            priostring = "high";
            break;
        case LOW:
            sdlprio = SDL_THREAD_PRIORITY_LOW;
            priostring = "low";
            break;
        case REALTIME:
            sdlprio = SDL_THREAD_PRIORITY_TIME_CRITICAL;
            priostring = "realtime";
            break;
        default:
            break;
    }
    if(!SDL_SetCurrentThreadPriority(sdlprio)) {
        debugLog("couldn't set thread priority to {}: {}", priostring, SDL_GetError());
    }
#ifdef MCENGINE_PLATFORM_WINDOWS
    static int logcpus = get_logical_cpu_count();
    // tested in a windows vm, this causes things to just behave WAY worse than if you leave it alone with low core counts
    if(logcpus > 4 && is_main_thread()) {
        // only allow setting normal/high for process priority class
        if(!SetPriorityClass(GetCurrentProcess(),
                             (prio == REALTIME || prio == HIGH) ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS)) {
            debugLog("couldn't set process priority class to {}: {}", priostring, GetLastError());
        }
    }
#endif
}

}  // namespace McThread
