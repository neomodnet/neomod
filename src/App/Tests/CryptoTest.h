// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

class CryptoTest : public App {
    NOCOPY_NOMOVE(CryptoTest)
   public:
    CryptoTest();
    ~CryptoTest() override = default;

    void update() override;

   private:
    int m_passes{0};
    int m_failures{0};
    bool m_done{false};
};

}  // namespace Mc::Tests
