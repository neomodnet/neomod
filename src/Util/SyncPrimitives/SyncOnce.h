// Copyright (c) 2025, WH, All rights reserved.
#pragma once
// call_once + once_flag

#include "config.h"

#ifdef USE_NSYNC
#include "nsync_once.h"

#include <atomic>
#include <tuple>
#include <utility>
#else
#include <mutex>
#endif  // USE_NSYNC

namespace Sync {
#ifdef USE_NSYNC
using namespace nsync;

// ===================================================================
// nsync_once_flag: wrapper for nsync_once
// ===================================================================
class nsync_once_flag {
   private:
    nsync_once m_flag{};

   public:
    constexpr nsync_once_flag() noexcept = default;
    ~nsync_once_flag() = default;

    nsync_once_flag(const nsync_once_flag&) = delete;
    nsync_once_flag& operator=(const nsync_once_flag&) = delete;
    nsync_once_flag(nsync_once_flag&&) = delete;
    nsync_once_flag& operator=(nsync_once_flag&&) = delete;

    nsync_once* native_handle() noexcept { return &m_flag; }
};

using once_flag = nsync_once_flag;

// ===================================================================
// call_once: execute callable exactly once
// ===================================================================
template <typename Callable, typename... Args>
void call_once(once_flag& flag, Callable&& f, Args&&... args) {
    // check if already completed (2)
    if(flag.native_handle()->load(std::memory_order_acquire) == 2) {
        return;
    }

    struct wrapper_t {
        Callable&& func;
        std::tuple<Args&&...> args_tuple;

        void invoke() {
            if constexpr(sizeof...(Args) == 0) {
                std::forward<Callable>(func)();
            } else {
                std::apply(std::forward<Callable>(func), std::move(args_tuple));
            }
        }

        static void static_invoke(void* arg) { static_cast<wrapper_t*>(arg)->invoke(); }
    };

    wrapper_t wrapper{std::forward<Callable>(f), std::forward_as_tuple(std::forward<Args>(args)...)};

    nsync_run_once_arg(flag.native_handle(), &wrapper_t::static_invoke, &wrapper);
}

#else
using std::call_once;
using std::once_flag;
#endif  // USE_NSYNC
}  // namespace Sync
