// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

namespace Mc::Tests {

class ConVarTest : public App {
    NOCOPY_NOMOVE(ConVarTest)
   public:
    ConVarTest();
    ~ConVarTest() override;

    void update() override;

   private:
    void testTypesAndParsing();
    void testPermissions();
    void testLayers();
    void testProtectionLock();
    void testGameplayGate();
    void testCallbacks();
    void testSubmittable();
    void testDefaults();
    void testCommands();

    int m_passes{0};
    int m_failures{0};
    bool m_done{false};
};

}  // namespace Mc::Tests
