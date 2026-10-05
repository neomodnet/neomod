// Copyright (c) 2025, WH, All rights reserved.
#pragma once
// stop_token

#if __has_include("config.h")
#include "config.h"
#endif
#include "SyncMutex.h"

#ifdef USE_NSYNC
#include "nsync_note.h"
#include "noinclude.h"

#ifdef _MSC_VER
#ifndef YieldProcessor
#define YieldProcessor _mm_pause
#pragma intrinsic(_mm_pause)
void _mm_pause(void);
#endif
#endif

#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>
#else
#include <stop_token>
#endif

namespace Sync {
#ifdef USE_NSYNC
using namespace nsync;

// ===================================================================
// stop_token: nsync-based cancellation token
// ===================================================================

// nostopstate constant
struct nostopstate_t {
    explicit nostopstate_t() = default;
};
inline constexpr nostopstate_t nostopstate{};

namespace detail {

forceinline void yield_pause() noexcept {
#ifdef _MSC_VER

    YieldProcessor();

#elif defined(__GNUC__) || defined(__clang__)

#if defined(__arm__) || defined(__aarch64__) || defined(__arm64ec__)
    __asm__ __volatile__("dmb ishst\n\tyield" : : : "memory");
#elif defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__("rep; nop" : : : "memory");
#else
    __asm__ __volatile__("" : : : "memory");
#endif

#endif
}

struct stop_cb_base {
    using cb_fn = void (*)(stop_cb_base*) noexcept;
    cb_fn m_callback;
    stop_cb_base* m_prev = nullptr;
    stop_cb_base* m_next = nullptr;
    bool* m_destroyed = nullptr;
    std::atomic<bool> m_done{false};

    explicit stop_cb_base(cb_fn fn) noexcept : m_callback(fn) {}
    void run() noexcept { m_callback(this); }
};

struct stop_state {
    std::atomic<uint32_t> m_refcount{1};
    std::atomic<uint32_t> m_value;  // stop_requested (bit 0) + source_count (bits 1+)
    nsync_note m_note;
    nsync_mu m_mutex{};
    stop_cb_base* m_head = nullptr;
    std::thread::id m_requester;

    static constexpr uint32_t stop_requested_bit = 1;
    static constexpr uint32_t ssrc_counter_inc = 2;

    stop_state()
        : m_value(ssrc_counter_inc),  // starts with 1 source
          m_note(nsync_note_new(nullptr, nsync_time_no_deadline)) {
        assert(m_note && "stop_state: nsync_note_new failed");
    }

    ~stop_state() {
        if(m_note) nsync_note_free(m_note);
    }

    stop_state(const stop_state&) = delete;
    stop_state(stop_state&&) = delete;
    stop_state& operator=(const stop_state&) = delete;
    stop_state& operator=(stop_state&&) = delete;

    void add_ref() noexcept { m_refcount.fetch_add(1, std::memory_order_relaxed); }

