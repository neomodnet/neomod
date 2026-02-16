// Copyright (c) 2009, 2D Boy & PG & 2025, WH, All rights reserved.
#include "UString.h"

#include "simdutf.h"

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <memory>
#include <ranges>
#include <utility>

static constexpr char16_t ESCAPE_CHAR = u'\\';

UString::UString(const char16_t *str) noexcept {
    if(!str || !*str) return;
    this->sUnicode.assign(str);
    updateUtf8();
}

UString::UString(const char16_t *str, int length) noexcept {
    if(!str || length <= 0) return;
    this->sUnicode.assign(str, length);
    updateUtf8();
}

UString::UString(std::u16string_view str) noexcept {
    if(str.empty()) return;
    this->sUnicode.assign(str);
    updateUtf8();
}

UString::UString(std::string_view utf8) noexcept {
    if(utf8.empty()) return;
    this->sUtf8.assign(utf8);
    constructFromSupposedUtf8();
}

UString::UString(std::wstring_view str) noexcept {
    if(str.empty()) return;
#if WCHAR_MAX <= 0xFFFF
    this->sUnicode.assign(reinterpret_cast<std::u16string_view &>(str));
#else
    constructFromUtf32(reinterpret_cast<const char32_t *>(str.data()), str.length());
#endif
    updateUtf8();
}

UString::UString(const wchar_t *str) noexcept {
    if(!str || !*str) return;
#if WCHAR_MAX <= 0xFFFF
    this->sUnicode.assign(reinterpret_cast<const char16_t *>(str), std::wcslen(str));
#else
    constructFromUtf32(reinterpret_cast<const char32_t *>(str), std::wcslen(str));
#endif
    updateUtf8();
}

UString::UString(const wchar_t *str, int length) noexcept {
    if(!str || length <= 0) return;
#if WCHAR_MAX <= 0xFFFF
    this->sUnicode.assign(reinterpret_cast<const char16_t *>(str), length);
#else
    constructFromUtf32(reinterpret_cast<const char32_t *>(str), length);
#endif
    updateUtf8();
}

UString::UString(const char *utf8) noexcept {
    if(!utf8 || !*utf8) return;
    this->sUtf8.assign(utf8, std::strlen(utf8));
    constructFromSupposedUtf8();
}

UString::UString(const char *utf8, int length) noexcept {
    if(!utf8 || length <= 0) return;
    this->sUtf8.assign(utf8, length);
    constructFromSupposedUtf8();
}

UString::UString(std::string utf8) noexcept {
    if(utf8.empty()) return;
#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
    this->sUtf8.assign_range(std::move(utf8));
#else
    this->sUtf8.assign(std::make_move_iterator(utf8.begin()), std::make_move_iterator(utf8.end()));
#endif
    constructFromSupposedUtf8();
}

UString::UString(std::wstring wstring) noexcept {
    if(wstring.empty()) return;
#if WCHAR_MAX <= 0xFFFF
#if defined(__cpp_lib_containers_ranges) && __cpp_lib_containers_ranges >= 202202L
    this->sUnicode.assign_range(std::move(reinterpret_cast<std::u16string &&>(wstring)));
#else
    this->sUnicode.assign(std::make_move_iterator(reinterpret_cast<std::u16string &&>(wstring).begin()),
                          std::make_move_iterator(reinterpret_cast<std::u16string &&>(wstring).end()));
#endif
#else
    constructFromUtf32(reinterpret_cast<std::u32string &&>(wstring));
#endif
    updateUtf8();
}

UString::UString(std::u16string utf16) noexcept {
    if(utf16.empty()) return;
    this->sUnicode = std::move(utf16);
    updateUtf8();
}

UString &UString::operator=(std::nullptr_t) noexcept {
    this->clear();
    return *this;
}

void UString::clear() noexcept {
    this->sUnicode.clear();
    this->sUtf8.clear();
}

UString UString::join(std::span<const UString> strings, std::string_view delim) noexcept {
    if(strings.empty()) return {};

    UString delimStr(delim);
    UString result = strings[0];

    for(size_t i = 1; i < strings.size(); ++i) {
        result += delimStr;
        result += strings[i];
    }

    return result;
}

bool UString::isWhitespaceOnly() const noexcept {
    return this->sUnicode.empty() ||
           std::ranges::all_of(this->sUnicode, [](char16_t c) { return std::iswspace(static_cast<wint_t>(c)) != 0; });
}

