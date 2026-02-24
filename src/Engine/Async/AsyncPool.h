// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "types.h"

#include "AsyncCancellable.h"

#include "SyncMutex.h"
#include "SyncCV.h"
#include "SyncJthread.h"

#include <queue>
#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include <type_traits>

enum class Lane : u8 {
    Foreground,  // default; short/frame-adjacent work
    Background,  // long-running work (archival, enumeration, etc.)
};

// type-erased task hierarchy (adapted from SoLoudThread.h)
class AsyncPool final {
    NOCOPY_NOMOVE(AsyncPool)

    struct TaskBase {
        NOCOPY_NOMOVE(TaskBase)
       public:
        TaskBase() noexcept = default;
        virtual ~TaskBase() noexcept = default;
        virtual void execute() noexcept = 0;
    };

    template <typename T>
    struct Task : TaskBase {
        std::function<T()> work;
        std::promise<T> promise;

        Task(std::function<T()> w) noexcept : work(std::move(w)) {}

        void execute() noexcept override {
            if constexpr(std::is_void_v<T>) {
                this->work();
                this->promise.set_value();
            } else {
                this->promise.set_value(this->work());
            }
        }

        std::future<T> get_future() { return this->promise.get_future(); }
    };

    struct FireAndForgetTask : TaskBase {
        std::function<void()> work;

        FireAndForgetTask(std::function<void()> w) noexcept : work(std::move(w)) {}

        void execute() noexcept override { this->work(); }
    };

   public:
    explicit AsyncPool(size_t thread_count);
    ~AsyncPool();

    // submit work, get a future back
    template <typename F>
    auto submit(F&& func, Lane lane = Lane::Foreground) -> Async::Future<std::invoke_result_t<F>> {
        using T = std::invoke_result_t<F>;
        auto task = std::make_unique<Task<T>>(std::forward<F>(func));
        auto future = Async::Future<T>(task->get_future());

        {
            Sync::scoped_lock lock(m_workMutex);
            queue_for(lane).push(std::move(task));
        }
        m_pending.fetch_add(1, std::memory_order_relaxed);
        notify_for(lane);

        return future;
    }

    // fire-and-forget (no promise/future overhead)
    template <typename F>
    void dispatch(F&& func, Lane lane = Lane::Foreground) {
        auto task = std::make_unique<FireAndForgetTask>(std::forward<F>(func));

        {
            Sync::scoped_lock lock(m_workMutex);
            queue_for(lane).push(std::move(task));
        }
        m_pending.fetch_add(1, std::memory_order_relaxed);
        notify_for(lane);
    }

    // stop accepting work, join all threads
    void shutdown();

    [[nodiscard]] size_t thread_count() const noexcept { return m_fgThreads.size() + m_bgThreads.size(); }
    [[nodiscard]] size_t pending_count() const noexcept { return m_pending.load(std::memory_order_relaxed); }

   private:
    void fg_worker_loop(const Sync::stop_token& stoken, size_t index) noexcept;
    void bg_worker_loop(const Sync::stop_token& stoken, size_t index) noexcept;

    std::queue<std::unique_ptr<TaskBase>>& queue_for(Lane lane) {
        return lane == Lane::Foreground ? m_fgQueue : m_bgQueue;
    }

    void notify_for(Lane lane) {
        if(lane == Lane::Foreground) {
            m_fgCV.notify_one();
        } else {
            // wake a bg thread, and also a fg thread so it can work-steal
            m_bgCV.notify_one();
            m_fgCV.notify_one();
        }
    }

    std::queue<std::unique_ptr<TaskBase>> m_fgQueue;
    std::queue<std::unique_ptr<TaskBase>> m_bgQueue;
    Sync::mutex m_workMutex;
    Sync::condition_variable_any m_fgCV;
    Sync::condition_variable_any m_bgCV;
    std::atomic<size_t> m_pending{0};

    std::vector<Sync::jthread> m_fgThreads;
    std::vector<Sync::jthread> m_bgThreads;
    bool m_shutdown{false};
};

// free-function API via global pool pointer
namespace Async {

AsyncPool& pool();

template <typename F>
auto submit(F&& f, Lane lane = Lane::Foreground) -> Future<std::invoke_result_t<F>> {
    return pool().submit(std::forward<F>(f), lane);
}

template <typename F>
void dispatch(F&& f, Lane lane = Lane::Foreground) {
    pool().dispatch(std::forward<F>(f), lane);
}

// cancellable submit: composes submit() with a stop_source.
// the callable receives a const Sync::stop_token& and should check stop_requested() periodically.
template <typename F>
auto submit_cancellable(F&& f, Lane lane = Lane::Foreground) -> CancellableHandle<std::invoke_result_t<F, const Sync::stop_token&>> {
    using T = std::invoke_result_t<F, const Sync::stop_token&>;

    Sync::stop_source source;
    auto token = source.get_token();

    auto future = pool().submit([func = std::forward<F>(f), tok = std::move(token)]() mutable -> T {
        return func(tok);
    }, lane);

    return CancellableHandle<T>(std::move(future), std::move(source));
}

}  // namespace Async
