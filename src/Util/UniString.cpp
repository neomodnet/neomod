// Copyright (c) 2026, WH, All rights reserved.
#include "UniString.h"
#include "noinclude.h"

#include "simdutf.h"

#include <string>
#include <cstring>
#include <bit>
#include <cassert>
#include <utility>
#include <new>
#include <type_traits>

namespace UniString {

uSz num_codepoints(std::string_view utf8) noexcept {
    if(utf8.empty()) return 0;
    return simdutf::count_utf8(utf8);
}

uSz num_codepoints(std::u16string_view utf16) noexcept {
    if(utf16.empty()) return 0;
    return simdutf::count_utf16le(utf16);
}

uSz num_codepoints(std::u32string_view utf32) noexcept { return utf32.size(); }

namespace {

// U+FFFD in UTF-8
constexpr char UTF8_REPLACEMENT[]{'\xEF', '\xBF', '\xBD'};

// replace invalid UTF-8 sequences with U+FFFD.
// first_error is the byte offset of the first known-bad byte (from validate_utf8_with_errors)
std::string sanitize_utf8(const char *input, uSz len, uSz first_error) noexcept {
    std::string ret;
    ret.reserve(len);
    // copy the valid prefix before the first error
    ret.append(input, first_error);
    ret.append(&UTF8_REPLACEMENT[0], sizeof(UTF8_REPLACEMENT));
    uSz i = first_error + 1;
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

struct AlignedBuffer {
    NOCOPY_NOMOVE(AlignedBuffer)
   private:
    static constexpr std::align_val_t alignment{16};

   public:
    AlignedBuffer() = delete;
    // NOLINTNEXTLINE
    AlignedBuffer(const u8 *src, uSz size) noexcept : src_data(src), in_size(size) {
        if(reinterpret_cast<uintptr_t>(src) % static_cast<uSz>(alignment) != 0) {
            make_mutable();
        } else {
            data = const_cast<u8 *>(src);  // NOLINT
        }
    }
    ~AlignedBuffer() noexcept {
        if(allocated) {
            ::operator delete(static_cast<void *>(data), alignment);
        }
    }

    template <typename T>
    [[nodiscard]] const T *get() const noexcept {
        return reinterpret_cast<const T *>(data);
    }

    template <typename T>
    [[nodiscard]] T *get_mut() noexcept {
        if(!is_mutable()) make_mutable();
        return reinterpret_cast<T *>(data);
    }

   private:
    [[nodiscard]] bool is_mutable() const noexcept { return allocated || data == &small[0]; }

    void make_mutable() noexcept {
        if(is_mutable()) return;
        if(in_size <= sizeof(small)) {
            data = &small[0];
        } else {
            data = static_cast<u8 *>(::operator new(in_size, alignment));
            allocated = true;
        }
        std::memcpy(static_cast<void *>(data), static_cast<const void *>(src_data), in_size);
    }

    const u8 *src_data;
    uSz in_size;

    alignas(static_cast<uSz>(alignment)) u8 small[128];
    u8 *data{nullptr};
    bool allocated{false};
};

inline std::pair<uSz, simdutf::encoding_type> get_bom_and_encoding(const u8 *data, uSz size) noexcept {
    uSz bom_size = 0;

    // check up to 4 bytes, since a UTF-32 BOM is 4 bytes
    simdutf::encoding_type detected = simdutf::BOM::check_bom(data, std::min<uSz>(4, size));

    if(detected != simdutf::encoding_type::unspecified) {
        // remove BOM from conversion
        bom_size = simdutf::BOM::bom_byte_size(detected);
        // sanity
        assert(bom_size <= size);
    } else {
        // if there was no BOM, autodetect encoding
        detected = simdutf::autodetect_encoding(data, size);
    }
    return std::pair{bom_size, detected};
}

}  // namespace

std::string to_utf8(const char *arbitrarily_encoded_data, uSz size) noexcept {
    if(unlikely(!arbitrarily_encoded_data || size == 0)) return {};

    // detect encoding with BOM support
    const auto [bom_pfx_bytes, detected] =
        get_bom_and_encoding(reinterpret_cast<const u8 *>(arbitrarily_encoded_data), size);

    const uSz in_bytes = size - bom_pfx_bytes;
    if(in_bytes == 0) {
        return {};
    }

    const char *src_start = &arbitrarily_encoded_data[bom_pfx_bytes];

    std::string ret;
    if(detected == simdutf::encoding_type::unspecified || detected == simdutf::encoding_type::UTF8) {
        auto result = simdutf::validate_utf8_with_errors(src_start, in_bytes);
        if(result.error == simdutf::error_code::SUCCESS) {
            ret.assign(src_start, in_bytes);
        } else {
            ret = sanitize_utf8(src_start, in_bytes, result.count);
        }
        return ret;
    }

    AlignedBuffer aligned_buf{reinterpret_cast<const u8 *>(src_start), in_bytes};

    switch(detected) {
        case simdutf::encoding_type::UTF16_LE: {
            const uSz in_u16_len = in_bytes / 2;
            const auto *maybe_invalid_u16 = aligned_buf.get<char16_t>();
            if(simdutf::validate_utf16le(maybe_invalid_u16, in_u16_len)) {
                const uSz out_u8_len = simdutf::utf8_length_from_utf16le(maybe_invalid_u16, in_u16_len);
                ret.resize_and_overwrite(out_u8_len, [&](char *data, uSz) -> uSz {
                    return simdutf::convert_valid_utf16le_to_utf8(maybe_invalid_u16, in_u16_len, data);
                });
            } else {
                auto *valid_u16 = aligned_buf.get_mut<char16_t>();
                simdutf::to_well_formed_utf16le(valid_u16, in_u16_len, valid_u16);
                const uSz out_u8_len = simdutf::utf8_length_from_utf16le(valid_u16, in_u16_len);
                ret.resize_and_overwrite(out_u8_len, [&](char *data, uSz) -> uSz {
                    return simdutf::convert_valid_utf16le_to_utf8(valid_u16, in_u16_len, data);
                });
            }
        } break;

        case simdutf::encoding_type::UTF16_BE: {
            const uSz in_u16_len = in_bytes / 2;
            const auto *maybe_invalid_u16 = aligned_buf.get<char16_t>();
            if(simdutf::validate_utf16be(maybe_invalid_u16, in_u16_len)) {
                const uSz out_u8_len = simdutf::utf8_length_from_utf16be(maybe_invalid_u16, in_u16_len);
                ret.resize_and_overwrite(out_u8_len, [&](char *data, uSz) -> uSz {
                    return simdutf::convert_valid_utf16be_to_utf8(maybe_invalid_u16, in_u16_len, data);
                });
            } else {
                auto *valid_u16 = aligned_buf.get_mut<char16_t>();
                simdutf::to_well_formed_utf16be(valid_u16, in_u16_len, valid_u16);
                const uSz out_u8_len = simdutf::utf8_length_from_utf16be(valid_u16, in_u16_len);
                ret.resize_and_overwrite(out_u8_len, [&](char *data, uSz) -> uSz {
                    return simdutf::convert_valid_utf16be_to_utf8(valid_u16, in_u16_len, data);
                });
            }
        } break;

        case simdutf::encoding_type::UTF32_LE: {
            const uSz in_u32_len = in_bytes / 4;
            const auto *maybe_invalid_u32 = aligned_buf.get<char32_t>();
            if(simdutf::validate_utf32(maybe_invalid_u32, in_u32_len)) {
                const uSz out_u8_len = simdutf::utf8_length_from_utf32(maybe_invalid_u32, in_u32_len);
                ret.resize_and_overwrite(out_u8_len, [&](char *data, uSz) -> uSz {
                    return simdutf::convert_valid_utf32_to_utf8(maybe_invalid_u32, in_u32_len, data);
                });
            } else {
                auto *valid_u32 = aligned_buf.get_mut<char32_t>();
                sanitize_utf32(valid_u32, in_u32_len);
                const uSz out_u8_len = simdutf::utf8_length_from_utf32(valid_u32, in_u32_len);
                ret.resize_and_overwrite(out_u8_len, [&](char *data, uSz) -> uSz {
                    return simdutf::convert_valid_utf32_to_utf8(valid_u32, in_u32_len, data);
                });
            }
        } break;

        case simdutf::encoding_type::UTF32_BE: {
            // simdutf has no UTF-32 endianness swap or direct BE->UTF-8 conversion,
            // so byte-swap to native order first. should be rare anyways
            const uSz in_u32_len = in_bytes / 4;
            auto *native = aligned_buf.get_mut<char32_t>();
            for(uSz i = 0; i < in_u32_len; i++) native[i] = std::byteswap(native[i]);
            if(!simdutf::validate_utf32(native, in_u32_len)) sanitize_utf32(native, in_u32_len);
            const uSz out_u8_len = simdutf::utf8_length_from_utf32(native, in_u32_len);
            ret.resize_and_overwrite(out_u8_len, [&](char *data, uSz) -> uSz {
                return simdutf::convert_valid_utf32_to_utf8(native, in_u32_len, data);
            });
        } break;

        case simdutf::encoding_type::unspecified:
        case simdutf::encoding_type::UTF8:
            // already handled above
        case simdutf::encoding_type::Latin1:
            /* ... the function might return simdutf::encoding_type::UTF8,
            * simdutf::encoding_type::UTF16_LE, simdutf::encoding_type::UTF16_BE, or
            * simdutf::encoding_type::UTF32_LE.
            */
            std::unreachable();
            break;
    }

    return ret;
}

std::string to_utf8(std::string_view maybe_utf8) noexcept { return to_utf8(maybe_utf8.data(), maybe_utf8.size()); }

std::string to_utf8(std::u16string_view utf16) noexcept {
    if(utf16.empty()) return {};

    static constexpr auto do_convert = [](const char16_t *data, uSz size) {
        std::string ret;
        const uSz len = simdutf::utf8_length_from_utf16le(data, size);
        ret.resize_and_overwrite(
            len, [&](char *out, uSz) -> uSz { return simdutf::convert_valid_utf16le_to_utf8(data, size, out); });
        return ret;
    };

    if(simdutf::validate_utf16le(utf16.data(), utf16.size())) return do_convert(utf16.data(), utf16.size());

    std::u16string sanitized(utf16);
    simdutf::to_well_formed_utf16le(sanitized.data(), sanitized.size(), sanitized.data());
    return do_convert(sanitized.data(), sanitized.size());
}

std::string to_utf8(std::u32string_view utf32) noexcept {
    if(utf32.empty()) return {};

    static constexpr auto do_convert = [](const char32_t *data, uSz size) {
        std::string ret;
        const uSz len = simdutf::utf8_length_from_utf32(data, size);
        ret.resize_and_overwrite(
            len, [&](char *out, uSz) -> uSz { return simdutf::convert_valid_utf32_to_utf8(data, size, out); });
        return ret;
    };

    if(simdutf::validate_utf32(utf32.data(), utf32.size())) return do_convert(utf32.data(), utf32.size());

    std::u32string sanitized(utf32);
    sanitize_utf32(sanitized.data(), sanitized.size());
    return do_convert(sanitized.data(), sanitized.size());
}

std::u16string to_utf16(std::string_view utf8) noexcept {
    if(utf8.empty()) return {};

    static constexpr auto do_convert = [](const char *data, uSz size) {
        std::u16string ret;
        const uSz len = simdutf::utf16_length_from_utf8(data, size);
        ret.resize_and_overwrite(
            len, [&](char16_t *out, uSz) -> uSz { return simdutf::convert_valid_utf8_to_utf16le(data, size, out); });
        return ret;
    };

    auto result = simdutf::validate_utf8_with_errors(utf8.data(), utf8.size());
    if(result.error == simdutf::error_code::SUCCESS) return do_convert(utf8.data(), utf8.size());

    auto sanitized = sanitize_utf8(utf8.data(), utf8.size(), result.count);
    return do_convert(sanitized.data(), sanitized.size());
}

std::u16string to_utf16(std::u32string_view utf32) noexcept {
    if(utf32.empty()) return {};

    static constexpr auto do_convert = [](const char32_t *data, uSz size) {
        std::u16string ret;
        const uSz len = simdutf::utf16_length_from_utf32(data, size);
        ret.resize_and_overwrite(
            len, [&](char16_t *out, uSz) -> uSz { return simdutf::convert_valid_utf32_to_utf16le(data, size, out); });
        return ret;
    };

    if(simdutf::validate_utf32(utf32.data(), utf32.size())) return do_convert(utf32.data(), utf32.size());

    std::u32string sanitized(utf32);
    sanitize_utf32(sanitized.data(), sanitized.size());
    return do_convert(sanitized.data(), sanitized.size());
}

std::u32string to_utf32(std::string_view utf8) noexcept {
    if(utf8.empty()) return {};

    static constexpr auto do_convert = [](const char *data, uSz size) {
        std::u32string ret;
        const uSz len = simdutf::utf32_length_from_utf8(data, size);
        ret.resize_and_overwrite(
            len, [&](char32_t *out, uSz) -> uSz { return simdutf::convert_valid_utf8_to_utf32(data, size, out); });
        return ret;
    };

    auto result = simdutf::validate_utf8_with_errors(utf8.data(), utf8.size());
    if(result.error == simdutf::error_code::SUCCESS) return do_convert(utf8.data(), utf8.size());

    auto sanitized = sanitize_utf8(utf8.data(), utf8.size(), result.count);
    return do_convert(sanitized.data(), sanitized.size());
}

std::u32string to_utf32(std::u16string_view utf16) noexcept {
    if(utf16.empty()) return {};

    static constexpr auto do_convert = [](const char16_t *data, uSz size) {
        std::u32string ret;
        const uSz len = simdutf::utf32_length_from_utf16le(data, size);
        ret.resize_and_overwrite(
            len, [&](char32_t *out, uSz) -> uSz { return simdutf::convert_valid_utf16le_to_utf32(data, size, out); });
        return ret;
    };

    if(simdutf::validate_utf16le(utf16.data(), utf16.size())) return do_convert(utf16.data(), utf16.size());

    std::u16string sanitized(utf16);
    simdutf::to_well_formed_utf16le(sanitized.data(), sanitized.size(), sanitized.data());
    return do_convert(sanitized.data(), sanitized.size());
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