size_t UString::numCodepoints() const noexcept {
    return simdutf::count_utf16le(this->sUnicode.data(), this->sUnicode.size());
}

// private helper
int UString::findCharSimd(char16_t ch, int start, int end) const noexcept {
    const char16_t *searchStart = this->sUnicode.data() + start;
    const char16_t *searchEnd = this->sUnicode.data() + end;
    const char16_t *result = simdutf::find(searchStart, searchEnd, ch);
    return (result != searchEnd) ? static_cast<int>(result - this->sUnicode.data()) : -1;
}

int UString::find(char16_t ch, std::optional<int> startOpt, std::optional<int> endOpt,
                  bool respectEscapeChars) const noexcept {
    int len = length();
    int start = startOpt.value_or(0);
    int end = endOpt.value_or(len);

    if(start < 0 || end > len || start >= end) return -1;

    if(!respectEscapeChars) {
        return findCharSimd(ch, start, end);
    }

    bool escaped = false;
    for(int i = start; i < end; i++) {
        if(!escaped && this->sUnicode[i] == ESCAPE_CHAR) {
            escaped = true;
        } else {
            if(!escaped && this->sUnicode[i] == ch) return i;
            escaped = false;
        }
    }

    return -1;
}

int UString::findFirstOf(const UString &str, int start, bool respectEscapeChars) const noexcept {
    int len = length();
    int strLen = str.length();
    if(start < 0 || start >= len || strLen == 0) return -1;

    // delegate to find(char16_t)
    if(strLen == 1) {
        return find(str.sUnicode[0], start, std::nullopt, respectEscapeChars);
    }

    // multi-character case, build character set
    std::bitset<0x10000> charMap{};
    for(int i = 0; i < strLen; i++) {
        charMap[str.sUnicode[i]] = true;
    }

    bool escaped = false;
    for(int i = start; i < len; i++) {
        if(respectEscapeChars && !escaped && this->sUnicode[i] == ESCAPE_CHAR) {
            escaped = true;
        } else {
            if(!escaped && charMap[this->sUnicode[i]]) return i;
            escaped = false;
        }
    }

    return -1;
}

int UString::find(const UString &str, std::optional<int> startOpt, std::optional<int> endOpt) const noexcept {
    int strLen = str.length();
    int len = length();

    int start = startOpt.value_or(0);
    int end = endOpt.value_or(len);

    if(start < 0 || end > len || start >= end || strLen == 0 || strLen > end - start) return -1;

    // single character, use simd
    if(strLen == 1) {
        return findCharSimd(str.sUnicode[0], start, end);
    }

    // full-string search
    if(end == len) {
        size_t pos = this->sUnicode.find(str.sUnicode, start);
        return (pos != std::u16string::npos) ? static_cast<int>(pos) : -1;
    }

    // bounded search, extract substring
    auto tempSubstr = this->sUnicode.substr(start, end - start);
    size_t pos = tempSubstr.find(str.sUnicode);
    return (pos != std::u16string::npos) ? static_cast<int>(pos + start) : -1;
}

int UString::findLast(const UString &str, std::optional<int> startOpt, std::optional<int> endOpt) const noexcept {
    int strLen = str.length();
    int len = length();
    int start = startOpt.value_or(0);
    int end = endOpt.value_or(len);

    if(start < 0 || end > len || start >= end || strLen == 0 || strLen > end - start) return -1;

    // full-string search
    if(end == len) {
        size_t pos = this->sUnicode.rfind(str.sUnicode, end - 1);
        if(pos != std::u16string::npos && pos >= static_cast<size_t>(start)) return static_cast<int>(pos);
        return -1;
    }

    // bounded search, manual backward search
    for(int i = end - strLen; i >= start; i--) {
        if(std::equal(str.sUnicode.begin(), str.sUnicode.end(), this->sUnicode.begin() + i)) return i;
    }

    return -1;
}

