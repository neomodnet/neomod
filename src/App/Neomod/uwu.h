#pragma once

#include "UString.h"
#include "Thread.h"
#include "SyncMutex.h"
#include "SyncCV.h"
#include "SyncJthread.h"

#include <optional>
#include <atomic>

// Unified Wait Utilities (◕‿◕✿)
namespace uwu {

// Promise for queuing work where only the latest enqueued function runs.
// Older pending functions are discarded in favor of newer ones.
//
// Func must be a callable returning Ret. To pass arguments, capture them
// in a lambda at the call site:
//   promise.enqueue([arg1, &arg2]() { return compute(arg1, arg2); });
template <typename Func>
struct lazy_promise {
    using Ret = std::invoke_result_t<Func>;

    lazy_promise()
        requires(std::is_default_constructible_v<Ret>)
    = default;
    lazy_promise()
        requires(!std::is_default_constructible_v<Ret>)
    = delete;

    lazy_promise(Ret default_ret) : ret(std::move(default_ret)) {}

    void enqueue(Func &&func) {
        {
            Sync::scoped_lock lock(this->work_mtx);
            if(!this->thread.joinable()) {
                ++threads_created;
                this->thread = Sync::jthread([this](const Sync::stop_token &stoken) { this->run(stoken); });
            }
            this->pending = std::move(func);
        }
        this->cv.notify_one();
    }

    // Returns locked access to the result. Hold the lock while reading.
    std::pair<Sync::unique_lock<Sync::mutex>, const Ret &> get() {
        Sync::unique_lock<Sync::mutex> lock(this->ret_mtx);
        return {std::move(lock), this->ret};
    }

    // Non-blocking: returns a copy if lockable, nullopt otherwise.
    std::optional<Ret> try_get() {
        Sync::unique_lock<Sync::mutex> lock(this->ret_mtx, Sync::try_to_lock);
        if(!lock.owns_lock()) return std::nullopt;
        return this->ret;
    }

    void set(Ret &&new_ret) {
        Sync::scoped_lock lock(this->ret_mtx);
        this->ret = std::move(new_ret);
        this->generation.fetch_add(1, std::memory_order_release);
    }

    bool has_pending() const {
        Sync::scoped_lock lock(this->work_mtx);
        return this->pending.has_value();
    }

    // Monotonically increasing counter, incremented each time the result updates.
    uint64_t get_generation() const { return this->generation.load(std::memory_order_acquire); }

   private:
    static inline uint64_t threads_created{0};

    void run(const Sync::stop_token &stoken) {
        McThread::set_current_thread_name(fmt::format("lazy_promise{}", threads_created % 128));  // just for uniqueness
        McThread::set_current_thread_prio(McThread::Priority::LOW);

        while(!stoken.stop_requested()) {
            std::optional<Func> current;
            {
                Sync::unique_lock<Sync::mutex> lock(this->work_mtx);
                this->cv.wait(lock, stoken, [this]() { return this->pending.has_value(); });

                if(stoken.stop_requested()) break;
                if(!this->pending.has_value()) continue;

                current = std::move(this->pending);
                this->pending.reset();
            }

            Ret result = (*current)();

            {
                Sync::scoped_lock lock(this->ret_mtx);
                this->ret = std::move(result);
                this->generation.fetch_add(1, std::memory_order_release);
            }
        }
    }

    mutable Sync::mutex work_mtx;
    Sync::condition_variable_any cv;
    std::optional<Func> pending;

    Sync::mutex ret_mtx;
    Ret ret;

    Sync::jthread thread;

    std::atomic<uint64_t> generation{0};
};

};  // namespace uwu
