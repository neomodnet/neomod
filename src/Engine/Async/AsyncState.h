// Copyright (c) 2026, WH, All rights reserved.
#pragma once

// the object a submitted callable turns into, shared by the pool that runs it and the Future that observes it.
// everything in here is an implementation detail of AsyncPool.h/AsyncFuture.h; nothing is meant to be used directly.

#include "noinclude.h"
#include "types.h"
#include "AsyncTypes.h"

#include <atomic>
#include <cassert>
#include <optional>
#include <type_traits>
#include <utility>

namespace Async::detail {

class StateBase;

// pool entry points (AsyncPool.cpp)
// schedule on its lane, or for the next update() if on_main. takes over the execution ref
void enqueue(StateBase *task) noexcept;
// drop a lane-queued task before it starts (skip + complete)
bool cancel_queued(StateBase *task) noexcept;

// one heap object per submitted callable: the pool's queue entry, and the completion state its Future observes.
// refcounted: one ref for the pending execution (released by whoever runs it), one for the Future (if any),
// one per continuation that captured it.
class StateBase {
    NOCOPY_NOMOVE(StateBase)
   public:
    StateBase(Lane on_lane, bool main_thread) noexcept : lane(on_lane), on_main(main_thread) {}
    virtual ~StateBase() = default;

    // run the callable and store its result (does not complete())
    virtual void execute() noexcept = 0;
    // store a default result without running (cancelled before it started, or dropped at shutdown)
    virtual void skip() noexcept = 0;
    // the state this one is registered on has completed: schedule this one.
    // enqueues on the recorded lane (or the main queue); when_all overrides it with a countdown.
    virtual void fire() noexcept;

    // publish the result: wakes waiters and fires the registered continuation
    void complete() noexcept {
        StateBase *const prev = m_link.exchange(this, std::memory_order_acq_rel);
        assert(prev != this && "state completed twice");
        m_link.notify_all();
        if(prev != nullptr) prev->fire();
    }

    // register c to be fired once this state completes (immediately if it already has).
    // at most one continuation per state: Future::then consumes the future.
    void set_continuation(StateBase *c) noexcept {
        StateBase *expected = nullptr;
        if(m_link.compare_exchange_strong(expected, c, std::memory_order_acq_rel, std::memory_order_acquire)) return;
        assert(expected == this && "a state can only have one continuation");
        c->fire();
    }

    [[nodiscard]] bool done() const noexcept { return m_link.load(std::memory_order_acquire) == this; }

    // block until done(). on a pool thread this runs other queued tasks in the meantime, so a task that
    // waits on a sub-task can never wedge the pool. (AsyncPool.cpp)
    void wait() const noexcept;

    void add_ref() noexcept { m_refs.fetch_add(1, std::memory_order_relaxed); }
    void release() noexcept {
        if(m_refs.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }

    Lane lane;
    bool on_main;              // run by Async::update() on the main thread instead of a worker
    StateBase *next{nullptr};  // the pool's queues are threaded through the states themselves

   private:
    std::atomic<u32> m_refs{1};
    // nullptr: pending, no continuation; this: completed; otherwise the continuation waiting to be fired
    // (a state is never its own continuation, so its address doubles as the completed marker)
    std::atomic<StateBase *> m_link{nullptr};
};

template <typename T>
class State : public StateBase {
   public:
    using StateBase::StateBase;

    [[nodiscard]] T take() noexcept {
        assert(m_value.has_value() && "no result stored");
        return std::move(*m_value);
    }

    void skip() noexcept override {
        if constexpr(std::is_default_constructible_v<T>) m_value.emplace();
    }

   protected:
    std::optional<T> m_value;
};

template <>
class State<void> : public StateBase {
   public:
    using StateBase::StateBase;

    void skip() noexcept override {}
};

template <typename F, typename T = std::invoke_result_t<F>>
class Task final : public State<T> {
   public:
    Task(F fn, Lane lane, bool on_main) : State<T>(lane, on_main), m_fn(std::move(fn)) {}

    void execute() noexcept override {
        if constexpr(std::is_void_v<T>) {
            m_fn();
        } else {
            this->m_value.emplace(m_fn());
        }
    }

   private:
    F m_fn;
};

template <typename F>
auto *make_task(F &&fn, Lane lane, bool on_main = false) {
    return new Task<std::decay_t<F>>(std::forward<F>(fn), lane, on_main);
}

// owning, move-only reference to a state
template <typename S>
class Ref {
   public:
    Ref() noexcept = default;
    explicit Ref(S *s) noexcept : m_s(s) {}  // adopts one reference
    ~Ref() { reset(); }

    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
    Ref(Ref &&o) noexcept : m_s(std::exchange(o.m_s, nullptr)) {}
    Ref &operator=(Ref &&o) noexcept {
        if(this != &o) {
            reset();
            m_s = std::exchange(o.m_s, nullptr);
        }
        return *this;
    }

    void reset() noexcept {
        if(S *s = std::exchange(m_s, nullptr)) s->release();
    }
    [[nodiscard]] S *get() const noexcept { return m_s; }
    S *operator->() const noexcept { return m_s; }
    explicit operator bool() const noexcept { return m_s != nullptr; }

   private:
    S *m_s{nullptr};
};

template <typename T, typename Cb>
struct then_result {
    using type = std::invoke_result_t<Cb, T>;
};
template <typename Cb>
struct then_result<void, Cb> {
    using type = std::invoke_result_t<Cb>;
};
template <typename T, typename Cb>
using then_result_t = typename then_result<T, Cb>::type;

// the continuation task for cb on ante: registered on ante, returned holding one extra ref for the caller's Future
template <typename T, typename Cb>
auto *continue_with(Ref<State<T>> ante, Cb &&cb, Lane lane, bool on_main) {
    State<T> *raw = ante.get();
    auto *task = make_task(
        [a = std::move(ante), c = std::forward<Cb>(cb)]() mutable -> then_result_t<T, Cb> {
            if constexpr(std::is_void_v<T>) {
                return c();
            } else {
                return c(a->take());
            }
        },
        lane, on_main);
    task->add_ref();
    raw->set_continuation(task);
    return task;
}

}  // namespace Async::detail
