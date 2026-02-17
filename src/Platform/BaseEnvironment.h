#pragma once
// Copyright (c) 2025, WH, All rights reserved.

#include "config.h"
#include "noinclude.h"

#include <cstdint>
#include <type_traits>

namespace Env {
enum class OS : uint8_t {
    WINDOWS = 1 << 0,
    LINUX = 1 << 1,
    WASM = 1 << 2,
    MAC = 1 << 3,
    NONE = 0,
};
enum class BUILD : uint8_t {
    RELEASE = 1 << 0,
    EDGE = 1 << 1,
    DEBUG = 1 << 2,
    NONE = 0,
};
enum class FEAT : uint8_t {
    STEAM = 1 << 0,
    DISCORD = 1 << 1,
    MAINCB = 1 << 2,
    NONE = 0,
};
enum class AUD : uint8_t {
    BASS = 1 << 0,
    WASAPI = 1 << 1,
    SDL = 1 << 2,
    SOLOUD = 1 << 3,
    NONE = 0,
};
enum class REND : uint8_t {
    GL = 1 << 0,
    GLES32 = 1 << 1,
    DX11 = 1 << 2,
    SDLGPU = 1 << 3,
    NONE = 0,
};

constexpr OS operator|(OS lhs, OS rhs) {
    return static_cast<OS>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}
constexpr BUILD operator|(BUILD lhs, BUILD rhs) {
    return static_cast<BUILD>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}
constexpr FEAT operator|(FEAT lhs, FEAT rhs) {
    return static_cast<FEAT>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}
constexpr AUD operator|(AUD lhs, AUD rhs) {
    return static_cast<AUD>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}
constexpr REND operator|(REND lhs, REND rhs) {
    return static_cast<REND>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

// system we were compiled for
consteval OS getOS() {
#if defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || defined(__CYGWIN__) || defined(__CYGWIN32__) || \
    defined(__TOS_WIN__) || defined(__WINDOWS__)
    return OS::WINDOWS;
#elif defined(__linux__)
    return OS::LINUX;
#elif defined(__APPLE__)
    return OS::MAC;
#elif defined(__EMSCRIPTEN__)
    return OS::WASM;
#else
#error "Compiling for an unknown target!"
    return OS::NONE;
#endif
}

consteval BUILD getBuildType() {
    return
#if defined(CI_DEVBUILD)  // not used atm
        BUILD::EDGE |
#endif
#if defined(_DEBUG)
        BUILD::DEBUG |
#else
        BUILD::RELEASE |
#endif
        BUILD::NONE;
}

// miscellaneous compile-time features
consteval FEAT getFeatures() {
    return
#ifdef MCENGINE_FEATURE_STEAMWORKS
        FEAT::STEAM |
#endif
#if defined(MCENGINE_FEATURE_DISCORD)
        FEAT::DISCORD |
#endif
#if defined(MCENGINE_PLATFORM_WASM) || defined(MCENGINE_FEATURE_MAINCALLBACKS)
        FEAT::MAINCB |
#endif
        FEAT::NONE;
}

consteval AUD getAudioBackend() {
    return
#ifdef MCENGINE_FEATURE_BASS
        AUD::BASS |
#endif
#ifdef MCENGINE_FEATURE_BASS_WASAPI
        AUD::WASAPI |
#endif
#ifdef MCENGINE_FEATURE_SOLOUD
        AUD::SOLOUD |
#endif
        AUD::NONE;
}

// graphics renderer type (multiple can be enabled at the same time, like DX11 + GL for windows)
consteval REND getRenderers() {
    return
#ifdef MCENGINE_FEATURE_OPENGL
        REND::GL |
#endif
#ifdef MCENGINE_FEATURE_GLES32
        REND::GLES32 |
#endif
#ifdef MCENGINE_FEATURE_DIRECTX11
        REND::DX11 |
#endif
#ifdef MCENGINE_FEATURE_SDLGPU
        REND::SDLGPU |
#endif
#if !(defined(MCENGINE_FEATURE_OPENGL) || defined(MCENGINE_FEATURE_GLES32) || defined(MCENGINE_FEATURE_DIRECTX11) || \
      defined(MCENGINE_FEATURE_SDLGPU))
#error "No renderer is defined! Check the build configuration, or \"config.h\"."
#endif
        REND::NONE;
}

template <typename T>
struct Not {
    T value;
    consteval Not(T v) : value(v) {}
};

template <typename T>
consteval Not<T> operator!(T value) {
    return Not<T>(value);
}

// check if a specific config mask matches current config
template <typename T>
consteval bool matchesCurrentConfig(T mask) {
    if constexpr(std::is_same_v<T, OS>) {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(getOS())) != 0;
    } else if constexpr(std::is_same_v<T, BUILD>) {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(getBuildType())) != 0;
    } else if constexpr(std::is_same_v<T, FEAT>) {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(getFeatures())) != 0;
    } else if constexpr(std::is_same_v<T, AUD>) {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(getAudioBackend())) != 0;
    } else if constexpr(std::is_same_v<T, REND>) {
        return (static_cast<uint8_t>(mask) & static_cast<uint8_t>(getRenderers())) != 0;
    } else {
        static_assert(always_false_v<T>, "Unsupported type for cfg");
        return false;
    }
}

