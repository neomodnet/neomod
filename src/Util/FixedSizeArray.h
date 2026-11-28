// Copyright (c) 2025, WH, All rights reserved.
#pragma once

#include "Algorithms.h"

#include <memory>
#include <vector>
#include <cassert>
#include <cstddef>
#include <cstring>  // memcpy

// "fixed-size std::vector", but 2*sizeof(size_t) bytes instead of 3*sizeof(size_t)
template <typename T>
struct FixedSizeArray {
    struct zero_init_t {};
    static inline constexpr zero_init_t zero_init{};

    FixedSizeArray() = default;
    ~FixedSizeArray() = default;

    explicit FixedSizeArray(size_t size) : data_(std::make_unique_for_overwrite<T[]>(size)), size_(size) {}
    FixedSizeArray(size_t size, zero_init_t /**/) : data_(std::make_unique<T[]>(size)), size_(size) {}

    FixedSizeArray(const FixedSizeArray &other)
        : data_(other.size_ ? std::make_unique_for_overwrite<T[]>(other.size_) : nullptr), size_(other.size_) {
        if constexpr(std::is_trivially_copyable_v<T>) {
            if(size_) memcpy(data_.get(), other.data_.get(), size_ * sizeof(T));
        } else {
            Mc::copy_n(other.data_.get(), size_, data_.get());
        }
    }

    FixedSizeArray &operator=(const FixedSizeArray &other) {
        if(this != &other) {
            size_ = other.size_;
            data_ = other.size_ ? std::make_unique_for_overwrite<T[]>(other.size_) : nullptr;
            if constexpr(std::is_trivially_copyable_v<T>) {
                if(size_) memcpy(data_.get(), other.data_.get(), size_ * sizeof(T));
            } else {
                Mc::copy_n(other.data_.get(), size_, data_.get());
            }
        }
        return *this;
    }

    FixedSizeArray(FixedSizeArray &&other) noexcept : data_(std::move(other.data_)), size_(other.size_) {
        other.size_ = 0;
    }

    FixedSizeArray &operator=(FixedSizeArray &&other) noexcept {
        if(this != &other) {
            data_ = std::move(other.data_);
            size_ = other.size_;
            other.size_ = 0;
        }
        return *this;
    }

    // move construct/assign from a std::unique_ptr<T[]> + size
    explicit FixedSizeArray(std::unique_ptr<T[]> &&uptr, size_t size) noexcept : data_(std::move(uptr)), size_(size) {}

    // constructors/assignment operators from a std::vector
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    explicit FixedSizeArray(std::vector<T> &&vec) noexcept
        : data_(vec.size() ? std::make_unique_for_overwrite<T[]>(vec.size()) : nullptr), size_(vec.size()) {
        if constexpr(std::is_trivially_copyable_v<T>) {
            if(size_) memcpy(data_.get(), vec.data(), size_ * sizeof(T));
        } else {
            std::move(vec.begin(), vec.end(), data_.get());
        }
    }

    explicit FixedSizeArray(const std::vector<T> &vec) noexcept
        : data_(vec.size() ? std::make_unique_for_overwrite<T[]>(vec.size()) : nullptr), size_(vec.size()) {
        if constexpr(std::is_trivially_copyable_v<T>) {
            if(size_) memcpy(data_.get(), vec.data(), size_ * sizeof(T));
        } else {
            Mc::copy(vec.begin(), vec.end(), data_.get());
        }
    }

    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    FixedSizeArray &operator=(std::vector<T> &&vec) noexcept {
        size_ = vec.size();
        data_ = size_ ? std::make_unique_for_overwrite<T[]>(size_) : nullptr;
        if constexpr(std::is_trivially_copyable_v<T>) {
            if(size_) memcpy(data_.get(), vec.data(), size_ * sizeof(T));
        } else {
            std::move(vec.begin(), vec.end(), data_.get());
        }
        return *this;
    }

    FixedSizeArray &operator=(const std::vector<T> &vec) noexcept {
        size_ = vec.size();
        data_ = size_ ? std::make_unique_for_overwrite<T[]>(size_) : nullptr;
        if constexpr(std::is_trivially_copyable_v<T>) {
            if(size_) memcpy(data_.get(), vec.data(), size_ * sizeof(T));
        } else {
            Mc::copy(vec.begin(), vec.end(), data_.get());
        }
        return *this;
    }

    [[nodiscard]] constexpr inline T *data() noexcept { return data_.get(); }
    [[nodiscard]] constexpr inline const T *data() const noexcept { return data_.get(); }
    [[nodiscard]] constexpr inline const T &operator[](size_t i) const noexcept {
        assert(!!data_ && "const T &operator[](size_t i) const: !data_");
        assert(i < size_ && "const T &operator[](size_t i) const: i >= size_");
        return data_[i];
    }
    [[nodiscard]] constexpr inline T &operator[](size_t i) noexcept {
        assert(!!data_ && "T &operator[](size_t i): !data_");
        assert(i < size_ && "T &operator[](size_t i): i >= size_");
        return data_[i];
    }

    [[nodiscard]] constexpr inline T &front() noexcept {
        assert(!empty());
        return operator[](0);
    }

    [[nodiscard]] constexpr inline T &back() noexcept {
        assert(!empty());
        return operator[](size_ - 1);
    }

    [[nodiscard]] inline constexpr const T &front() const noexcept {
        assert(!empty());
        return operator[](0);
    }

    [[nodiscard]] inline constexpr const T &back() const noexcept {
        assert(!empty());
        return operator[](size_ - 1);
    }

    [[nodiscard]] constexpr inline size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr inline bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] constexpr inline T *begin() noexcept { return data_.get(); }
    [[nodiscard]] constexpr inline T *end() noexcept { return data_.get() + size_; }
    [[nodiscard]] constexpr inline const T *begin() const noexcept { return data_.get(); }
    [[nodiscard]] constexpr inline const T *end() const noexcept { return data_.get() + size_; }

    [[nodiscard]] constexpr inline const T *cbegin() const noexcept { return data_.get(); }
    [[nodiscard]] constexpr inline const T *cend() const noexcept { return data_.get() + size_; }

    void clear() noexcept {
        size_ = 0;
        data_.reset();
    }

   private:
    std::unique_ptr<T[]> data_{nullptr};
    size_t size_{0};
};
