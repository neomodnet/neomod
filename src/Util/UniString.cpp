// Copyright (c) 2026, WH, All rights reserved.
#include "UniString.h"
#include "noinclude.h"

#include "simdutf.h"

#include <string>
#include <cstring>
#include <bit>
#include <utility>

namespace UniString {

uSz num_codepoints(std::string_view utf8) noexcept {
    if(utf8.empty()) return 0;
    return simdutf::count_utf8(utf8);
}

uSz num_codepoints(std::u16string_view utf16) noexcept {
    if(utf16.empty()) return 0;
    return simdutf::count_utf16(utf16);
}

uSz num_codepoints(std::u32string_view utf32) noexcept { return utf32.size(); }

namespace {
// U+FFFD in UTF-8
constexpr char UTF8_REPLACEMENT[]{'\xEF', '\xBF', '\xBD'};

// rebuild with invalid UTF-8 sequences replaced by U+FFFD
std::string replace_invalid_utf8(const char *input, uSz len) noexcept {
    std::string ret;
    ret.reserve(len);
    uSz i = 0;
    while(i < len) {
        auto result = simdutf::validate_utf8_with_errors(input + i, len - i);
        ret.append(input + i, result.count);
        if(result.error == simdutf::error_code::SUCCESS) break;
        ret.append(&UTF8_REPLACEMENT[0], sizeof(UTF8_REPLACEMENT));
        i += result.count + 1;
    }
    return ret;
}

// replace invalid codepoints with U+FFFD in-place
void sanitize_utf32(char32_t *data, uSz len) noexcept {
    for(uSz i = 0; i < len; i++) {
        if(data[i] > 0x10FFFF || (data[i] >= 0xD800 && data[i] <= 0xDFFF)) data[i] = 0xFFFD;
    }
}

// copy a raw byte stream of the given byte order into native-endian code units
// (copying instead of reinterpreting the bytes in place means the source alignment doesn't matter)
template <typename CharT>
std::basic_string<CharT> to_native_units(const char *bytes, uSz num_bytes, std::endian source) noexcept {
    std::basic_string<CharT> ret;
    ret.resize_and_overwrite(num_bytes / sizeof(CharT), [&](CharT *out, uSz count) -> uSz {
        std::memcpy(out, bytes, count * sizeof(CharT));
        if(source != std::endian::native) {
            for(uSz i = 0; i < count; i++) out[i] = std::byteswap(out[i]);
        }
        return count;
    });
    return ret;
}
}  // namespace

std::string to_utf8(const char *arbitrarily_encoded_data, uSz size) noexcept {
    if(unlikely(!arbitrarily_encoded_data || size == 0)) return {};

    // trust the BOM if there is one, otherwise guess. the guess is validated by autodetect_encoding itself,
    // so BOM-less UTF-8 (the common case) needs no second validation pass
    const auto bom = simdutf::BOM::check_bom(arbitrarily_encoded_data, size);
    const bool has_bom = bom != simdutf::encoding_type::unspecified;
    const auto detected = has_bom ? bom : simdutf::autodetect_encoding(arbitrarily_encoded_data, size);

    const uSz bom_bytes = has_bom ? simdutf::BOM::bom_byte_size(bom) : 0;
    const char *src = arbitrarily_encoded_data + bom_bytes;
    const uSz in_bytes = size - bom_bytes;

    switch(detected) {
        case simdutf::encoding_type::UTF16_LE:
            return to_utf8(to_native_units<char16_t>(src, in_bytes, std::endian::little));
        case simdutf::encoding_type::UTF16_BE:
            return to_utf8(to_native_units<char16_t>(src, in_bytes, std::endian::big));
        case simdutf::encoding_type::UTF32_LE:
            return to_utf8(to_native_units<char32_t>(src, in_bytes, std::endian::little));
        case simdutf::encoding_type::UTF32_BE:
            return to_utf8(to_native_units<char32_t>(src, in_bytes, std::endian::big));
        case simdutf::encoding_type::UTF8:
            if(!has_bom) return {src, in_bytes};
            [[fallthrough]];
        case simdutf::encoding_type::unspecified:
            if(simdutf::validate_utf8(src, in_bytes)) return {src, in_bytes};
            return replace_invalid_utf8(src, in_bytes);
        case simdutf::encoding_type::Latin1:
            // never returned by check_bom or autodetect_encoding
            break;
    }
    std::unreachable();
}

std::string to_utf8(std::string_view arbitrarily_encoded_data) noexcept {
    return to_utf8(arbitrarily_encoded_data.data(), arbitrarily_encoded_data.size());
}

std::string sanitize_utf8(std::string utf8) noexcept {
    if(simdutf::validate_utf8(utf8.data(), utf8.size())) return utf8;
    return replace_invalid_utf8(utf8.data(), utf8.size());
}

std::string to_utf8(std::u16string_view utf16) noexcept {
    if(utf16.empty()) return {};

    // unpaired surrogates become U+FFFD, and the length already accounts for that, so no validation pass is needed
    std::string ret;
    ret.resize_and_overwrite(
        simdutf::utf8_length_from_utf16_with_replacement(utf16.data(), utf16.size()).count, [&](char *out, uSz) -> uSz {
            return simdutf::convert_utf16_to_utf8_with_replacement(utf16.data(), utf16.size(), out);
        });
    return ret;
}

// the remaining conversions validate while converting and write nothing on invalid input,
// so an empty result for a non-empty input means it has to be sanitized first

std::string to_utf8(std::u32string_view utf32) noexcept {
    if(utf32.empty()) return {};

    std::string ret;
    ret.resize_and_overwrite(simdutf::utf8_length_from_utf32(utf32.data(), utf32.size()), [&](char *out, uSz) -> uSz {
        return simdutf::convert_utf32_to_utf8(utf32.data(), utf32.size(), out);
    });
    if(!ret.empty()) return ret;

    std::u32string sanitized(utf32);
    sanitize_utf32(sanitized.data(), sanitized.size());
    return to_utf8(sanitized);
}

std::u16string to_utf16(std::string_view utf8) noexcept {
    if(utf8.empty()) return {};

    std::u16string ret;
    ret.resize_and_overwrite(simdutf::utf16_length_from_utf8(utf8.data(), utf8.size()), [&](char16_t *out, uSz) -> uSz {
        return simdutf::convert_utf8_to_utf16(utf8.data(), utf8.size(), out);
    });
    if(!ret.empty()) return ret;

    return to_utf16(replace_invalid_utf8(utf8.data(), utf8.size()));
}

std::u16string to_utf16(std::u32string_view utf32) noexcept {
    if(utf32.empty()) return {};

    std::u16string ret;
    ret.resize_and_overwrite(
        simdutf::utf16_length_from_utf32(utf32.data(), utf32.size()),
        [&](char16_t *out, uSz) -> uSz { return simdutf::convert_utf32_to_utf16(utf32.data(), utf32.size(), out); });
    if(!ret.empty()) return ret;

    std::u32string sanitized(utf32);
    sanitize_utf32(sanitized.data(), sanitized.size());
    return to_utf16(sanitized);
}

std::u32string to_utf32(std::string_view utf8) noexcept {
    if(utf8.empty()) return {};

    std::u32string ret;
    ret.resize_and_overwrite(simdutf::utf32_length_from_utf8(utf8.data(), utf8.size()), [&](char32_t *out, uSz) -> uSz {
        return simdutf::convert_utf8_to_utf32(utf8.data(), utf8.size(), out);
    });
    if(!ret.empty()) return ret;

    return to_utf32(replace_invalid_utf8(utf8.data(), utf8.size()));
}

std::u32string to_utf32(std::u16string_view utf16) noexcept {
    if(utf16.empty()) return {};

    std::u32string ret;
    ret.resize_and_overwrite(
        simdutf::utf32_length_from_utf16(utf16.data(), utf16.size()),
        [&](char32_t *out, uSz) -> uSz { return simdutf::convert_utf16_to_utf32(utf16.data(), utf16.size(), out); });
    if(!ret.empty()) return ret;

    std::u16string sanitized(utf16);
    simdutf::to_well_formed_utf16(sanitized.data(), sanitized.size(), sanitized.data());
    return to_utf32(sanitized);
}

std::string to_utf8(std::wstring_view wide) noexcept {
    if(wide.empty()) return {};
#if WCHAR_MAX <= 0xFFFF
    return to_utf8(std::u16string_view{reinterpret_cast<const char16_t *>(wide.data()), wide.size()});
#else
    return to_utf8(std::u32string_view{reinterpret_cast<const char32_t *>(wide.data()), wide.size()});
#endif
}

std::wstring to_wide(std::string_view utf8) noexcept {
    if(utf8.empty()) return {};
#if WCHAR_MAX <= 0xFFFF
    auto u16 = to_utf16(utf8);
    return std::wstring{reinterpret_cast<const wchar_t *>(u16.data()), u16.size()};
#else
    auto u32 = to_utf32(utf8);
    return std::wstring{reinterpret_cast<const wchar_t *>(u32.data()), u32.size()};
#endif
}

u8codepoint_view::u8codepoint_view(std::string_view sv) noexcept : m_sv(sv) {}

char32_t u8codepoint_view::iterator::operator*() const noexcept {
    char32_t c = pos[0];
    if(c < 0x80) return c;
    if(c < 0xE0) return (c & 0x1F) << 6 | (pos[1] & 0x3F);
    if(c < 0xF0) return (c & 0x0F) << 12 | (pos[1] & 0x3F) << 6 | (pos[2] & 0x3F);
    return (c & 0x07) << 18 | (pos[1] & 0x3F) << 12 | (pos[2] & 0x3F) << 6 | (pos[3] & 0x3F);
}

u8codepoint_view::iterator &u8codepoint_view::iterator::operator++() noexcept {
    if(pos[0] < 0x80)
        pos += 1;
    else if(pos[0] < 0xE0)
        pos += 2;
    else if(pos[0] < 0xF0)
        pos += 3;
    else
        pos += 4;

    return *this;
}

bool u8codepoint_view::iterator::operator==(const iterator &o) const noexcept { return pos == o.pos; }

u8codepoint_view::iterator u8codepoint_view::begin() const noexcept {
    return {reinterpret_cast<const u8 *>(m_sv.data())};
}

u8codepoint_view::iterator u8codepoint_view::end() const noexcept {
    return {reinterpret_cast<const u8 *>(m_sv.data() + m_sv.size())};
}

u16codepoint_view::u16codepoint_view(std::u16string_view sv) noexcept : m_sv(sv) {}

char32_t u16codepoint_view::iterator::operator*() const noexcept {
    if(*pos >= 0xD800 && *pos <= 0xDBFF) return 0x10000 + (char32_t(*pos - 0xD800) << 10) + (pos[1] - 0xDC00);
    return *pos;
}

u16codepoint_view::iterator &u16codepoint_view::iterator::operator++() noexcept {
    pos += (*pos >= 0xD800 && *pos <= 0xDBFF) ? 2 : 1;
    return *this;
}

bool u16codepoint_view::iterator::operator==(const iterator &o) const noexcept { return pos == o.pos; }

u16codepoint_view::iterator u16codepoint_view::begin() const noexcept {
    return {m_sv.data() /* NOLINT(bugprone-suspicious-stringview-data-usage) */};
}

u16codepoint_view::iterator u16codepoint_view::end() const noexcept { return {m_sv.data() + m_sv.size()}; }

u8codepoint_view codepoints(std::string_view sv) noexcept { return u8codepoint_view{sv}; }

u16codepoint_view codepoints(std::u16string_view sv) noexcept { return u16codepoint_view{sv}; }

uSz prev(std::string_view sv, uSz pos) noexcept {
    if(pos == 0) return 0;
    do {
        pos--;
    } while(pos > 0 && (static_cast<u8>(sv[pos]) & 0xC0) == 0x80);
    return pos;
}

uSz next(std::string_view sv, uSz pos) noexcept {
    if(pos >= sv.size()) return sv.size();
    do {
        pos++;
    } while(pos < sv.size() && (static_cast<u8>(sv[pos]) & 0xC0) == 0x80);
    return pos;
}

}  // namespace UniString
