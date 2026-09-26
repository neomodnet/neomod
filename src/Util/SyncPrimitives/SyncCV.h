// Copyright (c) 2025, WH, All rights reserved.
#pragma once
// condition_variable + condition_variable_any

#include "config.h"

#include "SyncMutex.h"
#include "SyncStoptoken.h"

#ifdef USE_NSYNC
#include "nsync_cv.h"

#include <type_traits>
#include <utility>
#endif  // USE_NSYNC

#include <chrono>
#include <condition_variable>

namespace Sync {
#ifdef USE_NSYNC
using namespace nsync;

// ===================================================================
// condition_variable_any: condition variable for any lockable type, stop_token waits wake on a stop request
// ===================================================================
class nsync_condition_variable_any_t {
   public:
    constexpr nsync_condition_variable_any_t() noexcept = default;
    ~nsync_condition_variable_any_t() = default;

    nsync_condition_variable_any_t(const nsync_condition_variable_any_t&) = delete;
    nsync_condition_variable_any_t& operator=(const nsync_condition_variable_any_t&) = delete;
    nsync_condition_variable_any_t(nsync_condition_variable_any_t&&) = delete;
    nsync_condition_variable_any_t& operator=(nsync_condition_variable_any_t&&) = delete;

    void notify_one() noexcept { nsync_cv_signal(&m_cv); }
    void notify_all() noexcept { nsync_cv_broadcast(&m_cv); }

    template <typename Lock>
    void wait(Lock& lock) {
        wait_impl(lock, nsync_time_no_deadline, nullptr);
    }

    template <typename Lock, typename Predicate>
    void wait(Lock& lock, Predicate pred) {
        while(!pred()) {
            wait(lock);
        }
    }

    template <typename Lock, typename Clock, typename Duration>
    std::cv_status wait_until(Lock& lock, const std::chrono::time_point<Clock, Duration>& abs_time) {
        return wait_until_impl(lock, abs_time, nullptr);
    }

    template <typename Lock, typename Clock, typename Duration, typename Predicate>
    bool wait_until(Lock& lock, const std::chrono::time_point<Clock, Duration>& abs_time, Predicate pred) {
        while(!pred()) {
            if(wait_until(lock, abs_time) == std::cv_status::timeout) {
                return pred();
            }
        }
        return true;
    }

    template <typename Lock, typename Rep, typename Period>
    std::cv_status wait_for(Lock& lock, const std::chrono::duration<Rep, Period>& rel_time) {
        return wait_until(lock, std::chrono::steady_clock::now() + rel_time);
    }

    template <typename Lock, typename Rep, typename Period, typename Predicate>
    bool wait_for(Lock& lock, const std::chrono::duration<Rep, Period>& rel_time, Predicate pred) {
        return wait_until(lock, std::chrono::steady_clock::now() + rel_time, std::move(pred));
    }

    template <typename Lock, typename Predicate>
    bool wait(Lock& lock, const stop_token& stoken, Predicate pred) {
        while(!stoken.stop_requested()) {
            if(pred()) {
                return true;
            }
            wait_impl(lock, nsync_time_no_deadline, stoken.native_handle());
        }
        return pred();
    }

    template <typename Lock, typename Clock, typename Duration, typename Predicate>
    bool wait_until(Lock& lock, const stop_token& stoken, const std::chrono::time_point<Clock, Duration>& abs_time,
                    Predicate pred) {
        while(!stoken.stop_requested()) {
            if(pred()) {
                return true;
            }
            if(wait_until_impl(lock, abs_time, stoken.native_handle()) == std::cv_status::timeout) {
                return pred();
            }
        }
        return pred();
    }

    template <typename Lock, typename Rep, typename Period, typename Predicate>
    bool wait_for(Lock& lock, const stop_token& stoken, const std::chrono::duration<Rep, Period>& rel_time,
                  Predicate pred) {
        return wait_until(lock, stoken, std::chrono::steady_clock::now() + rel_time, std::move(pred));
    }

