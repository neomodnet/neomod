// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "AsyncFuture.h"
#include "SyncStoptoken.h"

namespace Async {

// returned by submit_cancellable / submit_then_cancellable; holds future + stop source
template <typename T>
struct CancellableHandle : public Future<T> {
    Sync::stop_source stop;

    void cancel() { stop.request_stop(); }
};

}  // namespace Async
