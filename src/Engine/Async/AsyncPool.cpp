// Copyright (c) 2026, WH, All rights reserved.
#include "AsyncPool.h"

#include "Thread.h"
#include "UString.h"
#include "Logging.h"

#include <cassert>

static AsyncPool *s_pool{nullptr};

namespace Async {

AsyncPool &pool() {
    assert(s_pool && "Async::pool() called before AsyncPool was created");
    return *s_pool;
}

}  // namespace Async

AsyncPool::AsyncPool(size_t thread_count) {
    assert(!s_pool && "only one AsyncPool instance allowed");
    s_pool = this;

    const size_t bg = std::clamp<size_t>(thread_count / 4, 1, 8);
    const size_t fg = thread_count - bg;

    m_fgThreads.reserve(fg);
    for(size_t i = 0; i < fg; i++) {
        m_fgThreads.emplace_back([this](const Sync::stop_token &stoken, size_t index) { fg_worker_loop(stoken, index); }, i);
    }

    m_bgThreads.reserve(bg);
    for(size_t i = 0; i < bg; i++) {
        m_bgThreads.emplace_back([this](const Sync::stop_token &stoken, size_t index) { bg_worker_loop(stoken, index); }, i);
    }

    debugLog("AsyncPool: started {} worker threads ({} fg, {} bg)", thread_count, fg, bg);
}

AsyncPool::~AsyncPool() {
    shutdown();
    s_pool = nullptr;
}

void AsyncPool::shutdown() {
    {
        Sync::scoped_lock lock(m_workMutex);
        if(m_shutdown) return;
        m_shutdown = true;
    }
    m_fgCV.notify_all();
    m_bgCV.notify_all();

    // jthread destructors call request_stop() + join()
    m_fgThreads.clear();
    m_bgThreads.clear();
}

void AsyncPool::fg_worker_loop(const Sync::stop_token &stoken, size_t index) noexcept {
    McThread::set_current_thread_name(fmt::format("async_fg_{}", index));
    McThread::set_current_thread_prio(McThread::Priority::NORMAL);

    while(true) {
        std::unique_ptr<TaskBase> task;
        {
            Sync::unique_lock<Sync::mutex> lock(m_workMutex);
            m_fgCV.wait(lock, stoken, [this] { return !m_fgQueue.empty() || !m_bgQueue.empty() || m_shutdown; });

            // try foreground first, then steal from background
            if(!m_fgQueue.empty()) {
                task = std::move(m_fgQueue.front());
                m_fgQueue.pop();
            } else if(!m_bgQueue.empty()) {
                task = std::move(m_bgQueue.front());
                m_bgQueue.pop();
            } else {
                return;
            }
        }

        task->execute();
        m_pending.fetch_sub(1, std::memory_order_relaxed);
    }
}

void AsyncPool::bg_worker_loop(const Sync::stop_token &stoken, size_t index) noexcept {
    McThread::set_current_thread_name(fmt::format("async_bg_{}", index));
    McThread::set_current_thread_prio(McThread::Priority::NORMAL);

    while(true) {
        std::unique_ptr<TaskBase> task;
        {
            Sync::unique_lock<Sync::mutex> lock(m_workMutex);
            m_bgCV.wait(lock, stoken, [this] { return !m_bgQueue.empty() || m_shutdown; });

            if(m_bgQueue.empty()) return;

            task = std::move(m_bgQueue.front());
            m_bgQueue.pop();
        }

        task->execute();
        m_pending.fetch_sub(1, std::memory_order_relaxed);
    }
}
