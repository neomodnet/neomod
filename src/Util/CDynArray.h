// Copyright (c) 2026, WH, All rights reserved.
#pragma once
// basic std::vector for trivial types that doesn't default-initialize on resize()

#include "noinclude.h"
#include "types.h"

#include <type_traits>
#include <memory>
#include <iterator>
#include <cstring>
#include <cstdlib>

namespace Mc {
struct CDynArrayDeleter {
    void operator()(void* p) const noexcept;
};

template <typename T>
struct CDynArray final : private std::unique_ptr<T[], CDynArrayDeleter> {
    using value_type = T;
    using iterator = T*;
    using const_iterator = const T*;

    static constexpr uSz BASE_CAPACITY{4};

    struct zero_init_t {};
    static inline constexpr zero_init_t zero_init{};

    constexpr CDynArray() : std::unique_ptr<T[], CDynArrayDeleter>(static_cast<T*>(std::malloc(BASE_CAPACITY * sizeof(T)))) {
        static_assert(std::is_trivial_v<T>);
    }
    ~CDynArray();
    CDynArray(CDynArray&& other) noexcept;

    CDynArray& operator=(CDynArray&& other) noexcept;

    template <std::forward_iterator InputIt>
    explicit CDynArray(InputIt first, InputIt last) {
        this->assign(first, last);
    }

    // for taking ownership of some raw pointer allocated with malloc
    // don't use this if you don't know what you're doing
    explicit CDynArray(T* to_own, uSz size) noexcept;
    explicit CDynArray(uSz size) noexcept;

    // zero-initialized
    explicit CDynArray(uSz size, zero_init_t /**/) noexcept;

    CDynArray(const CDynArray& other) noexcept;

    CDynArray& operator=(const CDynArray& other) noexcept;

    void reserve(uSz cap);

    void resize(uSz size);
    void resize(uSz size, const T& fillValue);

    void assign(uSz num, const T& value);

    template <std::forward_iterator InputIt>
    void assign(InputIt first, InputIt last) {
        const uSz count = static_cast<uSz>(std::distance(first, last));
        this->resize(count);
        T* dest = this->data();
        for(; first != last; ++first, ++dest) *dest = *first;
    }

    void clear();

    [[nodiscard]] T* data() noexcept;
    [[nodiscard]] const T* data() const noexcept;
    [[nodiscard]] const T& operator[](uSz i) const noexcept;
    [[nodiscard]] T& operator[](uSz i) noexcept;

    [[nodiscard]] T& front() noexcept;
    [[nodiscard]] T& back() noexcept;

    [[nodiscard]] const T& front() const noexcept;
    [[nodiscard]] const T& back() const noexcept;

    [[nodiscard]] uSz size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] iterator begin() noexcept;
    [[nodiscard]] iterator end() noexcept;
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;

    [[nodiscard]] const_iterator cbegin() const noexcept;
    [[nodiscard]] const_iterator cend() const noexcept;

    iterator insert(const_iterator pos, uSz n, const value_type& x) noexcept;

    template <std::forward_iterator InputIt>
    iterator insert(const_iterator pos, InputIt first, InputIt last) {
        const uSz offset = static_cast<uSz>(pos - this->cbegin());
        const uSz count = static_cast<uSz>(std::distance(first, last));
        if(count == 0) return this->begin() + offset;

        const uSz newSize = m_size + count;
        if(newSize > m_capacity || !this->get()) reserve(newSize * 2);

        if(offset < m_size) {
            std::memmove(static_cast<void*>(this->data() + offset + count), static_cast<void*>(this->data() + offset),
                         (m_size - offset) * sizeof(T));
        }

        T* dest = this->data() + offset;
        for(; first != last; ++first, ++dest) *dest = *first;

        m_size = newSize;
        return this->begin() + offset;
    }

    void push_back(const T& e);

    T& emplace_back();
    T& emplace_back(T&& e);

    void pop_back();

    void erase(const_iterator it);
    void erase(const_iterator first, const_iterator last);
    void shrink_to_fit();

    [[nodiscard]] uSz capacity() const noexcept;

   private:
    uSz m_size{0};
    uSz m_capacity{BASE_CAPACITY};
};

// partial specialization for pointer types (delegates to CDynArray<uintptr_t>)
// (this is so only one explicit instantiation is needed for all pointer types)
template <typename T>
struct CDynArray<T*> final {
    using value_type [[gnu::may_alias]] = T*;
    using iterator = value_type*;
    using const_iterator = const value_type*;

    constexpr CDynArray() = default;
    ~CDynArray() = default;
    CDynArray(CDynArray&&) noexcept = default;
    CDynArray& operator=(CDynArray&&) noexcept = default;
    CDynArray(const CDynArray&) noexcept = default;
    CDynArray& operator=(const CDynArray&) noexcept = default;

