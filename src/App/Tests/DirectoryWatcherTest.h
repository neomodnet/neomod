// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"
#include "DirectoryWatcher.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Mc::Tests {

// DirectoryWatcher: watches of one directory each get its events, a stopped watch gets none (also none that were
// already queued), and callbacks may stop or start watches, their own included
class DirectoryWatcherTest : public App {
    NOCOPY_NOMOVE(DirectoryWatcherTest)
   public:
    DirectoryWatcherTest();
    ~DirectoryWatcherTest() override = default;

    void update() override;

   private:
    // a watch of `dir` that records the names of the files it's told about under `name`
    Mc::Registration record(const std::string &dir, const std::string &name);
    // records under "rearming", and replaces itself with a new one when it sees "third"
    Mc::Registration rearming();
    [[nodiscard]] size_t seen(const std::string &watch, const std::string &file) const;
    void finish();

    int m_passes{0};
    int m_failures{0};

    enum class Phase : u8 { SETTLE, SHARED, QUEUED, REENTRANT, REARMED, DONE } m_phase{Phase::SETTLE};
    f64 m_phaseStart;

    std::string m_root;
    std::string m_one;
    std::string m_two;
    std::unordered_map<std::string, std::vector<std::string>> m_seen;
    Mc::Registration m_first, m_second, m_other, m_victim, m_rearming, m_late;
};

}  // namespace Mc::Tests