// specialization for !<mask>
template <typename T>
consteval bool matchesCurrentConfig(Not<T> not_mask) {
    return !matchesCurrentConfig(not_mask.value);
}

// base case
consteval bool cfg() { return true; }
// recursive case for variadic template
template <typename T, typename... Rest>
consteval bool cfg(T first, Rest... rest) {
    return matchesCurrentConfig(first) && cfg(rest...);
}
}  // namespace Env

using Env::AUD;
using Env::BUILD;
using Env::FEAT;
using Env::OS;
using Env::REND;

#ifdef __AVX512F__
#define OPTIMAL_UNROLL 10
#elif defined(__AVX2__)
#define OPTIMAL_UNROLL 8
#elif defined(__SSE2__)
#define OPTIMAL_UNROLL 6
#else
#define OPTIMAL_UNROLL 4
#endif

#define MC_QUOTE(s) #s
#define MC_STRINGIZE(s) MC_QUOTE(s)

#define MC_DO_PRAGMA(x) _Pragma(MC_STRINGIZE(x))
#define MC_MESSAGE(msg) MC_DO_PRAGMA(message(msg))

#if defined(__GNUC__) || defined(__clang__)
#ifdef __clang__
#define MC_VECTORIZE_LOOP MC_DO_PRAGMA(clang loop vectorize(enable))
#define MC_UNR_cnt(num) MC_DO_PRAGMA(clang loop unroll_count(num))
#define NULL_PUSH
#define NULL_POP
#else
#define MC_VECTORIZE_LOOP MC_DO_PRAGMA(GCC ivdep)
#define MC_UNR_cnt(num) MC_DO_PRAGMA(GCC unroll num)
#define NULL_PUSH MC_DO_PRAGMA(GCC diagnostic ignored "-Wformat") MC_DO_PRAGMA(GCC diagnostic push)
#define NULL_POP MC_DO_PRAGMA(GCC diagnostic pop)
#endif

#define MC_VEC_UNR_cnt(num) MC_VECTORIZE_LOOP MC_UNR_cnt(num)
#define MC_UNROLL_VECTOR MC_VEC_UNR_cnt(OPTIMAL_UNROLL)
#define MC_UNROLL MC_UNR_cnt(OPTIMAL_UNROLL)

#ifdef _OPENMP
#define ACCUMULATE(op, var) MC_DO_PRAGMA(omp simd reduction(op : var))  // use openmp if available, otherwise unroll
#else
#define ACCUMULATE(op, var) MC_UNR_cnt(OPTIMAL_UNROLL)
#endif

