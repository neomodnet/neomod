// Copyright (c) 2009, 2D Boy & PG & 2025, WH, All rights reserved.
#pragma once
#include <algorithm>
#include <cassert>
#include <cstring>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <charconv>

#include "BaseEnvironment.h"  // for Env::cfg (consteval)

#ifdef MCENGINE_PLATFORM_WINDOWS
#include <malloc.h>
#else
#include <cstdlib>
#endif

#include "fmt/format.h"
#include "fmt/printf.h"
#include "fmt/compile.h"

using fmt::literals::operator""_cf;
using std::string_view_literals::operator""sv;

#ifndef MCENGINE_PLATFORM_WINDOWS
#define delete_if_not_windows = delete
#else
#define delete_if_not_windows
#endif

template <typename T>
class AlignedAllocator {
   public:
    using value_type = T;
    static constexpr std::align_val_t alignment{16};

    AlignedAllocator() noexcept = default;
    template <typename U>
    constexpr AlignedAllocator(const AlignedAllocator<U> & /**/) noexcept {}

    [[nodiscard]] T *allocate(size_t n) noexcept {
        if(n > std::numeric_limits<size_t>::max() / sizeof(T)) {
            fubar_abort();
        }

        const size_t bytes =
            ((n * sizeof(T)) + (static_cast<size_t>(alignment) - 1)) & ~(static_cast<size_t>(alignment) - 1);
#ifdef MCENGINE_PLATFORM_WINDOWS
        return static_cast<T *>(_aligned_malloc(bytes, static_cast<size_t>(alignment)));
#else
        return static_cast<T *>(std::aligned_alloc(static_cast<size_t>(alignment), bytes));
#endif
    }

    void deallocate(T *p, size_t /**/) noexcept {
#ifdef MCENGINE_PLATFORM_WINDOWS
        _aligned_free(p);
#else
        std::free(p);
#endif
    }

    template <typename U>
    constexpr bool operator==(const AlignedAllocator<U> & /**/) const noexcept {
        return true;
    }
};

class UString {
   public:
    template <typename... Args>
    [[nodiscard]] static UString format(std::string_view fmt, Args &&...args) noexcept;

    template <typename Range>
        requires std::ranges::range<Range> && std::convertible_to<std::ranges::range_value_t<Range>, UString>
    [[nodiscard]] static UString join(const Range &range, std::string_view delim = " ") noexcept;

    [[nodiscard]] static UString join(std::span<const UString> strings, std::string_view delim = " ") noexcept;

   public:
    // constructors
    constexpr UString() noexcept = default;
    UString(std::nullptr_t) = delete;
    UString(const char16_t *str) noexcept;
    UString(const char16_t *str, int length) noexcept;
    UString(std::u16string_view str) noexcept;
    UString(std::string_view utf8) noexcept;
    UString(std::wstring_view str) noexcept;

    UString(const wchar_t *str) noexcept;
    UString(const wchar_t *str, int length) noexcept;
    UString(const char *utf8) noexcept;
    UString(const char *utf8, int length) noexcept;

    UString(std::string utf8) noexcept;
    UString(std::wstring wchar) noexcept;
    UString(std::u16string utf16) noexcept;

