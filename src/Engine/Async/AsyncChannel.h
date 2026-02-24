// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "SyncMutex.h"

#include <vector>
#include <utility>

namespace Async {

// minimal thread-safe MPSC queue
template <typename T>
class Channel {
   public:
    void push(T item) {
        Sync::scoped_lock lk(m_mtx);
        m_items.push_back(std::move(item));
    }

    std::vector<T> drain() {
        Sync::scoped_lock lk(m_mtx);
        std::vector<T> out;
        out.swap(m_items);
        return out;
    }

   private:
    mutable Sync::mutex m_mtx;
    std::vector<T> m_items;
};

}  // namespace Async
