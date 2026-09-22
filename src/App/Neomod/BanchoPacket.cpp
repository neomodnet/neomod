// Copyright (c) 2023, kiwec, All rights reserved.

#include "BanchoPacket.h"
#include "UniString.h"
#include "Logging.h"

#include <algorithm>
#include <string>

void Packet::write_uleb128(u32 num) {
    do {
        u8 next = num & 0x7F;
        num >>= 7;
        if(num != 0) {
            next |= 0x80;
        }
        this->write<u8>(next);
    } while(num != 0);
}

void Packet::write_string(std::string_view str) {
    if(str.empty()) {
        this->write<u8>(0);
        return;
    }

    this->write<u8>(0x0B);
    this->write_uleb128(str.length());
    this->write_bytes({reinterpret_cast<const u8 *>(str.data()), str.length()});
}

void Packet::write_hash_chars(const MD5String &hash_str) {
    this->write<u8>(0x0B);
    this->write<u8>(hash_str.length());
    this->write_bytes({reinterpret_cast<const u8 *>(hash_str.data()), hash_str.length()});
}

void Packet::write_hash_digest(const MD5Hash &hash_digest) {
    this->write<u8>(0x0B);
    this->write<u8>(hash_digest.length());
    this->write_bytes(hash_digest);
}

u32 PacketReader::read_uleb128() {
    u32 result = 0;
    u32 shift = 0;
    u8 byte = 0;

    do {
        byte = this->read<u8>();
        result |= static_cast<u32>(byte & 0x7f) << shift;
        shift += 7;
    } while((byte & 0x80) && shift < 32);  // stop after 5 bytes; shift >= 32 would be UB

    return result;
}

std::string PacketReader::read_string() {
    if(this->read<u8>() == 0) return {};

    const u32 len = this->read_uleb128();
    const auto bytes = this->read_span(len);

    // assume utf-8, but make sure it's valid (sanity)
    return UniString::sanitize_utf8(std::string(bytes.begin(), bytes.end()));
}

MD5String PacketReader::read_hash_chars() {
    MD5String hash;
    if(this->read<u8>() == 0) return hash;

    const auto bytes = this->read_span(this->read_uleb128());
    if(bytes.size() != hash.length()) {
        debugLog("expected a {}-char hash, got {} bytes!", hash.length(), bytes.size());
    }
    std::ranges::copy(bytes.first(std::min(bytes.size(), hash.length())), hash.begin());
    return hash;
}

MD5Hash PacketReader::read_hash_digest() {
    MD5Hash hash;
    if(this->read<u8>() == 0) return hash;

    const auto bytes = this->read_span(this->read_uleb128());
    if(bytes.size() != hash.length()) {
        debugLog("expected a {}-byte hash, got {} bytes!", hash.length(), bytes.size());
    }
    std::ranges::copy(bytes.first(std::min(bytes.size(), hash.length())), hash.begin());
    return hash;
}

void PacketReader::skip_string() {
    if(this->read<u8>() == 0) return;
    this->skip_bytes(this->read_uleb128());
}