    void release() noexcept {
        if(m_refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    void add_source() noexcept { m_value.fetch_add(ssrc_counter_inc, std::memory_order_relaxed); }

    void sub_source() noexcept { m_value.fetch_sub(ssrc_counter_inc, std::memory_order_release); }

    [[nodiscard]] bool stop_requested() const noexcept {
        return m_value.load(std::memory_order_acquire) & stop_requested_bit;
    }

    [[nodiscard]] bool stop_possible() const noexcept {
        // stop is possible if already requested OR there are still sources
        return m_value.load(std::memory_order_acquire) != 0;
    }

    bool request_stop() noexcept {
        uint32_t old = m_value.load(std::memory_order_acquire);
        do {
            if(old & stop_requested_bit) return false;
        } while(!m_value.compare_exchange_weak(old, old | stop_requested_bit, std::memory_order_acq_rel,
                                               std::memory_order_acquire));

        nsync_note_notify(m_note);
        m_requester = std::this_thread::get_id();

        nsync_mu_lock(&m_mutex);
        while(m_head) {
            stop_cb_base* cb = m_head;
            m_head = m_head->m_next;
            const bool last_cb = (m_head == nullptr);
            if(m_head) m_head->m_prev = nullptr;

            nsync_mu_unlock(&m_mutex);

            bool destroyed = false;
            cb->m_destroyed = &destroyed;
            cb->run();

            if(!destroyed) {
                cb->m_destroyed = nullptr;
                cb->m_done.store(true, std::memory_order_release);
            }

            if(last_cb) return true;
            nsync_mu_lock(&m_mutex);
        }
        nsync_mu_unlock(&m_mutex);

        return true;
    }

    bool register_callback(stop_cb_base* cb) noexcept {
        if(stop_requested()) {
            cb->run();
            return false;
        }

        nsync_mu_lock(&m_mutex);

        if(stop_requested()) {
            nsync_mu_unlock(&m_mutex);
            cb->run();
            return false;
        }

        // if no sources remain and stop not requested, no point registering
        if(m_value.load(std::memory_order_relaxed) < ssrc_counter_inc) {
            nsync_mu_unlock(&m_mutex);
            return false;
        }

        cb->m_next = m_head;
        if(m_head) m_head->m_prev = cb;
        m_head = cb;

        nsync_mu_unlock(&m_mutex);
        return true;
    }

    void remove_callback(stop_cb_base* cb) noexcept {
        nsync_mu_lock(&m_mutex);

        if(cb == m_head) {
            m_head = cb->m_next;
            if(m_head) m_head->m_prev = nullptr;
            nsync_mu_unlock(&m_mutex);
            return;
        }

        if(cb->m_prev) {
            cb->m_prev->m_next = cb->m_next;
            if(cb->m_next) cb->m_next->m_prev = cb->m_prev;
            nsync_mu_unlock(&m_mutex);
            return;
        }

        nsync_mu_unlock(&m_mutex);

        // callback not in list, must have been removed by request_stop
        if(m_requester != std::this_thread::get_id()) {
            // wait for callback execution to complete
            while(!cb->m_done.load(std::memory_order_acquire)) {
                yield_pause();
            }
        } else {
            // destructor called from within callback itself
            if(cb->m_destroyed) *cb->m_destroyed = true;
        }
    }
};

class stop_state_ref {
    stop_state* m_ptr = nullptr;

   public:
    stop_state_ref() noexcept = default;
    explicit stop_state_ref(stop_state* p) noexcept : m_ptr(p) {}

    stop_state_ref(const stop_state_ref& other) noexcept : m_ptr(other.m_ptr) {
        if(m_ptr) m_ptr->add_ref();
    }

    stop_state_ref(stop_state_ref&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }

    // NOLINTNEXTLINE(cert-oop54-cpp, bugprone-unhandled-self-assignment)
    stop_state_ref& operator=(const stop_state_ref& other) noexcept {
        if(auto* ptr = other.m_ptr; ptr != m_ptr) {
            if(ptr) ptr->add_ref();
            if(m_ptr) m_ptr->release();
            m_ptr = ptr;
        }
        return *this;
    }

    stop_state_ref& operator=(stop_state_ref&& other) noexcept {
        stop_state_ref(std::move(other)).swap(*this);
        return *this;
    }

    ~stop_state_ref() {
        if(m_ptr) m_ptr->release();
    }

    void swap(stop_state_ref& other) noexcept { std::swap(m_ptr, other.m_ptr); }

    explicit operator bool() const noexcept { return m_ptr != nullptr; }
    stop_state* operator->() const noexcept { return m_ptr; }
    [[nodiscard]] stop_state* get() const noexcept { return m_ptr; }

    friend bool operator==(const stop_state_ref& a, const stop_state_ref& b) noexcept { return a.m_ptr == b.m_ptr; }
    friend bool operator!=(const stop_state_ref& a, const stop_state_ref& b) noexcept { return a.m_ptr != b.m_ptr; }
};

}  // namespace detail

template <typename Callback>
class stop_callback;

class stop_token {
   private:
    detail::stop_state_ref m_state;

    friend class stop_source;
    template <typename Callback>
    friend class stop_callback;

    explicit stop_token(detail::stop_state_ref state) noexcept : m_state(std::move(state)) {}

   public:
    stop_token() noexcept = default;

    stop_token(const stop_token&) noexcept = default;
    stop_token(stop_token&&) noexcept = default;

    ~stop_token() = default;

    stop_token& operator=(const stop_token&) noexcept = default;

    stop_token& operator=(stop_token&&) noexcept = default;

    [[nodiscard]] bool stop_requested() const noexcept { return m_state && m_state->stop_requested(); }

    [[nodiscard]] bool stop_possible() const noexcept { return m_state && m_state->stop_possible(); }

