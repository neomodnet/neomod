// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

class AsyncPoolTest : public App {
    NOCOPY_NOMOVE(AsyncPoolTest)
   public:
    AsyncPoolTest();
    ~AsyncPoolTest() override = default;

    void update() override;

   private:
    int m_passes = 0;
    int m_failures = 0;
    bool m_ran{false};
};

}  // namespace Mc::Tests