int UString::findIgnoreCase(const UString &str, std::optional<int> startOpt, std::optional<int> endOpt) const noexcept {
    int strLen = str.length();
    int len = length();
    int start = startOpt.value_or(0);
    int end = endOpt.value_or(len);

    if(start < 0 || end > len || start >= end || strLen == 0 || strLen > end - start) return -1;

    auto toLower = [](auto c) { return std::towlower(static_cast<wint_t>(c)); };

    auto sourceView =
        this->sUnicode | std::views::drop(start) | std::views::take(end - start) | std::views::transform(toLower);
    auto targetView = str.sUnicode | std::views::transform(toLower);

    auto result = std::ranges::search(sourceView, targetView);

    if(!result.empty()) return static_cast<int>(std::distance(sourceView.begin(), result.begin())) + start;

    return -1;
}

void UString::collapseEscapes() noexcept {
    int len = length();
    if(len == 0) return;

    std::u16string result;
    result.reserve(len);

    bool escaped = false;
    for(char16_t ch : this->sUnicode) {
        if(!escaped && ch == ESCAPE_CHAR) {
            escaped = true;
        } else {
            result.push_back(ch);
            escaped = false;
        }
    }

    this->sUnicode = std::move(result);
    updateUtf8();
}

void UString::append(const UString &str) noexcept {
    if(str.length() == 0) return;
    size_t oldLength = this->sUnicode.length();
    this->sUnicode.append(str.sUnicode);
    updateUtf8(oldLength);
}

void UString::append(char16_t ch) noexcept {
    size_t oldLength = this->sUnicode.length();
    this->sUnicode.push_back(ch);
    updateUtf8(oldLength);
}

void UString::insert(int offset, const UString &str) noexcept {
    if(str.length() == 0) return;

    int len = length();
    offset = std::clamp(offset, 0, len);
    this->sUnicode.insert(offset, str.sUnicode);
    updateUtf8();
}

void UString::insert(int offset, char16_t ch) noexcept {
    int len = length();
    offset = std::clamp(offset, 0, len);
    this->sUnicode.insert(offset, 1, ch);
    updateUtf8();
}

void UString::erase(int offset, int count) noexcept {
    int len = length();
    if(len == 0 || count == 0 || offset >= len) return;

    offset = std::clamp(offset, 0, len);
    count = std::clamp(count, 0, len - offset);

    this->sUnicode.erase(offset, count);
    updateUtf8();
}

UString UString::trim() const noexcept {
    int len = length();
    if(len == 0) return {};

    auto isWhitespace = [](char16_t c) { return std::iswspace(static_cast<wint_t>(c)) != 0; };

    auto start = std::ranges::find_if_not(this->sUnicode, isWhitespace);
    if(start == this->sUnicode.end()) return {};

    auto rstart = std::ranges::find_if_not(std::ranges::reverse_view(this->sUnicode), isWhitespace);
    auto end = rstart.base();

    int startPos = static_cast<int>(std::distance(this->sUnicode.begin(), start));
    int length = static_cast<int>(std::distance(start, end));

    return substr(startPos, length);
}

void UString::lowerCase() noexcept {
    if(length() == 0) return;

    std::ranges::transform(this->sUnicode, this->sUnicode.begin(),
                           [](char16_t c) { return static_cast<char16_t>(std::towlower(static_cast<wint_t>(c))); });

    updateUtf8();
}

void UString::upperCase() noexcept {
    if(length() == 0) return;

    std::ranges::transform(this->sUnicode, this->sUnicode.begin(),
                           [](char16_t c) { return static_cast<char16_t>(std::towupper(static_cast<wint_t>(c))); });

    updateUtf8();
}

UString &UString::operator+=(const UString &ustr) noexcept {
    append(ustr);
    return *this;
}

UString UString::operator+(const UString &ustr) const noexcept {
    UString result(*this);
    result.append(ustr);
    return result;
}

UString &UString::operator+=(char16_t ch) noexcept {
    append(ch);
    return *this;
}

UString UString::operator+(char16_t ch) const noexcept {
    UString result(*this);
    result.append(ch);
    return result;
}

UString &UString::operator+=(char ch) noexcept {
    append(static_cast<char16_t>(ch));
    return *this;
}

UString UString::operator+(char ch) const noexcept {
    UString result(*this);
    result.append(static_cast<char16_t>(ch));
    return result;
}

bool UString::equalsIgnoreCase(const UString &ustr) const noexcept {
    if(length() != ustr.length()) return false;
    if(length() == 0 && ustr.length() == 0) return true;

    return std::ranges::equal(this->sUnicode, ustr.sUnicode, [](char16_t a, char16_t b) {
        return std::towlower(static_cast<wint_t>(a)) == std::towlower(static_cast<wint_t>(b));
    });
}

