// Copyright (c) 2026, WH, All rights reserved.
#include "AsyncPoolTest.h"

#include "TestMacros.h"
#include "AsyncChannel.h"
#include "Engine.h"
#include "Timing.h"

#include <atomic>
#include <string>
#include <tuple>

namespace Mc::Tests {

AsyncPoolTest::AsyncPoolTest() { logRaw("AsyncPoolTest created"); }

void AsyncPoolTest::update() {
    switch(m_phase) {
        case SYNC_TESTS:
            runSyncTests();
            // set up first async test: then_on_main basic
            m_asyncHandle = Async::submit([] { return 42; }).then_on_main([this](int x) { m_thenOnMainResult = x; });
            m_phase = WAIT_THEN_ON_MAIN;
            return;

        case WAIT_THEN_ON_MAIN:
            if(m_thenOnMainResult == 0) return;
            m_phase = TEST_THEN_ON_MAIN;
            [[fallthrough]];

        case TEST_THEN_ON_MAIN:
            TEST_SECTION("then_on_main basic");
            TEST_ASSERT_EQ(m_thenOnMainResult, 42, "then_on_main delivers result to main thread");
            // set up next: then_on_main void
            m_asyncHandle = Async::submit([] {}).then_on_main([this]() { m_thenOnMainVoidCalled = true; });
            m_phase = WAIT_THEN_ON_MAIN_VOID;
            return;

        case WAIT_THEN_ON_MAIN_VOID:
            if(!m_thenOnMainVoidCalled) return;
            m_phase = TEST_THEN_ON_MAIN_VOID;
            [[fallthrough]];

        case TEST_THEN_ON_MAIN_VOID:
            TEST_SECTION("then_on_main void");
            TEST_ASSERT(m_thenOnMainVoidCalled, "then_on_main fires for void task");
            // set up next: cancellable then_on_main (completed)
            m_cancelHandle = Async::submit_cancellable([](const Sync::stop_token&) {
                                 return 99;
                             }).then_on_main([this](Async::Result<int> r) { m_cancelCompletedResult = r; });
            m_phase = WAIT_CANCEL_COMPLETED;
            return;

        case WAIT_CANCEL_COMPLETED:
            if(!m_cancelCompletedResult.ok() && m_cancelCompletedResult.value == 0) return;
            m_phase = TEST_CANCEL_COMPLETED;
            [[fallthrough]];

        case TEST_CANCEL_COMPLETED: {
            TEST_SECTION("cancellable then_on_main completed");
            TEST_ASSERT(m_cancelCompletedResult.ok(), "cancellable then_on_main reports completed");
            TEST_ASSERT_EQ(m_cancelCompletedResult.value, 99, "cancellable then_on_main delivers correct value");
            // set up next: cancellable then_on_main (cancelled)
            m_cancelHandle = Async::submit_cancellable([](const Sync::stop_token& tok) {
                                 while(!tok.stop_requested()) Timing::tinyYield();
                             }).then_on_main([this](Async::Result<void> r) { m_cancelCancelledResult = r; });
            Timing::sleepMS(5);
            m_cancelHandle.cancel();
            m_phase = WAIT_CANCEL_CANCELLED;
            return;
        }

        case WAIT_CANCEL_CANCELLED:
            if(m_cancelCancelledResult.ok()) return;  // still at initial value (completed); waiting for cancelled
            m_phase = TEST_CANCEL_CANCELLED;
            [[fallthrough]];

        case TEST_CANCEL_CANCELLED:
            TEST_SECTION("cancellable then_on_main cancelled");
            TEST_ASSERT(!m_cancelCancelledResult.ok(), "cancellable then_on_main reports cancelled");
            TEST_ASSERT_EQ((int)m_cancelCancelledResult.status, (int)Async::Status::cancelled, "status is cancelled");
            // set up next: auto-cancel on destroy
            {
                auto handle = Async::submit_cancellable([](const Sync::stop_token& tok) {
                                  while(!tok.stop_requested()) Timing::tinyYield();
                              }).then_on_main([this](Async::Result<void> r) { m_autoCancelResult = r; });
                Timing::sleepMS(5);
                // handle destroyed here; should signal cancellation
            }
            m_phase = WAIT_AUTO_CANCEL;
            return;

        case WAIT_AUTO_CANCEL:
            if(m_autoCancelResult.ok()) return;  // still at initial value; waiting
            m_phase = TEST_AUTO_CANCEL;
            [[fallthrough]];

        case TEST_AUTO_CANCEL:
            TEST_SECTION("cancellable then_on_main auto-cancel on destroy");
            TEST_ASSERT(!m_autoCancelResult.ok(), "auto-cancel on destroy reports cancelled");
            finish();
            return;

        case DONE:
            return;
    }
}

void AsyncPoolTest::runSyncTests() {
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
            futures[i] = Async::submit([&count] { count.fetch_add(1, std::memory_order_relaxed); }, Lane::Background);
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
            // handle goes out of scope here; destructor signals cancel but does not block
        }
        // task should observe the stop and exit on its own
        for(int i = 0; i < 10000 && !exited.load(std::memory_order_acquire); i++) {
            Timing::tinyYield();
        }
        TEST_ASSERT(exited.load(std::memory_order_acquire), "handle destructor signalled cancel");
    }

    // --- sync continuation tests (then, when_all, wait_all, make_ready_future) ---

    TEST_SECTION("then basic");
    {
        auto future = Async::submit([] { return 42; }).then([](int x) { return x * 2; });
        future.wait();
        TEST_ASSERT_EQ(future.get(), 84, "then transforms result");
    }

    TEST_SECTION("then void -> value");
    {
        auto future = Async::submit([] {}).then([] { return 7; });
        future.wait();
        TEST_ASSERT_EQ(future.get(), 7, "then after void task returns value");
    }

    TEST_SECTION("then chain");
    {
        auto future =
            Async::submit([] { return 2; }).then([](int x) { return x + 3; }).then([](int x) { return x * 10; });
        future.wait();
        TEST_ASSERT_EQ(future.get(), 50, "chained then produces correct result");
    }

    TEST_SECTION("when_all vector");
    {
        std::vector<Async::Future<int>> futures;
        for(int i = 0; i < 4; i++) {
            futures.push_back(Async::submit([i] { return i * 10; }));
        }
        auto all = Async::when_all(std::move(futures));
        all.wait();
        auto results = all.get();
        TEST_ASSERT_EQ((int)results.size(), 4, "when_all returns all results");
        bool correct = true;
        for(int i = 0; i < 4; i++) {
            if(results[i] != i * 10) correct = false;
        }
        TEST_ASSERT(correct, "when_all results are correct and in order");
    }

    TEST_SECTION("when_all vector void");
    {
        std::atomic<int> count{0};
        std::vector<Async::Future<void>> futures;
        for(int i = 0; i < 4; i++) {
            futures.push_back(Async::submit([&count] { count.fetch_add(1, std::memory_order_relaxed); }));
        }
        auto all = Async::when_all(std::move(futures));
        all.wait();
        all.get();
        TEST_ASSERT_EQ(count.load(std::memory_order_relaxed), 4, "when_all void completes all tasks");
    }

    TEST_SECTION("when_all variadic");
    {
        auto f1 = Async::submit([] { return 42; });
        auto f2 = Async::submit([] { return std::string("hello"); });
        auto all = Async::when_all(std::move(f1), std::move(f2));
        all.wait();
        auto [a, b] = all.get();
        TEST_ASSERT_EQ(a, 42, "when_all variadic first element correct");
        TEST_ASSERT(b == "hello", "when_all variadic second element correct");
    }

    TEST_SECTION("wait_all");
    {
        auto f1 = Async::submit([] { return 1; });
        auto f2 = Async::submit([] { return 2; });
        auto f3 = Async::submit([] { return 3; });
        Async::wait_all(f1, f2, f3);
        TEST_ASSERT(f1.is_ready() && f2.is_ready() && f3.is_ready(), "wait_all makes all futures ready");
        TEST_ASSERT_EQ(f1.get() + f2.get() + f3.get(), 6, "wait_all values are correct");
    }

    TEST_SECTION("wait_all vector");
    {
        std::vector<Async::Future<int>> futures;
        for(int i = 0; i < 4; i++) {
            futures.push_back(Async::submit([i] { return i; }));
        }
        Async::wait_all(futures);
        bool allReady = true;
        for(auto& f : futures) {
            if(!f.is_ready()) allReady = false;
        }
        TEST_ASSERT(allReady, "wait_all vector makes all futures ready");
    }

    TEST_SECTION("make_ready_future");
    {
        auto future = Async::make_ready_future(42);
        TEST_ASSERT(future.is_ready(), "make_ready_future is immediately ready");
        TEST_ASSERT_EQ(future.get(), 42, "make_ready_future has correct value");
    }

    TEST_SECTION("make_ready_future void");
    {
        auto future = Async::make_ready_future();
        TEST_ASSERT(future.is_ready(), "make_ready_future void is immediately ready");
        future.get();
        TEST_ASSERT(!future.valid(), "make_ready_future void consumed after get");
    }
}

void AsyncPoolTest::finish() {
    m_phase = DONE;
    TEST_PRINT_RESULTS("AsyncPoolTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
