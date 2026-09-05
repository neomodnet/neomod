#pragma once
// Copyright (c) 2025-2026, WH, All rights reserved.

#include "BaseEnvironment.h"

#include "fmt/format.h"

#include <string_view>
#include <cassert>
#include <source_location>
// #include <functional>

// the macros take either a single string directly or a format string with args (see Logger::_detail::log_int)

// main context-aware logging macro
#define logChannel(chan__, str__, ...)                                                                    \
    Logger::_detail::log_int((chan__), std::source_location::current(), Logger::_detail::log_level::info, \
                             (str__)__VA_OPT__(, ) __VA_ARGS__)

#define debugLog(str__, ...) logChannel(Logger::CHAN_DEFAULT, str__ __VA_OPT__(, ) __VA_ARGS__)

// log only if condition is true
#define logIf(cond__, ...) (static_cast<bool>(cond__) ? debugLog(__VA_ARGS__) : void(0))

// log only if cvar__.getBool() == true
#define logIfCV(cvar__, ...) logIf(cv::cvar__.getBool(), __VA_ARGS__)

// raw logging without any context
#define logRawChannel(chan__, str__, ...) \
    Logger::_detail::logRaw_int((chan__), Logger::_detail::log_level::info, (str__)__VA_OPT__(, ) __VA_ARGS__)

#define logRaw(str__, ...) logRawChannel(Logger::CHAN_DEFAULT, str__ __VA_OPT__(, ) __VA_ARGS__)

#define logEveryMS(msecs__, str__, ...)                                              \
    do {                                                                             \
        static uint64_t lasttms__{0};                                                \
        uint64_t nowtms__{Timing::getTicksMS()};                                     \
        nowtms__ - lasttms__ >= (msecs__)              ? void(lasttms__ = nowtms__), \
            debugLog(str__ __VA_OPT__(, ) __VA_ARGS__) : void(0);                    \
    } while(false)

// print the call stack immediately
#include "neotrace/neotrace.h"
#define MC_DO_BACKTRACE logRaw(neotrace::to_string(neotrace::trace::current()));

// main Logger API
namespace Logger {

enum Channel : uint8_t {
    CHAN_APP = (1 << 0),
    CHAN_ENGINE = (1 << 1),
    CHAN_NETWORK = (1 << 2),
    CHAN_DEFAULT = CHAN_APP | CHAN_ENGINE
};

namespace _detail {

// copied from spdlog, don't want to include it here
// (since we are using spdlog header-only, to lower compile time, only including spdlog things in 1 translation unit)
namespace log_level {
// NOLINTNEXTLINE(performance-enum-size)
enum level_enum : int { trace = 0, debug = 1, info = 2, warn = 3, err = 4, critical = 5, off = 6, n_levels };
}  // namespace log_level

void log_int(uint8_t log_channel, std::source_location loc, log_level::level_enum lvl, std::string_view str) noexcept;
void logRaw_int(uint8_t log_channel, log_level::level_enum lvl, std::string_view str) noexcept;

void logFmt_int(uint8_t log_channel, std::source_location loc, log_level::level_enum lvl, fmt::string_view fmt,
                fmt::format_args args) noexcept;
void logRawFmt_int(uint8_t log_channel, log_level::level_enum lvl, fmt::string_view fmt,
                   fmt::format_args args) noexcept;

template <typename... Args>
    requires(sizeof...(Args) > 0)
void log_int(uint8_t log_channel, std::source_location loc, log_level::level_enum lvl, fmt::format_string<Args...> fmt,
             const Args &...args) noexcept {
    logFmt_int(log_channel, loc, lvl, fmt.str, fmt::make_format_args(args...));
}
template <typename... Args>
    requires(sizeof...(Args) > 0)
void logRaw_int(uint8_t log_channel, log_level::level_enum lvl, fmt::format_string<Args...> fmt,
                const Args &...args) noexcept {
    logRawFmt_int(log_channel, lvl, fmt.str, fmt::make_format_args(args...));
}
}  // namespace _detail

// Logger::init() is called immediately after main()
void init(bool create_console) noexcept;
void shutdown() noexcept;

// manual trigger for console commands
void flush() noexcept;

// is stdout a terminal (util func.)
[[nodiscard]] bool isaTTY() noexcept;

// // custom log hooks
// enum class HookHandle : uint64_t {};
// using SinkCallback = std::function<void(std::string logStr)>;
// // patterns are spdlog patterns
// HookHandle addLogHook(SinkCallback sinkCallback, std::string logPattern = "%v");

// bool removeLogHook(HookHandle handle);

};  // namespace Logger
