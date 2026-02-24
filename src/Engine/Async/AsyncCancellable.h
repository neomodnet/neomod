// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "AsyncFuture.h"
#include "SyncStoptoken.h"

namespace Async {

// returned by submit_cancellable / submit_then_cancellable; holds future + stop source.
// auto-cancels on destruction/move-assignment (signals stop, but does not block).
// callers who need to block should call wait() explicitly.
template <typename T>
struct CancellableHandle : public Future<T> {
    Sync::stop_source stop;

    CancellableHandle() noexcept = default;
    CancellableHandle(Future<T> &&f, Sync::stop_source s) noexcept : Future<T>(std::move(f)), stop(std::move(s)) {}

    ~CancellableHandle() { this->cancel(); }

    CancellableHandle(const CancellableHandle &) = delete;
    CancellableHandle &operator=(const CancellableHandle &) = delete;

    CancellableHandle(CancellableHandle &&) noexcept = default;
    CancellableHandle &operator=(CancellableHandle &&other) noexcept {
        if(this != &other) {
            this->cancel();
            Future<T>::operator=(std::move(other));
            this->stop = std::move(other.stop);
        }
        return *this;
    }

    void cancel() { this->stop.request_stop(); }

    // continuation with cancellation status: queues cb(Result<T>) for the next main-thread update tick.
    // consumes this handle (future + stop_source moved out; original becomes inert).
    // returns CancellableHandle<void> that preserves cancel-on-destroy semantics.
    // declared here, defined in AsyncPool.h (needs Async::submit + Async::queue_main).
    template <typename Cb>
    CancellableHandle<void> then_on_main(Cb &&cb);
};

}  // namespace Async
