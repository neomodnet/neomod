// Copyright (c) 2023, kiwec, All rights reserved.

#include "BanchoPacket.h"
#include "BanchoProtocol.h"
#include "UString.h"

#include <cstdlib>
#include <cstring>
#include <cassert>

void Packet::read_bytes(u8 *bytes, size_t n) {
    if(this->pos + n > this->size) {
        this->pos = this->size + 1;
    } else {
        memcpy(bytes, this->memory + this->pos, n);
        this->pos += n;
    }
}

u32 Packet::read_uleb128() {
    u32 result = 0;
    u32 shift = 0;
    u8 byte = 0;

    do {
        byte = this->read<u8>();
        result |= (byte & 0x7f) << shift;
        shift += 7;
    } while(byte & 0x80);

    return result;
}

std::string Packet::read_stdstring() {
    u8 empty_check = this->read<u8>();
    if(empty_check == 0) return {};

    u32 len = this->read_uleb128();
    u8 *str = new u8[len + 1];
    this->read_bytes(str, len);

    std::string str_out((const char *)str, len);
    delete[] str;

    return str_out;
}

UString Packet::read_ustring() { return this->read_stdstring(); }

MD5String Packet::read_hash_chars() {
    MD5String hash;

    u8 empty_check = this->read<u8>();
    if(empty_check == 0) return hash;

    u32 len = this->read_uleb128();
    if(len > 32) {
        len = 32;
    }

    this->read_bytes((u8 *)hash.data(), len);
    return hash;
}

MD5Hash Packet::read_hash_digest() {
    MD5Hash hash;

    u8 empty_check = this->read<u8>();
    if(empty_check == 0) return hash;

    u32 len = this->read_uleb128();
    if(len > 16) {
        len = 16;
    }

    this->read_bytes((u8 *)hash.data(), len);
    return hash;
}

void Packet::skip_string() {
    u8 empty_check = this->read<u8>();
    if(empty_check == 0) {
        return;
    }

    u32 len = this->read_uleb128();
    this->pos += len;
}

void Packet::write_bytes(u8 *bytes, size_t n) {
    assert(bytes != nullptr);

    if(this->pos + n > this->size) {
        this->memory = (unsigned char *)realloc(this->memory, this->size + n + 4096);
        assert(this->memory && "realloc failed");
        this->size += n + 4096;
        if(!this->memory) return;
    }

    memcpy(this->memory + this->pos, bytes, n);
    this->pos += n;
}

void Packet::write_uleb128(u32 num) {
    if(num == 0) {
        u8 zero = 0;
        this->write<u8>(zero);
        return;
    }

    while(num != 0) {
        u8 next = num & 0x7F;
        num >>= 7;
        if(num != 0) {
            next |= 0x80;
        }
        this->write<u8>(next);
    }
}

void Packet::write_hash_chars(const MD5String &hash_str) {
    this->write<u8>(0x0B);
    this->write<u8>(hash_str.length());
    this->write_bytes((u8 *)hash_str.data(), hash_str.length());
}

void Packet::write_hash_digest(const MD5Hash &hash_digest) {
    this->write<u8>(0x0B);
    this->write<u8>(hash_digest.length());
    this->write_bytes((u8 *)hash_digest.data(), hash_digest.length());
}

bool Packet::write_string_isnull(const char *str) {
    if(!str || str[0] == '\0') {
        const u8 zero = 0;
        this->write<u8>(zero);
        return true;
    }
    return false;
}

void Packet::write_string_nonnull(const char *str, size_t len) {
    const u8 empty_check = 0x0B;
    this->write<u8>(empty_check);

    this->write_uleb128(len);
    this->write_bytes((u8 *)str, len);
}

