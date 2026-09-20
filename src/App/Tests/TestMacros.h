// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "Logging.h"
#include "Environment.h"
#include "LaunchArgs.h"
#include "SString.h"

#include <cmath>
#include <optional>
#include <string>

namespace Mc::Tests {
// retrieve a -testarg:<name> value from launch args
// usage: auto path = getTestArg("skin_tier1");
//   launched with: -testarg:skin_tier1 "/path/to/skin"
// these dynamic switches can't be part of the LaunchArgs::ArgSwitch enum, so query the map directly
inline std::optional<std::string> getTestArg(std::string_view name) {
    std::string key = "-testarg:";
    key.append(name);
    SString::lower_inplace(key);  // switches are lowercased in the arg map
    const auto& args = Mc::LaunchArgs::get_map();
    if(auto it = args.find(key); it != args.end() && it->second.has_value()) {
        return it->second;
    }
    return std::nullopt;
}

namespace detail {
inline int todo_level, todo_do_loop;
inline void start_todo(int is_todo) {
    todo_level = (todo_level << 1) | (is_todo != 0);
    todo_do_loop = 1;
}
inline int loop_todo(void) {
    int do_loop = todo_do_loop;
    todo_do_loop = 0;
    return do_loop;
}
inline void end_todo(void) { todo_level >>= 1; }

// avoid unnecessary code bloat with xpass strings
inline void log_xpass(std::string_view msg, const char* file, int line) {
    if(todo_level > 0) logRaw("  XPASS: {} -- passes now, make it a regular assert ({}:{})", msg, file, line);
}
}  // namespace detail

}  // namespace Mc::Tests

// for a condition that is known not to hold yet (pins the target behavior of planned work):
// the expected failure is logged without counting as one, but an unexpected pass does count,
// so that the marker gets turned into a regular assert together with the fix
#define TEST_TODO_IF(is_todo__) \
    for(Mc::Tests::detail::start_todo(is_todo__); Mc::Tests::detail::loop_todo(); Mc::Tests::detail::end_todo())
#define TEST_TODO TEST_TODO_IF(true)

#define TEST_IS_TODO (Mc::Tests::detail::todo_level > 0)

// test result counters; declare as member variables in your test app class
// e.g. int m_passes = 0; int m_failures = 0;

#define TEST_ASSERT(cond, msg)                                                                    \
    do {                                                                                          \
        if(!(cond)) {                                                                             \
            logRaw("  {}: {} ({}:{})", TEST_IS_TODO ? "XFAIL" : "FAIL", msg, __FILE__, __LINE__); \
            TEST_IS_TODO ? ++m_passes : ++m_failures;                                             \
        } else {                                                                                  \
            Mc::Tests::detail::log_xpass(msg, __FILE__, __LINE__);                                \
            TEST_IS_TODO ? ++m_failures : ++m_passes;                                             \
        }                                                                                         \
    } while(0)

#define TEST_ASSERT_EQ(actual, expected, msg)                                                                         \
    do {                                                                                                              \
        if((actual) != (expected)) {                                                                                  \
            logRaw("  {}: {} -- expected {}, got {} ({}:{})", TEST_IS_TODO ? "XFAIL" : "FAIL", msg, expected, actual, \
                   __FILE__, __LINE__);                                                                               \
            TEST_IS_TODO ? ++m_passes : ++m_failures;                                                                 \
        } else {                                                                                                      \
            Mc::Tests::detail::log_xpass(msg, __FILE__, __LINE__);                                                    \
            TEST_IS_TODO ? ++m_failures : ++m_passes;                                                                 \
        }                                                                                                             \
    } while(0)

#define TEST_ASSERT_NEAR(actual, expected, eps, msg)                                                                   \
    do {                                                                                                               \
        if(std::abs((actual) - (expected)) > (eps)) {                                                                  \
            logRaw("  {}: {} -- expected ~{}, got {} ({}:{})", TEST_IS_TODO ? "XFAIL" : "FAIL", msg, expected, actual, \
                   __FILE__, __LINE__);                                                                                \
            TEST_IS_TODO ? ++m_passes : ++m_failures;                                                                  \
        } else {                                                                                                       \
            Mc::Tests::detail::log_xpass(msg, __FILE__, __LINE__);                                                     \
            TEST_IS_TODO ? ++m_failures : ++m_passes;                                                                  \
        }                                                                                                              \
    } while(0)

// section header for grouping related tests
#define TEST_SECTION(name) logRaw("--- {} ---", name)

// print final results and shut down
#define TEST_PRINT_RESULTS(test_name)                                                        \
    do {                                                                                     \
        logRaw("");                                                                          \
        logRaw("=== {} results: {} passed, {} failed ===", test_name, m_passes, m_failures); \
        if(m_failures > 0) {                                                                 \
            logRaw("SOME TESTS FAILED");                                                     \
        } else {                                                                             \
            logRaw("ALL TESTS PASSED");                                                      \
        }                                                                                    \
    } while(0)
