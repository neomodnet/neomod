// Copyright (c) 2026, WH, All rights reserved.
#include "CDynArray.h"
#include "noinclude.h"

#include <cassert>
#include <cstring>

namespace Mc {
void CDynArrayDeleter::operator()(void *p) const noexcept { std::free(p); }

template <typename T>
CDynArray<T>::~CDynArray() = default;

template <typename T>
CDynArray<T>::CDynArray(CDynArray &&other) noexcept
    : std::unique_ptr<T[], CDynArrayDeleter>(std::move(other)), m_size(other.m_size), m_capacity(other.m_capacity) {
    static_assert(std::is_trivial_v<T>);
    other.m_size = 0;
    other.m_capacity = 0;
}

template <typename T>
CDynArray<T> &CDynArray<T>::operator=(CDynArray &&other) noexcept {
    static_assert(std::is_trivial_v<T>);
    if(this != &other) {
        std::unique_ptr<T[], CDynArrayDeleter>::operator=(std::move(other));
        m_size = other.m_size;
        m_capacity = other.m_capacity;
        other.m_size = 0;
        other.m_capacity = 0;
    }
    return *this;
}

// for taking ownership of some raw pointer allocated with malloc
// don't use this if you don't know what you're doing
template <typename T>
CDynArray<T>::CDynArray(T *to_own, uSz size) noexcept
    : std::unique_ptr<T[], CDynArrayDeleter>(to_own), m_size(size), m_capacity(size) {
    static_assert(std::is_trivial_v<T>);
}

template <typename T>
CDynArray<T>::CDynArray(uSz size) noexcept
    : std::unique_ptr<T[], CDynArrayDeleter>(static_cast<T *>(std::malloc(size * sizeof(T)))),
      m_size(size),
      m_capacity(size) {
    static_assert(std::is_trivial_v<T>);
}

// zero-initialized
template <typename T>
CDynArray<T>::CDynArray(uSz size, zero_init_t /**/) noexcept
    : std::unique_ptr<T[], CDynArrayDeleter>(static_cast<T *>(std::calloc(size, sizeof(T)))),
      m_size(size),
      m_capacity(size) {
    static_assert(std::is_trivial_v<T>);
}

template <typename T>
CDynArray<T>::CDynArray(const CDynArray &other) noexcept
    : std::unique_ptr<T[], CDynArrayDeleter>(
          other.get() ? static_cast<T *>(std::malloc(std::max(other.m_size, BASE_CAPACITY) * sizeof(T))) : nullptr) {
    static_assert(std::is_trivial_v<T>);
    if(this->get()) {
        m_size = other.m_size;
        m_capacity = std::max(other.m_size, BASE_CAPACITY);
        std::memcpy(static_cast<void *>(this->get()), static_cast<void *>(other.get()), m_size * sizeof(T));
    }
}

template <typename T>
CDynArray<T> &CDynArray<T>::operator=(const CDynArray &other) noexcept {
    static_assert(std::is_trivial_v<T>);
    if(this != &other) {
        const uSz newCap = std::max(other.m_size, BASE_CAPACITY);
        this->reset(other.get() ? static_cast<T *>(std::malloc(newCap * sizeof(T))) : nullptr);
        if(this->get()) {
            m_size = other.m_size;
            m_capacity = newCap;
            std::memcpy(static_cast<void *>(this->get()), static_cast<void *>(other.get()), m_size * sizeof(T));
        } else {
            m_capacity = 0;
            m_size = 0;
        }
    }
    return *this;
}

template <typename T>
void CDynArray<T>::reserve(uSz cap) {
    const uSz actualCap = std::max(cap, BASE_CAPACITY);
    if(actualCap <= m_capacity && this->get() != nullptr) return;

    T *old = this->release();
    T *temp = nullptr;

    if(!(temp = static_cast<T *>(std::realloc(static_cast<void *>(old), actualCap * sizeof(T))))) {
        assert(false && "CDynArray::reserve: realloc failed");
        this->reset(old);
        return;
    }
    m_capacity = actualCap;
    this->reset(temp);
}

template <typename T>
void CDynArray<T>::resize(uSz size) {
    if(size <= m_capacity && this->get() != nullptr) {
        m_size = size;
        return;
    }
    this->reserve(size * 2);
    m_size = size;
}

template <typename T>
void CDynArray<T>::resize(uSz size, const T &value) {
    this->resize(size);
    if(size > 0) {
        if constexpr(sizeof(T) <= sizeof(u8)) {
            std::memset(this->data(), static_cast<u8>(value), size * sizeof(T));
        } else {
            std::fill(this->begin(), this->end(), value);
        }
    }
}

template <typename T>
void CDynArray<T>::assign(uSz num, const T &value) {
    this->resize(num, value);
}

template <typename T>
void CDynArray<T>::clear() {
    this->resize(0);
}
template <typename T>
T *CDynArray<T>::data() noexcept {
    return this->get();
}
template <typename T>
const T *CDynArray<T>::data() const noexcept {
    return this->get();
}
template <typename T>
const T &CDynArray<T>::operator[](uSz i) const noexcept {
    assert(!!this->data() && "const T &operator[](uSz i) const: !this->data()");
    assert(i < m_size && "const T &operator[](uSz i) const: i >= m_size");
    return this->data()[i];
}
template <typename T>
T &CDynArray<T>::operator[](uSz i) noexcept {
    assert(!!this->data() && "T &operator[](uSz i): !this->data()");
    assert(i < m_size && "T &operator[](uSz i): i >= m_size");
    return this->data()[i];
}
template <typename T>
T &CDynArray<T>::front() noexcept {
    assert(!this->empty());
    return operator[](0);
}
template <typename T>
T &CDynArray<T>::back() noexcept {
    assert(!this->empty());
    return operator[](m_size - 1);
}
template <typename T>
const T &CDynArray<T>::front() const noexcept {
    assert(!this->empty());
    return operator[](0);
}
template <typename T>
const T &CDynArray<T>::back() const noexcept {
    assert(!this->empty());
    return operator[](m_size - 1);
}
template <typename T>
uSz CDynArray<T>::size() const noexcept {
    return m_size;
}
template <typename T>
bool CDynArray<T>::empty() const noexcept {
    return m_size == 0;
}
template <typename T>
CDynArray<T>::iterator CDynArray<T>::begin() noexcept {
    return this->get();
}
template <typename T>
CDynArray<T>::iterator CDynArray<T>::end() noexcept {
    return this->get() + m_size;
}
template <typename T>
CDynArray<T>::const_iterator CDynArray<T>::begin() const noexcept {
    return this->get();
}
template <typename T>
CDynArray<T>::const_iterator CDynArray<T>::end() const noexcept {
    return this->get() + m_size;
}
template <typename T>
CDynArray<T>::const_iterator CDynArray<T>::cbegin() const noexcept {
    return this->get();
}
template <typename T>
CDynArray<T>::const_iterator CDynArray<T>::cend() const noexcept {
    return this->get() + m_size;
}
template <typename T>
T *CDynArray<T>::insert(CDynArray<T>::const_iterator pos, uSz n, const value_type &x) noexcept {
    if(n == 0) return this->begin();

    const sSz offset = pos - this->cbegin();
    if(offset + n >= m_capacity) {
        this->reserve((m_size + 1) * 2);
    }
    if constexpr(sizeof(T) <= sizeof(u8)) {
        std::memset(this->data() + offset, static_cast<u8>(x), n * sizeof(T));
    } else {
        std::fill(this->begin(), this->end(), x);
    }
    return this->begin() + offset;
}

template <typename T>
void CDynArray<T>::push_back(const T &e) {
    if(m_size >= m_capacity || this->data() == nullptr) {
        this->reserve((m_size + 1) * 2);
    }
    this->data()[m_size++] = e;
}

template <typename T>
T &CDynArray<T>::emplace_back() {
    if(m_size >= m_capacity || this->data() == nullptr) {
        this->reserve((m_size + 1) * 2);
    }
    return this->data()[m_size++];
}

template <typename T>
T &CDynArray<T>::emplace_back(T &&e) {
    if(m_size >= m_capacity || this->data() == nullptr) {
        this->reserve((m_size + 1) * 2);
    }
    return (this->data()[m_size++] = std::move(e));
}

template <typename T>
void CDynArray<T>::pop_back() {
    assert(m_size > 0);
    --m_size;
}

template <typename T>
void CDynArray<T>::erase(CDynArray<T>::const_iterator it) {
    assert(it >= this->begin() && it < this->end());
    const uSz idx = static_cast<uSz>(it - this->begin());
    if(idx + 1 < m_size)
        std::memmove(static_cast<void *>(this->data() + idx), static_cast<void *>(this->data() + idx + 1),
                     (m_size - idx - 1) * sizeof(T));
    --m_size;
}

template <typename T>
void CDynArray<T>::erase(CDynArray<T>::const_iterator first, CDynArray<T>::const_iterator last) {
    assert(first >= this->begin() && last <= this->end() && first <= last);
    if(first == last) return;
    const uSz idx = static_cast<uSz>(first - this->begin());
    const uSz count = static_cast<uSz>(last - first);
    if(idx + count < m_size)
        std::memmove(static_cast<void *>(this->data() + idx), static_cast<void *>(this->data() + idx + count),
                     (m_size - idx - count) * sizeof(T));
    m_size -= count;
}

template <typename T>
void CDynArray<T>::shrink_to_fit() {
    const uSz target = std::max(m_size, BASE_CAPACITY);
    if(target >= m_capacity) return;

    T *old = this->release();
    T *temp = static_cast<T *>(std::realloc(static_cast<void *>(old), target * sizeof(T)));
    if(!temp) {
        this->reset(old);
        return;
    }
    m_capacity = target;
    this->reset(temp);
}

template <typename T>
uSz CDynArray<T>::capacity() const noexcept {
    return m_capacity;
}

}  // namespace Mc

#define INSTANTIATE_CDYNARRAY
#include "CDynArray-inst.h"