    inline constexpr UString(std::string_view utf8, std::u16string_view unicode) noexcept
        : sUnicode(unicode), sUtf8(utf8) {}
#define US_(str__) \
    UString { str__##sv, u##str__##sv }  // is C++ even powerful enough to do this without macros

    // member functions
    UString(const UString &ustr) noexcept = default;
    UString(UString &&ustr) noexcept : sUnicode(std::move(ustr.sUnicode)), sUtf8(std::move(ustr.sUtf8)) {}
    UString &operator=(const UString &ustr) noexcept = default;
    UString &operator=(UString &&ustr) noexcept {
        if(this == &ustr) return *this;
        sUnicode = std::move(ustr.sUnicode);
        sUtf8 = std::move(ustr.sUtf8);
        return *this;
    }
    UString &operator=(std::nullptr_t) noexcept;
    ~UString() noexcept = default;

    // basic operations
    void clear() noexcept;

    // getters
    [[nodiscard]] constexpr int length() const noexcept { return static_cast<int>(this->sUnicode.length()); }
    [[nodiscard]] constexpr int lengthUtf8() const noexcept { return static_cast<int>(this->sUtf8.length()); }
    [[nodiscard]] size_t numCodepoints() const noexcept;
    [[nodiscard]] constexpr std::string_view utf8View() const noexcept { return this->sUtf8; }
    [[nodiscard]] constexpr const char *toUtf8() const noexcept { return this->sUtf8.c_str(); }
    [[nodiscard]] constexpr std::u16string_view u16View() const noexcept { return this->sUnicode; }
    [[nodiscard]] constexpr const char16_t *u16_str() const noexcept { return this->sUnicode.c_str(); }

    [[nodiscard]] std::wstring to_wstring() const noexcept;
    [[nodiscard]] std::wstring_view wstringView() const noexcept delete_if_not_windows;
    [[nodiscard]] const wchar_t *wchar_str() const noexcept delete_if_not_windows;

    // platform-specific string access (for filesystem operations, etc.)
    [[nodiscard]] constexpr const auto *plat_str() const noexcept {
        // this crazy casting->.data() thing is required for MSVC, otherwise its not constexpr
        if constexpr(Env::cfg(OS::WINDOWS))
            return static_cast<std::wstring_view>(reinterpret_cast<const std::wstring &>(this->sUnicode)).data();
        else
            return this->sUtf8.c_str();
    }

    // state queries
    [[nodiscard]] constexpr bool isEmpty() const noexcept { return this->sUnicode.empty(); }
    [[nodiscard]] bool isWhitespaceOnly() const noexcept;

    // string tests
    [[nodiscard]] constexpr bool endsWith(char ch) const noexcept {
        return !this->sUtf8.empty() && this->sUtf8.back() == ch;
    }
    [[nodiscard]] constexpr bool endsWith(char16_t ch) const noexcept {
        return !this->sUnicode.empty() && this->sUnicode.back() == ch;
    }
    [[nodiscard]] constexpr bool endsWith(const UString &suffix) const noexcept {
        if(this->sUnicode.empty()) return false;
        int suffixLen = suffix.length();
        int thisLen = length();
        return suffixLen <= thisLen &&
               std::equal(suffix.sUnicode.begin(), suffix.sUnicode.end(), this->sUnicode.end() - suffixLen);
    }
    [[nodiscard]] constexpr bool startsWith(char ch) const noexcept {
        return !this->sUtf8.empty() && this->sUtf8.front() == ch;
    }
    [[nodiscard]] constexpr bool startsWith(char16_t ch) const noexcept {
        return !this->sUnicode.empty() && this->sUnicode.front() == ch;
    }
    [[nodiscard]] constexpr bool startsWith(const UString &prefix) const noexcept {
        if(this->sUnicode.empty()) return false;
        int prefixLen = prefix.length();
        int thisLen = length();
        return prefixLen <= thisLen &&
               std::equal(prefix.sUnicode.begin(), prefix.sUnicode.end(), this->sUnicode.begin());
    }

    // search functions
    [[nodiscard]] int find(char16_t ch, std::optional<int> startOpt = std::nullopt,
                           std::optional<int> endOpt = std::nullopt, bool respectEscapeChars = false) const noexcept;
    [[nodiscard]] int findFirstOf(const UString &str, int start = 0, bool respectEscapeChars = false) const noexcept;
    [[nodiscard]] int find(const UString &str, std::optional<int> startOpt = std::nullopt,
                           std::optional<int> endOpt = std::nullopt) const noexcept;
    [[nodiscard]] int findLast(const UString &str, std::optional<int> startOpt = std::nullopt,
                               std::optional<int> endOpt = std::nullopt) const noexcept;
    [[nodiscard]] int findIgnoreCase(const UString &str, std::optional<int> startOpt = std::nullopt,
                                     std::optional<int> endOpt = std::nullopt) const noexcept;

    // iterators for range-based for loops
    [[nodiscard]] constexpr auto begin() const noexcept { return this->sUnicode.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return this->sUnicode.end(); }
    [[nodiscard]] constexpr auto cbegin() const noexcept { return this->sUnicode.cbegin(); }
    [[nodiscard]] constexpr auto cend() const noexcept { return this->sUnicode.cend(); }

    // modifiers
    void collapseEscapes() noexcept;
    void append(const UString &str) noexcept;
    void append(char16_t ch) noexcept;
    void insert(int offset, const UString &str) noexcept;
    void insert(int offset, char16_t ch) noexcept;

    void erase(int offset, int count) noexcept;

    [[nodiscard]] inline constexpr const char16_t &front() const noexcept {
        assert(!this->isEmpty());
        return operator[](0);
    }
    [[nodiscard]] inline constexpr const char16_t &back() const noexcept {
        assert(!this->isEmpty());
        return operator[](this->length() - 1);
    }
    inline void pop_back() noexcept {
        if(!this->isEmpty()) this->erase(this->length() - 1, 1);
    }
    inline void pop_front() noexcept {
        if(!this->isEmpty()) this->erase(0, 1);
    }

    // actions (non-modifying)
    template <typename T = UString>
    [[nodiscard]] constexpr T substr(int offset, int charCount = -1) const noexcept {
        int len = length();
        offset = std::clamp<int>(offset, 0, len);

        if(charCount < 0) charCount = len - offset;
        charCount = std::clamp<int>(charCount, 0, len - offset);

        UString result;
        result.sUnicode = this->sUnicode.substr(offset, charCount);
        result.updateUtf8();

        if constexpr(std::is_same_v<T, UString>)
            return result;
        else
            return result.to<T>().first;
    }

    template <typename T = UString>
    [[nodiscard]] std::vector<T> split(const UString &delim) const noexcept {
        std::vector<T> results;
        int delimLen = delim.length();
        int thisLen = length();
        if(delimLen < 1 || thisLen < 1) return results;

        int start = 0;
        int end = 0;

        while((end = find(delim, start)) != -1) {
            results.push_back(substr<T>(start, end - start));
            start = end + delimLen;
        }
        results.push_back(substr<T>(start));

        return results;
    }

    [[nodiscard]] UString trim() const noexcept;

    // type conversions
    template <typename T>
    [[nodiscard]] constexpr std::pair<T, std::errc> to() const noexcept {
        if(this->sUtf8.empty()) return {T{}, std::errc::invalid_argument};

        std::errc reterr{std::errc{}};
        if constexpr(std::is_same_v<T, UString>)
            return {*this, reterr};
        else if constexpr(std::is_same_v<T, std::string>)
            return {std::string{this->sUtf8}, reterr};
        else if constexpr(std::is_same_v<T, std::string_view>)
            return {std::string_view{this->sUtf8}, reterr};
        else if constexpr(std::is_same_v<T, std::u16string>)
            return {std::u16string{this->sUnicode}, reterr};
        else if constexpr(std::is_same_v<T, std::u16string_view>)
            return {std::u16string_view{this->sUnicode}, reterr};
        else {
            T ret{};

            const char *begin{this->sUtf8.data()};
            const char *end{this->sUtf8.data() + this->sUtf8.size()};
            if constexpr(std::is_same_v<T, bool>) {
                long temp{};
                auto [_, ec] = std::from_chars(begin, end, temp);
                reterr = ec;
                ret = (temp > 0);
            } else {
                auto [_, ec] = std::from_chars(begin, end, ret);
                reterr = ec;
            }

            return {ret, reterr};
        }
    }

    // case conversion
    void lowerCase() noexcept;
    void upperCase() noexcept;

    // operators
    [[nodiscard]] constexpr const char16_t &operator[](int index) const noexcept { return this->sUnicode[index]; }

    friend bool operator==(const UString &ustr, const std::string &utf8v) noexcept {
        return static_cast<const std::string_view &>(ustr.sUtf8) == utf8v;
    }
    friend auto operator<=>(const UString &ustr, const std::string &utf8v) noexcept {
        return std::operator<=>(static_cast<const std::string_view &>(ustr.sUtf8), utf8v);
    }
    friend bool operator==(const UString &ustr, const std::u16string &utf16v) noexcept {
        return ustr.sUnicode == utf16v;
    }
    friend auto operator<=>(const UString &ustr, const std::u16string &utf16v) noexcept {
        return std::operator<=>(ustr.sUnicode, utf16v);
    }
    friend bool operator==(const std::string &utf8v, const UString &ustr) noexcept {
        return static_cast<const std::string_view &>(ustr.sUtf8) == utf8v;
    }
    friend auto operator<=>(const std::string &utf8v, const UString &ustr) noexcept {
        return std::operator<=>(static_cast<const std::string_view &>(ustr.sUtf8), utf8v);
    }
    friend bool operator==(const std::u16string &utf16v, const UString &ustr) noexcept {
        return ustr.sUnicode == utf16v;
    }
    friend auto operator<=>(const std::u16string &utf16v, const UString &ustr) noexcept {
        return std::operator<=>(ustr.sUnicode, utf16v);
    }
    friend bool operator==(const UString &ustr, std::string_view utf8v) noexcept {
        return static_cast<const std::string_view &>(ustr.sUtf8) == utf8v;
    }
    friend auto operator<=>(const UString &ustr, std::string_view utf8v) noexcept {
        return std::operator<=>(static_cast<const std::string_view &>(ustr.sUtf8), utf8v);
    }
    friend bool operator==(const UString &ustr, std::u16string_view utf16v) noexcept { return ustr.sUnicode == utf16v; }
    friend auto operator<=>(const UString &ustr, std::u16string_view utf16v) noexcept {
        return std::operator<=>(static_cast<const std::u16string_view &>(ustr.sUnicode), utf16v);
    }
    friend bool operator==(std::string_view utf8v, const UString &ustr) noexcept {
        return static_cast<const std::string_view &>(ustr.sUtf8) == utf8v;
    }
    friend auto operator<=>(std::string_view utf8v, const UString &ustr) noexcept {
        return std::operator<=>(static_cast<const std::string_view &>(ustr.sUtf8), utf8v);
    }
    friend bool operator==(std::u16string_view utf16v, const UString &ustr) noexcept { return ustr.sUnicode == utf16v; }
    friend auto operator<=>(std::u16string_view utf16v, const UString &ustr) noexcept {
        return std::operator<=>(static_cast<const std::u16string_view &>(ustr.sUnicode), utf16v);
    }
    friend bool operator==(const UString &u1, const UString &u2) noexcept { return u1.sUnicode == u2.sUnicode; };
    friend auto operator<=>(const UString &u1, const UString &u2) noexcept {
        return std::operator<=>(u1.sUnicode, u2.sUnicode);
    };

    UString &operator+=(const UString &ustr) noexcept;
    [[nodiscard]] UString operator+(const UString &ustr) const noexcept;
    UString &operator+=(char16_t ch) noexcept;
    [[nodiscard]] UString operator+(char16_t ch) const noexcept;
    UString &operator+=(char ch) noexcept;
    [[nodiscard]] UString operator+(char ch) const noexcept;

    [[nodiscard]] bool equalsIgnoreCase(const UString &ustr) const noexcept;
    [[nodiscard]] bool lessThanIgnoreCase(const UString &ustr) const noexcept;

    friend struct std::hash<UString>;

   private:
    using alignedUTF8String = std::basic_string<char, std::char_traits<char>, AlignedAllocator<char>>;

    // deduplication helper
    [[nodiscard]] int findCharSimd(char16_t ch, int start, int end) const noexcept;

    // constructor helpers
    void constructFromUtf32(std::u32string utf32) noexcept;
    void constructFromUtf32(const char32_t *utf32, size_t length) noexcept;
    void constructFromSupposedUtf8() noexcept;

    // for updating utf8 representation when unicode representation changes
    void updateUtf8(size_t startUtf16 = 0) noexcept;

    std::u16string sUnicode;
    alignedUTF8String sUtf8;
};

namespace std {
template <>
struct hash<UString> {
    uint64_t operator()(const UString &str) const noexcept { return hash<std::u16string>()(str.sUnicode); }
};
}  // namespace std

// forward decls to avoid including simdutf here
namespace simdutf {
extern size_t utf8_length_from_utf16le(const char16_t *input, size_t length) noexcept;
extern size_t convert_utf16le_to_utf8(const char16_t *input, size_t length, char *utf8_output) noexcept;
}  // namespace simdutf

// need a specialization for fmt, so that UStrings can be passed directly without needing .toUtf8() (for fmt::format)
namespace fmt {
template <>
struct formatter<UString> : formatter<string_view> {
    template <typename FormatContext>
    auto format(const UString &ustr, FormatContext &ctx) const noexcept {
        return formatter<string_view>::format(ustr.utf8View(), ctx);
    }
};

// u16string_view support
template <>
struct formatter<std::u16string_view> : formatter<string_view> {
    template <typename FormatContext>
    auto format(std::u16string_view str, FormatContext &ctx) const noexcept {
        size_t utf8_length = simdutf::utf8_length_from_utf16le(str.data(), str.size());
        std::string result;
        result.resize_and_overwrite(utf8_length, [&](char *data, size_t /* size */) -> size_t {
            return simdutf::convert_utf16le_to_utf8(str.data(), str.size(), data);
        });
        return formatter<string_view>::format(result, ctx);
    }
};
}  // namespace fmt

template <typename Range>
    requires std::ranges::range<Range> && std::convertible_to<std::ranges::range_value_t<Range>, UString>
UString UString::join(const Range &range, std::string_view delim) noexcept {
    if(std::ranges::empty(range)) return {};

    UString delimStr(delim);
    auto it = std::ranges::begin(range);
    UString result = *it;
    ++it;

    for(; it != std::ranges::end(range); ++it) {
        result += delimStr;
        result += *it;
    }

    return result;
}

#undef delete_if_not_windows
