// Copyright (c) 2025, WH, All rights reserved.
#pragma once

#include "config.h"

#include <atomic>
#include <memory>

namespace Mc {

// fallback for libc++ missing std::atomic<std::shared_ptr<T>> (STILL! and std::atomic_*(std::shared_ptr<T>) is already deprecated in C++20 ...)
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
template <typename T>
using atomic_sharedptr = std::atomic<std::shared_ptr<T>>;
#else
template <typename T>
class atomic_sharedptr {
    mutable std::shared_ptr<T> m_ptr;

   public:
    atomic_sharedptr() = default;
    ~atomic_sharedptr() = default;

    explicit atomic_sharedptr(std::shared_ptr<T> desired) noexcept : m_ptr(std::move(desired)) {}

    atomic_sharedptr(const atomic_sharedptr&) = delete;
    atomic_sharedptr& operator=(const atomic_sharedptr&) = delete;
    atomic_sharedptr(atomic_sharedptr&&) = delete;
    atomic_sharedptr& operator=(atomic_sharedptr&&) = delete;

    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const noexcept {
        return std::atomic_load_explicit(&m_ptr, order);
    }

    void store(std::shared_ptr<T> desired, std::memory_order order = std::memory_order_seq_cst) noexcept {
        std::atomic_store_explicit(&m_ptr, std::move(desired), order);
    }

    std::shared_ptr<T> exchange(std::shared_ptr<T> desired,
                                std::memory_order order = std::memory_order_seq_cst) noexcept {
        return std::atomic_exchange_explicit(&m_ptr, std::move(desired), order);
    }

    bool compare_exchange_weak(std::shared_ptr<T>& expected, std::shared_ptr<T> desired,
                               std::memory_order order = std::memory_order_seq_cst) noexcept {
        return std::atomic_compare_exchange_weak_explicit(&m_ptr, &expected, std::move(desired), order);
    }

    bool compare_exchange_strong(std::shared_ptr<T>& expected, std::shared_ptr<T> desired,
                                 std::memory_order order = std::memory_order_seq_cst) noexcept {
        return std::atomic_compare_exchange_strong_explicit(&m_ptr, &expected, std::move(desired), order);
    }

    operator std::shared_ptr<T>() const { return load(); }
    auto operator->() const noexcept { return load().operator->(); }
    auto operator*() const noexcept { return load().operator*(); }
    explicit operator bool() const noexcept { return load().operator bool(); }

    atomic_sharedptr& operator=(std::shared_ptr<T> desired) {
        store(std::move(desired));
        return *this;
    }
};
#endif

}  // namespace Mc
