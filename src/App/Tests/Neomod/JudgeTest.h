// Copyright (c) 2026, kiwec, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

// plays maps headless and writes every judgement, sound, score and per-frame judge state to a dump file.
// run it on two builds and diff the dumps to check that gameplay didn't change.
//   -testarg:maps DIR      the .osu files, matched to replays by MD5
//   -testarg:replays DIR   plays every .osr under DIR with its own mods, then with other mod combinations
//   -testarg:auto 1        plays every map with autoplay, with several mod combinations
//   -testarg:out FILE      the dump file
//   -testarg:bench N       plays every map with autoplay N times and logs the time per frame, without a dump
class JudgeTest : public App {
    NOCOPY_NOMOVE(JudgeTest)
   public:
    JudgeTest();
    ~JudgeTest() override = default;

    void update() override;

   private:
    bool m_ran{false};
};

}  // namespace Mc::Tests
