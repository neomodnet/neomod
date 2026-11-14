#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.

#include "types.h"
#include "DatabaseBeatmapTypes.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

struct Skin;
namespace neomod::Primitives {
class TimingPoints;
}

namespace neomod::HitSoundUtils {

// all external state that hitsound resolution depends on (see makeContext())
struct HitSoundContext {
    i32 timingPointSampleSet;    // from the TIMING_INFO samplesAt() the hitsound's time
    i32 timingPointVolume;       // ditto
    i32 timingPointSampleIndex;  // ditto
    u8 defaultSampleSet;         // the map's
    u8 forcedSampleSet;          // cv::skin_force_hitsound_sample_set
    bool layeredHitSounds;       // from Skin::o_layered_hitsounds
    bool ignoreSampleVolume;     // cv::ignore_beatmap_sample_volume
    bool boostVolume;            // cv::snd_boost_hitsound_volume
};

// result of resolving which sounds should be played, without actually playing them
struct ResolvedHitSound {
    f32 volume;
    u8 set;        // index into sound lookup table: 0=normal, 1=soft, 2=drum
    u8 slider;     // 0=hit, 1=slider
    u8 hit;        // 0=normal, 1=whistle, 2=finish, 3=clap
    u16 index;     // custom sample index (0: user skin sound, 1+: map sounds)
    u16 filename;  // HITSAMPLE_BITS::filename of a normal hit sound (0: none)
};

// resolves which slider tick sound to play; returns the sample set index (0-2) and volume.
// uses the normal sample set per osu! reference behavior.
struct ResolvedSliderTick {
    f32 volume;
    u8 set;     // 0=normal, 1=soft, 2=drum
    u16 index;  // custom sample index (0: user skin sound, 1+: map sounds)
};

// played sample indices for passing to stop()
struct Set_Slider_Hit {
    u8 set;
    u8 slider;
    u8 hit;
    u16 index;
};

// the timing point a hitsound at `timeMS` takes its sample set and volume from: the one active timingpoints_offset
// after it
[[nodiscard]] DatabaseBeatmapTypes::TIMING_INFO samplesAt(const Primitives::TimingPoints &timing, i32 timeMS);
// a hitsound's context from those samples, the map's default sample set, whether the skin layers its hitsounds and
// whether the map has a storyboard (the rest from the hitsound convars)
[[nodiscard]] HitSoundContext makeContext(const DatabaseBeatmapTypes::TIMING_INFO &samples, u8 defaultSampleSet,
                                          bool layeredHitSounds, bool storyboard);
// the pan a hitsound plays at: `pan` (from where it happens on the playfield) through sound_panning and its multiplier,
// centered for FPoSu and FPS unless their own panning convars are on
[[nodiscard]] f32 playedPan(f32 pan);

// what the sample a map has for a custom sample index (1+) is called, without its extension: "<set>-<sound>" for index
// 1, "<set>-<sound><index>" after it. set: 0 normal, 1 soft, 2 drum; sound: "hitclap", "slidertick", ...
[[nodiscard]] std::string mapSampleName(u8 set, std::string_view sound, u16 index);

// plays resolved hitsounds with skin/map sounds; returns the ones it played, for stopSliderSounds()
std::vector<Set_Slider_Hit> play(const Skin &skin, std::span<const ResolvedHitSound> sounds, f32 pan, f32 pitch);
void playSliderTick(const Skin &skin, ResolvedSliderTick tick, f32 pan);
// the given ones, or every slider sound if there are none
void stopSliderSounds(const Skin &skin, const std::vector<Set_Slider_Hit> &specific_sets);

// pure versions that take all dependencies as parameters
[[nodiscard]] u8 getNormalSet(DatabaseBeatmapTypes::HITSAMPLE_BITS info, const HitSoundContext &ctx);
[[nodiscard]] u8 getAdditionSet(DatabaseBeatmapTypes::HITSAMPLE_BITS info, const HitSoundContext &ctx);
[[nodiscard]] f32 getVolume(DatabaseBeatmapTypes::HITSAMPLE_BITS info, const HitSoundContext &ctx, u8 hitSoundType,
                            bool is_sliderslide);

// determines which sounds should be played without actually playing them
[[nodiscard]] std::vector<ResolvedHitSound> resolve(DatabaseBeatmapTypes::HITSAMPLE_BITS info,
                                                    const HitSoundContext &ctx, bool is_sliderslide);

[[nodiscard]] ResolvedSliderTick resolveSliderTick(DatabaseBeatmapTypes::HITSAMPLE_BITS info,
                                                   const HitSoundContext &ctx);
}  // namespace neomod::HitSoundUtils
