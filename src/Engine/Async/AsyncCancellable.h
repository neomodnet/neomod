// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "AsyncFuture.h"
#include "SyncStoptoken.h"

namespace Async {

// returned by submit_cancellable / submit_then_cancellable; holds future + stop source.
// auto-cancels and waits on destruction/move-assignment (like jthread).
template <typename T>
struct CancellableHandle : public Future<T> {
    Sync::stop_source stop;

    CancellableHandle() noexcept = default;
    CancellableHandle(Future<T> &&f, Sync::stop_source s) noexcept : Future<T>(std::move(f)), stop(std::move(s)) {}

    ~CancellableHandle() {
        this->cancel();
        if(this->valid()) this->wait();
    }

    CancellableHandle(const CancellableHandle &) = delete;
    CancellableHandle &operator=(const CancellableHandle &) = delete;

    CancellableHandle(CancellableHandle &&) noexcept = default;
    CancellableHandle &operator=(CancellableHandle &&other) noexcept {
        if(this != &other) {
            this->cancel();
            if(this->valid()) this->wait();
            Future<T>::operator=(std::move(other));
            this->stop = std::move(other.stop);
        }
        return *this;
    }

    void cancel() { this->stop.request_stop(); }
};

}  // namespace Async
