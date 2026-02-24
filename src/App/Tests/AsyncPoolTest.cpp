// Copyright (c) 2026, WH, All rights reserved.
#include "AsyncPoolTest.h"

#include "TestMacros.h"
#include "AsyncPool.h"
#include "AsyncChannel.h"
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

    TEST_SECTION("channel push + drain");
    {
        Async::Channel<int> ch;
        ch.push(1);
        ch.push(2);
        ch.push(3);
        auto items = ch.drain();
        TEST_ASSERT_EQ((int)items.size(), 3, "drain returns all pushed items");
        TEST_ASSERT_EQ(items[0], 1, "first item correct");
        TEST_ASSERT_EQ(items[1], 2, "second item correct");
        TEST_ASSERT_EQ(items[2], 3, "third item correct");
    }

    TEST_SECTION("channel drain empties");
    {
        Async::Channel<int> ch;
        ch.push(42);
        auto first = ch.drain();
        auto second = ch.drain();
        TEST_ASSERT_EQ((int)first.size(), 1, "first drain returns item");
        TEST_ASSERT_EQ((int)second.size(), 0, "second drain returns empty");
    }

    TEST_SECTION("channel empty drain");
    {
        Async::Channel<std::string> ch;
        auto items = ch.drain();
        TEST_ASSERT_EQ((int)items.size(), 0, "drain on empty channel returns empty");
    }

    TEST_SECTION("channel concurrent push + drain");
    {
        Async::Channel<int> ch;
        constexpr int N = 100;
        constexpr int NUM_PRODUCERS = 4;

        Async::Future<void> producers[NUM_PRODUCERS];
        for(int p = 0; p < NUM_PRODUCERS; p++) {
            producers[p] = Async::submit([&ch, p] {
                for(int i = 0; i < N; i++) {
                    ch.push(p * N + i);
                }
            });
        }

        for(int p = 0; p < NUM_PRODUCERS; p++) {
            producers[p].wait();
        }

        auto items = ch.drain();
        TEST_ASSERT_EQ((int)items.size(), N * NUM_PRODUCERS, "all items from all producers received");
    }

    TEST_SECTION("channel move-only types");
    {
        Async::Channel<std::unique_ptr<int>> ch;
        ch.push(std::make_unique<int>(7));
        ch.push(std::make_unique<int>(13));
        auto items = ch.drain();
        TEST_ASSERT_EQ((int)items.size(), 2, "drain returns 2 move-only items");
        TEST_ASSERT_EQ(*items[0], 7, "first unique_ptr value correct");
        TEST_ASSERT_EQ(*items[1], 13, "second unique_ptr value correct");
    }

    TEST_SECTION("cancellable handle auto-cancel");
    {
        std::atomic<bool> exited{false};
        {
            auto handle = Async::submit_cancellable([&exited](const Sync::stop_token& stoken) {
                while(!stoken.stop_requested()) {
                    Timing::tinyYield();
                }
                exited.store(true, std::memory_order_release);
            });
            Timing::sleepMS(5);
            // handle goes out of scope here; destructor should cancel + wait
        }
        TEST_ASSERT(exited.load(std::memory_order_acquire), "handle destructor auto-cancelled and waited");
    }

    TEST_PRINT_RESULTS("AsyncPoolTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
