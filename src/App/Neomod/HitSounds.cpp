#include "HitSounds.h"

#include "BeatmapInterface.h"
#include "OsuConVars.h"
// #include "Logging.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "SoundEngine.h"
#include "Sound.h"

i32 HitSamples::getNormalSet(const HitSoundContext &ctx) const {
    if(ctx.forcedSampleSet > 0) return ctx.forcedSampleSet;
    if(this->normalSet != 0) return this->normalSet;
    if(ctx.timingPointSampleSet != 0) return ctx.timingPointSampleSet;
    return ctx.defaultSampleSet;
}

i32 HitSamples::getAdditionSet(const HitSoundContext &ctx) const {
    if(ctx.forcedSampleSet > 0) return ctx.forcedSampleSet;
    if(this->additionSet != 0) return this->additionSet;
    return this->getNormalSet(ctx);
}

f32 HitSamples::getVolume(const HitSoundContext &ctx, i32 hitSoundType, bool is_sliderslide) const {
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
        if(this->volume > 0) {
            volume *= (f32)this->volume / 100.0f;
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
static constexpr const i32 HIT_IDX = 0;
static constexpr const i32 SLIDER_IDX = 1;
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
              (Sound* Skin::*)nullptr,        // SET-sliderfinish and SET-sliderclap aren't actually valid
              (Sound* Skin::*)nullptr         //
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
              (Sound* Skin::*)nullptr,      //
              (Sound* Skin::*)nullptr       //
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
              (Sound* Skin::*)nullptr,      //
              (Sound* Skin::*)nullptr       //
          }}};  //
#undef A_

// maps SampleSetType (1-3) to SOUND_METHODS index (0-2)
static constexpr i32 sampleSetToIndex(i32 set) {
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
static constexpr i32 hitSoundToIndex(i32 hitSound) {
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

std::vector<ResolvedHitSound> HitSamples::resolve(const HitSoundContext &ctx, bool is_sliderslide) const {
    std::vector<ResolvedHitSound> result;

    namespace HT = HitSoundType;
    for(const auto type : {HT::NORMAL, HT::WHISTLE, HT::FINISH, HT::CLAP}) {
        // special case for NORMAL (play if this->hitSounds == 0 or layered hitsounds are enabled)

        // NOTE: LayeredHitSounds seems to be forced even if the map uses custom hitsounds
        //       according to https://osu.ppy.sh/community/forums/topics/15937
        if(!(this->hitSounds & type) &&
           !((type == HT::NORMAL) && ((this->hitSounds == 0) || ctx.layeredHitSounds)))
            continue;

        const f32 vol = this->getVolume(ctx, type, is_sliderslide);
        if(vol <= 0.f) continue;  // don't play silence

        const i32 set = type == HT::NORMAL ? this->getNormalSet(ctx) : this->getAdditionSet(ctx);

        const i32 set_idx = sampleSetToIndex(set);
        const i32 slider_idx = is_sliderslide ? SLIDER_IDX : HIT_IDX;
        const i32 hit_idx = hitSoundToIndex(type);

        // check that a valid sound method exists in the lookup table
        if(SOUND_METHODS[set_idx][slider_idx][hit_idx] != nullptr) {
            result.push_back({set_idx, slider_idx, hit_idx, vol});
        }
    }

    return result;
}

ResolvedSliderTick HitSamples::resolveSliderTick(const HitSoundContext &ctx) const {
    // slider ticks use the normal sample set per osu! reference behavior
    const i32 set = this->getNormalSet(ctx);
    const i32 set_idx = sampleSetToIndex(set);

    // slider tick volume: no hitcircle-type modifier (is_sliderslide=true), no boost
    f32 volume = 1.0f;
    if(!ctx.ignoreSampleVolume) {
        if(this->volume > 0) {
            volume *= (f32)this->volume / 100.0f;
        } else {
            volume *= (f32)ctx.timingPointVolume / 100.0f;
        }
    }

    return {set_idx, volume};
}

// global-dependent methods (delegate to pure versions)

std::vector<HitSamples::Set_Slider_Hit> HitSamples::play(f32 pan, i32 delta, i32 play_time, bool is_sliderslide) {
    const auto &map_iface = osu->getMapInterface();
    if(unlikely(!map_iface)) return {};  // sanity

    // Don't play hitsounds when seeking
    if(unlikely(map_iface->bWasSeekFrame)) return {};

    const Skin *skin = map_iface->getSkin();
    if(unlikely(!skin)) return {};  // sanity

    if(!cv::sound_panning.getBool() || (cv::mod_fposu.getBool() && !cv::mod_fposu_sound_panning.getBool()) ||
       (cv::mod_fps.getBool() && !cv::mod_fps_sound_panning.getBool())) {
        pan = 0.0f;
    } else {
        pan *= cv::sound_panning_multiplier.getFloat();
    }

    f32 pitch = 0.f;
    if(cv::snd_pitch_hitsounds.getBool()) {
        f32 range = map_iface->getHitWindow100();
        pitch = (f32)delta / range * cv::snd_pitch_hitsounds_factor.getFloat();
    }

    // build context from current state
    const BeatmapDifficulty *beatmap = map_iface->getBeatmap();
    const auto ti = (play_time != -1 && beatmap) ? beatmap->getTimingInfoForTime(play_time)
                                                  : map_iface->getCurrentTimingInfo();
    HitSoundContext ctx{
        .timingPointSampleSet = ti.sampleSet,
        .timingPointVolume = ti.volume,
        .defaultSampleSet = map_iface->getDefaultSampleSet(),
        .layeredHitSounds = skin->o_layered_hitsounds,
        .forcedSampleSet = cv::skin_force_hitsound_sample_set.getInt(),
        .ignoreSampleVolume = cv::ignore_beatmap_sample_volume.getBool(),
        .boostVolume = cv::snd_boost_hitsound_volume.getBool(),
    };

    auto resolved = this->resolve(ctx, is_sliderslide);

    // actually play the resolved sounds
    std::vector<Set_Slider_Hit> played_list;
    for(const auto &r : resolved) {
        Sound *Skin::*sound_ptr = SOUND_METHODS[r.set][r.slider][r.hit];
        Sound *snd = skin->*sound_ptr;
        if(!snd) continue;

        if(is_sliderslide && snd->isPlaying()) continue;

        if(soundEngine->play(snd, pan, pitch, r.volume)) {
            played_list.push_back({r.set, r.slider, r.hit});
        }
    }

    return played_list;
}

void HitSamples::stop(const std::vector<Set_Slider_Hit> &specific_sets) {
    // TODO @kiwec: map hitsounds are not supported
    const auto &map_iface = osu->getMapInterface();
    if(unlikely(!map_iface)) return;  // sanity
    const Skin *skin = map_iface->getSkin();
    if(unlikely(!skin)) return;  // sanity

    // stop specified previously played sounds, otherwise stop everything
    if(!specific_sets.empty()) {
        for(const auto &triple : specific_sets) {
            assert(SOUND_METHODS[triple.set][triple.slider][triple.hit]);
            Sound *to_stop = skin->*SOUND_METHODS[triple.set][triple.slider][triple.hit];

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
            Sound *snd_memb = skin->*slider_snd_ptr;
            if(snd_memb != nullptr && snd_memb->isPlaying()) {
                // debugLog("stopping {}", snd_memb->getFilePath());
                soundEngine->stop(snd_memb);
            }
        }
    }
}
