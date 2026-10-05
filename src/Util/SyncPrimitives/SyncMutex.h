// Copyright (c) 2025, WH, All rights reserved.
#pragma once
// mutex + recursive_mutex + RAII locking things

#if __has_include("config.h")
#include "config.h"
#endif

#ifdef USE_NSYNC
#include "nsync_mu.h"

#include <atomic>
#include <thread>
#include <cassert>

#else  // USE_NSYNC
#include <shared_mutex>
#endif

#include <mutex>  // for std::lock (works with generic-compatible mutexes)

namespace Sync {
#ifdef USE_NSYNC
using namespace nsync;

// ===================================================================
// fundamental mutex wrapping nsync_mu
// ===================================================================
class nsync_mutex_t {
   private:
    nsync_mu m_mutex{};

   public:
    constexpr nsync_mutex_t() noexcept = default;
    ~nsync_mutex_t() = default;

    nsync_mutex_t(const nsync_mutex_t&) = delete;
    nsync_mutex_t& operator=(const nsync_mutex_t&) = delete;
    nsync_mutex_t(nsync_mutex_t&&) = delete;
    nsync_mutex_t& operator=(nsync_mutex_t&&) = delete;

    void lock() { nsync_mu_lock(&m_mutex); }
    bool try_lock() noexcept { return nsync_mu_trylock(&m_mutex) != 0; }
    void unlock() { nsync_mu_unlock(&m_mutex); }

    // native handle for condition variable use
    nsync_mu* native_handle() noexcept { return &m_mutex; }
};

// forward declarations for RAII guards
template <typename Mutex>
class lock_guard;
template <typename Mutex>
class unique_lock;
template <typename... Mutexes>
class scoped_lock;

// type alias for convenience
using mutex = nsync_mutex_t;

// ===================================================================
// defer lock tags (matching std library)
// ===================================================================
struct defer_lock_t {
    explicit defer_lock_t() = default;
};
struct try_to_lock_t {
    explicit try_to_lock_t() = default;
};
struct adopt_lock_t {
    explicit adopt_lock_t() = default;
};

inline constexpr defer_lock_t defer_lock{};
inline constexpr try_to_lock_t try_to_lock{};
inline constexpr adopt_lock_t adopt_lock{};

// ===================================================================
// nsync_shared_mutex_t: reader/writer lock using nsync_mu
// ===================================================================
class nsync_shared_mutex_t {
   private:
    nsync_mu m_mutex{};

   public:
    constexpr nsync_shared_mutex_t() noexcept = default;
    ~nsync_shared_mutex_t() = default;

    nsync_shared_mutex_t(const nsync_shared_mutex_t&) = delete;
    nsync_shared_mutex_t& operator=(const nsync_shared_mutex_t&) = delete;
    nsync_shared_mutex_t(nsync_shared_mutex_t&&) = delete;
    nsync_shared_mutex_t& operator=(nsync_shared_mutex_t&&) = delete;

    // exclusive/write lock operations
    void lock() { nsync_mu_lock(&m_mutex); }
    bool try_lock() noexcept { return nsync_mu_trylock(&m_mutex) != 0; }
    void unlock() { nsync_mu_unlock(&m_mutex); }

    // shared/read lock operations
    void lock_shared() { nsync_mu_rlock(&m_mutex); }
    bool try_lock_shared() noexcept { return nsync_mu_rtrylock(&m_mutex) != 0; }
    void unlock_shared() { nsync_mu_runlock(&m_mutex); }

    // native handle for condition variable compatibility
    nsync_mu* native_handle() noexcept { return &m_mutex; }
};

// type alias
using shared_mutex = nsync_shared_mutex_t;

// ===================================================================
// shared_lock: RAII wrapper for shared/read lock ownership
// ===================================================================
template <typename Mutex>
class shared_lock {
   public:
    using mutex_type = Mutex;