bool UString::lessThanIgnoreCase(const UString &ustr) const noexcept {
    auto it1 = this->sUnicode.begin();
    auto it2 = ustr.sUnicode.begin();

    while(it1 != this->sUnicode.end() && it2 != ustr.sUnicode.end()) {
        const auto c1 = std::towlower(static_cast<wint_t>(*it1));
        const auto c2 = std::towlower(static_cast<wint_t>(*it2));
        if(c1 != c2) return c1 < c2;
        ++it1;
        ++it2;
    }

    // if we've reached the end of one string but not the other,
    // the shorter string is lexicographically less
    return it1 == this->sUnicode.end() && it2 != ustr.sUnicode.end();
}

// only to be used in very specific scenarios
std::wstring UString::to_wstring() const noexcept {
#ifdef MCENGINE_PLATFORM_WINDOWS
    return std::wstring{reinterpret_cast<const wchar_t *>(this->sUnicode.data())};
#else
    std::wstring ret;
    size_t utf32Length = simdutf::utf32_length_from_utf16(this->sUnicode.data(), this->sUnicode.length());
    ret.resize_and_overwrite(utf32Length, [&](wchar_t *data, size_t /* size */) -> size_t {
        return simdutf::convert_utf16_to_utf32(this->sUnicode.data(), this->sUnicode.size(),
                                               reinterpret_cast<char32_t *>(data));
    });
    return ret;
#endif
};

#ifdef MCENGINE_PLATFORM_WINDOWS
// "deprecated"
std::wstring_view UString::wstringView() const noexcept {
    return static_cast<std::wstring_view>(reinterpret_cast<const std::wstring &>(this->sUnicode));
}
const wchar_t *UString::wchar_str() const noexcept { return reinterpret_cast<const wchar_t *>(this->sUnicode.data()); }
#endif

void UString::constructFromUtf32(std::u32string utf32) noexcept {
    if(utf32.empty()) return;

    size_t utf16Length = simdutf::utf16_length_from_utf32(utf32);
    this->sUnicode.resize_and_overwrite(utf16Length, [&](char16_t *data, size_t size) -> size_t {
        return simdutf::convert_utf32_to_utf16le(utf32, std::span<char16_t>(data, size));
    });
}

void UString::constructFromUtf32(const char32_t *utf32, size_t char32Length) noexcept {
    if(!utf32 || char32Length == 0) return;

    size_t utf16Length = simdutf::utf16_length_from_utf32(utf32, char32Length);
    this->sUnicode.resize_and_overwrite(utf16Length, [&](char16_t *data, size_t /*size*/) -> size_t {
        return simdutf::convert_utf32_to_utf16le(utf32, char32Length, data);
    });
}

void UString::updateUtf8(size_t startUtf16) noexcept {
    if(this->sUnicode.empty()) {
        this->sUtf8.clear();
        return;
    }

    if(startUtf16 == 0) {
        // full conversion
        size_t utf8Length = simdutf::utf8_length_from_utf16le(this->sUnicode.data(), this->sUnicode.size());

        this->sUtf8.resize_and_overwrite(utf8Length, [&](char *data, size_t /* size */) -> size_t {
            return simdutf::convert_utf16le_to_utf8(this->sUnicode.data(), this->sUnicode.size(), data);
        });
    } else {
        // partial conversion (append only the new portion)
        // assumes sUtf8 is already valid up to the position corresponding to startUtf16
        const char16_t *src = this->sUnicode.data() + startUtf16;
        const size_t srcLength = this->sUnicode.size() - startUtf16;

        size_t additionalUtf8Length = simdutf::utf8_length_from_utf16le(src, srcLength);
        size_t oldUtf8Length = this->sUtf8.size();

        this->sUtf8.resize_and_overwrite(
            oldUtf8Length + additionalUtf8Length, [&](char *data, size_t /* size */) -> size_t {
                size_t written = simdutf::convert_utf16le_to_utf8(src, srcLength, data + oldUtf8Length);
                return oldUtf8Length + written;
            });
    }
}

