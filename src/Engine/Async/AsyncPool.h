// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "types.h"

#include "AsyncCancellable.h"
#include "AsyncChannel.h"

#include "SyncMutex.h"
#include "SyncCV.h"
#include "SyncJthread.h"

#include <queue>
#include <functional>
#include <memory>
#include <vector>
#include <atomic>
#include <type_traits>
#include <tuple>

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

    // queue a callback to run on the main thread during the next Engine::onUpdate()
    void queue_main(std::function<void()> fn) { m_mainQueue.push(std::move(fn)); }

    // drain and execute all queued main-thread callbacks. called from Engine::onUpdate().
    void update() {
        auto items = m_mainQueue.drain();
        for(auto& fn : items) fn();
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

    Async::Channel<std::function<void()>> m_mainQueue;
};

// ---------------------------------------------------------------------------
// free-function API via global pool pointer
// ---------------------------------------------------------------------------
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
auto submit_cancellable(F&& f, Lane lane = Lane::Foreground)
    -> CancellableHandle<std::invoke_result_t<F, const Sync::stop_token&>> {
    using T = std::invoke_result_t<F, const Sync::stop_token&>;

    Sync::stop_source source;
    auto token = source.get_token();

    auto future =
        pool().submit([func = std::forward<F>(f), tok = std::move(token)]() mutable -> T { return func(tok); }, lane);

    return CancellableHandle<T>(std::move(future), std::move(source));
}

inline void queue_main(std::function<void()> fn) { pool().queue_main(std::move(fn)); }
inline void update() { pool().update(); }

// ---------------------------------------------------------------------------
// make_ready_future: create a future that is immediately ready
// ---------------------------------------------------------------------------

template <typename T>
Future<T> make_ready_future(T&& value) {
    std::promise<T> p;
    p.set_value(std::forward<T>(value));
    return Future<T>(p.get_future());
}

inline Future<void> make_ready_future() {
    std::promise<void> p;
    p.set_value();
    return Future<void>(p.get_future());
}

// ---------------------------------------------------------------------------
// wait_all: block until all futures are ready
// ---------------------------------------------------------------------------

template <typename T>
void wait_all(std::vector<Future<T>>& futures) {
    for(auto& f : futures) f.wait();
}

template <typename... Ts>
void wait_all(Future<Ts>&... futures) {
    (futures.wait(), ...);
}

// ---------------------------------------------------------------------------
// when_all: compose multiple futures into one
// ---------------------------------------------------------------------------

// homogeneous vector of non-void futures
template <typename T>
    requires(!std::is_void_v<T>)
auto when_all(std::vector<Future<T>>&& futures) -> Future<std::vector<T>> {
    // shared_ptr makes the lambda copyable for std::function (Future is move-only).
    // the bridging task blocks one pool thread while iterating; acceptable for realistic usage.
    auto sf = std::make_shared<std::vector<Future<T>>>(std::move(futures));
    return submit([sf]() -> std::vector<T> {
        std::vector<T> results;
        results.reserve(sf->size());
        for(auto& f : *sf) results.push_back(f.get());
        return results;
    });
}

// homogeneous vector of void futures
inline auto when_all(std::vector<Future<void>>&& futures) -> Future<void> {
    auto sf = std::make_shared<std::vector<Future<void>>>(std::move(futures));
    return submit([sf]() {
        for(auto& f : *sf) f.get();
    });
}

// heterogeneous variadic (different types, all non-void)
template <typename T1, typename T2, typename... Rest>
auto when_all(Future<T1>&& f1, Future<T2>&& f2, Future<Rest>&&... rest) -> Future<std::tuple<T1, T2, Rest...>> {
    auto sf = std::make_shared<std::tuple<Future<T1>, Future<T2>, Future<Rest>...>>(std::move(f1), std::move(f2),
                                                                                    std::move(rest)...);
    return submit([sf]() -> std::tuple<T1, T2, Rest...> {
        return std::apply([](auto&... fs) { return std::make_tuple(fs.get()...); }, *sf);
    });
}

// ---------------------------------------------------------------------------
// Future<T> continuation implementations (declared in AsyncFuture.h)
// ---------------------------------------------------------------------------

template <typename T>
template <typename Cb>
auto Future<T>::then(Cb&& cb, Lane lane) -> Future<detail::then_result_t<T, Cb>> {
    // wrap in shared_ptr so the lambda is copyable (std::future is move-only)
    auto sf = std::make_shared<std::future<T>>(std::move(m_future));
    if constexpr(std::is_void_v<T>) {
        return Async::submit(
            [sf, c = std::forward<Cb>(cb)]() mutable {
                sf->get();
                return c();
            },
            lane);
    } else {
        return Async::submit([sf, c = std::forward<Cb>(cb)]() mutable { return c(sf->get()); }, lane);
    }
}

template <typename T>
template <typename Cb>
Future<void> Future<T>::then_on_main(Cb&& cb) {
    auto sf = std::make_shared<std::future<T>>(std::move(m_future));
    return Async::submit([sf, c = std::forward<Cb>(cb)]() mutable {
        if constexpr(std::is_void_v<T>) {
            sf->get();
            Async::queue_main([c = std::move(c)]() mutable { c(); });
        } else {
            auto val = sf->get();
            Async::queue_main([c = std::move(c), v = std::move(val)]() mutable { c(std::move(v)); });
        }
    });
}

// ---------------------------------------------------------------------------
// CancellableHandle<T>::then_on_main (declared in AsyncCancellable.h)
// ---------------------------------------------------------------------------

template <typename T>
template <typename Cb>
CancellableHandle<void> CancellableHandle<T>::then_on_main(Cb&& cb) {
    auto sf = std::make_shared<std::future<T>>(std::move(this->m_future));
    // move stop_source out; the original handle becomes inert
    // (its destructor's cancel() is a no-op on a moved-from stop_source)
    auto captured_stop = std::move(this->stop);
    auto stop_copy = captured_stop;  // shared with the bridging lambda

    auto future = Async::submit([sf, c = std::forward<Cb>(cb), s = stop_copy]() mutable {
        if constexpr(std::is_void_v<T>) {
            sf->get();
            auto status = s.stop_requested() ? Status::cancelled : Status::completed;
            Async::queue_main([c = std::move(c), status]() mutable { c(Result<void>{status}); });
        } else {
            auto val = sf->get();
            auto status = s.stop_requested() ? Status::cancelled : Status::completed;
            Async::queue_main(
                [c = std::move(c), v = std::move(val), status]() mutable { c(Result<T>{std::move(v), status}); });
        }
    });

    return CancellableHandle<void>(std::move(future), std::move(captured_stop));
}

}  // namespace Async
