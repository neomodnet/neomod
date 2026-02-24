// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"

#include <chrono>
#include <future>

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

    void wait() const { m_future.wait(); }

   private:
    std::future<T> m_future;
};

template <>
class Future<void> {
   public:
    Future() noexcept = default;
    ~Future() = default;
    explicit Future(std::future<void>&& f) noexcept : m_future(std::move(f)) {}

    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
    Future(Future&&) noexcept = default;
    Future& operator=(Future&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept { return m_future.valid(); }

    [[nodiscard]] bool is_ready() const {
        return m_future.valid() && m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    void get() { m_future.get(); }

    void wait() const { m_future.wait(); }

   private:
    std::future<void> m_future;
};

}  // namespace Async