    forceinline explicit CDynArray(uSz size) noexcept : m_impl(size) {}
    forceinline explicit CDynArray(uSz size, typename CDynArray<uintptr_t>::zero_init_t z) noexcept : m_impl(size, z) {}

    forceinline void reserve(uSz cap) { m_impl.reserve(cap); }
    forceinline void resize(uSz size) { m_impl.resize(size); }
    forceinline void resize(uSz size, const T& fillValue) { m_impl.resize(size, fillValue); }

    forceinline void assign(uSz num, const T& value) { m_impl.assign(num, value); }

    template <std::forward_iterator InputIt>
    forceinline void assign(InputIt first, InputIt last) {
        const uSz count = static_cast<uSz>(std::distance(first, last));
        m_impl.resize(count);
        uintptr_t* dest = m_impl.data();
        for(; first != last; ++first, ++dest) *dest = reinterpret_cast<uintptr_t>(*first);
    }

    forceinline void clear() { m_impl.clear(); }
    forceinline void shrink_to_fit() { m_impl.shrink_to_fit(); }

    forceinline iterator insert(const_iterator pos, uSz n, const value_type& x) noexcept {
        return m_impl.insert(reinterpret_cast<const uintptr_t*>(pos), n, x);
    };

    template <std::forward_iterator InputIt>
    forceinline iterator insert(const_iterator pos, InputIt first, InputIt last) {
        const uSz offset = static_cast<uSz>(pos - cbegin());
        const uSz count = static_cast<uSz>(std::distance(first, last));
        if(count == 0) return begin() + offset;

        const uSz oldSize = m_impl.size();
        const uSz newSize = oldSize + count;
        if(newSize > m_impl.capacity()) m_impl.reserve(newSize * 2);
        m_impl.resize(newSize);

        if(offset < oldSize) {
            std::memmove(m_impl.data() + offset + count, m_impl.data() + offset,
                         (oldSize - offset) * sizeof(uintptr_t));
        }

        uintptr_t* dest = m_impl.data() + offset;
        for(; first != last; ++first, ++dest) *dest = reinterpret_cast<uintptr_t>(*first);

        return begin() + offset;
    }

    forceinline void push_back(value_type e) { m_impl.push_back(reinterpret_cast<uintptr_t>(e)); }
    forceinline value_type& emplace_back() { return m_impl.emplace_back(); }
    forceinline value_type& emplace_back(value_type e) { return m_impl.push_back(reinterpret_cast<uintptr_t>(e)); }
    forceinline void pop_back() { m_impl.pop_back(); }

    forceinline void erase(const_iterator it) { m_impl.erase(reinterpret_cast<const uintptr_t*>(it)); }
    forceinline void erase(const_iterator first, const_iterator last) {
        m_impl.erase(reinterpret_cast<const uintptr_t*>(first), reinterpret_cast<const uintptr_t*>(last));
    }

    [[nodiscard]] forceinline constexpr value_type* data() noexcept {
        return reinterpret_cast<value_type*>(m_impl.data());
    }
    [[nodiscard]] forceinline constexpr const value_type* data() const noexcept {
        return reinterpret_cast<const value_type*>(m_impl.data());
    }

    [[nodiscard]] forceinline constexpr value_type& operator[](uSz i) noexcept { return data()[i]; }
    [[nodiscard]] forceinline constexpr const value_type& operator[](uSz i) const noexcept { return data()[i]; }

    [[nodiscard]] forceinline constexpr value_type& front() noexcept { return data()[0]; }
    [[nodiscard]] forceinline constexpr value_type& back() noexcept { return data()[m_impl.size() - 1]; }
    [[nodiscard]] forceinline constexpr const value_type& front() const noexcept { return data()[0]; }
    [[nodiscard]] forceinline constexpr const value_type& back() const noexcept { return data()[m_impl.size() - 1]; }

    [[nodiscard]] forceinline constexpr uSz size() const noexcept { return m_impl.size(); }
    [[nodiscard]] forceinline constexpr bool empty() const noexcept { return m_impl.empty(); }
    [[nodiscard]] forceinline constexpr uSz capacity() const noexcept { return m_impl.capacity(); }

    [[nodiscard]] forceinline constexpr iterator begin() noexcept { return data(); }
    [[nodiscard]] forceinline constexpr iterator end() noexcept { return data() + m_impl.size(); }
    [[nodiscard]] forceinline constexpr const_iterator begin() const noexcept { return data(); }
    [[nodiscard]] forceinline constexpr const_iterator end() const noexcept { return data() + m_impl.size(); }
    [[nodiscard]] forceinline constexpr const_iterator cbegin() const noexcept { return data(); }
    [[nodiscard]] forceinline constexpr const_iterator cend() const noexcept { return data() + m_impl.size(); }

   private:
    CDynArray<uintptr_t> m_impl;
};
}  // namespace Mc
