// Copyright (c) 2026, WH, All rights reserved.
#include "DirectoryWatcherTest.h"

#include "TestMacros.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Paths.h"
#include "Timing.h"

#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <system_error>

namespace Mc::Tests {
namespace {
// the watcher reports a change once it has been stable for two of its 100 ms checks
constexpr f64 SETTLE_SECS{0.5};
constexpr f64 TIMEOUT_SECS{3.};

void createFile(const std::string &dir, const char *name) { File(dir + name, File::MODE::WRITE).writeLine(name); }
}  // namespace

DirectoryWatcherTest::DirectoryWatcherTest()
    : m_phaseStart(Timing::getTimeReal()),
      m_root(Mc::Paths::cache() + "/dirwatchertest/"),
      m_one(m_root + "one/"),
      m_two(m_root + "two/") {
    logRaw("DirectoryWatcherTest created");
    std::error_code ec;
    std::filesystem::remove_all(File::getFsPath(m_root), ec);
    Environment::createDirectory(m_one);
    Environment::createDirectory(m_two);

    m_first = directoryWatcher->watch_directory(m_one, [this](const FileChangeEvent &ev) {
        const std::string file = Environment::getFileNameFromFilePath(ev.path);
        m_seen["first"].push_back(file);
        // a later watch of the same directory, stopped before its turn
        if(file == "third") m_victim.reset();
    });
    m_second = record(m_one, "second");
    m_other = record(m_two, "other");
}

Mc::Registration DirectoryWatcherTest::record(const std::string &dir, const std::string &name) {
    return directoryWatcher->watch_directory(dir, [this, name](const FileChangeEvent &ev) {
        m_seen[name].push_back(Environment::getFileNameFromFilePath(ev.path));
    });
}

Mc::Registration DirectoryWatcherTest::rearming() {
    // (the captured name puts the closure on the heap: a callback running from storage that its own stop freed shows
    // under asan)
    return directoryWatcher->watch_directory(m_one, [this, name = std::string{"rearming"}](const FileChangeEvent &ev) {
        const std::string file = Environment::getFileNameFromFilePath(ev.path);
        if(file == "third") {
            // stops the watch whose callback this is, while it runs
            m_rearming = rearming();
            m_late = record(m_one, "late");
        }
        m_seen[name].push_back(file);
    });
}

size_t DirectoryWatcherTest::seen(const std::string &watch, const std::string &file) const {
    const auto it = m_seen.find(watch);
    return it == m_seen.end() ? 0 : static_cast<size_t>(std::ranges::count(it->second, file));
}

void DirectoryWatcherTest::update() {
    const f64 elapsed = Timing::getTimeReal() - m_phaseStart;
    const auto next = [&](Phase phase) {
        m_phase = phase;
        m_phaseStart = Timing::getTimeReal();
    };

    switch(m_phase) {
        case Phase::SETTLE:
            // (the worker takes its first listing of a new directory without reporting anything)
            if(elapsed < SETTLE_SECS) return;
            createFile(m_one, "first");
            createFile(m_two, "zero");
            next(Phase::SHARED);
            return;

        case Phase::SHARED:
            if((!seen("first", "first") || !seen("other", "zero")) && elapsed < TIMEOUT_SECS) return;
            TEST_SECTION("watches of one directory");
            TEST_ASSERT_EQ(seen("first", "first"), 1, "the first watch got the event");
            TEST_ASSERT_EQ(seen("second", "first"), 1, "a second watch of the same directory got it too");
            TEST_ASSERT_EQ(seen("other", "zero"), 1, "a watch of another directory got that directory's event");
            TEST_ASSERT_EQ(seen("other", "first") + seen("first", "zero") + seen("second", "zero"), 0,
                           "no watch got the other directory's event");

            // stopped while its event is queued: blocking the main thread keeps the watcher from dispatching what its
            // worker queues meanwhile
            createFile(m_one, "second");
            Timing::sleepMS(1000);
            m_second.reset();
            next(Phase::QUEUED);
            return;

        case Phase::QUEUED:
            TEST_SECTION("stopped with an event queued");
            TEST_ASSERT_EQ(seen("first", "second"), 1, "the event was queued and dispatched right after the stop");
            TEST_ASSERT_EQ(seen("second", "second"), 0, "the stopped watch didn't get it");

            m_victim = record(m_one, "victim");
            m_rearming = rearming();
            createFile(m_one, "third");
            next(Phase::REENTRANT);
            return;

        case Phase::REENTRANT:
            if(!seen("first", "third") && elapsed < TIMEOUT_SECS) return;
            TEST_SECTION("callbacks stopping and starting watches");
            TEST_ASSERT_EQ(seen("first", "third"), 1, "the first watch got the event");
            TEST_ASSERT_EQ(seen("victim", "third"), 0, "a watch stopped by an earlier callback didn't get it");
            TEST_ASSERT_EQ(seen("rearming", "third"), 1, "a callback replacing its own watch ran once");
            TEST_ASSERT_EQ(seen("late", "third"), 0, "a watch started by a callback didn't get the event before it");

            createFile(m_one, "fourth");
            next(Phase::REARMED);
            return;

        case Phase::REARMED:
            if(!seen("first", "fourth") && elapsed < TIMEOUT_SECS) return;
            TEST_SECTION("watches started by callbacks");
            TEST_ASSERT_EQ(seen("first", "fourth"), 1, "the first watch got the next event");
            TEST_ASSERT_EQ(seen("rearming", "fourth"), 1, "the replacement watch got it");
            TEST_ASSERT_EQ(seen("late", "fourth"), 1, "the watch started by a callback got it");
            TEST_ASSERT_EQ(seen("victim", "fourth") + seen("second", "fourth"), 0, "the stopped watches didn't");
            finish();
            return;

        case Phase::DONE:
            return;
    }
}

void DirectoryWatcherTest::finish() {
    m_phase = Phase::DONE;
    for(auto *watch : {&m_first, &m_second, &m_other, &m_victim, &m_rearming, &m_late}) watch->reset();
    std::error_code ec;
    std::filesystem::remove_all(File::getFsPath(m_root), ec);
    TEST_PRINT_RESULTS("DirectoryWatcherTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
