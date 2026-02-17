#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.
#include "MD5Hash.h"
#include "types.h"

#include <cstdlib>

struct Packet {
    u16 id{0};
    u8 *memory{nullptr};
    size_t size{0};
    size_t pos{0};
    u8 *extra{nullptr};
    i32 extra_int{0};  // lazy

    void reserve(u32 newsize) {
        if(newsize <= this->size) return;
        this->memory = (u8 *)realloc(this->memory, newsize);
        this->size = newsize;
    }

    void read_bytes(u8 *bytes, size_t n);
    u32 read_uleb128();
    std::string read_stdstring();
    UString read_ustring();
    void skip_string();
    MD5String read_hash_chars();
    MD5Hash read_hash_digest();

    template <typename T>
    T read() {
        T result{};
        if(this->pos + sizeof(T) > this->size) {
            this->pos = this->size + 1;
            return result;
        } else {
            memcpy(&result, this->memory + this->pos, sizeof(T));
            this->pos += sizeof(T);
            return result;
        }
    }

    void write_uleb128(u32 num);
    void write_hash_chars(const MD5String &hash_str);
    void write_hash_digest(const MD5Hash &hash_digest);

    inline void write_string(const char *str) {
        if(this->write_string_isnull(str)) return;
        this->write_string_nonnull(str, strlen(str));
    }

    inline void write_string(std::string_view strview) {
        if(strview.empty()) {
            const u8 zero = 0;
            this->write<u8>(zero);
            return;
        }
        this->write_string_nonnull(strview.data(), strview.length());
    }

    void write_bytes(u8 *bytes, size_t n);

    template <typename T>
    void write(T t) {
        this->write_bytes((u8 *)&t, sizeof(T));
    }

    // debug helper
    [[nodiscard]] static std::string_view inpacket_to_string(u16 incoming_packet_id);

   private:
    // check for null, if it is, write 0 and return true
    bool write_string_isnull(const char *str);
    // unchecked
    void write_string_nonnull(const char *str, size_t len);
};
