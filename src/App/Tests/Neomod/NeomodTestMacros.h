// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "Logging.h"
#include "Environment.h"

#include <cmath>
#include <optional>
#include <string>

// retrieve a -testarg:<name> value from launch args
// usage: auto path = getTestArg("skin_tier1");
//   launched with: -testarg:skin_tier1 "/path/to/skin"
inline std::optional<std::string> getTestArg(std::string_view name) {
    std::string key = "-testarg:";
    key.append(name);
    auto &args = env->getLaunchArgs();
    if(auto it = args.find(key); it != args.end() && it->second.has_value()) {
        return it->second;
    }
    return std::nullopt;
}

// test result counters; declare as member variables in your test app class
// e.g. int m_passes = 0; int m_failures = 0;

#define TEST_ASSERT(cond, msg)                                     \
    do {                                                           \
        if(!(cond)) {                                              \
            logRaw("  FAIL: {} ({}:{})", msg, __FILE__, __LINE__); \
            m_failures++;                                          \
        } else {                                                   \
            m_passes++;                                            \
        }                                                          \
    } while(0)

#define TEST_ASSERT_EQ(actual, expected, msg)                                                               \
    do {                                                                                                    \
        if((actual) != (expected)) {                                                                        \
            logRaw("  FAIL: {} -- expected {}, got {} ({}:{})", msg, expected, actual, __FILE__, __LINE__); \
            m_failures++;                                                                                   \
        } else {                                                                                            \
            m_passes++;                                                                                     \
        }                                                                                                   \
    } while(0)

#define TEST_ASSERT_NEAR(actual, expected, eps, msg)                                                         \
    do {                                                                                                     \
        if(std::abs((actual) - (expected)) > (eps)) {                                                        \
            logRaw("  FAIL: {} -- expected ~{}, got {} ({}:{})", msg, expected, actual, __FILE__, __LINE__); \
            m_failures++;                                                                                    \
        } else {                                                                                             \
            m_passes++;                                                                                      \
        }                                                                                                    \
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