    shared_lock() noexcept : m_p(nullptr), m_owns(false) {}

    explicit shared_lock(mutex_type& m) : m_p(std::addressof(m)), m_owns(false) {
        lock();
        m_owns = true;
    }

    shared_lock(mutex_type& m, defer_lock_t /* */) noexcept : m_p(std::addressof(m)), m_owns(false) {}
    shared_lock(mutex_type& m, try_to_lock_t /* */) : m_p(std::addressof(m)), m_owns(m_p->try_lock_shared()) {}
    shared_lock(mutex_type& m, adopt_lock_t /* */) noexcept : m_p(std::addressof(m)), m_owns(true) {}

    ~shared_lock() {
        if(m_owns) unlock();
    }

    shared_lock(shared_lock&& other) noexcept : m_p(other.m_p), m_owns(other.m_owns) {
        other.m_p = nullptr;
        other.m_owns = false;
    }

    shared_lock& operator=(shared_lock&& other) noexcept {
        if(this != std::addressof(other)) {
            if(m_owns) unlock();
            m_p = other.m_p;
            m_owns = other.m_owns;
            other.m_p = nullptr;
            other.m_owns = false;
        }
        return *this;
    }

    shared_lock(const shared_lock&) = delete;
    shared_lock& operator=(const shared_lock&) = delete;

    void lock() {
        assert(!!m_p && "shared_lock::lock: mutex is null (may have already been released)");
        assert(!m_owns && "shared_lock::lock: tried to re-lock owned mutex");
        m_p->lock_shared();
        m_owns = true;
    }

    bool try_lock() {
        assert(!!m_p && "shared_lock::try_lock: mutex is null (may have already been released)");
        assert(!m_owns && "shared_lock::try_lock: cannot re-lock owned mutex");
        m_owns = m_p->try_lock_shared();
        return m_owns;
    }

    void unlock() {
        assert(m_owns && "shared_lock::unlock: cannot unlock unowned mutex");
        if(m_p) {
            m_p->unlock_shared();
            m_owns = false;
        }
    }

    void swap(shared_lock& other) noexcept {
        std::swap(m_p, other.m_p);
        std::swap(m_owns, other.m_owns);
    }

    mutex_type* release() noexcept {
        mutex_type* ret = m_p;
        m_p = nullptr;
        m_owns = false;
        return ret;
    }

    [[nodiscard]] bool owns_lock() const noexcept { return m_owns; }
    explicit operator bool() const noexcept { return m_owns; }
    [[nodiscard]] mutex_type* mutex() const noexcept { return m_p; }

   private:
    mutex_type* m_p;
    bool m_owns;
};

template <typename Mutex>
void swap(shared_lock<Mutex>& lhs, shared_lock<Mutex>& rhs) noexcept {
    lhs.swap(rhs);
}

// ===================================================================
// lock_guard: simple RAII mutex wrapper
// ===================================================================
template <typename Mutex>
class lock_guard {
   public:
    using mutex_type = Mutex;

    explicit lock_guard(mutex_type& m) : m_p(m) { m_p.lock(); }

    lock_guard(mutex_type& m, adopt_lock_t /* */) noexcept : m_p(m) {}

    ~lock_guard() { m_p.unlock(); }

    lock_guard(const lock_guard&) = delete;
    lock_guard& operator=(const lock_guard&) = delete;
    lock_guard(lock_guard&&) = delete;
    lock_guard& operator=(lock_guard&&) = delete;

   private:
    mutex_type& m_p;
};

// ===================================================================
// unique_lock: flexible RAII mutex wrapper
// ===================================================================
template <typename Mutex>
class unique_lock {
   public:
    using mutex_type = Mutex;

    unique_lock() noexcept : m_p(nullptr), m_owns(false) {}

    explicit unique_lock(mutex_type& m) : m_p(std::addressof(m)), m_owns(false) {
        lock();
        m_owns = true;
    }