#else

#define MC_VECTORIZE_LOOP
#define MC_UNR_cnt(num)
#define MC_VEC_UNR_cnt(num)
#define MC_UNROLL_VECTOR
#define MC_UNROLL
#define NULL_PUSH
#define NULL_POP
#define ACCUMULATE(op, var)
#endif  // defined(__GNUC__) || defined(__clang__)

#if !(defined(MCENGINE_PLATFORM_WINDOWS) || defined(_WIN32) || defined(_WIN64) || defined(__WIN32__) || \
      defined(__CYGWIN__) || defined(__CYGWIN32__) || defined(__TOS_WIN__) || defined(__WINDOWS__))

#if defined(__has_builtin) && __has_builtin(__builtin_debugtrap)
#define fubar_abort_                            \
    [] [[gnu::always_inline]] [[noreturn]] () { \
        __builtin_debugtrap();                  \
        ::abort();                              \
    }
#else
#ifndef MCENGINE_PLATFORM_WASM
extern "C" int raise(int sig);
#define fubar_abort_                            \
    [] [[gnu::always_inline]] [[noreturn]] () { \
        ::raise(5 /* SIGTRAP */);               \
        ::abort();                              \
    }
#else
#define fubar_abort_ [] [[gnu::always_inline]] [[noreturn]] () { ::abort(); }
#endif
#endif

#define fubar_abort() fubar_abort_()

typedef void* HWND;

#else  // Windows build

#include "WinDebloatDefs.h"

#include <basetsd.h>
#include <windef.h>
#include <intrin.h>

#ifndef fileno
#define fileno _fileno
#endif

#ifndef isatty
#define isatty _isatty
#endif

#ifndef strcasecmp
#define strcasecmp _stricmp
#endif

#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

#define fubar_abort_     \
    [] [[noreturn]] () { \
        __debugbreak();  \
        ::abort();       \
    }

#define fubar_abort() fubar_abort_()

#if defined(_MSC_VER)
typedef SSIZE_T ssize_t;
#endif

#endif

#if defined(_X86_) || defined(__i386__) || (defined(_WIN32) && !defined(_WIN64))
#define MC_ARCH32
#elif defined(_AMD64_) || defined(__x86_64__) || (defined(_WIN64))
#define MC_ARCH64
#elif defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__)
#define MC_AARCH64
#elif defined(__wasm32__) || defined(__EMSCRIPTEN__)
#define MC_WASM32
#else
MC_MESSAGE("WARNING: unknown compilation arch??")
#endif

#ifdef MCENGINE_PLATFORM_WINDOWS

#ifdef MC_ARCH64
#define MC_ARCHSTR "x64"
#elif defined(MC_ARCH32)
#define MC_ARCHSTR "x32"
#elif defined(MC_AARCH64)
#define MC_ARCHSTR "arm64"
#else
#define MC_ARCHSTR "?"
#endif  // MC_ARCH64

#define OS_NAME "win-" MC_ARCHSTR

#else

#ifdef MC_ARCH64
#define MC_ARCHSTR "x86-64"
#elif defined(MC_ARCH32)
#define MC_ARCHSTR "i686"
#elif defined(MC_AARCH64)
#define MC_ARCHSTR "aarch64"
#elif defined(MC_WASM32)
#define MC_ARCHSTR "wasm32"
#else
#define MC_ARCHSTR "?"
#endif  // MC_ARCH64

#ifdef __linux__
#define OS_NAME "linux-" MC_ARCHSTR
#elif defined(__APPLE__)
#define OS_NAME "macos-" MC_ARCHSTR
#elif defined(MCENGINE_PLATFORM_WASM) || defined(__EMSCRIPTEN__)
#define OS_NAME "wasm-" MC_ARCHSTR
#endif

#endif  // MCENGINE_PLATFORM_WINDOWS

#ifndef OS_NAME
#error "OS not currently supported"
#endif
