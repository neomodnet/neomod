// Copyright (c) 2026, kiwec, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

class AlgorithmsTest : public App {
    NOCOPY_NOMOVE(AlgorithmsTest)
   public:
    AlgorithmsTest();
    ~AlgorithmsTest() override = default;

    void update() override;

   private:
    int m_passes{0};
    int m_failures{0};
    bool m_done{false};
};

}  // namespace Mc::Tests
