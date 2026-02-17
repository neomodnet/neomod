#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "ModFlags.h"

class DatabaseBeatmap;

namespace Replay {

#ifndef BUILD_TOOLS_ONLY

template <typename T>
concept GenericReader = requires(T &t) {
    t.template read<u64>();
    t.template read<f32>();
    t.template read<i32>();
};

template <typename T>
concept GenericWriter = requires(T &t) {
    t.template write<u64>(u64{});
    t.template write<f32>(f32{});
    t.template write<i32>(i32{});
};

#endif

struct Mods {
    ModFlags flags{};

    f32 speed = 1.f;
    i32 notelock_type = 2;
    f32 autopilot_lenience = 0.75f;
    f32 ar_override = -1.f;
    f32 ar_overridenegative = 0.f;
    f32 cs_override = -1.f;
    f32 cs_overridenegative = 0.f;
    f32 hp_override = -1.f;
    f32 od_override = -1.f;
    f32 timewarp_multiplier = 1.5f;
    f32 minimize_multiplier = 0.5f;
    f32 artimewarp_multiplier = 0.5f;
    f32 arwobble_strength = 1.0f;
    f32 arwobble_interval = 7.0f;
    f32 wobble_strength = 25.f;
    f32 wobble_frequency = 1.f;
    f32 wobble_rotation_speed = 1.f;
    f32 jigsaw_followcircle_radius_factor = 0.f;
    f32 shirone_combo = 20.f;

    bool operator==(const Mods &) const = default;

    [[nodiscard]] inline bool has(ModFlags flag) const {
        using namespace flags::operators;
        return (this->flags & flag) == flag;
    }

    [[nodiscard]] LegacyFlags to_legacy() const;
    static Mods from_legacy(LegacyFlags legacy_flags);

    // Get AR/CS/OD, ignoring mods which change it over time
    // Used for ppv2 calculations.

    // if you know the beatmap's base values you can use these directly
    [[nodiscard]] f32 get_naive_ar(f32 baseAR) const;
    [[nodiscard]] f32 get_naive_cs(f32 baseCS) const;
    [[nodiscard]] f32 get_naive_hp(f32 baseHP) const;
    [[nodiscard]] f32 get_naive_od(f32 baseOD) const;

#ifndef BUILD_TOOLS_ONLY
    // these just wrap the above with map->getAR() map->getCS() etc.
    [[nodiscard]] f32 get_naive_ar(const DatabaseBeatmap *map) const;
    [[nodiscard]] f32 get_naive_cs(const DatabaseBeatmap *map) const;
    [[nodiscard]] f32 get_naive_hp(const DatabaseBeatmap *map) const;
    [[nodiscard]] f32 get_naive_od(const DatabaseBeatmap *map) const;

    [[nodiscard]] f64 get_scorev1_multiplier() const;

    static Mods from_cvars();
    static void use(const Mods &mods);

    // templated for either Packet or ByteBufferdFile::Reader/Writer

    template <GenericReader R>
    static Mods unpack(R &reader);

    template <GenericWriter W>
    static void pack_and_write(W &writer, const Replay::Mods &mods);
#endif
};

}  // namespace Replay
