// Copyright (c) 2026, WH, All rights reserved.
#include "AsyncPool.h"

#include "LaunchArgs.h"
#include "Logging.h"
#include "SyncCV.h"
#include "SyncJthread.h"
#include "SyncMutex.h"
#include "Thread.h"
#include "Timing.h"

#ifdef MCENGINE_PLATFORM_WASM
#include <emscripten/threading.h>
#endif

#include <algorithm>
#include <cassert>
#include <charconv>
#include <utility>
#include <vector>

namespace Async {
namespace {

using detail::StateBase;

thread_local bool tl_pool_thread{false};

void run(StateBase *task) noexcept {
    task->execute();
    task->complete();
    task->release();
}

size_t configured_thread_count() noexcept {
    size_t count = std::clamp<size_t>(McThread::get_logical_cpu_count() - 1, 2, 32);
#ifdef MCENGINE_PLATFORM_WASM
    // the browser side preallocates a fixed worker pool (PTHREAD_POOL_SIZE), leave room for the other engine threads
    count = std::min<size_t>(count, 8);
#endif
    if(const auto arg = Mc::LaunchArgs::has_arg(Mc::LaunchArgs::MISC_ASYNC_THREADS); arg && !arg->empty()) {
        size_t requested{0};
        if(std::from_chars(arg->data(), arg->data() + arg->size(), requested).ec == std::errc{})
            count = std::clamp<size_t>(requested, 2, 64);
    }
    return count;
}

// fifo threaded through the states' next pointers: queueing allocates nothing
struct Queue {
    StateBase *head{nullptr};
    StateBase *tail{nullptr};

    [[nodiscard]] bool empty() const noexcept { return head == nullptr; }

    void push(StateBase *task) noexcept {
        task->next = nullptr;
        if(tail) {
            tail->next = task;
        } else {
            head = task;
        }
        tail = task;
    }

    StateBase *pop() noexcept {
        StateBase *task = head;
        head = task->next;
        if(!head) tail = nullptr;
        return task;
    }

    bool remove(StateBase *task) noexcept {
        StateBase *prev = nullptr;
        for(StateBase *cur = head; cur; prev = cur, cur = cur->next) {
            if(cur != task) continue;
            if(prev) {
                prev->next = cur->next;
            } else {
                head = cur->next;
            }
            if(tail == cur) tail = prev;
            return true;
        }
        return false;
    }

    // detach the whole list, to be walked outside the lock
    StateBase *take_all() noexcept {
        tail = nullptr;
        return std::exchange(head, nullptr);
    }
};

class Pool final {
    NOCOPY_NOMOVE(Pool)
   public:
    Pool() {
        const size_t total = configured_thread_count();
        m_bgCount = std::clamp<size_t>(total / 4, 1, 8);
        m_fgCount = total - m_bgCount;
        m_stealCap = std::max<size_t>(1, m_fgCount / 2);

        m_threads.reserve(total);
        for(size_t i = 0; i < m_fgCount; i++) {
            m_threads.emplace_back([this, i] { worker_loop(Lane::Foreground, i); });
        }
        for(size_t i = 0; i < m_bgCount; i++) {
            m_threads.emplace_back([this, i] { worker_loop(Lane::Background, i); });
        }

        debugLog("AsyncPool: started {} worker threads ({} fg, {} bg, up to {} fg helping with bg work)", total,
                 m_fgCount, m_bgCount, m_stealCap);
    }

    ~Pool() { shutdown(); }

    void shutdown() {
        {
            Sync::scoped_lock lock(m_mutex);
            if(m_shutdown) return;
            m_shutdown = true;
        }
        m_fgCV.notify_all();
        m_bgCV.notify_all();
        m_threads.clear();  // joins; the workers drain both lanes first

        // main-thread continuations that fired during the drain have no frame left to run in
        StateBase *leftover = nullptr;
        {
            Sync::scoped_lock lock(m_mutex);
            leftover = m_mainQueue.take_all();
            m_joined = true;
        }
        size_t dropped = 0;
        while(leftover) {
            StateBase *next = leftover->next;
            leftover->skip();
            leftover->complete();
            leftover->release();
            leftover = next;
            dropped++;
        }
        if(dropped > 0) debugLog("AsyncPool: dropped {} main-thread continuation(s) at shutdown", dropped);
    }

    void enqueue(StateBase *task) {
        if(task->on_main) {
            enqueue_main(task);
            return;
        }
        bool queued = false;
        {
            Sync::scoped_lock lock(m_mutex);
            if(!m_shutdown) {
                (task->lane == Lane::Foreground ? m_fgQueue : m_bgQueue).push(task);
                m_pending++;
                queued = true;
            }
        }
        if(!queued) {
            run(task);  // no workers anymore: nothing is ever left unexecuted
            return;
        }
        if(task->lane == Lane::Foreground) {
            m_fgCV.notify_one();
        } else {
            m_bgCV.notify_one();
            m_fgCV.notify_one();  // an fg worker may take it
        }
    }

    bool cancel_queued(StateBase *task) {
        if(task->on_main || task->done()) return false;  // main-thread continuations always run
        {
            Sync::scoped_lock lock(m_mutex);
            if(!(task->lane == Lane::Foreground ? m_fgQueue : m_bgQueue).remove(task)) return false;
            m_pending--;
        }
        task->skip();
        task->complete();
        task->release();
        return true;
    }

