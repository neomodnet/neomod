// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "noinclude.h"
#include "types.h"

#include <string_view>
#include <string>

namespace UniString {
// in ill-formed text, lone trail bytes (UTF-8) and lone low surrogates (UTF-16) aren't counted
[[nodiscard]] uSz num_codepoints(std::string_view utf8) noexcept;
[[nodiscard]] uSz num_codepoints(std::u16string_view utf16) noexcept;
[[nodiscard]] uSz num_codepoints(std::u32string_view utf32) noexcept;

// decode text of unknown encoding (BOM, otherwise a heuristic guess) to UTF-8, e.g. file contents
[[nodiscard]] std::string to_utf8(const char *arbitrarily_encoded_data, uSz size) noexcept;
[[nodiscard]] std::string to_utf8(std::string_view arbitrarily_encoded_data) noexcept;
// replace ill-formed UTF-8 sequences with U+FFFD, for text that has to leave as valid UTF-8
[[nodiscard]] std::string sanitize_utf8(std::string utf8) noexcept;
[[nodiscard]] std::string to_utf8(std::u16string_view utf16) noexcept;
[[nodiscard]] std::string to_utf8(std::u32string_view utf32) noexcept;

[[nodiscard]] std::u16string to_utf16(std::string_view utf8) noexcept;
[[nodiscard]] std::u16string to_utf16(std::u32string_view utf32) noexcept;

[[nodiscard]] std::u32string to_utf32(std::string_view utf8) noexcept;
[[nodiscard]] std::u32string to_utf32(std::u16string_view utf16) noexcept;

[[nodiscard]] std::string to_utf8(std::wstring_view wide) noexcept;
[[nodiscard]] std::wstring to_wide(std::string_view utf8) noexcept;

namespace detail {
// how many bytes of the ill-formed UTF-8 sequence at `pos` one U+FFFD stands for
[[nodiscard]] neverinline u32 ill_formed_length(const char *pos, const char *end) noexcept;
}  // namespace detail

// codepoint iteration over any input: each ill-formed sequence reads as one U+FFFD
class u8codepoint_view {
    std::string_view m_sv;

   public:
    explicit constexpr u8codepoint_view(std::string_view sv) noexcept : m_sv(sv) {}
    struct iterator {
        const char *pos;  // of the current codepoint
        const char *end;
        char32_t codepoint{0};
        u32 length{0};  // of the current codepoint in bytes, 0 at the end

        forceinline constexpr iterator(const char *at, const char *textEnd) noexcept : pos(at), end(textEnd) {
            decode();
        }
        [[nodiscard]] constexpr char32_t operator*() const noexcept { return codepoint; }
        forceinline constexpr iterator &operator++() noexcept {
            pos += length;
            decode();
            return *this;
        }
        [[nodiscard]] constexpr bool operator==(const iterator &o) const noexcept { return pos == o.pos; }

       private:
        forceinline constexpr void decode() noexcept;
    };
    [[nodiscard]] forceinline constexpr iterator begin() const noexcept {
        return {m_sv.data(), m_sv.data() + m_sv.size()};
    }
    [[nodiscard]] constexpr iterator end() const noexcept {
        return {m_sv.data() + m_sv.size(), m_sv.data() + m_sv.size()};
    }
};

class u16codepoint_view {
    std::u16string_view m_sv;

   public:
    explicit constexpr u16codepoint_view(std::u16string_view sv) noexcept : m_sv(sv) {}
    struct iterator {
        const char16_t *pos;  // of the current codepoint
        const char16_t *end;
        char32_t codepoint{0};
        u32 length{0};  // of the current codepoint in code units, 0 at the end

        constexpr iterator(const char16_t *at, const char16_t *textEnd) noexcept : pos(at), end(textEnd) { decode(); }
        [[nodiscard]] constexpr char32_t operator*() const noexcept { return codepoint; }
        constexpr iterator &operator++() noexcept {
            pos += length;
            decode();
            return *this;
        }
        [[nodiscard]] constexpr bool operator==(const iterator &o) const noexcept { return pos == o.pos; }

