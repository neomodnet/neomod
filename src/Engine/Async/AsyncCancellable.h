// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "AsyncFuture.h"
#include "SyncStoptoken.h"

namespace Async {

// returned by submit_cancellable: a Future plus the stop_source of its task.
// cancel() signals stop and, if the task hasn't started yet, drops it from the queue (its result is then
// default-constructed). auto-cancels on destruction/move-assignment without blocking; call wait() to block.
template <typename T>
class CancellableHandle : public Future<T> {
   public:
    CancellableHandle() noexcept = default;
    CancellableHandle(Future<T> &&future, Sync::stop_source stop) noexcept
        : Future<T>(std::move(future)), m_stop(std::move(stop)) {}
    ~CancellableHandle() { cancel(); }

    CancellableHandle(const CancellableHandle &) = delete;
    CancellableHandle &operator=(const CancellableHandle &) = delete;
    CancellableHandle(CancellableHandle &&) noexcept = default;
    CancellableHandle &operator=(CancellableHandle &&other) noexcept {
        if(this != &other) {
            cancel();
            Future<T>::operator=(std::move(other));
            m_stop = std::move(other.m_stop);
        }
        return *this;
    }

    void cancel() noexcept {
        m_stop.request_stop();
        if(this->m_state) detail::cancel_queued(this->m_state.get());
    }

    // continuation with cancellation status: cb(Result<T>) runs on the main thread during Async::update() once
    // the task completes. it runs on cancellation too, with Status::cancelled (the status reflects whether stop
    // was requested by then, not whether the task noticed). consumes this handle; the returned handle keeps the
    // cancel-on-destroy semantics and is ready once cb has run.
    template <typename Cb>
    auto then_on_main(Cb &&cb) -> CancellableHandle<detail::then_result_t<Result<T>, Cb>> {
        using U = detail::then_result_t<Result<T>, Cb>;
        assert(this->valid() && "then_on_main() on an invalid handle");
        Sync::stop_source stop = std::move(m_stop);  // this handle becomes inert
        detail::State<U> *task = nullptr;
        if constexpr(std::is_void_v<T>) {
            task = detail::continue_with(
                std::move(this->m_state),
                [c = std::forward<Cb>(cb), s = stop]() mutable -> U {
                    return c(Result<void>{s.stop_requested() ? Status::cancelled : Status::completed});
                },
                Lane::Foreground, true);
        } else {
            task = detail::continue_with(
                std::move(this->m_state),
                [c = std::forward<Cb>(cb), s = stop](T v) mutable -> U {
                    return c(Result<T>{std::move(v), s.stop_requested() ? Status::cancelled : Status::completed});
                },
                Lane::Foreground, true);
        }
        return {detail::FutureAccess::adopt(task), std::move(stop)};
    }

   private:
    Sync::stop_source m_stop;
};

}  // namespace Async
