// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"
#include "AsyncPool.h"

namespace Mc::Tests {

class AsyncPoolTest : public App {
    NOCOPY_NOMOVE(AsyncPoolTest)
   public:
    AsyncPoolTest();
    ~AsyncPoolTest() override = default;

    void update() override;

   private:
    void runSyncTests();
    void finish();

    int m_passes = 0;
    int m_failures = 0;

    // phase-based state machine for multi-frame async tests
    // (sync tests run in the first frame, then async tests span multiple frames
    // so that Engine::onUpdate() naturally calls Async::update() between them)
    enum Phase : u8 {
        SYNC_TESTS,

        // then_on_main basic (non-void)
        WAIT_THEN_ON_MAIN,
        TEST_THEN_ON_MAIN,
        // then_on_main void
        WAIT_THEN_ON_MAIN_VOID,
        TEST_THEN_ON_MAIN_VOID,
        // cancellable then_on_main (completed)
        WAIT_CANCEL_COMPLETED,
        TEST_CANCEL_COMPLETED,
        // cancellable then_on_main (cancelled)
        WAIT_CANCEL_CANCELLED,
        TEST_CANCEL_CANCELLED,
        // cancellable auto-cancel on destroy
        WAIT_AUTO_CANCEL,
        TEST_AUTO_CANCEL,

        DONE
    };
    Phase m_phase{SYNC_TESTS};

    // async test state: handles kept alive across frames, results set by continuations
    Async::Future<void> m_asyncHandle;
    Async::CancellableHandle<void> m_cancelHandle;
    int m_thenOnMainResult{0};
    bool m_thenOnMainVoidCalled{false};
    Async::Result<int> m_cancelCompletedResult{0, Async::Status::cancelled};
    Async::Result<void> m_cancelCancelledResult{Async::Status::completed};
    Async::Result<void> m_autoCancelResult{Async::Status::completed};
};

}  // namespace Mc::Tests