    // for a waiting pool thread: any queued task, from the given lane first
    StateBase *try_pop_any(Lane first) {
        Sync::scoped_lock lock(m_mutex);
        Queue &primary = first == Lane::Foreground ? m_fgQueue : m_bgQueue;
        Queue &secondary = first == Lane::Foreground ? m_bgQueue : m_fgQueue;
        if(!primary.empty()) return pop_locked(primary);
        if(!secondary.empty()) return pop_locked(secondary);
        return nullptr;
    }

    void update() {
        StateBase *task = nullptr;
        {
            Sync::scoped_lock lock(m_mutex);
            task = m_mainQueue.take_all();
        }
        while(task) {
            StateBase *next = task->next;  // run() may free it
            run(task);
            task = next;
        }
    }

    [[nodiscard]] size_t thread_count() const noexcept { return m_fgCount + m_bgCount; }
    [[nodiscard]] size_t pending_count() const {
        Sync::scoped_lock lock(m_mutex);
        return m_pending;
    }

   private:
    void enqueue_main(StateBase *task) {
        {
            Sync::scoped_lock lock(m_mutex);
            if(!m_joined) {
                m_mainQueue.push(task);
                return;
            }
        }
        run(task);  // only the main thread is left
    }

    StateBase *pop_locked(Queue &queue) {
        m_pending--;
        return queue.pop();
    }

    void worker_loop(Lane lane, size_t index) noexcept {
        tl_pool_thread = true;
        {
            const std::string name = fmt::format("async_{}_{}", lane == Lane::Foreground ? "fg" : "bg", index);
            McThread::set_current_thread_name(name.c_str());
            McThread::set_current_thread_prio(McThread::Priority::NORMAL);
        }

        const bool fg = lane == Lane::Foreground;
        while(true) {
            StateBase *task = nullptr;
            bool stolen = false;  // an fg worker running a bg task
            {
                Sync::unique_lock<Sync::mutex> lock(m_mutex);
                if(fg) {
                    m_fgCV.wait(lock, [this] {
                        return !m_fgQueue.empty() || (!m_bgQueue.empty() && m_stealing < m_stealCap) || m_shutdown;
                    });
                    if(!m_fgQueue.empty()) {
                        task = pop_locked(m_fgQueue);
                    } else if(!m_bgQueue.empty() && m_stealing < m_stealCap) {
                        task = pop_locked(m_bgQueue);
                        stolen = true;
                        m_stealing++;
                    }
                } else {
                    m_bgCV.wait(lock, [this] { return !m_bgQueue.empty() || m_shutdown; });
                    if(!m_bgQueue.empty()) task = pop_locked(m_bgQueue);
                }
                if(!task) return;  // shutting down and nothing left for this lane
            }

            run(task);

            if(stolen) {
                bool more = false;
                {
                    Sync::scoped_lock lock(m_mutex);
                    m_stealing--;
                    more = !m_bgQueue.empty();
                }
                if(more) m_fgCV.notify_one();  // an fg worker that hit the cap may take the next one
            }
        }
    }

    // everything below the mutex is guarded by it
    mutable Sync::mutex m_mutex;
    Sync::condition_variable m_fgCV;
    Sync::condition_variable m_bgCV;
    Queue m_fgQueue;
    Queue m_bgQueue;
    Queue m_mainQueue;
    size_t m_pending{0};   // lane-queued tasks not yet started
    size_t m_stealing{0};  // fg workers currently running bg tasks
    bool m_shutdown{false};
    bool m_joined{false};

    size_t m_fgCount{0};
    size_t m_bgCount{0};
    size_t m_stealCap{0};  // how many fg workers may run bg tasks at once

    std::vector<Sync::jthread> m_threads;
};

Pool &pool() {
    static Pool instance;
    return instance;
}

}  // namespace

namespace detail {

void StateBase::fire() noexcept { pool().enqueue(this); }

void StateBase::wait() const noexcept {
    if(done()) return;

    // a main-thread continuation only runs inside Async::update(), which this wait would be blocking
    assert(!(on_main && McThread::is_main_thread()) && "waiting on a then_on_main future from the main thread");

    if(tl_pool_thread) {
        // run other queued work while waiting (the sub-tasks this one might be waiting for are among it),
        // so a task blocking on a task can never take a thread out of the pool
        while(!done()) {
            if(StateBase *task = pool().try_pop_any(lane)) {
                run(task);
            } else if(StateBase *link = m_link.load(std::memory_order_acquire); link != this) {
                m_link.wait(link, std::memory_order_acquire);
            }
        }
        return;
    }

#ifdef MCENGINE_PLATFORM_WASM
    // on WASM, a blocking wait on the main thread can deadlock if the worker needs to proxy calls back.
    // drain the pthreads proxy queue while spinning so those calls can complete.
    if(McThread::is_main_thread()) {
        while(!done()) {
            emscripten_main_thread_process_queued_calls();
            Timing::sleepMS(1);
        }
        return;
    }
#endif

    for(StateBase *link = m_link.load(std::memory_order_acquire); link != this;
        link = m_link.load(std::memory_order_acquire)) {
        m_link.wait(link, std::memory_order_acquire);
    }
}

void enqueue(StateBase *task) noexcept { pool().enqueue(task); }
bool cancel_queued(StateBase *task) noexcept { return pool().cancel_queued(task); }

}  // namespace detail

size_t get_thread_count() noexcept { return pool().thread_count(); }
size_t pending_count() noexcept { return pool().pending_count(); }
void update() { pool().update(); }
void shutdown() { pool().shutdown(); }

}  // namespace Async
