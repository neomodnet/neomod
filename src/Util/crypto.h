// Copyright (c) 2025, WH & 2025, kiwec, All rights reserved.
#pragma once

#include "types.h"

#include <array>
#include <concepts>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#ifndef UTIL_MD5HASH_H
#if defined(__GNUC__) && !defined(__clang__) && (defined(__MINGW32__) || defined(__MINGW64__))
struct MD5Hash;
#else
struct alignas(sizeof(void*) * 2) MD5Hash;
#endif
#endif

namespace crypto {

// call once to seed random number generators
void init() noexcept;

namespace prng {
// pseudorandom numbers
inline constexpr const i64 PRAND_MAX{9223372036854775807 /* INT64_MAX */};
// like C rand() but uses a properly-seeded mt19937_64 under the hood
[[nodiscard]] i64 prand() noexcept;
}  // namespace prng

namespace rng {
// fill with cryptographically secure random bytes
void get_bytes(std::span<u8> out);

// generate a random integral value
template <std::integral T = u64>
    requires(!std::same_as<T, bool>)
[[nodiscard]] T get_rand() {
    T result;
    get_bytes(std::span{reinterpret_cast<u8*>(&result), sizeof(T)});
    return result;
}

// fill a contiguous container/array of trivially copyable elements with random bytes
template <std::ranges::contiguous_range R>
    requires std::ranges::sized_range<R> && std::is_trivially_copyable_v<std::ranges::range_value_t<R>> &&
             std::ranges::output_range<R, std::ranges::range_value_t<R>>
void get_rand(R& out) {
    get_bytes(std::span{reinterpret_cast<u8*>(std::ranges::data(out)),
                        std::ranges::size(out) * sizeof(std::ranges::range_value_t<R>)});
}
}  // namespace rng

namespace hash {
[[nodiscard]] MD5Hash md5(std::span<const u8> data);
[[nodiscard]] MD5Hash md5(std::string_view data);
[[nodiscard]] std::array<u8, 32> sha256(std::span<const u8> data);
[[nodiscard]] std::array<u8, 32> sha256(std::string_view data);

// hash a file in chunks without loading it entirely into memory (nullopt if it couldn't be read)
[[nodiscard]] std::optional<MD5Hash> md5_file(std::string_view file_path);
[[nodiscard]] std::optional<std::array<u8, 32>> sha256_file(std::string_view file_path);
}  // namespace hash

namespace conv {
[[nodiscard]] std::string encode64(std::span<const u8> src);
// empty if src isn't valid (padded) base64
[[nodiscard]] std::vector<u8> decode64(std::string_view src);
[[nodiscard]] std::string encodehex(std::span<const u8> src);
}  // namespace conv

}  // namespace crypto

using namespace crypto::prng;
