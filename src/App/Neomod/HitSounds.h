#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.
#include <string>
#include <vector>
#include "types.h"

namespace SampleSetType {
enum {
    NORMAL = 1,
    SOFT = 2,
    DRUM = 3,
};
}

namespace HitSoundType {
enum {
    NORMAL = (1 << 0),
    WHISTLE = (1 << 1),
    FINISH = (1 << 2),
    CLAP = (1 << 3),

    VALID_HITSOUNDS = NORMAL | WHISTLE | FINISH | CLAP,
    VALID_SLIDER_HITSOUNDS = NORMAL | WHISTLE,
};
}

// all external state that hitsound resolution depends on, gathered from globals at the call site
struct HitSoundContext {
    i32 timingPointSampleSet;  // from TIMING_INFO::sampleSet for the current play_time
    i32 timingPointVolume;     // from TIMING_INFO::volume for the current play_time
    i32 defaultSampleSet;      // from BeatmapInterface::getDefaultSampleSet()
    bool layeredHitSounds;     // from Skin::o_layered_hitsounds
    i32 forcedSampleSet;       // cv::skin_force_hitsound_sample_set
    bool ignoreSampleVolume;   // cv::ignore_beatmap_sample_volume
    bool boostVolume;          // cv::snd_boost_hitsound_volume
};

// result of resolving which sounds should be played, without actually playing them
struct ResolvedHitSound {
    i32 set;     // index into sound lookup table: 0=normal, 1=soft, 2=drum
    i32 slider;  // 0=hit, 1=slider
    i32 hit;     // 0=normal, 1=whistle, 2=finish, 3=clap
    f32 volume;
};

// resolves which slider tick sound to play; returns the sample set index (0-2) and volume.
// uses the normal sample set per osu! reference behavior.
struct ResolvedSliderTick {
    i32 set;  // 0=normal, 1=soft, 2=drum
    f32 volume;
};

struct HitSamples final {
    u8 hitSounds = 0;           // bitfield of HitSoundTypes to play
    u8 normalSet = 0;           // SampleSetType of the normal sound
    u8 additionSet = 0;         // SampleSetType of the whistle, finish and clap sounds
    u8 volume = 0;              // volume of the sample, 1-100. if 0, use timing point volume instead
    i32 index = 0;              // index of the sample (for custom map sounds). if 0, use skin sound instead
    std::string filename = "";  // when not empty, ignore all the above mess (except volume) and just play that file

    struct Set_Slider_Hit {
        i32 set;
        i32 slider;
        i32 hit;
    };

    std::vector<Set_Slider_Hit> play(f32 pan, i32 delta, i32 play_time = -1, bool is_sliderslide = false);
    void stop(const std::vector<Set_Slider_Hit> &specific_sets = {});

    // pure versions that take all dependencies as parameters
    [[nodiscard]] i32 getNormalSet(const HitSoundContext &ctx) const;
    [[nodiscard]] i32 getAdditionSet(const HitSoundContext &ctx) const;
    [[nodiscard]] f32 getVolume(const HitSoundContext &ctx, i32 hitSoundType, bool is_sliderslide) const;

    // determines which sounds should be played without actually playing them
    [[nodiscard]] std::vector<ResolvedHitSound> resolve(const HitSoundContext &ctx, bool is_sliderslide) const;

    [[nodiscard]] ResolvedSliderTick resolveSliderTick(const HitSoundContext &ctx) const;
};
