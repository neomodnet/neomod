#include "HitSounds.h"

#include "BeatmapFile/BeatmapPrimitives.h"
#include "OsuConVars.h"
// #include "Logging.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "Sound.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <utility>

namespace neomod::HitSoundUtils {
using namespace DatabaseBeatmapTypes;

u8 getNormalSet(HITSAMPLE_BITS info, const HitSoundContext &ctx) {
    if(ctx.forcedSampleSet > 0) return ctx.forcedSampleSet;
    if(info.normalSet != 0) return info.normalSet;
    if(ctx.timingPointSampleSet != 0) return ctx.timingPointSampleSet;
    return ctx.defaultSampleSet;
}

u8 getAdditionSet(HITSAMPLE_BITS info, const HitSoundContext &ctx) {
    if(ctx.forcedSampleSet > 0) return ctx.forcedSampleSet;
    if(info.additionSet != 0) return info.additionSet;
    return getNormalSet(info, ctx);
}

f32 getVolume(HITSAMPLE_BITS info, const HitSoundContext &ctx, u8 hitSoundType, bool is_sliderslide) {
    f32 volume = 1.0f;

    // some hardcoded modifiers for hitcircle sounds
    if(!is_sliderslide) {
        switch(hitSoundType) {
            case HitSoundType::NORMAL:
                volume *= 0.8f;
                break;
            case HitSoundType::WHISTLE:
                volume *= 0.85f;
                break;
            case HitSoundType::FINISH:
                volume *= 1.0f;
                break;
            case HitSoundType::CLAP:
                volume *= 0.85f;
                break;
            default:
                std::unreachable();
        }
    }

    if(!ctx.ignoreSampleVolume) {
        if(info.volume > 0) {
            volume *= (f32)info.volume / 100.0f;
        } else {
            volume *= (f32)ctx.timingPointVolume / 100.0f;
        }
    }

    if(!is_sliderslide && ctx.boostVolume) {
        static constexpr const float ONE_OVER_E = 3.678795e-01f;
        static constexpr const float ONE_IDENT_MUL = 0.761463f;
        volume = (std::log(volume + ONE_OVER_E) + 1.f) * ONE_IDENT_MUL;
        volume = std::clamp(volume, 0.f, 1.f);
    }

    return volume;
}

// O(1) lookup table for sound names
// [set][is_sliderslide][hitSound]
static constexpr const u8 HIT_IDX = 0;
static constexpr const u8 SLIDER_IDX = 1;
#define A_ std::array
static constexpr auto SOUND_METHODS =  //
    A_{                                //
       // SampleSetType::NORMAL            //
       A_{//
          // HIT sounds
          A_{
              &Skin::s_normal_hitnormal,   // HitSoundType::NORMAL
              &Skin::s_normal_hitwhistle,  // HitSoundType::WHISTLE
              &Skin::s_normal_hitfinish,   // HitSoundType::FINISH
              &Skin::s_normal_hitclap      // HitSoundType::CLAP
          },
          // SLIDER sounds
          A_{
              &Skin::s_normal_sliderslide,    //
              &Skin::s_normal_sliderwhistle,  //
              (Sound * Skin::*)nullptr,       // SET-sliderfinish and SET-sliderclap aren't actually valid
              (Sound * Skin::*)nullptr        //
          }},
       // SampleSetType::SOFT
       A_{//
          // HIT sounds
          A_{
              &Skin::s_soft_hitnormal,   // ditto...
              &Skin::s_soft_hitwhistle,  //
              &Skin::s_soft_hitfinish,   //
              &Skin::s_soft_hitclap      //
          },                             //
          // SLIDER sounds
          A_{
              &Skin::s_soft_sliderslide,    //
              &Skin::s_soft_sliderwhistle,  //
              (Sound * Skin::*)nullptr,     //
              (Sound * Skin::*)nullptr      //
          }},                               //
       // SampleSetType::DRUM
       A_{//
          // HIT sounds
          A_{
              &Skin::s_drum_hitnormal,   //
              &Skin::s_drum_hitwhistle,  //
              &Skin::s_drum_hitfinish,   //
              &Skin::s_drum_hitclap      //
          },                             //
          // SLIDER sounds
          A_{
              &Skin::s_drum_sliderslide,    //
              &Skin::s_drum_sliderwhistle,  //
              (Sound * Skin::*)nullptr,     //
              (Sound * Skin::*)nullptr      //
          }}};  //
#undef A_

// maps SampleSetType (1-3) to SOUND_METHODS index (0-2)
static constexpr u8 sampleSetToIndex(u8 set) {
    switch(set) {
        case SampleSetType::NORMAL:
            return 0;
        case SampleSetType::SOFT:
            return 1;
        case SampleSetType::DRUM:
            return 2;
        default:
            return 0;
    }
}

// maps HitSoundType bitmask value to SOUND_METHODS hit index (0-3)
static constexpr u8 hitSoundToIndex(u8 hitSound) {
    switch(hitSound) {
        case HitSoundType::NORMAL:
            return 0;
        case HitSoundType::WHISTLE:
            return 1;
        case HitSoundType::FINISH:
            return 2;
        case HitSoundType::CLAP:
            return 3;
        default:
            return 0;
    }
}

std::vector<ResolvedHitSound> resolve(HITSAMPLE_BITS info, const HitSoundContext &ctx, bool is_sliderslide) {
    std::vector<ResolvedHitSound> result;

    using HT = HitSoundType;
    for(const auto type : {HT::NORMAL, HT::WHISTLE, HT::FINISH, HT::CLAP}) {
        // special case for NORMAL (play if info.hitSounds == 0 or layered hitsounds are enabled)

        // NOTE: LayeredHitSounds seems to be forced even if the map uses custom hitsounds
        //       according to https://osu.ppy.sh/community/forums/topics/15937
        if(!(info.hitSounds & type) && !((type == HT::NORMAL) && ((info.hitSounds == 0) || ctx.layeredHitSounds)))
            continue;

        const f32 vol = getVolume(info, ctx, type, is_sliderslide);
        if(vol <= 0.f) continue;  // don't play silence

        const u8 set = type == HT::NORMAL ? getNormalSet(info, ctx) : getAdditionSet(info, ctx);

        const u8 set_idx = sampleSetToIndex(set);
        const u8 slider_idx = is_sliderslide ? SLIDER_IDX : HIT_IDX;
        const u8 hit_idx = hitSoundToIndex(type);

        // check that a valid sound method exists in the lookup table
        if(SOUND_METHODS[set_idx][slider_idx][hit_idx] != nullptr) {
            result.push_back({vol, set_idx, slider_idx, hit_idx});
        }
    }

    return result;
}

ResolvedSliderTick resolveSliderTick(HITSAMPLE_BITS info, const HitSoundContext &ctx) {
    // slider ticks use the normal sample set per osu! reference behavior
    const u8 set = getNormalSet(info, ctx);
    const u8 set_idx = sampleSetToIndex(set);

    // slider tick volume: no hitcircle-type modifier (is_sliderslide=true), no boost
    f32 volume = 1.0f;
    if(!ctx.ignoreSampleVolume) {
        if(info.volume > 0) {
            volume *= (f32)info.volume / 100.0f;
        } else {
            volume *= (f32)ctx.timingPointVolume / 100.0f;
        }
    }

    return {volume, set_idx};
}

TIMING_INFO samplesAt(const Primitives::TimingPoints &timing, i32 timeMS) {
    return timing.getTimingInfo(timeMS + cv::timingpoints_offset.getInt());
}

HitSoundContext makeContext(const TIMING_INFO &samples, u8 defaultSampleSet, bool layeredHitSounds) {
    return {
        .timingPointSampleSet = samples.sampleSet,
        .timingPointVolume = samples.volume,
        .defaultSampleSet = defaultSampleSet,
        .forcedSampleSet = cv::skin_force_hitsound_sample_set.getVal<u8>(),
        .layeredHitSounds = layeredHitSounds,
        .ignoreSampleVolume = cv::ignore_beatmap_sample_volume.getBool(),
        .boostVolume = cv::snd_boost_hitsound_volume.getBool(),
    };
}

f32 playedPan(f32 pan) {
    if(!cv::sound_panning.getBool() || (cv::mod_fposu.getBool() && !cv::mod_fposu_sound_panning.getBool()) ||
       (cv::mod_fps.getBool() && !cv::mod_fps_sound_panning.getBool())) {
        return 0.0f;
    }
    return pan * cv::sound_panning_multiplier.getFloat();
}

std::vector<Set_Slider_Hit> play(const Skin &skin, std::span<const ResolvedHitSound> sounds, f32 pan, f32 pitch) {
    std::vector<Set_Slider_Hit> played_list;
    for(const auto &r : sounds) {
        Sound *Skin::*sound_ptr = SOUND_METHODS[r.set][r.slider][r.hit];
        Sound *snd = skin.*sound_ptr;
        if(!snd) continue;

        if(r.slider == SLIDER_IDX && snd->isPlaying()) continue;

        if(soundEngine->play(snd, pan, pitch, r.volume)) {
            played_list.push_back({r.set, r.slider, r.hit});
        }
    }

    return played_list;
}

void playSliderTick(const Skin &skin, ResolvedSliderTick tick, f32 pan) {
    static constexpr std::array SLIDERTICK_SAMPLESET_METHODS{
        &Skin::s_normal_slidertick,  //
        &Skin::s_soft_slidertick,    //
        &Skin::s_drum_slidertick,    //
    };

    if(tick.set >= SLIDERTICK_SAMPLESET_METHODS.size()) return;
    if(Sound *snd = skin.*SLIDERTICK_SAMPLESET_METHODS[tick.set]) soundEngine->play(snd, pan, 0.f, tick.volume);
}

void stopSliderSounds(const Skin &skin, const std::vector<Set_Slider_Hit> &specific_sets) {
    // TODO @kiwec: map hitsounds are not supported

    // stop specified previously played sounds, otherwise stop everything
    if(!specific_sets.empty()) {
        for(const auto &triple : specific_sets) {
            assert(SOUND_METHODS[triple.set][triple.slider][triple.hit]);
            Sound *to_stop = skin.*SOUND_METHODS[triple.set][triple.slider][triple.hit];

            if(to_stop && to_stop->isPlaying()) {
                // debugLog("stopping specific set {} {} {} {}", triple.set, triple.slider, triple.hit,
                //          to_stop->getFilePath());
                soundEngine->stop(to_stop);
            }
        }
        return;
    }

    // NOTE: Timing point might have changed since the time we called play().
    //       So for now we're stopping ALL slider sounds, but in the future
    //       we'll need to store the started sounds somewhere.

    // Bruteforce approach. Will be rewritten when adding map hitsounds.
    for(const auto &sample_set : SOUND_METHODS) {
        const auto &slider_sounds = sample_set[SLIDER_IDX];
        for(const auto &slider_snd_ptr : slider_sounds) {
            if(slider_snd_ptr == nullptr) continue;  // ugly
            Sound *snd_memb = skin.*slider_snd_ptr;
            if(snd_memb != nullptr && snd_memb->isPlaying()) {
                // debugLog("stopping {}", snd_memb->getFilePath());
                soundEngine->stop(snd_memb);
            }
        }
    }
}

}  // namespace neomod::HitSoundUtils