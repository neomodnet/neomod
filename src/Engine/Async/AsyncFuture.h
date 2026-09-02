// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "AsyncState.h"

#include <cassert>
#include <type_traits>
#include <utility>

namespace Async {

namespace detail {
struct FutureAccess;
}

// the result of a task submitted to the pool. move-only; get() and then() consume it.
template <typename T>
class Future {
    static_assert(!std::is_reference_v<T>, "Future of a reference type is not supported");

   public:
    Future() noexcept = default;
    ~Future() = default;

    Future(const Future &) = delete;
    Future &operator=(const Future &) = delete;
    Future(Future &&) noexcept = default;
    Future &operator=(Future &&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(m_state); }
    [[nodiscard]] bool is_ready() const noexcept { return m_state && m_state->done(); }

    // block until ready. from a pool thread this runs other queued tasks in the meantime.
    void wait() const noexcept {
        assert(valid() && "wait() on an invalid future");
        if(m_state) m_state->wait();
    }

    // block until ready, then hand out the result; the future is invalid afterwards
    T get() noexcept {
        assert(valid() && "get() on an invalid future");
        detail::Ref<detail::State<T>> state = std::move(m_state);
        state->wait();
        if constexpr(!std::is_void_v<T>) return state->take();
    }

    // continuation: cb(result) runs on a pool thread once this completes; returns the future of its result.
    // consumes this future. no thread is blocked in the meantime.
    template <typename Cb>
    auto then(Cb &&cb, Lane lane = Lane::Foreground) -> Future<detail::then_result_t<T, Cb>> {
        assert(valid() && "then() on an invalid future");
        return Future<detail::then_result_t<T, Cb>>(
            detail::continue_with(std::move(m_state), std::forward<Cb>(cb), lane, false));
    }

    // continuation: cb(result) runs on the main thread during Async::update() once this completes.
    // consumes this future. the returned future is ready once cb has run, so never wait() on it from the
    // main thread (asserts), and from a pool task only while the main thread keeps updating.
    template <typename Cb>
    auto then_on_main(Cb &&cb) -> Future<detail::then_result_t<T, Cb>> {
        assert(valid() && "then_on_main() on an invalid future");
        return Future<detail::then_result_t<T, Cb>>(
            detail::continue_with(std::move(m_state), std::forward<Cb>(cb), Lane::Foreground, true));
    }

   protected:
    template <typename>
    friend class Future;
    friend struct detail::FutureAccess;

    explicit Future(detail::State<T> *state) noexcept : m_state(state) {}  // adopts one reference

    detail::Ref<detail::State<T>> m_state;
};

namespace detail {

// the pool's side of a Future: wrapping a state, and getting at the state of a future handed back in
struct FutureAccess {
    template <typename T>
    static Future<T> adopt(State<T> *state) noexcept {
        return Future<T>(state);
    }
    template <typename T>
    static State<T> *state(const Future<T> &future) noexcept {
        return future.m_state.get();
    }
};

}  // namespace detail

}  // namespace Async
