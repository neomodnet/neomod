// Copyright (c) 2026, WH, All rights reserved.
#include "HitSoundTest.h"

#include "TestMacros.h"
#include "BeatmapFile/BeatmapPrimitives.h"
#include "Engine.h"
#include "HitObjects.h"
#include "HitSounds.h"
#include "OsuConVars.h"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace Mc::Tests {
using namespace neomod;
using namespace neomod::DatabaseBeatmapTypes;
using namespace neomod::HitSoundUtils;

// see https://github.com/ppy/osu/blob/69c27478832d873d8c376c017708784c6653e79c/osu.Game.Tests/Gameplay/TestSceneHitObjectSamples.cs
// for test reference

// helper for readable test names
static const char *setName(i32 idx) {
    switch(idx) {
        case 0:
            return "normal";
        case 1:
            return "soft";
        case 2:
            return "drum";
        default:
            return "?";
    }
}

static const char *hitName(i32 idx) {
    switch(idx) {
        case 0:
            return "hitnormal";
        case 1:
            return "hitwhistle";
        case 2:
            return "hitfinish";
        case 3:
            return "hitclap";
        default:
            return "?";
    }
}

// default context: normal set, 100% volume, layered hitsounds on, no overrides
static HitSoundContext defaultCtx() {
    return {
        .timingPointSampleSet = SampleSetType::NORMAL,
        .timingPointVolume = 100,
        .defaultSampleSet = SampleSetType::NORMAL,
        .forcedSampleSet = 0,
        .layeredHitSounds = true,
        .ignoreSampleVolume = false,
        .boostVolume = false,
    };
}

HitSoundTest::HitSoundTest() { logRaw("HitSoundTest created"); }

void HitSoundTest::update() {
    if(!m_ran) {
        m_ran = true;
        runTests();

        TEST_PRINT_RESULTS("HitSoundTest");

        engine->shutdown();
    }
}

