#pragma once
// Copyright (c) 2025-2026, WH, All rights reserved.

#include "BaseEnvironment.h"

#include "fmt/format.h"
#include "fmt/compile.h"

using fmt::literals::operator""_cf;

#include <string_view>
#include <cassert>
#include <source_location>
// #include <functional>

// helper macros to allow using a single string directly or a format string with args
#define _logFmtStart fmt::format(
#define _logFmtEnd(...) , __VA_ARGS__)

// main context-aware logging macro
#define debugLog(str__, ...)                                                                    \
    Logger::_detail::log_int(std::source_location::current(), Logger::_detail::log_level::info, \
                             __VA_OPT__(_logFmtStart)(str__) __VA_OPT__(_logFmtEnd(__VA_ARGS__)))

// log only if condition is true
#define logIf(cond__, ...) (static_cast<bool>(cond__) ? debugLog(__VA_ARGS__) : void(0))

// log only if cvar__.getBool() == true
#define logIfCV(cvar__, ...) logIf(cv::cvar__.getBool(), __VA_ARGS__)

// raw logging without any context
#define logRaw(str__, ...)                                        \
    Logger::_detail::logRaw_int(Logger::_detail::log_level::info, \
                                __VA_OPT__(_logFmtStart)(str__) __VA_OPT__(_logFmtEnd(__VA_ARGS__)))

// print the call stack immediately
// TODO: some portable way to do this
#if defined(MCENGINE_HAVE_STDSTACKTRACE) && defined(__has_include) && (__has_include(<stacktrace>))
#include "SString.h"
#include "fmt/ostream.h"

#include <stacktrace>
#define MC_DO_BACKTRACE logRaw("{}", fmt::streamed(std::stacktrace::current()));
// do {
//     for(const auto &line : SString::split_newlines(fmt::format("{}", fmt::streamed(std::stacktrace::current()))))
//         logRaw(line);
// } while(false);
#else
#define MC_DO_BACKTRACE (void)0;
#endif

// main Logger API
namespace Logger {
class ConsoleBoxSink;
namespace _detail {

// copied from spdlog, don't want to include it here
// (since we are using spdlog header-only, to lower compile time, only including spdlog things in 1 translation unit)
namespace log_level {
enum level_enum : int { trace = 0, debug = 1, info = 2, warn = 3, err = 4, critical = 5, off = 6, n_levels };
}

void log_int(std::source_location loc, log_level::level_enum lvl, std::string_view str) noexcept;
void logRaw_int(log_level::level_enum lvl, std::string_view str) noexcept;
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