    // direct access to underlying nsync_note for low-overhead condition variable usage
    [[nodiscard]] nsync_note native_handle() const noexcept { return m_state ? m_state->m_note : nullptr; }

    void swap(stop_token& other) noexcept { m_state.swap(other.m_state); }

    friend void swap(stop_token& a, stop_token& b) noexcept { a.swap(b); }

    friend bool operator==(const stop_token& a, const stop_token& b) noexcept { return a.m_state == b.m_state; }
    friend bool operator!=(const stop_token& a, const stop_token& b) noexcept { return !(a == b); }
};

class stop_source {
   private:
    detail::stop_state_ref m_state;

   public:
    stop_source() : m_state(new detail::stop_state()) {}
    explicit stop_source(nostopstate_t /* */) noexcept {}

    stop_source(const stop_source& other) noexcept : m_state(other.m_state) {
        if(m_state) m_state->add_source();
    }

    stop_source(stop_source&&) noexcept = default;

    // NOLINTNEXTLINE(cert-oop54-cpp)
    stop_source& operator=(const stop_source& other) noexcept {
        if(m_state != other.m_state) {
            stop_source tmp(std::move(*this));
            m_state = other.m_state;
            if(m_state) m_state->add_source();
        }
        return *this;
    }

    stop_source& operator=(stop_source&&) noexcept = default;

    ~stop_source() {
        if(m_state) m_state->sub_source();
    }

    [[nodiscard]] stop_token get_token() const noexcept { return stop_token{m_state}; }

    [[nodiscard]] bool stop_possible() const noexcept { return static_cast<bool>(m_state); }

    [[nodiscard]] bool stop_requested() const noexcept { return m_state && m_state->stop_requested(); }

    bool request_stop() noexcept { return m_state && m_state->request_stop(); }

    void swap(stop_source& other) noexcept { m_state.swap(other.m_state); }

    friend void swap(stop_source& a, stop_source& b) noexcept { a.swap(b); }

    friend bool operator==(const stop_source& a, const stop_source& b) noexcept { return a.m_state == b.m_state; }
    friend bool operator!=(const stop_source& a, const stop_source& b) noexcept { return !(a == b); }
};

template <typename Callback>
class stop_callback {
    static_assert(std::is_nothrow_destructible_v<Callback>);
    static_assert(std::is_invocable_v<Callback>);

   private:
    struct cb_impl : detail::stop_cb_base {
        Callback m_cb;

        template <typename C>
        explicit cb_impl(C&& cb)
            requires(!std::is_same_v<std::decay_t<C>, cb_impl>)
            : stop_cb_base(&execute), m_cb(std::forward<C>(cb)) {}

        static void execute(stop_cb_base* base) noexcept {
            std::forward<Callback>(static_cast<cb_impl*>(base)->m_cb)();
        }
    };

    cb_impl m_cb;
    detail::stop_state_ref m_state;

   public:
    using callback_type = Callback;

    template <typename C>
    explicit stop_callback(const stop_token& token, C&& cb) noexcept(std::is_nothrow_constructible_v<Callback, C>)
        requires(std::is_constructible_v<Callback, C>)
        : m_cb(std::forward<C>(cb)) {
        if(auto* state = token.m_state.get()) {
            if(state->register_callback(&m_cb)) {
                m_state = token.m_state;
            }
        }
    }

    template <typename C>
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    explicit stop_callback(stop_token&& token, C&& cb) noexcept(std::is_nothrow_constructible_v<Callback, C>)
        requires(std::is_constructible_v<Callback, C>)
        : m_cb(std::forward<C>(cb)) {
        if(auto* state = token.m_state.get()) {
            if(state->register_callback(&m_cb)) {
                m_state.swap(token.m_state);
            }
        }
    }

    ~stop_callback() {
        if(m_state) {
            m_state->remove_callback(&m_cb);
        }
    }

    stop_callback(const stop_callback&) = delete;
    stop_callback(stop_callback&&) = delete;
    stop_callback& operator=(const stop_callback&) = delete;
    stop_callback& operator=(stop_callback&&) = delete;
};

template <typename Callback>
stop_callback(stop_token, Callback) -> stop_callback<Callback>;

#else
using stop_token = std::stop_token;
using stop_source = std::stop_source;
template <typename Callback>
using stop_callback = std::stop_callback<Callback>;
using nostopstate_t = std::nostopstate_t;
inline constexpr nostopstate_t nostopstate = std::nostopstate;
#endif

}  // namespace Sync