// this is only called from specific constructors, so assume that the parameters utf8 == this->sUtf8, char8Length == this->sUtf8.length()
void UString::constructFromSupposedUtf8() noexcept {
    const char *utf8 = this->sUtf8.c_str();
    const size_t char8Length = this->sUtf8.length();
    assert(!!utf8 && (char8Length > 0));

    // detect encoding with BOM support
    size_t bomPrefixBytes = 0;

    // check up to 4 bytes, since a UTF-32 BOM is 4 bytes
    simdutf::encoding_type detected = simdutf::BOM::check_bom(utf8, std::min<size_t>(4, char8Length));

    if(detected != simdutf::encoding_type::unspecified) {
        // remove BOM from conversion
        bomPrefixBytes = simdutf::BOM::bom_byte_size(detected);
        // sanity
        assert(bomPrefixBytes <= char8Length);
    } else {
        // if there was no BOM, autodetect encoding
        detected = simdutf::autodetect_encoding(utf8, char8Length);
    }

    const char *utf8src = &(utf8[bomPrefixBytes]);
    const size_t utf8Length = char8Length - bomPrefixBytes;

    switch(detected) {
        case simdutf::encoding_type::unspecified:
            // fallthrough, assume UTF-8
        case simdutf::encoding_type::UTF8: {
            const size_t utf16Length = simdutf::utf16_length_from_utf8(utf8src, utf8Length);
            this->sUnicode.resize_and_overwrite(utf16Length, [&](char16_t *data, size_t /*size*/) -> size_t {
                return simdutf::convert_utf8_to_utf16le(utf8src, utf8Length, data);
            });
            break;
        }

        case simdutf::encoding_type::UTF16_LE:
        case simdutf::encoding_type::UTF16_BE: {
            // make GCC understand that our internal utf8 data is always aligned to alignof(char32_t)
            // after possibly 2 bytes for the BOM it's still aligned to alignof(char16_t)
            const void *alignedVoidSrc = std::assume_aligned<alignof(char16_t)>(utf8src);
            const auto *utf16src = reinterpret_cast<const char16_t *>(alignedVoidSrc);
            const size_t utf16Length = utf8Length / 2;
            this->sUnicode.assign(utf16src, utf16Length);
            if(utf16Length > 0 && detected == simdutf::encoding_type::UTF16_BE) {
                // swap to UTF16_LE internal representation
                simdutf::change_endianness_utf16(this->sUnicode.data(), this->sUnicode.length(), this->sUnicode.data());
            }
            break;
        }

        case simdutf::encoding_type::UTF32_LE:
        case simdutf::encoding_type::UTF32_BE: {
            const void *alignedVoidSrc = std::assume_aligned<alignof(char32_t)>(utf8src);
            const auto *utf32src = reinterpret_cast<const char32_t *>(alignedVoidSrc);
            const size_t utf32Length = utf8Length / 4;
            const size_t utf16Length = simdutf::utf16_length_from_utf32(utf32src, utf32Length);
            this->sUnicode.resize_and_overwrite(utf16Length, [&](char16_t *out, size_t /*size*/) -> size_t {
                if(detected == simdutf::encoding_type::UTF32_BE) {
                    const size_t cvtAmount = simdutf::convert_utf32_to_utf16be(utf32src, utf32Length, out);
                    if(cvtAmount > 0) simdutf::change_endianness_utf16(out, cvtAmount, out);
                    return cvtAmount;
                } else {
                    return simdutf::convert_utf32_to_utf16le(utf32src, utf32Length, out);
                }
            });

            break;
        }

        case simdutf::encoding_type::Latin1:
            /* ... the function might return simdutf::encoding_type::UTF8,
            * simdutf::encoding_type::UTF16_LE, simdutf::encoding_type::UTF16_BE, or
            * simdutf::encoding_type::UTF32_LE.
            */
            std::unreachable();
            break;
    }

    const bool wasAlreadyUTF8 =
        detected == simdutf::encoding_type::UTF8 || detected == simdutf::encoding_type::unspecified;

    // re-convert our malformed (not UTF-8) representation to proper UTF-8
    if(!wasAlreadyUTF8 || bomPrefixBytes > 0) {
        // fast-path, just strip the BOM prefix if it was already UTF-8
        if(wasAlreadyUTF8) {
            this->sUtf8.erase(0, bomPrefixBytes);
        } else {
            // otherwise fully re-convert from our new unicode representation
            updateUtf8();
        }
    }
}
