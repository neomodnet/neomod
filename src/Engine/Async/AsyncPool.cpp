// Copyright (c) 2026, WH, All rights reserved.
#include "AsyncPool.h"

#include "Thread.h"

#include "Logging.h"

#include <cassert>

AsyncPool& AsyncPool::get() {
    static AsyncPool instance{std::clamp<size_t>(McThread::get_logical_cpu_count() - 1, 2, 32)};
    return instance;
}

AsyncPool::AsyncPool(size_t thread_count) {
    const size_t bg = std::clamp<size_t>(thread_count / 4, 1, 8);
    const size_t fg = thread_count - bg;

    m_fgThreads.reserve(fg);
    for(size_t i = 0; i < fg; i++) {
        m_fgThreads.emplace_back([this, i]() { fg_worker_loop(i); });
    }

    m_bgThreads.reserve(bg);
    for(size_t i = 0; i < bg; i++) {
        m_bgThreads.emplace_back([this, i]() { bg_worker_loop(i); });
    }

    debugLog("AsyncPool: started {} worker threads ({} fg, {} bg)", thread_count, fg, bg);
}

AsyncPool::~AsyncPool() { shutdown(); }

void AsyncPool::shutdown() {
    {
        Sync::scoped_lock lock(m_workMutex);
        if(m_shutdown) return;
        m_shutdown = true;
    }
    m_fgCV.notify_all();
    m_bgCV.notify_all();

    m_fgThreads.clear();
    m_bgThreads.clear();
}

void AsyncPool::enqueue(std::unique_ptr<TaskBase> task, Lane lane) {
    {
        Sync::scoped_lock lock(m_workMutex);
        (lane == Lane::Foreground ? m_fgQueue : m_bgQueue).push(std::move(task));
    }
    m_pending.fetch_add(1, std::memory_order_relaxed);
    if(lane == Lane::Foreground) {
        m_fgCV.notify_one();
    } else {
        // wake a bg thread, and also a fg thread so it can work-steal
        m_bgCV.notify_one();
        m_fgCV.notify_one();
    }
}

void AsyncPool::fg_worker_loop(size_t index) noexcept {
    {
        const std::string thread_name = fmt::format("async_fg_{}", index);
        McThread::set_current_thread_name(thread_name.c_str());
        McThread::set_current_thread_prio(McThread::Priority::NORMAL);
    }

    while(true) {
        std::unique_ptr<TaskBase> task;
        {
            Sync::unique_lock<Sync::mutex> lock(m_workMutex);
            m_fgCV.wait(lock, [this] { return !m_fgQueue.empty() || !m_bgQueue.empty() || m_shutdown; });

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

void AsyncPool::bg_worker_loop(size_t index) noexcept {
    {
        const std::string thread_name = fmt::format("async_bg_{}", index);
        McThread::set_current_thread_name(thread_name.c_str());
        McThread::set_current_thread_prio(McThread::Priority::NORMAL);
    }

    while(true) {
        std::unique_ptr<TaskBase> task;
        {
            Sync::unique_lock<Sync::mutex> lock(m_workMutex);
            m_bgCV.wait(lock, [this] { return !m_bgQueue.empty() || m_shutdown; });

            if(m_bgQueue.empty()) return;

            task = std::move(m_bgQueue.front());
            m_bgQueue.pop();
        }

        task->execute();
        m_pending.fetch_sub(1, std::memory_order_relaxed);
    }
}

namespace Async {

auto when_all(std::vector<Future<void>>&& futures) -> Future<void> {
    auto sf = std::make_shared<std::vector<Future<void>>>(std::move(futures));
    return submit([sf]() {
        for(auto& f : *sf) f.get();
    });
}

}  // namespace Async