    unique_lock(mutex_type& m, defer_lock_t /* */) noexcept : m_p(std::addressof(m)), m_owns(false) {}
    unique_lock(mutex_type& m, try_to_lock_t /* */) : m_p(std::addressof(m)), m_owns(m_p->try_lock()) {}
    unique_lock(mutex_type& m, adopt_lock_t /* */) noexcept : m_p(std::addressof(m)), m_owns(true) {}

    ~unique_lock() {
        if(m_owns) unlock();
    }

    unique_lock(unique_lock&& other) noexcept : m_p(other.m_p), m_owns(other.m_owns) {
        other.m_p = nullptr;
        other.m_owns = false;
    }

    unique_lock& operator=(unique_lock&& other) noexcept {
        if(this != std::addressof(other)) {
            if(m_owns) unlock();
            m_p = other.m_p;
            m_owns = other.m_owns;
            other.m_p = nullptr;
            other.m_owns = false;
        }
        return *this;
    }

    unique_lock(const unique_lock&) = delete;
    unique_lock& operator=(const unique_lock&) = delete;

    void lock() {
        assert(!!m_p && "unique_lock::lock: mutex is null (may have already been released)");
        assert(!m_owns && "unique_lock::lock: tried to re-lock owned mutex");
        m_p->lock();
        m_owns = true;
    }

    bool try_lock() {
        assert(!!m_p && "unique_lock::try_lock: mutex is null (may have already been released)");
        assert(!m_owns && "unique_lock::try_lock: cannot to re-lock owned mutex");
        m_owns = m_p->try_lock();
        return m_owns;
    }

    void unlock() {
        assert(m_owns && "unique_lock::unlock: cannot un-lock unowned mutex");
        if(m_p) {
            m_p->unlock();
            m_owns = false;
        }
    }

    void swap(unique_lock& other) noexcept {
        std::swap(m_p, other.m_p);
        std::swap(m_owns, other.m_owns);
    }

    mutex_type* release() noexcept {
        mutex_type* ret = m_p;
        m_p = nullptr;
        m_owns = false;
        return ret;
    }

    [[nodiscard]] bool owns_lock() const noexcept { return m_owns; }
    explicit operator bool() const noexcept { return m_owns; }
    [[nodiscard]] mutex_type* mutex() const noexcept { return m_p; }

   private:
    mutex_type* m_p;
    bool m_owns;
};

template <typename Mutex>
void swap(unique_lock<Mutex>& lhs, unique_lock<Mutex>& rhs) noexcept {
    lhs.swap(rhs);
}

// ===================================================================
// scoped_lock: multiple mutex RAII wrapper
// ===================================================================

template <typename... Mutexes>
class scoped_lock {
   public:
    explicit scoped_lock(Mutexes&... mutexes) : m_mutexes(mutexes...) { std::lock(mutexes...); }

    explicit scoped_lock(adopt_lock_t /* */, Mutexes&... mutexes) noexcept : m_mutexes(mutexes...) {}

    ~scoped_lock() { unlock_all(std::index_sequence_for<Mutexes...>{}); }

    scoped_lock(const scoped_lock&) = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;
    scoped_lock(scoped_lock&&) = delete;
    scoped_lock& operator=(scoped_lock&&) = delete;

   private:
    std::tuple<Mutexes&...> m_mutexes;

    template <std::size_t... Indices>
    void unlock_all(std::index_sequence<Indices...> /* */) {
        (std::get<sizeof...(Indices) - 1 - Indices>(m_mutexes).unlock(), ...);
    }
};

// single mutex specialization
template <typename Mutex>
class scoped_lock<Mutex> {
   public:
    using mutex_type = Mutex;

    explicit scoped_lock(mutex_type& m) : m_p(m) { m_p.lock(); }
    explicit scoped_lock(adopt_lock_t /* */, mutex_type& m) noexcept : m_p(m) {}
    ~scoped_lock() { m_p.unlock(); }