std::string_view Packet::inpacket_to_string(u16 incoming_packet_id) {
    auto inpacket = static_cast<IncomingPackets>(incoming_packet_id);
    switch(inpacket) {
        case INP_USER_ID:
            return "USER_ID"sv;
        case INP_RECV_MESSAGE:
            return "RECV_MESSAGE"sv;
        case INP_PONG:
            return "PONG"sv;
        case INP_USER_STATS:
            return "USER_STATS"sv;
        case INP_USER_LOGOUT:
            return "USER_LOGOUT"sv;
        case INP_SPECTATOR_JOINED:
            return "SPECTATOR_JOINED"sv;
        case INP_SPECTATOR_LEFT:
            return "SPECTATOR_LEFT"sv;
        case INP_SPECTATE_FRAMES:
            return "SPECTATE_FRAMES"sv;
        case INP_VERSION_UPDATE:
            return "VERSION_UPDATE"sv;
        case INP_SPECTATOR_CANT_SPECTATE:
            return "SPECTATOR_CANT_SPECTATE"sv;
        case INP_GET_ATTENTION:
            return "GET_ATTENTION"sv;
        case INP_NOTIFICATION:
            return "NOTIFICATION"sv;
        case INP_ROOM_UPDATED:
            return "ROOM_UPDATED"sv;
        case INP_ROOM_CREATED:
            return "ROOM_CREATED"sv;
        case INP_ROOM_CLOSED:
            return "ROOM_CLOSED"sv;
        case INP_ROOM_JOIN_SUCCESS:
            return "ROOM_JOIN_SUCCESS"sv;
        case INP_ROOM_JOIN_FAIL:
            return "ROOM_JOIN_FAIL"sv;
        case INP_FELLOW_SPECTATOR_JOINED:
            return "FELLOW_SPECTATOR_JOINED"sv;
        case INP_FELLOW_SPECTATOR_LEFT:
            return "FELLOW_SPECTATOR_LEFT"sv;
        case INP_MATCH_STARTED:
            return "MATCH_STARTED"sv;
        case INP_MATCH_SCORE_UPDATED:
            return "MATCH_SCORE_UPDATED"sv;
        case INP_HOST_CHANGED:
            return "HOST_CHANGED"sv;
        case INP_MATCH_ALL_PLAYERS_LOADED:
            return "MATCH_ALL_PLAYERS_LOADED"sv;
        case INP_MATCH_PLAYER_FAILED:
            return "MATCH_PLAYER_FAILED"sv;
        case INP_MATCH_FINISHED:
            return "MATCH_FINISHED"sv;
        case INP_MATCH_SKIP:
            return "MATCH_SKIP"sv;
        case INP_CHANNEL_JOIN_SUCCESS:
            return "CHANNEL_JOIN_SUCCESS"sv;
        case INP_CHANNEL_INFO:
            return "CHANNEL_INFO"sv;
        case INP_LEFT_CHANNEL:
            return "LEFT_CHANNEL"sv;
        case INP_CHANNEL_AUTO_JOIN:
            return "CHANNEL_AUTO_JOIN"sv;
        case INP_PRIVILEGES:
            return "PRIVILEGES"sv;
        case INP_FRIENDS_LIST:
            return "FRIENDS_LIST"sv;
        case INP_PROTOCOL_VERSION:
            return "PROTOCOL_VERSION"sv;
        case INP_MAIN_MENU_ICON:
            return "MAIN_MENU_ICON"sv;
        case INP_MATCH_PLAYER_SKIPPED:
            return "MATCH_PLAYER_SKIPPED"sv;
        case INP_USER_PRESENCE:
            return "USER_PRESENCE"sv;
        case INP_RESTART:
            return "RESTART"sv;
        case INP_ROOM_INVITE:
            return "ROOM_INVITE"sv;
        case INP_CHANNEL_INFO_END:
            return "CHANNEL_INFO_END"sv;
        case INP_ROOM_PASSWORD_CHANGED:
            return "ROOM_PASSWORD_CHANGED"sv;
        case INP_SILENCE_END:
            return "SILENCE_END"sv;
        case INP_USER_SILENCED:
            return "USER_SILENCED"sv;
        case INP_USER_PRESENCE_SINGLE:
            return "USER_PRESENCE_SINGLE"sv;
        case INP_USER_PRESENCE_BUNDLE:
            return "USER_PRESENCE_BUNDLE"sv;
        case INP_USER_DM_BLOCKED:
            return "USER_DM_BLOCKED"sv;
        case INP_TARGET_IS_SILENCED:
            return "TARGET_IS_SILENCED"sv;
        case INP_VERSION_UPDATE_FORCED:
            return "VERSION_UPDATE_FORCED"sv;
        case INP_SWITCH_SERVER:
            return "SWITCH_SERVER"sv;
        case INP_ACCOUNT_RESTRICTED:
            return "ACCOUNT_RESTRICTED"sv;
        case INP_MATCH_ABORT:
            return "MATCH_ABORT"sv;
        case INP_PROTECT_VARIABLES:
            return "PROTECT_VARIABLES"sv;
        case INP_UNPROTECT_VARIABLES:
            return "UNPROTECT_VARIABLES"sv;
        case INP_FORCE_VALUES:
            return "FORCE_VALUES"sv;
        case INP_RESET_VALUES:
            return "RESET_VALUES"sv;
        case INP_REQUEST_MAP:
            return "REQUEST_MAP"sv;
    }

    return "packet out of range"sv;
}