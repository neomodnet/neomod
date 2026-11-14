// Copyright (c) 2026, WH, All rights reserved.
#include "Playback.h"

#include "Clock.h"
#include "ContainerRanges.h"
#include "Document.h"
#include "GameRules.h"
#include "HitSounds.h"
#include "MapObjects.h"
#include "Osu.h"
#include "Skin.h"

#include <algorithm>

using namespace neomod;
using Cue = HitObject::SoundCue;

namespace {
// a frame longer than this (in music time) is a hitch, whose cues stay silent
constexpr i32 MAX_FRAME_MS{300};

HitSoundUtils::HitSoundContext contextAt(const Document &document, const Skin &skin, i32 timeMS) {
    const Primitives::PRIMITIVE_CONTAINER &c = document.getPrimitives();
    // the editor shows no storyboard
    return HitSoundUtils::makeContext(HitSoundUtils::samplesAt(c.timingpoints, timeMS), c.defaultSampleSet,
                                      skin.o_layered_hitsounds, false);
}

f32 panAt(vec2 rawPos) { return HitSoundUtils::playedPan(GameRules::osuCoords2Pan(rawPos.x)); }
}  // namespace

Playback::~Playback() { this->stop(); }

void Playback::update(const Clock &clock, const MapObjects &objects, const Document &document) {
    const Skin *skin = osu->getSkin();
    const i32 from = clock.getPreviousTime();
    const i32 to = clock.getTime();
    if(!skin || !clock.isPlaying() || clock.isSeekFrame() || to < from || to - from > MAX_FRAME_MS) {
        this->stop();
        return;
    }

    const MapObjects::Sounds &sounds = objects.getSounds();
    const auto &cues = sounds.cues;
    for(const Cue &cue : crossed(cues, from, to)) {
        const HitSoundUtils::HitSoundContext context = contextAt(document, *skin, cue.timeMS);
        switch(cue.kind) {
            case Cue::Kind::HIT:
                HitSoundUtils::play(*skin, HitSoundUtils::resolve(cue.samples, context, false), panAt(cue.rawPos), 0.f);
                break;
            case Cue::Kind::TICK:
                HitSoundUtils::playSliderTick(*skin, HitSoundUtils::resolveSliderTick(cue.samples, context),
                                              panAt(cue.rawPos));
                break;
            case Cue::Kind::SLIDE:
                break;
        }
    }

    // a slide plays while the time is inside its slider (from wherever the time came)
    bool inSlide = false;
    for(auto it = Mc::ranges::lower_bound(cues, to - sounds.longestSlideMS, {}, &Cue::timeMS);
        it != cues.end() && it->timeMS <= to; ++it) {
        const Cue &cue = *it;
        if(cue.kind != Cue::Kind::SLIDE || cue.endTimeMS <= to) continue;
        HitSoundUtils::play(*skin, HitSoundUtils::resolve(cue.samples, contextAt(document, *skin, to), true),
                            panAt(cue.rawPos), 0.f);
        inSlide = true;
    }
    if(!inSlide) this->stop();
    this->sliding = inSlide;
}

std::span<const Cue> Playback::crossed(std::span<const Cue> cues, i32 from, i32 to) {
    const auto first = Mc::ranges::upper_bound(cues, from, {}, &Cue::timeMS);
    const auto last =
        std::upper_bound(first, cues.end(), to, [](i32 time, const Cue &cue) { return time < cue.timeMS; });
    return {first, last};
}

void Playback::stop() {
    if(!this->sliding) return;
    this->sliding = false;
    // every slider sound
    if(const Skin *skin = osu->getSkin()) HitSoundUtils::stopSliderSounds(*skin, {});
}