void HitSoundTest::runTests() {
    // -------------------------------------------------------
    // getNormalSet tests
    // -------------------------------------------------------
    TEST_SECTION("getNormalSet");
    {
        // hitobject normalSet wins over timing point
        HITSAMPLE_BITS s{};
        s.normalSet = SampleSetType::DRUM;
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::SOFT;
        TEST_ASSERT_EQ(getNormalSet(s, ctx), (i32)SampleSetType::DRUM, "hitobject normalSet overrides timing point");
    }
    {
        // timing point wins when hitobject normalSet is 0
        HITSAMPLE_BITS s{};
        s.normalSet = 0;
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::SOFT;
        TEST_ASSERT_EQ(getNormalSet(s, ctx), (i32)SampleSetType::SOFT,
                       "timing point sampleSet used when hitobject is 0");
    }
    {
        // default wins when both hitobject and timing point are 0
        HITSAMPLE_BITS s{};
        s.normalSet = 0;
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = 0;
        ctx.defaultSampleSet = SampleSetType::DRUM;
        TEST_ASSERT_EQ(getNormalSet(s, ctx), (i32)SampleSetType::DRUM, "default sampleSet used as final fallback");
    }
    {
        // forced sample set overrides everything
        HITSAMPLE_BITS s{};
        s.normalSet = SampleSetType::SOFT;
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::DRUM;
        ctx.forcedSampleSet = SampleSetType::NORMAL;
        TEST_ASSERT_EQ(getNormalSet(s, ctx), (i32)SampleSetType::NORMAL,
                       "forced sample set overrides hitobject and timing point");
    }

    // -------------------------------------------------------
    // getAdditionSet tests
    // -------------------------------------------------------
    TEST_SECTION("getAdditionSet");
    {
        // hitobject additionSet used directly
        HITSAMPLE_BITS s{};
        s.normalSet = SampleSetType::NORMAL;
        s.additionSet = SampleSetType::DRUM;
        auto ctx = defaultCtx();
        TEST_ASSERT_EQ(getAdditionSet(s, ctx), (i32)SampleSetType::DRUM, "hitobject additionSet used directly");
    }
    {
        // falls back to normalSet when additionSet is 0
        HITSAMPLE_BITS s{};
        s.normalSet = SampleSetType::SOFT;
        s.additionSet = 0;
        auto ctx = defaultCtx();
        TEST_ASSERT_EQ(getAdditionSet(s, ctx), (i32)SampleSetType::SOFT, "additionSet falls back to normalSet");
    }
    {
        // falls through the full chain: additionSet=0 -> normalSet=0 -> timing point
        HITSAMPLE_BITS s{};
        s.normalSet = 0;
        s.additionSet = 0;
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::DRUM;
        TEST_ASSERT_EQ(getAdditionSet(s, ctx), (i32)SampleSetType::DRUM,
                       "additionSet falls back through normalSet to timing point");
    }
    {
        // forced sample set overrides additionSet too
        HITSAMPLE_BITS s{};
        s.additionSet = SampleSetType::SOFT;
        auto ctx = defaultCtx();
        ctx.forcedSampleSet = SampleSetType::DRUM;
        TEST_ASSERT_EQ(getAdditionSet(s, ctx), (i32)SampleSetType::DRUM, "forced sample set overrides additionSet");
    }

    // -------------------------------------------------------
    // getVolume tests
    // -------------------------------------------------------
    TEST_SECTION("getVolume");
    {
        // hitobject volume overrides timing point volume
        HITSAMPLE_BITS s{};
        s.volume = 50;
        auto ctx = defaultCtx();
        ctx.timingPointVolume = 80;
        f32 vol = getVolume(s, ctx, HitSoundType::NORMAL, false);
        // 0.8 (NORMAL modifier) * 50/100 = 0.4
        TEST_ASSERT_NEAR(vol, 0.4f, 0.001f, "hitobject volume=50 with NORMAL modifier -> 0.4");
    }
    {
        // timing point volume used when hitobject volume is 0
        HITSAMPLE_BITS s{};
        s.volume = 0;
        auto ctx = defaultCtx();
        ctx.timingPointVolume = 80;
        f32 vol = getVolume(s, ctx, HitSoundType::NORMAL, false);
        // 0.8 * 80/100 = 0.64
        TEST_ASSERT_NEAR(vol, 0.64f, 0.001f, "timing point volume=80 with NORMAL modifier -> 0.64");
    }
    {
        // hitcircle sound type modifiers
        HITSAMPLE_BITS s{};
        s.volume = 100;
        auto ctx = defaultCtx();
        TEST_ASSERT_NEAR(getVolume(s, ctx, HitSoundType::NORMAL, false), 0.8f, 0.001f, "NORMAL volume modifier is 0.8");
        TEST_ASSERT_NEAR(getVolume(s, ctx, HitSoundType::WHISTLE, false), 0.85f, 0.001f,
                         "WHISTLE volume modifier is 0.85");
        TEST_ASSERT_NEAR(getVolume(s, ctx, HitSoundType::FINISH, false), 1.0f, 0.001f, "FINISH volume modifier is 1.0");
        TEST_ASSERT_NEAR(getVolume(s, ctx, HitSoundType::CLAP, false), 0.85f, 0.001f, "CLAP volume modifier is 0.85");
    }
    {
        // slider sounds have no hitcircle modifier
        HITSAMPLE_BITS s{};
        s.volume = 100;
        auto ctx = defaultCtx();
        TEST_ASSERT_NEAR(getVolume(s, ctx, HitSoundType::NORMAL, true), 1.0f, 0.001f,
                         "slider NORMAL has no volume modifier");
        TEST_ASSERT_NEAR(getVolume(s, ctx, HitSoundType::WHISTLE, true), 1.0f, 0.001f,
                         "slider WHISTLE has no volume modifier");
    }
    {
        // ignore_beatmap_sample_volume skips all volume scaling from map
        HITSAMPLE_BITS s{};
        s.volume = 50;
        auto ctx = defaultCtx();
        ctx.ignoreSampleVolume = true;
        f32 vol = getVolume(s, ctx, HitSoundType::NORMAL, false);
        // 0.8 (NORMAL modifier) * 1.0 (no sample volume applied) = 0.8
        TEST_ASSERT_NEAR(vol, 0.8f, 0.001f, "ignoreSampleVolume skips hitobject and timing point volume");
    }
    {
        // volume boost applies logarithmic curve to non-slider sounds
        HITSAMPLE_BITS s{};
        s.volume = 100;
        auto ctx = defaultCtx();
        ctx.boostVolume = true;
        f32 vol = getVolume(s, ctx, HitSoundType::NORMAL, false);
        // 0.8 boosted: (log(0.8 + 1/e) + 1) * 0.761463
        TEST_ASSERT(vol > 0.8f, "boost increases volume for NORMAL (was 0.8)");
        TEST_ASSERT(vol <= 1.0f, "boosted volume does not exceed 1.0");
    }
    {
        // volume boost does not apply to slider sounds
        HITSAMPLE_BITS s{};
        s.volume = 50;
        auto ctx = defaultCtx();
        ctx.boostVolume = true;
        f32 vol = getVolume(s, ctx, HitSoundType::NORMAL, true);
        // slider: no hitcircle modifier, volume=50/100 = 0.5, no boost
        TEST_ASSERT_NEAR(vol, 0.5f, 0.001f, "boost does not apply to slider sounds");
    }

    // -------------------------------------------------------
    // resolve() tests -- which sounds get resolved
    // -------------------------------------------------------
    TEST_SECTION("resolve");
    {
        // hitSounds=0 -> plays hitnormal only
        HITSAMPLE_BITS s{};
        s.hitSounds = 0;
        auto ctx = defaultCtx();
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 1, "hitSounds=0 resolves to 1 sound");
        if(!r.empty()) {
            TEST_ASSERT_EQ(r[0].hit, 0, "hitSounds=0 plays hitnormal");
            logRaw("    -> {}-{}", setName(r[0].set), hitName(r[0].hit));
        }
    }
    {
        // single hitsound: just WHISTLE
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::WHISTLE;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 1, "WHISTLE only (no layered) -> 1 sound");
        if(!r.empty()) {
            TEST_ASSERT_EQ(r[0].hit, 1, "only WHISTLE plays");
            logRaw("    -> {}-{}", setName(r[0].set), hitName(r[0].hit));
        }
    }
    {
        // layered hitsounds: WHISTLE + forced hitnormal
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::WHISTLE;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = true;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 2, "WHISTLE with layered -> 2 sounds (hitnormal + hitwhistle)");
        if(r.size() == 2) {
            TEST_ASSERT_EQ(r[0].hit, 0, "first is hitnormal");
            TEST_ASSERT_EQ(r[1].hit, 1, "second is hitwhistle");
            logRaw("    -> {}-{}, {}-{}", setName(r[0].set), hitName(r[0].hit), setName(r[1].set), hitName(r[1].hit));
        }
    }
    {
        // layered disabled, hitSounds=0 -> still plays hitnormal (special case)
        HITSAMPLE_BITS s{};
        s.hitSounds = 0;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 1, "hitSounds=0 always plays hitnormal even without layered");
        if(!r.empty()) {
            TEST_ASSERT_EQ(r[0].hit, 0, "plays hitnormal");
        }
    }
    {
        // multiple hitsounds: WHISTLE | CLAP with layered
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::WHISTLE | HitSoundType::CLAP;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = true;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 3, "WHISTLE|CLAP with layered -> 3 sounds");
        if(r.size() == 3) {
            TEST_ASSERT_EQ(r[0].hit, 0, "first is hitnormal (layered)");
            TEST_ASSERT_EQ(r[1].hit, 1, "second is hitwhistle");
            TEST_ASSERT_EQ(r[2].hit, 3, "third is hitclap");
            logRaw("    -> {}-{}, {}-{}, {}-{}", setName(r[0].set), hitName(r[0].hit), setName(r[1].set),
                   hitName(r[1].hit), setName(r[2].set), hitName(r[2].hit));
        }
    }
    {
        // all four hitsounds
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::NORMAL | HitSoundType::WHISTLE | HitSoundType::FINISH | HitSoundType::CLAP;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 4, "all 4 hitsounds -> 4 sounds");
        if(r.size() == 4) {
            TEST_ASSERT_EQ(r[0].hit, 0, "hitnormal");
            TEST_ASSERT_EQ(r[1].hit, 1, "hitwhistle");
            TEST_ASSERT_EQ(r[2].hit, 2, "hitfinish");
            TEST_ASSERT_EQ(r[3].hit, 3, "hitclap");
        }
    }

    // -------------------------------------------------------
    // resolve() -- sample set routing
    // -------------------------------------------------------
    TEST_SECTION("resolve sample set routing");
    {
        // normal sound uses normalSet, addition sounds use additionSet
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::NORMAL | HitSoundType::WHISTLE;
        s.normalSet = SampleSetType::DRUM;
        s.additionSet = SampleSetType::SOFT;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 2, "NORMAL+WHISTLE -> 2 sounds");
        if(r.size() == 2) {
            TEST_ASSERT_EQ(r[0].set, 2, "hitnormal uses normalSet=DRUM (idx 2)");
            TEST_ASSERT_EQ(r[1].set, 1, "hitwhistle uses additionSet=SOFT (idx 1)");
            logRaw("    -> {}-{}, {}-{}", setName(r[0].set), hitName(r[0].hit), setName(r[1].set), hitName(r[1].hit));
        }
    }
    {
        // with layered: hitnormal uses normalSet, addition uses additionSet
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::CLAP;
        s.normalSet = SampleSetType::SOFT;
        s.additionSet = SampleSetType::DRUM;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = true;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 2, "CLAP with layered -> 2 sounds");
        if(r.size() == 2) {
            TEST_ASSERT_EQ(r[0].set, 1, "layered hitnormal uses normalSet=SOFT (idx 1)");
            TEST_ASSERT_EQ(r[1].set, 2, "hitclap uses additionSet=DRUM (idx 2)");
        }
    }

    // -------------------------------------------------------
    // resolve() -- slider sounds
    // -------------------------------------------------------
    TEST_SECTION("resolve slider sounds");
    {
        // slider sounds use slider index
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::NORMAL;
        auto ctx = defaultCtx();
        auto r = resolve(s, ctx, true);
        TEST_ASSERT_EQ((int)r.size(), 1, "slider NORMAL -> 1 sound");
        if(!r.empty()) {
            TEST_ASSERT_EQ(r[0].slider, 1, "slider sound uses slider index");
            TEST_ASSERT_EQ(r[0].hit, 0, "slider hitnormal");
            logRaw("    -> {}-slider{}", setName(r[0].set), hitName(r[0].hit));
        }
    }
    {
        // slider FINISH and CLAP are filtered out (SOUND_METHODS has nullptr for those)
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::NORMAL | HitSoundType::WHISTLE | HitSoundType::FINISH | HitSoundType::CLAP;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = resolve(s, ctx, true);
        TEST_ASSERT_EQ((int)r.size(), 2, "slider: FINISH and CLAP filtered out -> 2 sounds");
        if(r.size() == 2) {
            TEST_ASSERT_EQ(r[0].hit, 0, "slider hitnormal");
            TEST_ASSERT_EQ(r[1].hit, 1, "slider hitwhistle");
        }
    }

    // -------------------------------------------------------
    // resolve() -- zero volume is skipped
    // -------------------------------------------------------
    TEST_SECTION("resolve zero volume");
    {
        // timing point volume=0 with hitobject volume=0 -> 0 volume -> skipped
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::NORMAL;
        s.volume = 0;
        auto ctx = defaultCtx();
        ctx.timingPointVolume = 0;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 0, "zero volume from timing point -> sound skipped");
    }

    // -------------------------------------------------------
    // resolve() -- forced sample set
    // -------------------------------------------------------
    TEST_SECTION("resolve forced sample set");
    {
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::NORMAL | HitSoundType::WHISTLE;
        s.normalSet = SampleSetType::SOFT;
        s.additionSet = SampleSetType::DRUM;
        auto ctx = defaultCtx();
        ctx.forcedSampleSet = SampleSetType::NORMAL;
        ctx.layeredHitSounds = false;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 2, "forced set -> 2 sounds");
        if(r.size() == 2) {
            TEST_ASSERT_EQ(r[0].set, 0, "forced: hitnormal uses NORMAL (idx 0)");
            TEST_ASSERT_EQ(r[1].set, 0, "forced: hitwhistle uses NORMAL (idx 0)");
            logRaw("    -> {}-{}, {}-{}", setName(r[0].set), hitName(r[0].hit), setName(r[1].set), hitName(r[1].hit));
        }
    }

    // -------------------------------------------------------
    // resolve() -- volume values in resolved sounds
    // -------------------------------------------------------
    TEST_SECTION("resolve volume values");
    {
        HITSAMPLE_BITS s{};
        s.hitSounds = HitSoundType::NORMAL | HitSoundType::FINISH;
        s.volume = 80;
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = resolve(s, ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 2, "NORMAL+FINISH -> 2 sounds");
        if(r.size() == 2) {
            // NORMAL: 0.8 * 80/100 = 0.64
            TEST_ASSERT_NEAR(r[0].volume, 0.64f, 0.001f, "hitnormal volume = 0.8 * 80/100");
            // FINISH: 1.0 * 80/100 = 0.80
            TEST_ASSERT_NEAR(r[1].volume, 0.80f, 0.001f, "hitfinish volume = 1.0 * 80/100");
        }
    }

    // -------------------------------------------------------
    // resolveSliderTick tests
    // -------------------------------------------------------
    TEST_SECTION("resolveSliderTick");
    {
        // slider ticks use the normal sample set, not the addition set (per osu! reference)
        HITSAMPLE_BITS s{};
        s.normalSet = SampleSetType::DRUM;
        s.additionSet = SampleSetType::SOFT;
        auto ctx = defaultCtx();
        auto tick = resolveSliderTick(s, ctx);
        TEST_ASSERT_EQ(tick.set, 2, "slider tick uses normalSet=DRUM (idx 2), not additionSet");
    }
    {
        // slider tick falls back through normalSet chain: hitobject=0 -> timing point
        HITSAMPLE_BITS s{};
        s.normalSet = 0;
        s.additionSet = SampleSetType::DRUM;
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::SOFT;
        auto tick = resolveSliderTick(s, ctx);
        TEST_ASSERT_EQ(tick.set, 1, "slider tick falls back to timing point SOFT (idx 1)");
    }
    {
        // slider tick falls back to default sample set
        HITSAMPLE_BITS s{};
        s.normalSet = 0;
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = 0;
        ctx.defaultSampleSet = SampleSetType::DRUM;
        auto tick = resolveSliderTick(s, ctx);
        TEST_ASSERT_EQ(tick.set, 2, "slider tick falls back to default DRUM (idx 2)");
    }
    {
        // forced sample set overrides slider tick set
        HITSAMPLE_BITS s{};
        s.normalSet = SampleSetType::SOFT;
        auto ctx = defaultCtx();
        ctx.forcedSampleSet = SampleSetType::DRUM;
        auto tick = resolveSliderTick(s, ctx);
        TEST_ASSERT_EQ(tick.set, 2, "forced sample set overrides slider tick to DRUM (idx 2)");
    }
    {
        // slider tick volume from hitobject
        HITSAMPLE_BITS s{};
        s.volume = 60;
        auto ctx = defaultCtx();
        auto tick = resolveSliderTick(s, ctx);
        TEST_ASSERT_NEAR(tick.volume, 0.6f, 0.001f, "slider tick volume from hitobject = 60/100");
    }
    {
        // slider tick volume from timing point
        HITSAMPLE_BITS s{};
        s.volume = 0;
        auto ctx = defaultCtx();
        ctx.timingPointVolume = 40;
        auto tick = resolveSliderTick(s, ctx);
        TEST_ASSERT_NEAR(tick.volume, 0.4f, 0.001f, "slider tick volume from timing point = 40/100");
    }
    {
        // slider tick volume with ignoreSampleVolume
        HITSAMPLE_BITS s{};
        s.volume = 50;
        auto ctx = defaultCtx();
        ctx.ignoreSampleVolume = true;
        auto tick = resolveSliderTick(s, ctx);
        TEST_ASSERT_NEAR(tick.volume, 1.0f, 0.001f, "slider tick ignores sample volume -> 1.0");
    }

    // -------------------------------------------------------
    // the context: the samples at a time, then the rest
    // -------------------------------------------------------
    TEST_SECTION("samplesAt / makeContext");
    {
        // normal at 100% from 0, an inherited soft point at 60% from 2502
        const Primitives::TimingPoints timing{{
            {.offset = 0,
             .msPerBeat = 500,
             .sampleSet = 1,
             .sampleIndex = 0,
             .volume = 100,
             .uninherited = true,
             .kiai = false},
            {.offset = 2502,
             .msPerBeat = -100,
             .sampleSet = 2,
             .sampleIndex = 0,
             .volume = 60,
             .uninherited = false,
             .kiai = false},
        }};
        const i32 offset = cv::timingpoints_offset.getInt();
        TEST_ASSERT_EQ(samplesAt(timing, 2502 - offset - 1).sampleSet, (i32)SampleSetType::NORMAL,
                       "a hitsound more than timingpoints_offset before a point keeps the previous samples");
        TEST_ASSERT_EQ(samplesAt(timing, 2502 - offset).sampleSet, (i32)SampleSetType::SOFT,
                       "a hitsound timingpoints_offset before a point takes its samples");
        TEST_ASSERT_EQ(samplesAt(timing, 2502 - offset).volume, 60, "and its volume");

        const HitSoundContext ctx = makeContext(samplesAt(timing, 3000), SampleSetType::DRUM, true);
        TEST_ASSERT_EQ(ctx.timingPointSampleSet, (i32)SampleSetType::SOFT, "context takes the samples' set");
        TEST_ASSERT_EQ(ctx.timingPointVolume, 60, "context takes the samples' volume");
        TEST_ASSERT_EQ((i32)ctx.defaultSampleSet, (i32)SampleSetType::DRUM, "context takes the map's default set");
        TEST_ASSERT(ctx.layeredHitSounds, "context takes the skin's layering");
        TEST_ASSERT_EQ((i32)ctx.forcedSampleSet, cv::skin_force_hitsound_sample_set.getInt(),
                       "context takes the forced sample set convar");
    }

    // -------------------------------------------------------
    // the sounds a perfect play makes, per object
    // -------------------------------------------------------
    TEST_SECTION("addSoundCues");
    {
        // 500 ms beats at slider velocity 1.4 and tick rate 1: 280 px is a two-beat span with a tick in its middle
        constexpr std::string_view map =
            "osu file format v14\n\n[Difficulty]\nSliderMultiplier:1.4\nSliderTickRate:1\n\n[TimingPoints]\n"
            "0,500,4,2,0,100,1,0\n\n[HitObjects]\n"
            "100,100,1000,1,2,0:0:0:0:\n"                                         // a circle with a whistle
            "64,192,2000,2,0,L|344:192,3,280,2|8|4|0,0:0|0:0|0:0|0:0,0:0:0:0:\n"  // edges: whistle, clap, finish, -
            "256,192,6000,12,4,7000,0:0:0:0:\n"                                   // a spinner with a finish
            "64,192,8000,2,0,L|344:192,4,280,8|4,0:0|0:0,0:0:0:0:\n";             // three repeats, two edge sounds
        auto c = Primitives::loadPrimitiveObjectsFromData(
            std::span{reinterpret_cast<const u8 *>(map.data()), map.size()}, {});
        TEST_ASSERT(!c.error, "the map loads");
        const auto objects = HitObjects::create(c, nullptr, nullptr);
        TEST_ASSERT_EQ(objects.size(), (uSz)4, "four objects");
        if(objects.size() == 4) {
            using Kind = HitObject::SoundCue::Kind;
            const auto cuesOf = [](const HitObject &obj) {
                std::vector<HitObject::SoundCue> cues;
                obj.addSoundCues(cues);
                return cues;
            };

            const auto circle = cuesOf(*objects[0]);
            TEST_ASSERT_EQ(circle.size(), (uSz)1, "a circle: one hit");
            if(circle.size() == 1) {
                TEST_ASSERT_EQ(circle[0].timeMS, 1000, "at its time");
                TEST_ASSERT(circle[0].kind == Kind::HIT, "a hit");
                TEST_ASSERT_EQ((i32)circle[0].samples.hitSounds, (i32)HitSoundType::WHISTLE, "with its whistle");
            }

            // head, slide, two repeats, a tick per span, end
            const auto slider = cuesOf(*objects[1]);
            TEST_ASSERT_EQ(slider.size(), (uSz)8, "a slider with two repeats and a tick per span: eight cues");
            const auto find = [](const std::vector<HitObject::SoundCue> &cues, i32 timeMS, Kind kind) {
                for(const auto &cue : cues) {
                    if(cue.timeMS == timeMS && cue.kind == kind) return &cue;
                }
                return static_cast<const HitObject::SoundCue *>(nullptr);
            };
            const auto hitSoundsAt = [&](const std::vector<HitObject::SoundCue> &cues, i32 timeMS) {
                const auto *cue = find(cues, timeMS, Kind::HIT);
                return cue ? (i32)cue->samples.hitSounds : -1;
            };
            TEST_ASSERT_EQ(hitSoundsAt(slider, 2000), (i32)HitSoundType::WHISTLE, "the head plays the first edge");
            TEST_ASSERT_EQ(hitSoundsAt(slider, 3000), (i32)HitSoundType::CLAP, "the first repeat the second");
            TEST_ASSERT_EQ(hitSoundsAt(slider, 4000), (i32)HitSoundType::FINISH, "the second repeat the third");
            TEST_ASSERT_EQ(hitSoundsAt(slider, 5000), 0, "the end the last");
            const auto *slide = find(slider, 2000, Kind::SLIDE);
            TEST_ASSERT(slide != nullptr, "the slide starts with the head");
            if(slide) TEST_ASSERT_EQ(slide->endTimeMS, 5000, "and ends with the slider");
            TEST_ASSERT(
                find(slider, 2500, Kind::TICK) && find(slider, 3500, Kind::TICK) && find(slider, 4500, Kind::TICK),
                "a tick in the middle of every span");
            if(const auto *end = find(slider, 5000, Kind::HIT)) {
                TEST_ASSERT_NEAR(end->rawPos.x, 344.f, 1.f, "three spans end at the far end, where the end is heard");
            }

            const auto spinner = cuesOf(*objects[2]);
            TEST_ASSERT_EQ(spinner.size(), (uSz)1, "a spinner: one hit");
            if(spinner.size() == 1) {
                TEST_ASSERT_EQ(spinner[0].timeMS, 7000, "at its end");
                TEST_ASSERT_EQ((i32)spinner[0].samples.hitSounds, (i32)HitSoundType::FINISH, "with its finish");
            }

            // three repeats with only a head and an end sample given: the repeats play the head's
            const auto fallback = cuesOf(*objects[3]);
            TEST_ASSERT_EQ(hitSoundsAt(fallback, 9000), (i32)HitSoundType::CLAP,
                           "more repeats than edge samples: a repeat plays the start's");
            TEST_ASSERT_EQ(hitSoundsAt(fallback, 11000), (i32)HitSoundType::CLAP, "every repeat");
            TEST_ASSERT_EQ(hitSoundsAt(fallback, 12000), (i32)HitSoundType::FINISH, "the end its own");
        }
    }
    {
        // edges a slider doesn't list play the object's hitsounds and sample sets (osu!stable's unified sliders,
        // which it writes without the edge fields; lazer reads them the same way)
        constexpr std::string_view map =
            "osu file format v14\n\n[Difficulty]\nSliderMultiplier:1.4\nSliderTickRate:1\n\n[TimingPoints]\n"
            "0,500,4,2,0,100,1,0\n\n[HitObjects]\n"
            "64,192,1000,2,2,L|344:192,2,280\n"                 // a whistle, nothing after the length
            "64,192,4000,2,8,L|344:192,1,280,,,2:3:0:0:\n"      // a clap and sets, but no edge sounds or sets
            "64,192,6000,2,4,L|344:192,1,280,2|0,,3:2:0:0:\n";  // edge sounds without edge sets
        auto c = Primitives::loadPrimitiveObjectsFromData(
            std::span{reinterpret_cast<const u8 *>(map.data()), map.size()}, {});
        TEST_ASSERT(!c.error, "the map with unified sliders loads");
        const auto objects = HitObjects::create(c, nullptr, nullptr);
        TEST_ASSERT_EQ(objects.size(), (uSz)3, "three sliders");
        if(objects.size() == 3) {
            const auto hitAt = [&](uSz index, i32 timeMS) {
                std::vector<HitObject::SoundCue> cues;
                objects[index]->addSoundCues(cues);
                for(const auto &cue : cues) {
                    if(cue.timeMS == timeMS && cue.kind == HitObject::SoundCue::Kind::HIT) return cue.samples;
                }
                return HITSAMPLE_BITS{.hitSounds = 0xff, .normalSet = 0xff, .additionSet = 0xff, .volume = 0};
            };
            TEST_ASSERT_EQ((i32)hitAt(0, 1000).hitSounds, (i32)HitSoundType::WHISTLE, "a unified slider's head");
            TEST_ASSERT_EQ((i32)hitAt(0, 2000).hitSounds, (i32)HitSoundType::WHISTLE, "its repeat");
            TEST_ASSERT_EQ((i32)hitAt(0, 3000).hitSounds, (i32)HitSoundType::WHISTLE, "and its end play its whistle");
            TEST_ASSERT_EQ((i32)hitAt(1, 4000).hitSounds, (i32)HitSoundType::CLAP, "empty edge lists: the object's");
            TEST_ASSERT_EQ((i32)hitAt(1, 5000).normalSet, (i32)SampleSetType::SOFT, "with its normal set");
            TEST_ASSERT_EQ((i32)hitAt(1, 5000).additionSet, (i32)SampleSetType::DRUM, "and its addition set");
            TEST_ASSERT_EQ((i32)hitAt(2, 6000).hitSounds, (i32)HitSoundType::WHISTLE, "listed edge sounds stay");
            TEST_ASSERT_EQ((i32)hitAt(2, 7000).hitSounds, 0, "every one of them");
            TEST_ASSERT_EQ((i32)hitAt(2, 7000).normalSet, (i32)SampleSetType::DRUM,
                           "edges without sets take the object's normal set");
            TEST_ASSERT_EQ((i32)hitAt(2, 6000).additionSet, (i32)SampleSetType::SOFT, "and its addition set");
        }
    }
}

}  // namespace Mc::Tests