       private:
        constexpr void decode() noexcept;
    };
    [[nodiscard]] constexpr iterator begin() const noexcept { return {m_sv.data(), m_sv.data() + m_sv.size()}; }
    [[nodiscard]] constexpr iterator end() const noexcept {
        return {m_sv.data() + m_sv.size(), m_sv.data() + m_sv.size()};
    }
};

[[nodiscard]] constexpr u8codepoint_view codepoints(std::string_view sv) noexcept { return u8codepoint_view{sv}; }
[[nodiscard]] constexpr u16codepoint_view codepoints(std::u16string_view sv) noexcept { return u16codepoint_view{sv}; }

// byte offset of the start of the codepoint before `pos`, as the iteration splits the text
[[nodiscard]] uSz prev(std::string_view sv, uSz pos) noexcept;

// byte offset past the end of the codepoint at `pos`, as the iteration splits the text
[[nodiscard]] uSz next(std::string_view sv, uSz pos) noexcept;

// the length is decided by branches on the lead byte, so the next position is predicted instead of waiting for the load.
// no [[likely]] for ASCII: it moves the other forms out of the loop this ends up in, two more jumps per character
constexpr void u8codepoint_view::iterator::decode() noexcept {
    if(pos == end) {
        codepoint = 0;
        length = 0;
        return;
    }
    const u32 lead = static_cast<u8>(pos[0]);
    if(lead < 0x80) {
        codepoint = lead;
        length = 1;
        return;
    }
    // a trail byte is 0x80..0xBF: the xor leaves its 6 bits, any other byte ends up above them
    const auto trail = [this](int i) -> u32 { return static_cast<u8>(pos[i]) ^ 0x80u; };
    const uSz available = static_cast<uSz>(end - pos);
    if(lead >= 0xE0) {
        if(lead < 0xF0) {
            if(available >= 3) {
                const u32 t1 = trail(1), t2 = trail(2);
                const u32 c = (lead & 0x0F) << 12 | t1 << 6 | t2;
                // neither overlong nor a surrogate
                if((t1 | t2) <= 0x3F && c >= 0x800 && (c >> 11) != 0x1B) {
                    codepoint = c;
                    length = 3;
                    return;
                }
            }
        } else if(available >= 4) {
            const u32 t1 = trail(1), t2 = trail(2), t3 = trail(3);
            // the leads 0xF5..0xFF land above U+10FFFF
            const u32 c = (lead - 0xF0) << 18 | t1 << 12 | t2 << 6 | t3;
            if((t1 | t2 | t3) <= 0x3F && c - 0x10000 < 0x100000) {
                codepoint = c;
                length = 4;
                return;
            }
        }
    } else if(available >= 2) {
        const u32 t1 = trail(1);
        // lone trail bytes and the overlong leads 0xC0, 0xC1 land outside of U+0080..U+07FF
        const u32 c = (lead - 0xC0) << 6 | t1;
        if(t1 <= 0x3F && c - 0x80 < 0x780) {
            codepoint = c;
            length = 2;
            return;
        }
    }
    codepoint = 0xFFFD;
    length = detail::ill_formed_length(pos, end);
}

constexpr void u16codepoint_view::iterator::decode() noexcept {
    if(pos == end) {
        codepoint = 0;
        length = 0;
        return;
    }
    const u32 unit = pos[0];
    if((unit & 0xF800) != 0xD800) {
        codepoint = unit;
        length = 1;
        return;
    }
    // a high surrogate has to be followed by a low one
    if(unit < 0xDC00 && end - pos >= 2 && (pos[1] & 0xFC00) == 0xDC00) {
        codepoint = 0x10000 + ((unit - 0xD800) << 10) + (pos[1] - 0xDC00u);
        length = 2;
        return;
    }
    codepoint = 0xFFFD;
    length = 1;
}

}  // namespace UniString
