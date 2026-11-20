// Copyright (c) 2026, WH, All rights reserved.
#include "UniString.h"
#include "noinclude.h"

#include "simdutf.h"

#include <algorithm>
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

// rebuild with ill-formed UTF-8 sequences replaced by U+FFFD, split up like the codepoint iteration does
std::string replace_invalid_utf8(const char *input, uSz len) noexcept {
    std::string ret;
    ret.reserve(len);
    uSz i = 0;
    while(i < len) {
        auto result = simdutf::validate_utf8_with_errors(input + i, len - i);
        ret.append(input + i, result.count);
        if(result.error == simdutf::error_code::SUCCESS) break;
        ret.append(&UTF8_REPLACEMENT[0], sizeof(UTF8_REPLACEMENT));
        i += result.count;
        i += codepoints({input + i, len - i}).begin().length;
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

u32 detail::ill_formed_length(const char *pos, const char *end) noexcept {
    const auto byte = [pos](u32 i) { return static_cast<u8>(pos[i]); };
    const uSz available = static_cast<uSz>(end - pos);
    // the bytes that still fit a well-formed sequence go together, at least the first one
    u32 total = 0;
    u8 firstTrailMin = 0x80, firstTrailMax = 0xBF;
    if(const u8 lead = byte(0); lead >= 0xE0 && lead <= 0xEF) {
        total = 3;
        if(lead == 0xE0) firstTrailMin = 0xA0;  // below: overlong
        if(lead == 0xED) firstTrailMax = 0x9F;  // above: surrogates
    } else if(lead >= 0xF0 && lead <= 0xF4) {
        total = 4;
        if(lead == 0xF0) firstTrailMin = 0x90;  // below: overlong
        if(lead == 0xF4) firstTrailMax = 0x8F;  // above: past U+10FFFF
    }
    if(available < 2 || total == 0 || byte(1) < firstTrailMin || byte(1) > firstTrailMax) return 1;
    u32 fitting = 2;
    while(fitting < total && fitting < available && (byte(fitting) & 0xC0) == 0x80) fitting++;
    return fitting;
}

namespace {
// where the sequence with the byte at `pos` in it starts
uSz sequence_start(std::string_view sv, uSz pos) noexcept {
    const auto isTrail = [sv](uSz i) { return (static_cast<u8>(sv[i]) & 0xC0) == 0x80; };
    if(!isTrail(pos)) return pos;
    // a trail byte belongs to the closest byte before it that isn't one, if the sequence from there reaches it
    for(uSz lead = pos; lead > 0 && pos - lead < 3;) {
        lead--;
        if(isTrail(lead)) continue;
        return lead + codepoints(sv.substr(lead)).begin().length > pos ? lead : pos;
    }
    return pos;
}
}  // namespace

uSz prev(std::string_view sv, uSz pos) noexcept {
    pos = std::min(pos, sv.size());
    return pos == 0 ? 0 : sequence_start(sv, pos - 1);
}

uSz next(std::string_view sv, uSz pos) noexcept {
    if(pos >= sv.size()) return sv.size();
    const uSz start = sequence_start(sv, pos);
    return start + codepoints(sv.substr(start)).begin().length;
}

}  // namespace UniString
