// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "AsyncPool.h"
#include "SyncMutex.h"
#include "SyncStoptoken.h"
#include "noinclude.h"

#include <cassert>
#include <type_traits>
#include <utility>
#include <vector>

namespace Async {

// Owns the work submitted through it, for work that has to end with whatever owns the scope (an object that goes
// away while the program runs, or code that gets unloaded). cancel(), and the destructor, request stop, drop what
// hasn't started (main-thread work included, which then never runs) and wait for what is running, so that nothing of
// it runs or exists afterwards. A task may take the scope's stop token. Its tasks must not wait for main-thread work,
// since cancel() may be waiting for them on the main thread.
class Scope {
    NOCOPY_NOMOVE(Scope)
   public:
    Scope() = default;
    ~Scope() { this->cancel(); }

    // like Async::submit(); f may take a const Sync::stop_token&
    template <typename F>
    auto submit(F &&f, Lane lane = Lane::Foreground) {
        auto fn = this->bind(std::forward<F>(f));
        static_assert(droppable<std::invoke_result_t<decltype(fn) &>>,
                      "a task dropped before it ran yields a default-constructed result");
        auto *task = detail::make_task(std::move(fn), lane);
        task->add_ref();  // the future's
        this->track(task, nullptr);
        detail::enqueue(task);
        return detail::FutureAccess::adopt(task);
    }

    // like Async::queue_main()
    template <typename F>
    void queue_main(F &&f) {
        auto *task = detail::make_task(std::forward<F>(f), Lane::Foreground, true);
        this->track(task, nullptr);
        detail::enqueue(task);
    }

    // like future.then() and future.then_on_main(), for any future (one from outside the scope too)
    template <typename T, typename Cb>
    auto then(Future<T> &&future, Cb &&cb, Lane lane = Lane::Foreground) -> Future<detail::then_result_t<T, Cb>> {
        return this->chain(std::move(future), std::forward<Cb>(cb), lane, false);
    }
    template <typename T, typename Cb>
    auto then_on_main(Future<T> &&future, Cb &&cb) -> Future<detail::then_result_t<T, Cb>> {
        return this->chain(std::move(future), std::forward<Cb>(cb), Lane::Foreground, true);
    }

    // stops and drops everything it was given, as the destructor does; the scope takes new work afterwards
    void cancel();

    [[nodiscard]] Sync::stop_token token() const {
        Sync::scoped_lock lock(this->mutex);
        return this->stop.get_token();
    }

   private:
    template <typename T>
    static constexpr bool droppable = std::is_void_v<T> || std::is_default_constructible_v<T>;

    template <typename F>
    auto bind(F &&f) {
        if constexpr(std::is_invocable_v<std::decay_t<F> &, const Sync::stop_token &>) {
            return [fn = std::forward<F>(f), tok = this->token()]() mutable { return fn(tok); };
        } else {
            return std::forward<F>(f);
        }
    }

    template <typename T, typename Cb>
    auto chain(Future<T> &&future, Cb &&cb, Lane lane, bool on_main) -> Future<detail::then_result_t<T, Cb>> {
        static_assert(droppable<detail::then_result_t<T, Cb>>,
                      "a task dropped before it ran yields a default-constructed result");
        assert(future.valid() && "a continuation of an invalid future");
        detail::State<T> *antecedent = detail::FutureAccess::state(future);
        // (registered on the antecedent right away: it may have run already by the time it's tracked)
        auto *task = detail::continue_with(detail::FutureAccess::take(future), std::forward<Cb>(cb), lane, on_main);
        this->track(task, antecedent);
        return detail::FutureAccess::adopt(task);
    }

    // keeps a reference of its own; `antecedent` is what it's registered on until it fires
    void track(detail::StateBase *task, detail::StateBase *antecedent);

    struct Owned {
        detail::Ref<detail::StateBase> task;
        detail::StateBase *antecedent;
    };
    mutable Sync::mutex mutex;
    std::vector<Owned> owned;
    Sync::stop_source stop;
};

}  // namespace Async
