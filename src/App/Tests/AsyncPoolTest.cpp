// Copyright (c) 2026, WH, All rights reserved.
#include "AsyncPoolTest.h"

#include "TestMacros.h"
#include "AsyncPool.h"
#include "Engine.h"
#include "Timing.h"

#include <atomic>

namespace Mc::Tests {

AsyncPoolTest::AsyncPoolTest() { logRaw("AsyncPoolTest created"); }

void AsyncPoolTest::update() {
    if(m_ran) return;
    m_ran = true;

    TEST_SECTION("submit + get");
    {
        auto future = Async::submit([] { return 42; });
        future.wait();
        TEST_ASSERT_EQ(future.get(), 42, "submit returns correct value");
    }

    TEST_SECTION("submit void");
    {
        auto future = Async::submit([] {});
        future.get();
        TEST_ASSERT(future.valid() == false, "void future consumed after get");
    }

    TEST_SECTION("dispatch");
    {
        std::atomic<bool> flag{false};
        Async::dispatch([&flag] { flag.store(true, std::memory_order_release); });

        // spin briefly waiting for the flag
        for(int i = 0; i < 10000 && !flag.load(std::memory_order_acquire); i++) {
            Timing::tinyYield();
        }
        TEST_ASSERT(flag.load(std::memory_order_acquire), "dispatch ran the task");
    }

    TEST_SECTION("multiple submits");
    {
        constexpr int N = 16;
        Async::Future<int> futures[N];
        for(int i = 0; i < N; i++) {
            futures[i] = Async::submit([i] { return i * i; });
        }

        bool allCorrect = true;
        for(int i = 0; i < N; i++) {
            futures[i].wait();
            if(futures[i].get() != i * i) {
                allCorrect = false;
            }
        }
        TEST_ASSERT(allCorrect, "all N submits returned correct values");
    }

    TEST_SECTION("is_ready");
    {
        auto future = Async::submit([] { return 1; });
        future.wait();
        TEST_ASSERT(future.is_ready(), "future is ready after wait");
    }

    TEST_SECTION("thread_count");
    {
        TEST_ASSERT(Async::pool().thread_count() >= 1, "pool has at least 1 thread");
    }

    TEST_SECTION("submit_cancellable");
    {
        std::atomic<bool> exited{false};
        auto handle = Async::submit_cancellable([&exited](const Sync::stop_token& stoken) {
            while(!stoken.stop_requested()) {
                Timing::tinyYield();
            }
            exited.store(true, std::memory_order_release);
        });

        // let the task start
        Timing::sleepMS(5);

        handle.cancel();
        handle.wait();
        TEST_ASSERT(exited.load(std::memory_order_acquire), "cancellable task observed stop and exited");
    }

    TEST_SECTION("background lane submit");
    {
        auto future = Async::submit([] { return 99; }, Lane::Background);
        future.wait();
        TEST_ASSERT_EQ(future.get(), 99, "background submit returns correct value");
    }

    TEST_SECTION("background lane dispatch");
    {
        std::atomic<bool> flag{false};
        Async::dispatch([&flag] { flag.store(true, std::memory_order_release); }, Lane::Background);

        for(int i = 0; i < 10000 && !flag.load(std::memory_order_acquire); i++) {
            Timing::tinyYield();
        }
        TEST_ASSERT(flag.load(std::memory_order_acquire), "background dispatch ran the task");
    }

    TEST_SECTION("work stealing");
    {
        // submit enough background tasks to exceed bg thread count;
        // foreground threads should steal and help complete them
        constexpr int N = 16;
        std::atomic<int> count{0};
        Async::Future<void> futures[N];
        for(int i = 0; i < N; i++) {
            futures[i] = Async::submit([&count] {
                count.fetch_add(1, std::memory_order_relaxed);
            }, Lane::Background);
        }

        for(int i = 0; i < N; i++) {
            futures[i].wait();
        }
        TEST_ASSERT_EQ(count.load(std::memory_order_relaxed), N, "all background tasks completed (work stealing)");
    }

    TEST_SECTION("thread_count >= 2");
    {
        TEST_ASSERT(Async::pool().thread_count() >= 2, "pool has at least 2 threads");
    }

    TEST_PRINT_RESULTS("AsyncPoolTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