   private:
    nsync_cv m_cv{};

    template <typename Lock, typename Deadline>
    void wait_impl(Lock& lock, Deadline deadline, nsync_note cancel) {
        if constexpr(std::is_same_v<Lock, unique_lock<mutex>>) {
            // given the nsync_mu itself, nsync can move woken waiters to its queue instead of waking them all
            nsync_cv_wait_with_deadline(&m_cv, lock.mutex()->native_handle(), deadline, cancel);
        } else {
            nsync_cv_wait_with_deadline_generic(
                &m_cv, &lock, [](void* l) { static_cast<Lock*>(l)->lock(); },
                [](void* l) { static_cast<Lock*>(l)->unlock(); }, deadline, cancel);
        }
    }

    template <typename Lock, typename Clock, typename Duration>
    std::cv_status wait_until_impl(Lock& lock, const std::chrono::time_point<Clock, Duration>& abs_time,
                                   nsync_note cancel) {
        const auto now = Clock::now();
        if(abs_time <= now) {
            return std::cv_status::timeout;
        }
        wait_impl(
            lock,
            std::chrono::system_clock::now() + std::chrono::ceil<std::chrono::system_clock::duration>(abs_time - now),
            cancel);
        // nsync deadlines are on the system clock, the caller's clock decides whether this was the timeout
        return Clock::now() < abs_time ? std::cv_status::no_timeout : std::cv_status::timeout;
    }
};

// ===================================================================
// condition_variable: condition_variable_any restricted to unique_lock<mutex>, like std::condition_variable
// ===================================================================
class nsync_condition_variable_t {
   public:
    constexpr nsync_condition_variable_t() noexcept = default;
    ~nsync_condition_variable_t() = default;

    nsync_condition_variable_t(const nsync_condition_variable_t&) = delete;
    nsync_condition_variable_t& operator=(const nsync_condition_variable_t&) = delete;
    nsync_condition_variable_t(nsync_condition_variable_t&&) = delete;
    nsync_condition_variable_t& operator=(nsync_condition_variable_t&&) = delete;

    void notify_one() noexcept { m_cv.notify_one(); }
    void notify_all() noexcept { m_cv.notify_all(); }

    void wait(unique_lock<mutex>& lock) { m_cv.wait(lock); }

    template <typename Predicate>
    void wait(unique_lock<mutex>& lock, Predicate pred) {
        m_cv.wait(lock, std::move(pred));
    }

    template <typename Clock, typename Duration>
    std::cv_status wait_until(unique_lock<mutex>& lock, const std::chrono::time_point<Clock, Duration>& abs_time) {
        return m_cv.wait_until(lock, abs_time);
    }

    template <typename Clock, typename Duration, typename Predicate>
    bool wait_until(unique_lock<mutex>& lock, const std::chrono::time_point<Clock, Duration>& abs_time,
                    Predicate pred) {
        return m_cv.wait_until(lock, abs_time, std::move(pred));
    }

    template <typename Rep, typename Period>
    std::cv_status wait_for(unique_lock<mutex>& lock, const std::chrono::duration<Rep, Period>& rel_time) {
        return m_cv.wait_for(lock, rel_time);
    }

    template <typename Rep, typename Period, typename Predicate>
    bool wait_for(unique_lock<mutex>& lock, const std::chrono::duration<Rep, Period>& rel_time, Predicate pred) {
        return m_cv.wait_for(lock, rel_time, std::move(pred));
    }

   private:
    nsync_condition_variable_any_t m_cv;
};

// type aliases matching standard library
using condition_variable = nsync_condition_variable_t;
using condition_variable_any = nsync_condition_variable_any_t;

#else
// standard library fallback
using condition_variable = std::condition_variable;
using condition_variable_any = std::condition_variable_any;
#endif

}  // namespace Sync
