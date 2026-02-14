// Copyright (c) 2016, PG, All rights reserved.
#include "Replay.h"

#include "GameRules.h"
#include "DifficultyCalculator.h"

#ifndef BUILD_TOOLS_ONLY
#include "BeatmapInterface.h"
#include "CBaseUICheckbox.h"
#include "CBaseUISlider.h"
#include "DatabaseBeatmap.h"
#include "ModSelector.h"
#include "OsuConVars.h"
#include "Osu.h"
#include "ByteBufferedFile.h"
#include "BanchoPacket.h"
#include "Logging.h"
#include "UI.h"
#else
#include <algorithm>
#endif

using namespace flags::operators;

namespace Replay {

f32 Mods::get_naive_ar(f32 baseAR) const {
    f32 AR = this->ar_override;
    if(AR < 0.f) {
        if(this->ar_overridenegative < 0.0f) {
            AR = this->ar_overridenegative;
        } else {
            AR = std::clamp<f32>(baseAR * (this->has(ModFlags::HardRock) ? 1.4f
                                           : this->has(ModFlags::Easy)   ? 0.5f
                                                                         : 1.0f),
                                 0.0f, 10.0f);
        }
    }

    if(this->has(ModFlags::AROverrideLock)) {
        AR = GameRules::arWithSpeed(AR, 1.f / this->speed);
    }

    return AR;
}

f32 Mods::get_naive_cs(f32 baseCS) const {
    f32 CS = this->cs_override;
    if(CS < 0.f) {
        if(this->cs_overridenegative < 0.0f) {
            CS = this->cs_overridenegative;
        } else {
            CS = std::clamp<f32>(baseCS * (this->has(ModFlags::HardRock) ? 1.3f  // different!
                                           : this->has(ModFlags::Easy)   ? 0.5f
                                                                         : 1.0f),
                                 0.0f, 10.0f);
        }
    }

    return CS;
}

f32 Mods::get_naive_hp(f32 baseHP) const {
    f32 HP = this->hp_override;
    if(HP < 0.f) {
        HP = std::clamp<f32>(baseHP * (this->has(ModFlags::HardRock) ? 1.4f
                                       : this->has(ModFlags::Easy)   ? 0.5f
                                                                     : 1.0f),
                             0.0f, 10.0f);
    }

    return HP;
}

f32 Mods::get_naive_od(f32 baseOD) const {
    f32 OD = this->od_override;
    if(OD < 0.f) {
        OD = std::clamp<f32>(baseOD * (this->has(ModFlags::HardRock) ? 1.4f
                                       : this->has(ModFlags::Easy)   ? 0.5f
                                                                     : 1.0f),
                             0.0f, 10.0f);
    }

    if(this->has(ModFlags::ODOverrideLock)) {
        OD = GameRules::odWithSpeed(OD, 1.f / this->speed);
    }

    return OD;
}

LegacyFlags Mods::to_legacy() const {
    LegacyFlags legacy_flags{};
    if(this->speed > 1.f) {
        legacy_flags |= LegacyFlags::DoubleTime;
        if(flags::has<ModFlags::NoPitchCorrection>(this->flags)) legacy_flags |= LegacyFlags::Nightcore;
    } else if(this->speed < 1.f) {
        legacy_flags |= LegacyFlags::HalfTime;
    }

    if(flags::has<ModFlags::NoFail>(this->flags)) legacy_flags |= LegacyFlags::NoFail;
    if(flags::has<ModFlags::Easy>(this->flags)) legacy_flags |= LegacyFlags::Easy;
    if(flags::has<ModFlags::TouchDevice>(this->flags)) legacy_flags |= LegacyFlags::TouchDevice;
    if(flags::has<ModFlags::Hidden>(this->flags)) legacy_flags |= LegacyFlags::Hidden;
    if(flags::has<ModFlags::HardRock>(this->flags)) legacy_flags |= LegacyFlags::HardRock;
    if(flags::has<ModFlags::SuddenDeath>(this->flags)) legacy_flags |= LegacyFlags::SuddenDeath;
    if(flags::has<ModFlags::Relax>(this->flags)) legacy_flags |= LegacyFlags::Relax;
    if(flags::has<ModFlags::Flashlight>(this->flags)) legacy_flags |= LegacyFlags::Flashlight;
    if(flags::has<ModFlags::SpunOut>(this->flags)) legacy_flags |= LegacyFlags::SpunOut;
    if(flags::has<ModFlags::Autopilot>(this->flags)) legacy_flags |= LegacyFlags::Autopilot;
    if(flags::has<ModFlags::Perfect>(this->flags)) legacy_flags |= LegacyFlags::Perfect;
    if(flags::has<ModFlags::Target>(this->flags)) legacy_flags |= LegacyFlags::Target;
    if(flags::has<ModFlags::ScoreV2>(this->flags)) legacy_flags |= LegacyFlags::ScoreV2;
    if(flags::has<ModFlags::Autoplay>(this->flags)) {
        legacy_flags &= ~(LegacyFlags::Relax | LegacyFlags::Autopilot);
        legacy_flags |= LegacyFlags::Autoplay;
    }

    // NOTE: Ignoring nightmare, fposu

    return legacy_flags;
}

Mods Mods::from_legacy(LegacyFlags legacy_flags) {
    ModFlags neoflags{};
    if(flags::has<LegacyFlags::NoFail>(legacy_flags)) neoflags |= ModFlags::NoFail;
    if(flags::has<LegacyFlags::Easy>(legacy_flags)) neoflags |= ModFlags::Easy;
    if(flags::has<LegacyFlags::TouchDevice>(legacy_flags)) neoflags |= ModFlags::TouchDevice;
    if(flags::has<LegacyFlags::Hidden>(legacy_flags)) neoflags |= ModFlags::Hidden;
    if(flags::has<LegacyFlags::HardRock>(legacy_flags)) neoflags |= ModFlags::HardRock;
    if(flags::has<LegacyFlags::SuddenDeath>(legacy_flags)) neoflags |= ModFlags::SuddenDeath;
    if(flags::has<LegacyFlags::Relax>(legacy_flags)) neoflags |= ModFlags::Relax;
    if(flags::has<LegacyFlags::Nightcore>(legacy_flags)) neoflags |= ModFlags::NoPitchCorrection;
    if(flags::has<LegacyFlags::Flashlight>(legacy_flags)) neoflags |= ModFlags::Flashlight;
    if(flags::has<LegacyFlags::SpunOut>(legacy_flags)) neoflags |= ModFlags::SpunOut;
    if(flags::has<LegacyFlags::Autopilot>(legacy_flags)) neoflags |= ModFlags::Autopilot;
    if(flags::has<LegacyFlags::Perfect>(legacy_flags)) neoflags |= ModFlags::Perfect;
    if(flags::has<LegacyFlags::Target>(legacy_flags)) neoflags |= ModFlags::Target;
    if(flags::has<LegacyFlags::ScoreV2>(legacy_flags)) neoflags |= ModFlags::ScoreV2;
    if(flags::has<LegacyFlags::Nightmare>(legacy_flags)) neoflags |= ModFlags::Nightmare;
    if(flags::has<LegacyFlags::FPoSu>(legacy_flags)) neoflags |= ModFlags::FPoSu;
    if(flags::has<LegacyFlags::Mirror>(legacy_flags)) {
        // NOTE: We don't know whether the original score was only horizontal, only vertical, or both
        neoflags |= (ModFlags::MirrorHorizontal | ModFlags::MirrorVertical);
    }
    if(flags::has<LegacyFlags::Autoplay>(legacy_flags)) {
        neoflags &= ~(ModFlags::Relax | ModFlags::Autopilot);
        neoflags |= ModFlags::Autoplay;
    }

    Mods mods;
    mods.flags = neoflags;
    if(flags::has<LegacyFlags::DoubleTime>(legacy_flags))
        mods.speed = 1.5f;
    else if(flags::has<LegacyFlags::HalfTime>(legacy_flags))
        mods.speed = 0.75f;
    return mods;
}

#ifndef BUILD_TOOLS_ONLY

f32 Mods::get_naive_ar(const DatabaseBeatmap *map) const {
    f32 baseAR = 5.0;
    if(!map) {
        debugLog("WARNING: NULL beatmap!!!");
    } else {
        baseAR = map->getAR();
    }
    return get_naive_ar(baseAR);
}

f32 Mods::get_naive_cs(const DatabaseBeatmap *map) const {
    f32 baseCS = 5.0;
    if(!map) {
        debugLog("WARNING: NULL beatmap!!!");
    } else {
        baseCS = map->getCS();
    }
    return get_naive_cs(baseCS);
}

f32 Mods::get_naive_hp(const DatabaseBeatmap *map) const {
    f32 baseHP = 5.0;
    if(!map) {
        debugLog("WARNING: NULL beatmap!!!");
    } else {
        baseHP = map->getHP();
    }
    return get_naive_hp(baseHP);
}

f32 Mods::get_naive_od(const DatabaseBeatmap *map) const {
    f32 baseOD = 5.0;
    if(!map) {
        debugLog("WARNING: NULL beatmap!!!");
    } else {
        baseOD = map->getOD();
    }
    return get_naive_od(baseOD);
}

f64 Mods::get_scorev1_multiplier() const {
    return DifficultyCalculator::getScoreV1ScoreMultiplier(this->flags, this->speed);
}

Mods Mods::from_cvars() {
    using enum ModFlags;
    Mods mods;

#define ADDIFCV(cvar__, mod__) cv::cvar__.getBool() ? (void)(mods.flags |= (mod__)) : (void)(0)

    ADDIFCV(mod_nofail, NoFail);
    ADDIFCV(drain_disabled, NoHP);  // Not an actual "mod", it's in the options menu
    ADDIFCV(mod_easy, Easy);
    ADDIFCV(mod_autopilot, Autopilot);
    ADDIFCV(mod_relax, Relax);
    ADDIFCV(mod_hidden, Hidden);
    ADDIFCV(mod_hardrock, HardRock);
    ADDIFCV(mod_flashlight, Flashlight);
    ADDIFCV(mod_suddendeath, SuddenDeath);
    ADDIFCV(mod_perfect, Perfect);
    ADDIFCV(mod_nightmare, Nightmare);
    ADDIFCV(nightcore_enjoyer, NoPitchCorrection);
    ADDIFCV(mod_touchdevice, TouchDevice);
    ADDIFCV(mod_spunout, SpunOut);
    ADDIFCV(mod_scorev2, ScoreV2);
    ADDIFCV(mod_fposu, FPoSu);
    ADDIFCV(mod_target, Target);
    ADDIFCV(ar_override_lock, AROverrideLock);
    ADDIFCV(od_override_lock, ODOverrideLock);
    ADDIFCV(mod_timewarp, Timewarp);
    ADDIFCV(mod_artimewarp, ARTimewarp);
    ADDIFCV(mod_minimize, Minimize);
    ADDIFCV(mod_jigsaw1, Jigsaw1);
    ADDIFCV(mod_jigsaw2, Jigsaw2);
    ADDIFCV(mod_wobble, Wobble1);
    ADDIFCV(mod_wobble2, Wobble2);
    ADDIFCV(mod_arwobble, ARWobble);
    ADDIFCV(mod_fullalternate, FullAlternate);
    ADDIFCV(mod_shirone, Shirone);
    ADDIFCV(mod_mafham, Mafham);
    ADDIFCV(mod_halfwindow, HalfWindow);
    ADDIFCV(mod_halfwindow_allow_300s, HalfWindowAllow300s);
    ADDIFCV(mod_ming3012, Ming3012);
    ADDIFCV(mod_no100s, No100s);
    ADDIFCV(mod_no50s, No50s);
    ADDIFCV(mod_singletap, Singletap);
    ADDIFCV(mod_no_keylock, NoKeylock);
    ADDIFCV(mod_no_pausing, NoPausing);
    if(cv::mod_autoplay.getBool()) {
        mods.flags &= ~(Relax | Autopilot);
        mods.flags |= Autoplay;
    }

#undef ADDIFCV

    if(f32 speed_override = cv::speed_override.getFloat(); speed_override >= 0.05f) {
        mods.speed = std::max(speed_override, 0.05f);
    } else {
        mods.speed = 1.f;
    }

    mods.notelock_type = cv::notelock_type.getInt();
    mods.autopilot_lenience = cv::autopilot_lenience.getFloat();
    mods.ar_override = cv::ar_override.getFloat();
    mods.ar_overridenegative = cv::ar_overridenegative.getFloat();
    mods.cs_override = cv::cs_override.getFloat();
    mods.cs_overridenegative = cv::cs_overridenegative.getFloat();
    mods.hp_override = cv::hp_override.getFloat();
    mods.od_override = cv::od_override.getFloat();
    mods.timewarp_multiplier = cv::mod_timewarp_multiplier.getFloat();
    mods.minimize_multiplier = cv::mod_minimize_multiplier.getFloat();
    mods.artimewarp_multiplier = cv::mod_artimewarp_multiplier.getFloat();
    mods.arwobble_strength = cv::mod_arwobble_strength.getFloat();
    mods.arwobble_interval = cv::mod_arwobble_interval.getFloat();
    mods.wobble_strength = cv::mod_wobble_strength.getFloat();
    mods.wobble_rotation_speed = cv::mod_wobble_rotation_speed.getFloat();
    mods.jigsaw_followcircle_radius_factor = cv::mod_jigsaw_followcircle_radius_factor.getFloat();
    mods.shirone_combo = cv::mod_shirone_combo.getFloat();

    return mods;
}

void Mods::use(const Mods &mods) {
    using enum ModFlags;
    // Reset mod selector buttons and sliders
    const auto &mod_selector = ui->getModSelector();
    mod_selector->resetMods();

    // Set cvars
#define CVFROMFLAG(cvar__, mod__) cv::cvar__.setValue(flags::has<mod__>(mods.flags))
#define CVFROMPROP(cvar__, modsetting__) cv::cvar__.setValue(mods.modsetting__)

    // FIXME: NoHP should not be changed here, it's a global option
    CVFROMFLAG(drain_disabled, NoHP);
    CVFROMFLAG(mod_nofail, NoFail);
    CVFROMFLAG(mod_easy, Easy);
    CVFROMFLAG(mod_hidden, Hidden);
    CVFROMFLAG(mod_hardrock, HardRock);
    CVFROMFLAG(mod_flashlight, Flashlight);
    CVFROMFLAG(mod_suddendeath, SuddenDeath);
    CVFROMFLAG(mod_perfect, Perfect);
    CVFROMFLAG(mod_nightmare, Nightmare);
    CVFROMFLAG(nightcore_enjoyer, NoPitchCorrection);
    CVFROMFLAG(mod_touchdevice, TouchDevice);
    CVFROMFLAG(mod_spunout, SpunOut);
    CVFROMFLAG(mod_scorev2, ScoreV2);
    CVFROMFLAG(mod_fposu, FPoSu);
    CVFROMFLAG(mod_target, Target);
    CVFROMFLAG(ar_override_lock, AROverrideLock);
    CVFROMFLAG(od_override_lock, ODOverrideLock);
    CVFROMFLAG(mod_timewarp, Timewarp);
    CVFROMFLAG(mod_artimewarp, ARTimewarp);
    CVFROMFLAG(mod_minimize, Minimize);
    CVFROMFLAG(mod_jigsaw1, Jigsaw1);
    CVFROMFLAG(mod_jigsaw2, Jigsaw2);
    CVFROMFLAG(mod_wobble, Wobble1);
    CVFROMFLAG(mod_wobble2, Wobble2);
    CVFROMFLAG(mod_arwobble, ARWobble);
    CVFROMFLAG(mod_fullalternate, FullAlternate);
    CVFROMFLAG(mod_shirone, Shirone);
    CVFROMFLAG(mod_mafham, Mafham);
    CVFROMFLAG(mod_halfwindow, HalfWindow);
    CVFROMFLAG(mod_halfwindow_allow_300s, HalfWindowAllow300s);
    CVFROMFLAG(mod_ming3012, Ming3012);
    CVFROMFLAG(mod_no100s, No100s);
    CVFROMFLAG(mod_no50s, No50s);
    CVFROMFLAG(mod_singletap, Singletap);
    CVFROMFLAG(mod_no_keylock, NoKeylock);
    CVFROMFLAG(mod_no_pausing, NoPausing);

    CVFROMPROP(notelock_type, notelock_type);
    CVFROMPROP(autopilot_lenience, autopilot_lenience);
    CVFROMPROP(mod_timewarp_multiplier, timewarp_multiplier);
    CVFROMPROP(mod_minimize_multiplier, minimize_multiplier);
    CVFROMPROP(mod_artimewarp_multiplier, artimewarp_multiplier);
    CVFROMPROP(mod_arwobble_strength, arwobble_strength);
    CVFROMPROP(mod_arwobble_interval, arwobble_interval);
    CVFROMPROP(mod_wobble_strength, wobble_strength);
    CVFROMPROP(mod_wobble_rotation_speed, wobble_rotation_speed);
    CVFROMPROP(mod_jigsaw_followcircle_radius_factor, jigsaw_followcircle_radius_factor);
    CVFROMPROP(mod_shirone_combo, shirone_combo);
    CVFROMPROP(ar_override, ar_override);
    CVFROMPROP(ar_overridenegative, ar_overridenegative);
    CVFROMPROP(cs_override, cs_override);
    CVFROMPROP(cs_overridenegative, cs_overridenegative);
    CVFROMPROP(hp_override, hp_override);
    CVFROMPROP(od_override, od_override);

#undef CVFROMPROP
#undef CVFROMFLAG

    if(flags::has<Autoplay>(mods.flags)) {
        cv::mod_autoplay.setValue(true);
        cv::mod_autopilot.setValue(false);
        cv::mod_relax.setValue(false);
    } else {
        cv::mod_autoplay.setValue(false);
        cv::mod_autopilot.setValue(flags::has<Autopilot>(mods.flags));
        cv::mod_relax.setValue(flags::has<Relax>(mods.flags));
    }

    f32 speed_override = mods.speed == 1.f ? -1.f : mods.speed;
    cv::speed_override.setValue(speed_override);

    // Update mod selector UI
    mod_selector->enableModsFromFlags(mods.to_legacy());
    cv::speed_override.setValue(speed_override);  // enableModsFromFlags() edits cv::speed_override
    mod_selector->ARLock->setChecked(flags::has<AROverrideLock>(mods.flags));
    mod_selector->ODLock->setChecked(flags::has<ODOverrideLock>(mods.flags));
    mod_selector->speedSlider->setValue(mods.speed, false, false);
    mod_selector->CSSlider->setValue(mods.cs_override, false, false);
    mod_selector->ARSlider->setValue(mods.ar_override, false, false);
    mod_selector->ODSlider->setValue(mods.od_override, false, false);
    mod_selector->HPSlider->setValue(mods.hp_override, false, false);
    mod_selector->updateOverrideSliderLabels();
    mod_selector->updateExperimentalButtons();

    // FIXME: this is already called like 5 times from the previous calls
    osu->updateMods();
}

template <GenericReader R>
Mods Mods::unpack(R &reader) {
    Mods mods;

    mods.flags = static_cast<ModFlags>(reader.template read<u64>());
    mods.speed = reader.template read<f32>();
    mods.notelock_type = reader.template read<i32>();
    mods.ar_override = reader.template read<f32>();
    mods.ar_overridenegative = reader.template read<f32>();
    mods.cs_override = reader.template read<f32>();
    mods.cs_overridenegative = reader.template read<f32>();
    mods.hp_override = reader.template read<f32>();
    mods.od_override = reader.template read<f32>();
    using enum ModFlags;
    if(flags::has<Autopilot>(mods.flags)) {
        mods.autopilot_lenience = reader.template read<f32>();
    }
    if(flags::has<Timewarp>(mods.flags)) {
        mods.timewarp_multiplier = reader.template read<f32>();
    }
    if(flags::has<Minimize>(mods.flags)) {
        mods.minimize_multiplier = reader.template read<f32>();
    }
    if(flags::has<ARTimewarp>(mods.flags)) {
        mods.artimewarp_multiplier = reader.template read<f32>();
    }
    if(flags::has<ARWobble>(mods.flags)) {
        mods.arwobble_strength = reader.template read<f32>();
        mods.arwobble_interval = reader.template read<f32>();
    }
    if(flags::any<Wobble1 | Wobble2>(mods.flags)) {
        mods.wobble_strength = reader.template read<f32>();
        mods.wobble_frequency = reader.template read<f32>();
        mods.wobble_rotation_speed = reader.template read<f32>();
    }
    if(flags::any<Jigsaw1 | Jigsaw2>(mods.flags)) {
        mods.jigsaw_followcircle_radius_factor = reader.template read<f32>();
    }
    if(flags::has<Shirone>(mods.flags)) {
        mods.shirone_combo = reader.template read<f32>();
    }

    return mods;
}

template <GenericWriter W>
void Mods::pack_and_write(W &writer, const Mods &mods) {
    writer.template write<u64>(static_cast<u64>(mods.flags));
    writer.template write<f32>(mods.speed);
    writer.template write<i32>(mods.notelock_type);
    writer.template write<f32>(mods.ar_override);
    writer.template write<f32>(mods.ar_overridenegative);
    writer.template write<f32>(mods.cs_override);
    writer.template write<f32>(mods.cs_overridenegative);
    writer.template write<f32>(mods.hp_override);
    writer.template write<f32>(mods.od_override);
    using enum ModFlags;
    if(flags::has<Autopilot>(mods.flags)) {
        writer.template write<f32>(mods.autopilot_lenience);
    }
    if(flags::has<Timewarp>(mods.flags)) {
        writer.template write<f32>(mods.timewarp_multiplier);
    }
    if(flags::has<Minimize>(mods.flags)) {
        writer.template write<f32>(mods.minimize_multiplier);
    }
    if(flags::has<ARTimewarp>(mods.flags)) {
        writer.template write<f32>(mods.artimewarp_multiplier);
    }
    if(flags::has<ARWobble>(mods.flags)) {
        writer.template write<f32>(mods.arwobble_strength);
        writer.template write<f32>(mods.arwobble_interval);
    }
    if(flags::any<Wobble1 | Wobble2>(mods.flags)) {
        writer.template write<f32>(mods.wobble_strength);
        writer.template write<f32>(mods.wobble_frequency);
        writer.template write<f32>(mods.wobble_rotation_speed);
    }
    if(flags::any<Jigsaw1 | Jigsaw2>(mods.flags)) {
        writer.template write<f32>(mods.jigsaw_followcircle_radius_factor);
    }
    if(flags::has<Shirone>(mods.flags)) {
        writer.template write<f32>(mods.shirone_combo);
    }
}

// explicit instantiations
template Mods Mods::unpack<ByteBufferedFile::Reader>(ByteBufferedFile::Reader &);
template Mods Mods::unpack<Packet>(Packet &);

template void Mods::pack_and_write<ByteBufferedFile::Writer>(ByteBufferedFile::Writer &, const Mods &);
template void Mods::pack_and_write<Packet>(Packet &, const Mods &);

#endif  // BUILD_TOOLS_ONLY

}  // namespace Replay
