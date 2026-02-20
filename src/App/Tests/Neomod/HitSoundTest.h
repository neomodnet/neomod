// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

class HitSoundTest : public App {
    NOCOPY_NOMOVE(HitSoundTest)
   public:
    HitSoundTest();
    ~HitSoundTest() override = default;

    void update() override;

   private:
    void runTests();

    int m_passes = 0;
    int m_failures = 0;
    bool m_ran = false;
};

}  // namespace Mc::Tests
