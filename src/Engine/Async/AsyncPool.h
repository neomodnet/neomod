// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "AsyncTypes.h"
#include "AsyncFuture.h"
#include "AsyncCancellable.h"

#include <cassert>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// fixed-size worker pool with two lanes: Foreground for short/frame-adjacent work, Background for long-running
// work. fg workers also take bg work, but at least half of them always stay free for fg tasks; bg workers never
// take fg work. waiting on a future from a pool thread runs other queued tasks in the meantime, so tasks may
// block on their own sub-tasks. continuations (Future::then etc.) never occupy a thread while pending.
namespace Async {

[[nodiscard]] size_t get_thread_count() noexcept;
[[nodiscard]] size_t pending_count() noexcept;  // queued, not yet started

// run the main-thread continuations that became ready since the last call (Engine::onUpdate)
void update();
// finish all queued work and join the workers. anything submitted afterwards runs inline on the caller.
void shutdown();

// submit work, get a future back
template <typename F>
auto submit(F &&f, Lane lane = Lane::Foreground) -> Future<std::invoke_result_t<F>> {
    auto *task = detail::make_task(std::forward<F>(f), lane);
    task->add_ref();  // the future's
    detail::enqueue(task);
    return detail::FutureAccess::adopt(task);
}

// fire-and-forget
template <typename F>
void dispatch(F &&f, Lane lane = Lane::Foreground) {
    detail::enqueue(detail::make_task(std::forward<F>(f), lane));
}

// run f on the main thread during the next Async::update()
template <typename F>
void queue_main(F &&f) {
    detail::enqueue(detail::make_task(std::forward<F>(f), Lane::Foreground, true));
}

// cancellable submit: the callable receives a const Sync::stop_token& and should check stop_requested()
// periodically. cancelling before the task starts skips it entirely (its result is default-constructed).
template <typename F>
auto submit_cancellable(F &&f, Lane lane = Lane::Foreground)
    -> CancellableHandle<std::invoke_result_t<F, const Sync::stop_token &>> {
    using T = std::invoke_result_t<F, const Sync::stop_token &>;
    static_assert(std::is_void_v<T> || std::is_default_constructible_v<T>,
                  "a task cancelled before it starts yields a default-constructed result");

    Sync::stop_source source;
    auto future = submit(
        [func = std::forward<F>(f), tok = source.get_token()]() mutable -> T {
            // cancelled after being taken off the queue, before starting
            if(tok.stop_requested()) {
                if constexpr(std::is_void_v<T>) {
                    return;
                } else {
                    return T{};
                }
            }
            return func(tok);
        },
        lane);

    return CancellableHandle<T>(std::move(future), std::move(source));
}

// ---------------------------------------------------------------------------
// make_ready_future: create a future that is immediately ready
// ---------------------------------------------------------------------------

template <typename T>
Future<std::decay_t<T>> make_ready_future(T &&value) {
    auto *task = detail::make_task([v = std::forward<T>(value)]() mutable { return std::move(v); }, Lane::Foreground);
    task->execute();
    task->complete();
    return detail::FutureAccess::adopt(task);  // the execution ref becomes the future's
}

inline Future<void> make_ready_future() {
    auto *task = detail::make_task([] {}, Lane::Foreground);
    task->execute();
    task->complete();
    return detail::FutureAccess::adopt(task);
}

// ---------------------------------------------------------------------------
// wait_all: block until all futures are ready
// ---------------------------------------------------------------------------

template <typename T>
void wait_all(std::vector<Future<T>> &futures) {
    for(auto &f : futures) f.wait();
}

template <typename... Ts>
void wait_all(Future<Ts> &...futures) {
    (futures.wait(), ...);
}

// ---------------------------------------------------------------------------
// when_all: compose multiple futures into one
// ---------------------------------------------------------------------------

namespace detail {

template <typename T, typename Fn>
void for_each_future(std::vector<Future<T>> &futures, Fn fn) {
    for(auto &f : futures) fn(f);
}
template <typename... Ts, typename Fn>
void for_each_future(std::tuple<Future<Ts>...> &futures, Fn fn) {
    std::apply([&fn](auto &...f) { (fn(f), ...); }, futures);
}

// the results of a group of completed futures (none of the get()s block)
template <typename T>
    requires(!std::is_void_v<T>)
std::vector<T> collect_results(std::vector<Future<T>> &futures) {
    std::vector<T> results;
    results.reserve(futures.size());
    for(auto &f : futures) results.push_back(f.get());
    return results;
}
inline void collect_results(std::vector<Future<void>> &futures) {
    for(auto &f : futures) f.get();
}
template <typename... Ts>
std::tuple<Ts...> collect_results(std::tuple<Future<Ts>...> &futures) {
    return std::apply([](auto &...f) { return std::make_tuple(f.get()...); }, futures);
}

// owns a group of futures and completes with their collected results once the last of them has
template <typename Inputs>
class WhenAll final : public State<decltype(collect_results(std::declval<Inputs &>()))> {
    using R = decltype(collect_results(std::declval<Inputs &>()));

   public:
    WhenAll(Inputs inputs, Lane lane) : State<R>(lane, false), m_inputs(std::move(inputs)) {}

    // register on every input. the last one to complete fires this (start() itself, if there are none)
    void start() noexcept {
        for_each_future(m_inputs, [this](auto &f) {
            assert(f.valid() && "when_all() on an invalid future");
            m_remaining.fetch_add(1, std::memory_order_relaxed);
            FutureAccess::state(f)->set_continuation(this);
        });
        fire();
    }

    void execute() noexcept override {
        if constexpr(std::is_void_v<R>) {
            collect_results(m_inputs);
        } else {
            this->m_value.emplace(collect_results(m_inputs));
        }
    }

    void fire() noexcept override {
        if(m_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) State<R>::fire();
    }

   private:
    Inputs m_inputs;
    std::atomic<u32> m_remaining{1};  // inputs still pending, plus one held by start() until all are registered
};

template <typename Inputs>
auto when_all(Inputs inputs, Lane lane) {
    auto *task = new WhenAll<Inputs>(std::move(inputs), lane);
    task->add_ref();  // the future's
    task->start();
    return FutureAccess::adopt(task);
}

}  // namespace detail

// homogeneous vector of non-void futures
template <typename T>
    requires(!std::is_void_v<T>)
auto when_all(std::vector<Future<T>> &&futures, Lane lane = Lane::Foreground) -> Future<std::vector<T>> {
    return detail::when_all(std::move(futures), lane);
}

// homogeneous vector of void futures
inline auto when_all(std::vector<Future<void>> &&futures, Lane lane = Lane::Foreground) -> Future<void> {
    return detail::when_all(std::move(futures), lane);
}

// heterogeneous variadic (different types, all non-void)
template <typename T1, typename T2, typename... Rest>
auto when_all(Future<T1> &&f1, Future<T2> &&f2, Future<Rest> &&...rest) -> Future<std::tuple<T1, T2, Rest...>> {
    return detail::when_all(std::make_tuple(std::move(f1), std::move(f2), std::move(rest)...), Lane::Foreground);
}

}  // namespace Async
