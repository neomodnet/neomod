// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"
#include "StaticPImpl.h"

namespace Mc::Tests {

// sanity suite for NetworkHandler, exercising the same code on native (curl) and
// emscripten (fetch). focuses on the async cancellation paths added alongside this test.
class NetworkTest final : public App {
    NOCOPY_NOMOVE(NetworkTest)
   public:
    NetworkTest();
    ~NetworkTest() override;

    void update() override;
    struct NetworkTestImpl;

   private:
    StaticPImpl<NetworkTestImpl, 1056> m;
};

}  // namespace Mc::Tests
