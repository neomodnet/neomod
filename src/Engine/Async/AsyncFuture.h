// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "config.h"

#include <chrono>
#include <future>

#ifdef MCENGINE_PLATFORM_WASM
extern "C" void emscripten_main_thread_process_queued_calls(void);
namespace McThread {
bool is_main_thread() noexcept;
}
#endif

namespace Async {

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

   private:
    std::future<T> m_future;
};

}  // namespace Async
