#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.

#include "types.h"
#include "DatabaseBeatmapTypes.h"

#include <string>
#include <vector>
#include <memory>

class BeatmapInterface;

namespace neomod::HitSoundUtils {

// all external state that hitsound resolution depends on, gathered from globals at the call site
struct HitSoundContext {
    i32 timingPointSampleSet;  // from TIMING_INFO::sampleSet for the current play_time
    i32 timingPointVolume;     // from TIMING_INFO::volume for the current play_time
    u8 defaultSampleSet;       // from BeatmapInterface::getDefaultSampleSet()
    u8 forcedSampleSet;        // cv::skin_force_hitsound_sample_set
    bool layeredHitSounds;     // from Skin::o_layered_hitsounds
    bool ignoreSampleVolume;   // cv::ignore_beatmap_sample_volume
    bool boostVolume;          // cv::snd_boost_hitsound_volume
};

// result of resolving which sounds should be played, without actually playing them
struct ResolvedHitSound {
    f32 volume;
    u8 set;     // index into sound lookup table: 0=normal, 1=soft, 2=drum
    u8 slider;  // 0=hit, 1=slider
    u8 hit;     // 0=normal, 1=whistle, 2=finish, 3=clap
};

// resolves which slider tick sound to play; returns the sample set index (0-2) and volume.
// uses the normal sample set per osu! reference behavior.
struct ResolvedSliderTick {
    f32 volume;
    u8 set;  // 0=normal, 1=soft, 2=drum
};

// played sample indices for passing to stop()
struct Set_Slider_Hit {
    u8 set;
    u8 slider;
    u8 hit;
};

std::vector<Set_Slider_Hit> play(BeatmapInterface *pf, DatabaseBeatmapTypes::HITSAMPLE_BITS info, f32 pan, i32 delta, i32 play_time = -1,
                                 bool is_sliderslide = false);

void stopSliderSounds(BeatmapInterface *pf, const std::vector<Set_Slider_Hit> &specific_sets);

// pure versions that take all dependencies as parameters
[[nodiscard]] u8 getNormalSet(DatabaseBeatmapTypes::HITSAMPLE_BITS info, const HitSoundContext &ctx);
[[nodiscard]] u8 getAdditionSet(DatabaseBeatmapTypes::HITSAMPLE_BITS info, const HitSoundContext &ctx);
[[nodiscard]] f32 getVolume(DatabaseBeatmapTypes::HITSAMPLE_BITS info, const HitSoundContext &ctx, u8 hitSoundType, bool is_sliderslide);

// determines which sounds should be played without actually playing them
[[nodiscard]] std::vector<ResolvedHitSound> resolve(DatabaseBeatmapTypes::HITSAMPLE_BITS info, const HitSoundContext &ctx,
                                                    bool is_sliderslide);

[[nodiscard]] ResolvedSliderTick resolveSliderTick(DatabaseBeatmapTypes::HITSAMPLE_BITS info, const HitSoundContext &ctx);
}  // namespace neomod::HitSoundUtils
