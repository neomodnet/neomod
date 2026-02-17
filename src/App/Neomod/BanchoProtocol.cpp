// Copyright (c) 2023, kiwec, All rights reserved.
#include "BanchoProtocol.h"

#include "Bancho.h"
#include "BeatmapInterface.h"
#include "Osu.h"

Room::Room(Packet &packet) {
    this->id = packet.read<u16>();
    this->in_progress = packet.read<u8>();
    this->match_type = packet.read<u8>();
    this->mods = packet.read<LegacyFlags>();
    this->name = packet.read_stdstring();

    this->has_password = packet.read<u8>() > 0;
    if(this->has_password) {
        // Discard password. It should be an empty string, but just in case, read it properly.
        packet.pos--;
        packet.read_stdstring();
    }

    this->map_name = packet.read_stdstring();
    this->map_id = packet.read<i32>();

    this->map_md5 = packet.read_hash_chars();

    this->nb_players = 0;
    for(auto &slot : this->slots) {
        slot.status = packet.read<u8>();
    }
    for(auto &slot : this->slots) {
        slot.team = packet.read<u8>();
    }
    for(auto &slot : this->slots) {
        if(!slot.is_locked()) {
            this->nb_open_slots++;
        }

        if(slot.has_player()) {
            slot.player_id = packet.read<i32>();
            this->nb_players++;
        }
    }

    this->host_id = packet.read<i32>();
    this->mode = packet.read<u8>();
    this->win_condition = (WinCondition)packet.read<u8>();
    this->team_type = packet.read<u8>();
    this->freemods = packet.read<u8>();
    if(this->freemods) {
        for(auto &slot : this->slots) {
            slot.mods = packet.read<LegacyFlags>();
        }
    }

    this->seed = packet.read<u32>();
}

void Room::pack(Packet &packet) {
    packet.write<u16>(this->id);
    packet.write<u8>(this->in_progress);
    packet.write<u8>(this->match_type);
    packet.write<LegacyFlags>(this->mods);
    packet.write_string(this->name);
    packet.write_string(this->password);
    packet.write_string(this->map_name);
    packet.write<i32>(this->map_id);
    packet.write_hash_chars(this->map_md5);
    for(auto &slot : this->slots) {
        packet.write<u8>(slot.status);
    }
    for(auto &slot : this->slots) {
        packet.write<u8>(slot.team);
    }
    for(auto &slot : this->slots) {
        if(slot.has_player()) {
            packet.write<i32>(slot.player_id);
        }
    }

    packet.write<i32>(this->host_id);
    packet.write<u8>(this->mode);
    packet.write<u8>((u8)this->win_condition);
    packet.write<u8>(this->team_type);
    packet.write<u8>(this->freemods);
    if(this->freemods) {
        for(auto &slot : this->slots) {
            packet.write<LegacyFlags>(slot.mods);
        }
    }

    packet.write<u32>(this->seed);
}

bool Room::is_host() const { return this->host_id == BanchoState::get_uid(); }

ScoreFrame ScoreFrame::get() {
    u8 slot_id = 0;
    for(u8 i = 0; i < 16; i++) {
        if(BanchoState::room.slots[i].player_id == BanchoState::get_uid()) {
            slot_id = i;
            break;
        }
    }

    const auto &score = osu->getScore();
    auto perfect = (score->getNumSliderBreaks() == 0 && score->getNumMisses() == 0 && score->getNum50s() == 0 &&
                    score->getNum100s() == 0);

    return ScoreFrame{
        .time = (i32)osu->getMapInterface()->getCurMusicPos(),  // NOTE: might be incorrect
        .slot_id = slot_id,
        .num300 = (u16)score->getNum300s(),
        .num100 = (u16)score->getNum100s(),
        .num50 = (u16)score->getNum50s(),
        .num_geki = (u16)score->getNum300gs(),
        .num_katu = (u16)score->getNum100ks(),
        .num_miss = (u16)score->getNumMisses(),
        .total_score = (i32)score->getScore(),
        .max_combo = (u16)score->getComboMax(),
        .current_combo = (u16)score->getCombo(),
        .is_perfect = perfect,
        .current_hp = (u8)(osu->getMapInterface()->getHealth() * 200.0),
        .tag = 0,         // tag gamemode currently not supported
        .is_scorev2 = 0,  // scorev2 currently not supported
    };
}
