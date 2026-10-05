// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

// BeatmapFile on its own. -testarg:corpus DIR also reads every .osu under DIR: the sections have to give back the
// bytes, and the records they parse have to come back the same through format(). -testarg:dump FILE (with a corpus)
// writes what the game loads from each file, to compare two builds with
class BeatmapFileTest : public App {
    NOCOPY_NOMOVE(BeatmapFileTest)
   public:
    BeatmapFileTest();
    ~BeatmapFileTest() override = default;

    void update() override;

   private:
    void runTests();
    void runCorpus(const std::string &dir);

    int m_passes{0};
    int m_failures{0};
    bool m_ran{false};
};

}  // namespace Mc::Tests
