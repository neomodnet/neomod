// Copyright (c) 2023, kiwec, All rights reserved.
#include "BanchoProtocol.h"

#include "Bancho.h"
#include "BeatmapInterface.h"
#include "Osu.h"

Room::Room(PacketReader &packet) {
    this->id = packet.read<u16>();
    this->in_progress = packet.read<u8>();
    this->match_type = packet.read<u8>();
    this->mods = Replay::Mods::from_legacy(packet.read<LegacyFlags>());
    this->name = packet.read_string();

    // the password is a string whose presence marker doubles as the has_password flag. it should be empty, but skip
    // whatever is there just in case
    this->has_password = packet.peek<u8>() > 0;
    packet.skip_string();

    this->map_name = packet.read_string();
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
            slot.mods = Replay::Mods::from_legacy(packet.read<LegacyFlags>());
        }
    }

    this->seed = packet.read<u32>();

    // a neomod server follows up with the full mods (the legacy ones above only approximate them): the room's, then in
    // a freemod room those of every slot with a player in it, each after its length (what a newer version adds to
    // them gets skipped). they're taken only if they're all there: one that doesn't send them keeps the room at the
    // legacy ones, and cut off ones would read as zeros (overrides of 0)
    if(BanchoState::fully_supports_neomod && packet.remaining() > 0) {
        bool complete = true;
        const auto read_mods = [&packet, &complete] {
            PacketReader blob{packet.read_span(packet.read_uleb128())};
            const Replay::Mods mods = Replay::Mods::unpack(blob);
            complete &= packet.good() && blob.good();
            return mods;
        };

        const Replay::Mods mods = read_mods();
        std::array<Replay::Mods, 16> slot_mods{};
        for(size_t i = 0; i < slot_mods.size(); i++) {
            if(this->freemods && this->slots[i].has_player()) slot_mods[i] = read_mods();
        }
        if(complete) {
            this->has_full_mods = true;
            this->mods = mods;
            for(size_t i = 0; i < slot_mods.size(); i++) this->slots[i].mods = slot_mods[i];
        }
    }
}

void Room::pack(Packet &packet) {
    packet.write<u16>(this->id);
    packet.write<u8>(this->in_progress);
    packet.write<u8>(this->match_type);
    packet.write<LegacyFlags>(this->mods.to_legacy());
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
            packet.write<LegacyFlags>(slot.mods.to_legacy());
        }
    }

    packet.write<u32>(this->seed);

    if(this->has_full_mods) {
        const auto write_mods = [&packet](const Replay::Mods &mods) {
            Packet blob;
            Replay::Mods::pack_and_write(blob, mods);
            packet.write_uleb128(blob.data.size());
            packet.write_bytes(blob.data);
        };

        write_mods(this->mods);
        if(this->freemods) {
            for(const auto &slot : this->slots) {
                if(slot.has_player()) write_mods(slot.mods);
            }
        }
    }
}

bool Room::is_host() const { return this->host_id == BanchoState::get_uid(); }

Replay::Mods Room::get_player_mods(const Slot &slot) const {
    return this->freemods ? with_shared_mods(slot.mods, this->mods) : this->mods;
}

Replay::Mods Room::with_shared_mods(Replay::Mods own, const Replay::Mods &shared) {
    using namespace flags::operators;
    constexpr ModFlags SHARED_FLAGS = ModFlags::Timewarp | ModFlags::Target;

    own.flags = (own.flags & ~SHARED_FLAGS) | (shared.flags & SHARED_FLAGS);
    own.speed = shared.speed;
    own.timewarp_multiplier = shared.timewarp_multiplier;
    return own;
}

ScoreFrame ScoreFrame::get() {
    u8 slot_id = 0;
    for(u8 i = 0; i < 16; i++) {
        if(BanchoState::room.slots[i].player_id == BanchoState::get_uid()) {
            slot_id = i;
            break;
        }
    }

    const auto *score = osu->getScore();
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
