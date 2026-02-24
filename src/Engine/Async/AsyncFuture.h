// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "config.h"
#include "AsyncTypes.h"

#include <chrono>
#include <future>
#include <memory>
#include <type_traits>

#ifdef MCENGINE_PLATFORM_WASM
extern "C" void emscripten_main_thread_process_queued_calls(void);
namespace McThread {
bool is_main_thread() noexcept;
}
#endif

namespace Async {

namespace detail {
template <typename T, typename Cb>
struct then_result {
    using type = std::invoke_result_t<Cb, T>;
};
template <typename Cb>
struct then_result<void, Cb> {
    using type = std::invoke_result_t<Cb>;
};
template <typename T, typename Cb>
using then_result_t = typename then_result<T, Cb>::type;
}  // namespace detail

template <typename T>
class Future {
   public:
    Future() noexcept = default;
    ~Future() = default;
    explicit Future(std::future<T>&& f) noexcept : m_future(std::move(f)) {}

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept { return m_future.valid(); }

    [[nodiscard]] bool is_ready() const {
        return m_future.valid() && m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    T get() { return m_future.get(); }

    void wait() const {
#ifdef MCENGINE_PLATFORM_WASM
        // on WASM, a blocking wait on the main thread can deadlock if the worker needs to proxy calls back.
        // drain the pthreads proxy queue while spinning so those calls can complete.
        if(McThread::is_main_thread()) {
            while(m_future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
                emscripten_main_thread_process_queued_calls();
            }
            return;
        }
#endif
        m_future.wait();
    }

    // continuation: runs cb(result) on a pool thread, returns Future<U>.
    // consumes this future (valid() == false after call).
    // note: the bridging task blocks a pool thread while waiting on this future.
    // declared here, defined in AsyncPool.h (needs Async::submit).
    template <typename Cb>
    auto then(Cb&& cb, Lane lane = Lane::Foreground) -> Future<detail::then_result_t<T, Cb>>;

    // continuation: queues cb(result) for the next main-thread update tick.
    // consumes this future. returned Future<void> represents "cb was queued" (not "cb has run").
    template <typename Cb>
    Future<void> then_on_main(Cb&& cb);

   protected:
    std::future<T> m_future;
};

}  // namespace Async
