#pragma once
// Copyright (c) 2023, kiwec, All rights reserved.
#include "MD5Hash.h"
#include "types.h"

#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// an outgoing bancho packet: its id and the payload the write_* methods build up (little-endian, strings in the osu!
// binary format). BANCHO::Net::send_packet() copies it into the next request, so it can be dropped or reused right
// after, and its buffer goes away with it
struct Packet {
    Packet() = default;
    explicit Packet(u16 id) : id(id) {}

    u16 id{0};
    std::vector<u8> data;

    template <typename T>
    void write(T t) {
        static_assert(std::is_trivially_copyable_v<T>);
        this->write_bytes({reinterpret_cast<const u8 *>(&t), sizeof(T)});
    }

    void write_bytes(std::span<const u8> bytes) { this->data.insert(this->data.end(), bytes.begin(), bytes.end()); }
    void write_uleb128(u32 num);
    void write_string(std::string_view str);
    void write_hash_chars(const MD5String &hash_str);
    void write_hash_digest(const MD5Hash &hash_digest);
};

// a cursor over the bytes of an incoming packet (or any other blob in the same format, like an .osr); doesn't own them.
// reads are bounds-checked: one that would go past the end fails the reader, which reads as zeros/empty from then on,
// so a handler can read a whole packet unguarded and check good() at the end (or not; the zeros are harmless)
class PacketReader {
   public:
    explicit PacketReader(std::span<const u8> data, u16 id = 0) : id(id), data(data) {}

    u16 id;

    template <typename T>
    [[nodiscard]] T read() {
        static_assert(std::is_trivially_copyable_v<T>);
        T result{};
        if(const auto bytes = this->read_span(sizeof(T)); !bytes.empty()) memcpy(&result, bytes.data(), sizeof(T));
        return result;
    }

    // read() without advancing
    template <typename T>
    [[nodiscard]] T peek() {
        const uSz start = this->pos;
        T result = this->read<T>();
        this->pos = start;
        return result;
    }

    // the next n bytes (empty unless they are all there), advancing past them
    [[nodiscard]] std::span<const u8> read_span(uSz n) {
        const uSz start = this->pos;
        this->skip_bytes(n);
        return this->good() ? this->data.subspan(start, n) : std::span<const u8>{};
    }

    [[nodiscard]] u32 read_uleb128();
    [[nodiscard]] std::string read_string();
    [[nodiscard]] MD5String read_hash_chars();
    [[nodiscard]] MD5Hash read_hash_digest();

    template <typename T>
    void skip() {
        this->skip_bytes(sizeof(T));
    }
    void skip_bytes(uSz n) {
        if(this->overrun || n > this->remaining()) {
            this->overrun = true;
        } else {
            this->pos += n;
        }
    }
    void skip_string();

    // false once a read went past the end
    [[nodiscard]] bool good() const { return !this->overrun; }
    [[nodiscard]] uSz size() const { return this->data.size(); }
    [[nodiscard]] uSz remaining() const { return this->data.size() - this->pos; }

   private:
    std::span<const u8> data;
    uSz pos{0};
    bool overrun{false};
};
