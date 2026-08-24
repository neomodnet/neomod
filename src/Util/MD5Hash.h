#ifndef UTIL_MD5HASH_H
#define UTIL_MD5HASH_H

#include <algorithm>
#include <cstring>
#include <array>
#include <string_view>
#include <string>

#ifndef BUILD_TOOLS_ONLY
#include "fmt/base.h"
#include "ankerl/unordered_dense.h"
namespace Hash {
namespace flat = ankerl::unordered_dense;
}
#endif

using MD5Byte = unsigned char;

#if defined(__GNUC__) && !defined(__clang__) && (defined(__MINGW32__) || defined(__MINGW64__))
// inexplicably broken in unpredictable ways with mingw-gcc
#define ALIGNED_TO(x)
#else
#define ALIGNED_TO(x) alignas(x)
#endif

struct ALIGNED_TO(sizeof(void *) * 2) MD5String final : public std::array<char, 32> {
    using array::array;
    constexpr MD5String() : array() {}
    MD5String(const char *str);

    static constexpr MD5String digest_to_md5chars(const std::array<MD5Byte, 16> &digest) {
        MD5String out;
        for(unsigned long long i = 0; i < 16; i++) {
            out[i * 2] = "0123456789abcdef"[digest[i] >> 4];
            out[i * 2 + 1] = "0123456789abcdef"[digest[i] & 0xf];
        }
        return out;
    }

    static constexpr std::array<MD5Byte, 16> md5chars_to_digest(const std::array<char, 32> &charray) {
        static constexpr const auto nibble = [](char c) -> MD5Byte {
            if(c >= '0' && c <= '9') return c - '0';
            if(c >= 'a' && c <= 'f') return c - 'a' + 10;
            if(c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };

        std::array<MD5Byte, 16> digest;
        for(unsigned long long i = 0; i < 16; i++) {
            digest[i] = (nibble((charray)[i * 2]) << 4) | nibble((charray)[i * 2 + 1]);
        }
        return digest;
    }

    constexpr MD5String(const std::array<MD5Byte, 16> &digest) : array(digest_to_md5chars(digest)) {}

    [[nodiscard]] constexpr size_t length() const { return this->size(); }
    [[nodiscard]] constexpr std::array<MD5Byte, 16> bytes() const { return md5chars_to_digest(*this); }
    [[nodiscard]] constexpr std::string_view string() const { return {this->begin(), this->end()}; }

    inline void clear() { std::memset(this->data(), 0, this->size() * sizeof(char)); }

    [[nodiscard]] inline bool empty() const {
        for(auto byte : *this) {
            if(!!byte) return false;
        }
        return true;
    }
};

struct ALIGNED_TO(sizeof(void *) * 2) MD5Hash final : public std::array<MD5Byte, 16> {
    using array::array;

    constexpr MD5Hash() : array() {}
    MD5Hash(const char *str) : array(MD5String{str}.bytes()) {}
    constexpr MD5Hash(const MD5String &charray) : array(charray.bytes()) {}
    constexpr MD5Hash(const std::array<char, 32> &charray) : array(MD5String::md5chars_to_digest(charray)) {}

    [[nodiscard]] constexpr size_t length() const { return this->size(); }

    [[nodiscard]] constexpr inline MD5String to_chars() const { return MD5String{*this}; }
    [[nodiscard]] inline bool operator==(std::string_view other) const { return this->to_chars().string() == other; }

    inline void clear() { std::memset(this->data(), 0, this->size() * sizeof(MD5Byte)); }

    // you'd have to be extremely unlucky to have an MD5 of all zeros
    [[nodiscard]] inline bool empty() const {
        for(auto byte : *this) {
            if(!!byte) return false;
        }
        return true;
    }

    // this is just a fast heuristic check, a better one would be to check if there's a sequence of contiguous
    // zero memory over a certain length instead of the absolute number of zeros, but that's much slower
    [[nodiscard]] inline bool is_suspicious() const { return std::ranges::count(*this, 0) >= 10; }

    static const MD5Hash sentinel;
};

constexpr inline MD5Hash MD5Hash::sentinel{std::array<char, 32>{                                         //
                                                                'D', 'E', 'A', 'D', 'B', 'E', 'E', 'F',  //
                                                                'D', 'E', 'A', 'D', 'B', 'E', 'E', 'F',  //
                                                                'D', 'E', 'A', 'D', 'B', 'E', 'E', 'F',  //
                                                                'D', 'E', 'A', 'D', 'B', 'E', 'E', 'F'}};

#undef ALIGNED_TO

namespace std {

template <>
struct hash<MD5String> {
    uint64_t operator()(const MD5String &md5) const noexcept { return hash<string_view>{}(md5.string()); }
};

template <>
struct hash<MD5Hash> {
    uint64_t operator()(const MD5Hash &md5) const noexcept {
        return hash<string_view>{}(string_view(reinterpret_cast<const char *>(md5.data()), md5.size()));
    }
};
}  // namespace std

#ifndef BUILD_TOOLS_ONLY

namespace fmt {
template <>
struct formatter<MD5String> : formatter<string_view> {
    template <typename FormatContext>
    auto format(const MD5String &md5, FormatContext &ctx) const noexcept {
        return formatter<string_view>::format(md5.string(), ctx);
    }
};

template <>
struct formatter<MD5Hash> : formatter<MD5String> {
    template <typename FormatContext>
    auto format(const MD5Hash &md5dig, FormatContext &ctx) const noexcept {
        return formatter<MD5String>::format(MD5String{md5dig}, ctx);
    }
};
}  // namespace fmt

template <>
struct Hash::flat::hash<MD5Hash> : Hash::flat::hash<std::string_view> {
    using is_avalanching = void;

    uint64_t operator()(const MD5Hash &md5) const noexcept {
        return Hash::flat::hash<std::string_view>{}(
            std::string_view(reinterpret_cast<const char *>(md5.data()), md5.size()));
    }
};

#endif

#endif