    scoped_lock(const scoped_lock&) = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;
    scoped_lock(scoped_lock&&) = delete;
    scoped_lock& operator=(scoped_lock&&) = delete;

   private:
    mutex_type& m_p;
};

// empty specialization
template <>
class scoped_lock<> {
   public:
    explicit scoped_lock() = default;
    explicit scoped_lock(adopt_lock_t /* */) noexcept {}
    ~scoped_lock() = default;

    scoped_lock(const scoped_lock&) = delete;
    scoped_lock& operator=(const scoped_lock&) = delete;
    scoped_lock(scoped_lock&&) = delete;
    scoped_lock& operator=(scoped_lock&&) = delete;
};

// ===================================================================
// nsync_recursive_mutex_t: recursive mutex using nsync primitives
// ===================================================================
class nsync_recursive_mutex_t {
   private:
    nsync_mu m_mutex{};  // held for as long as a thread owns this
    // relaxed is enough: only the owner stores its own id, and clears it before releasing m_mutex
    std::atomic<std::thread::id> m_owner;
    int m_count = 0;  // only accessed by the owner

   public:
    constexpr nsync_recursive_mutex_t() noexcept = default;
    ~nsync_recursive_mutex_t() = default;

    nsync_recursive_mutex_t(const nsync_recursive_mutex_t&) = delete;
    nsync_recursive_mutex_t& operator=(const nsync_recursive_mutex_t&) = delete;
    nsync_recursive_mutex_t(nsync_recursive_mutex_t&&) = delete;
    nsync_recursive_mutex_t& operator=(nsync_recursive_mutex_t&&) = delete;

    void lock() {
        const auto current_id = std::this_thread::get_id();
        if(m_owner.load(std::memory_order_relaxed) != current_id) {
            nsync_mu_lock(&m_mutex);
            m_owner.store(current_id, std::memory_order_relaxed);
        }
        ++m_count;
    }

    bool try_lock() noexcept {
        const auto current_id = std::this_thread::get_id();
        if(m_owner.load(std::memory_order_relaxed) != current_id) {
            if(!nsync_mu_trylock(&m_mutex)) {
                return false;
            }
            m_owner.store(current_id, std::memory_order_relaxed);
        }
        ++m_count;
        return true;
    }

    void unlock() {
        assert((m_owner.load(std::memory_order_relaxed) == std::this_thread::get_id()) &&
               "recursive_mutex::unlock: not owner");

        if(--m_count == 0) {
            m_owner.store(std::thread::id{}, std::memory_order_relaxed);
            nsync_mu_unlock(&m_mutex);
        }
    }

    // WARNING: this is not expected/tested to work correctly with condition variables
    // leaving it here for possible future expansion (maybe)
    nsync_mu* native_handle() noexcept { return &m_mutex; }
};

// type alias
using recursive_mutex = nsync_recursive_mutex_t;

#else
// stdlib fallback aliases
using mutex = std::mutex;
using recursive_mutex = std::recursive_mutex;
using shared_mutex = std::shared_mutex;

template <typename Mutex>
using shared_lock = std::shared_lock<Mutex>;
template <typename Mutex>
using lock_guard = std::lock_guard<Mutex>;
template <typename Mutex>
using unique_lock = std::unique_lock<Mutex>;
template <typename... Mutexes>
using scoped_lock = std::scoped_lock<Mutexes...>;

using defer_lock_t = std::defer_lock_t;
using try_to_lock_t = std::try_to_lock_t;
using adopt_lock_t = std::adopt_lock_t;

inline constexpr defer_lock_t defer_lock = std::defer_lock;
inline constexpr try_to_lock_t try_to_lock = std::try_to_lock;
inline constexpr adopt_lock_t adopt_lock = std::adopt_lock;

#endif  // USE_NSYNC
}  // namespace Sync
