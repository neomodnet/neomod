// Copyright (c) 2026, WH, All rights reserved.
#include "HitSoundTest.h"

#include "NeomodTestMacros.h"
#include "Engine.h"
#include "HitSounds.h"

namespace Mc::Tests {

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
        .layeredHitSounds = true,
        .forcedSampleSet = 0,
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
        HitSamples s{.normalSet = SampleSetType::DRUM};
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::SOFT;
        TEST_ASSERT_EQ(s.getNormalSet(ctx), (i32)SampleSetType::DRUM, "hitobject normalSet overrides timing point");
    }
    {
        // timing point wins when hitobject normalSet is 0
        HitSamples s{.normalSet = 0};
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::SOFT;
        TEST_ASSERT_EQ(s.getNormalSet(ctx), (i32)SampleSetType::SOFT,
                       "timing point sampleSet used when hitobject is 0");
    }
    {
        // default wins when both hitobject and timing point are 0
        HitSamples s{.normalSet = 0};
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = 0;
        ctx.defaultSampleSet = SampleSetType::DRUM;
        TEST_ASSERT_EQ(s.getNormalSet(ctx), (i32)SampleSetType::DRUM, "default sampleSet used as final fallback");
    }
    {
        // forced sample set overrides everything
        HitSamples s{.normalSet = SampleSetType::SOFT};
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::DRUM;
        ctx.forcedSampleSet = SampleSetType::NORMAL;
        TEST_ASSERT_EQ(s.getNormalSet(ctx), (i32)SampleSetType::NORMAL,
                       "forced sample set overrides hitobject and timing point");
    }

    // -------------------------------------------------------
    // getAdditionSet tests
    // -------------------------------------------------------
    TEST_SECTION("getAdditionSet");
    {
        // hitobject additionSet used directly
        HitSamples s{.normalSet = SampleSetType::NORMAL, .additionSet = SampleSetType::DRUM};
        auto ctx = defaultCtx();
        TEST_ASSERT_EQ(s.getAdditionSet(ctx), (i32)SampleSetType::DRUM, "hitobject additionSet used directly");
    }
    {
        // falls back to normalSet when additionSet is 0
        HitSamples s{.normalSet = SampleSetType::SOFT, .additionSet = 0};
        auto ctx = defaultCtx();
        TEST_ASSERT_EQ(s.getAdditionSet(ctx), (i32)SampleSetType::SOFT, "additionSet falls back to normalSet");
    }
    {
        // falls through the full chain: additionSet=0 -> normalSet=0 -> timing point
        HitSamples s{.normalSet = 0, .additionSet = 0};
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::DRUM;
        TEST_ASSERT_EQ(s.getAdditionSet(ctx), (i32)SampleSetType::DRUM,
                       "additionSet falls back through normalSet to timing point");
    }
    {
        // forced sample set overrides additionSet too
        HitSamples s{.additionSet = SampleSetType::SOFT};
        auto ctx = defaultCtx();
        ctx.forcedSampleSet = SampleSetType::DRUM;
        TEST_ASSERT_EQ(s.getAdditionSet(ctx), (i32)SampleSetType::DRUM, "forced sample set overrides additionSet");
    }

    // -------------------------------------------------------
    // getVolume tests
    // -------------------------------------------------------
    TEST_SECTION("getVolume");
    {
        // hitobject volume overrides timing point volume
        HitSamples s{.volume = 50};
        auto ctx = defaultCtx();
        ctx.timingPointVolume = 80;
        f32 vol = s.getVolume(ctx, HitSoundType::NORMAL, false);
        // 0.8 (NORMAL modifier) * 50/100 = 0.4
        TEST_ASSERT_NEAR(vol, 0.4f, 0.001f, "hitobject volume=50 with NORMAL modifier -> 0.4");
    }
    {
        // timing point volume used when hitobject volume is 0
        HitSamples s{.volume = 0};
        auto ctx = defaultCtx();
        ctx.timingPointVolume = 80;
        f32 vol = s.getVolume(ctx, HitSoundType::NORMAL, false);
        // 0.8 * 80/100 = 0.64
        TEST_ASSERT_NEAR(vol, 0.64f, 0.001f, "timing point volume=80 with NORMAL modifier -> 0.64");
    }
    {
        // hitcircle sound type modifiers
        HitSamples s{.volume = 100};
        auto ctx = defaultCtx();
        TEST_ASSERT_NEAR(s.getVolume(ctx, HitSoundType::NORMAL, false), 0.8f, 0.001f, "NORMAL volume modifier is 0.8");
        TEST_ASSERT_NEAR(s.getVolume(ctx, HitSoundType::WHISTLE, false), 0.85f, 0.001f,
                         "WHISTLE volume modifier is 0.85");
        TEST_ASSERT_NEAR(s.getVolume(ctx, HitSoundType::FINISH, false), 1.0f, 0.001f, "FINISH volume modifier is 1.0");
        TEST_ASSERT_NEAR(s.getVolume(ctx, HitSoundType::CLAP, false), 0.85f, 0.001f, "CLAP volume modifier is 0.85");
    }
    {
        // slider sounds have no hitcircle modifier
        HitSamples s{.volume = 100};
        auto ctx = defaultCtx();
        TEST_ASSERT_NEAR(s.getVolume(ctx, HitSoundType::NORMAL, true), 1.0f, 0.001f,
                         "slider NORMAL has no volume modifier");
        TEST_ASSERT_NEAR(s.getVolume(ctx, HitSoundType::WHISTLE, true), 1.0f, 0.001f,
                         "slider WHISTLE has no volume modifier");
    }
    {
        // ignore_beatmap_sample_volume skips all volume scaling from map
        HitSamples s{.volume = 50};
        auto ctx = defaultCtx();
        ctx.ignoreSampleVolume = true;
        f32 vol = s.getVolume(ctx, HitSoundType::NORMAL, false);
        // 0.8 (NORMAL modifier) * 1.0 (no sample volume applied) = 0.8
        TEST_ASSERT_NEAR(vol, 0.8f, 0.001f, "ignoreSampleVolume skips hitobject and timing point volume");
    }
    {
        // volume boost applies logarithmic curve to non-slider sounds
        HitSamples s{.volume = 100};
        auto ctx = defaultCtx();
        ctx.boostVolume = true;
        f32 vol = s.getVolume(ctx, HitSoundType::NORMAL, false);
        // 0.8 boosted: (log(0.8 + 1/e) + 1) * 0.761463
        TEST_ASSERT(vol > 0.8f, "boost increases volume for NORMAL (was 0.8)");
        TEST_ASSERT(vol <= 1.0f, "boosted volume does not exceed 1.0");
    }
    {
        // volume boost does not apply to slider sounds
        HitSamples s{.volume = 50};
        auto ctx = defaultCtx();
        ctx.boostVolume = true;
        f32 vol = s.getVolume(ctx, HitSoundType::NORMAL, true);
        // slider: no hitcircle modifier, volume=50/100 = 0.5, no boost
        TEST_ASSERT_NEAR(vol, 0.5f, 0.001f, "boost does not apply to slider sounds");
    }

    // -------------------------------------------------------
    // resolve() tests -- which sounds get resolved
    // -------------------------------------------------------
    TEST_SECTION("resolve");
    {
        // hitSounds=0 -> plays hitnormal only
        HitSamples s{.hitSounds = 0};
        auto ctx = defaultCtx();
        auto r = s.resolve(ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 1, "hitSounds=0 resolves to 1 sound");
        if(!r.empty()) {
            TEST_ASSERT_EQ(r[0].hit, 0, "hitSounds=0 plays hitnormal");
            logRaw("    -> {}-{}", setName(r[0].set), hitName(r[0].hit));
        }
    }
    {
        // single hitsound: just WHISTLE
        HitSamples s{.hitSounds = HitSoundType::WHISTLE};
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = s.resolve(ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 1, "WHISTLE only (no layered) -> 1 sound");
        if(!r.empty()) {
            TEST_ASSERT_EQ(r[0].hit, 1, "only WHISTLE plays");
            logRaw("    -> {}-{}", setName(r[0].set), hitName(r[0].hit));
        }
    }
    {
        // layered hitsounds: WHISTLE + forced hitnormal
        HitSamples s{.hitSounds = HitSoundType::WHISTLE};
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = true;
        auto r = s.resolve(ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 2, "WHISTLE with layered -> 2 sounds (hitnormal + hitwhistle)");
        if(r.size() == 2) {
            TEST_ASSERT_EQ(r[0].hit, 0, "first is hitnormal");
            TEST_ASSERT_EQ(r[1].hit, 1, "second is hitwhistle");
            logRaw("    -> {}-{}, {}-{}", setName(r[0].set), hitName(r[0].hit), setName(r[1].set), hitName(r[1].hit));
        }
    }
    {
        // layered disabled, hitSounds=0 -> still plays hitnormal (special case)
        HitSamples s{.hitSounds = 0};
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = s.resolve(ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 1, "hitSounds=0 always plays hitnormal even without layered");
        if(!r.empty()) {
            TEST_ASSERT_EQ(r[0].hit, 0, "plays hitnormal");
        }
    }
    {
        // multiple hitsounds: WHISTLE | CLAP with layered
        HitSamples s{.hitSounds = HitSoundType::WHISTLE | HitSoundType::CLAP};
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = true;
        auto r = s.resolve(ctx, false);
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
        HitSamples s{.hitSounds =
                         HitSoundType::NORMAL | HitSoundType::WHISTLE | HitSoundType::FINISH | HitSoundType::CLAP};
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = s.resolve(ctx, false);
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
        HitSamples s{
            .hitSounds = HitSoundType::NORMAL | HitSoundType::WHISTLE,
            .normalSet = SampleSetType::DRUM,
            .additionSet = SampleSetType::SOFT,
        };
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = s.resolve(ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 2, "NORMAL+WHISTLE -> 2 sounds");
        if(r.size() == 2) {
            TEST_ASSERT_EQ(r[0].set, 2, "hitnormal uses normalSet=DRUM (idx 2)");
            TEST_ASSERT_EQ(r[1].set, 1, "hitwhistle uses additionSet=SOFT (idx 1)");
            logRaw("    -> {}-{}, {}-{}", setName(r[0].set), hitName(r[0].hit), setName(r[1].set), hitName(r[1].hit));
        }
    }
    {
        // with layered: hitnormal uses normalSet, addition uses additionSet
        HitSamples s{
            .hitSounds = HitSoundType::CLAP,
            .normalSet = SampleSetType::SOFT,
            .additionSet = SampleSetType::DRUM,
        };
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = true;
        auto r = s.resolve(ctx, false);
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
        HitSamples s{.hitSounds = HitSoundType::NORMAL};
        auto ctx = defaultCtx();
        auto r = s.resolve(ctx, true);
        TEST_ASSERT_EQ((int)r.size(), 1, "slider NORMAL -> 1 sound");
        if(!r.empty()) {
            TEST_ASSERT_EQ(r[0].slider, 1, "slider sound uses slider index");
            TEST_ASSERT_EQ(r[0].hit, 0, "slider hitnormal");
            logRaw("    -> {}-slider{}", setName(r[0].set), hitName(r[0].hit));
        }
    }
    {
        // slider FINISH and CLAP are filtered out (SOUND_METHODS has nullptr for those)
        HitSamples s{.hitSounds =
                         HitSoundType::NORMAL | HitSoundType::WHISTLE | HitSoundType::FINISH | HitSoundType::CLAP};
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = s.resolve(ctx, true);
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
        HitSamples s{.hitSounds = HitSoundType::NORMAL, .volume = 0};
        auto ctx = defaultCtx();
        ctx.timingPointVolume = 0;
        auto r = s.resolve(ctx, false);
        TEST_ASSERT_EQ((int)r.size(), 0, "zero volume from timing point -> sound skipped");
    }

    // -------------------------------------------------------
    // resolve() -- forced sample set
    // -------------------------------------------------------
    TEST_SECTION("resolve forced sample set");
    {
        HitSamples s{
            .hitSounds = HitSoundType::NORMAL | HitSoundType::WHISTLE,
            .normalSet = SampleSetType::SOFT,
            .additionSet = SampleSetType::DRUM,
        };
        auto ctx = defaultCtx();
        ctx.forcedSampleSet = SampleSetType::NORMAL;
        ctx.layeredHitSounds = false;
        auto r = s.resolve(ctx, false);
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
        HitSamples s{
            .hitSounds = HitSoundType::NORMAL | HitSoundType::FINISH,
            .volume = 80,
        };
        auto ctx = defaultCtx();
        ctx.layeredHitSounds = false;
        auto r = s.resolve(ctx, false);
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
        HitSamples s{
            .normalSet = SampleSetType::DRUM,
            .additionSet = SampleSetType::SOFT,
        };
        auto ctx = defaultCtx();
        auto tick = s.resolveSliderTick(ctx);
        TEST_ASSERT_EQ(tick.set, 2, "slider tick uses normalSet=DRUM (idx 2), not additionSet");
    }
    {
        // slider tick falls back through normalSet chain: hitobject=0 -> timing point
        HitSamples s{.normalSet = 0, .additionSet = SampleSetType::DRUM};
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = SampleSetType::SOFT;
        auto tick = s.resolveSliderTick(ctx);
        TEST_ASSERT_EQ(tick.set, 1, "slider tick falls back to timing point SOFT (idx 1)");
    }
    {
        // slider tick falls back to default sample set
        HitSamples s{.normalSet = 0};
        auto ctx = defaultCtx();
        ctx.timingPointSampleSet = 0;
        ctx.defaultSampleSet = SampleSetType::DRUM;
        auto tick = s.resolveSliderTick(ctx);
        TEST_ASSERT_EQ(tick.set, 2, "slider tick falls back to default DRUM (idx 2)");
    }
    {
        // forced sample set overrides slider tick set
        HitSamples s{.normalSet = SampleSetType::SOFT};
        auto ctx = defaultCtx();
        ctx.forcedSampleSet = SampleSetType::DRUM;
        auto tick = s.resolveSliderTick(ctx);
        TEST_ASSERT_EQ(tick.set, 2, "forced sample set overrides slider tick to DRUM (idx 2)");
    }
    {
        // slider tick volume from hitobject
        HitSamples s{.volume = 60};
        auto ctx = defaultCtx();
        auto tick = s.resolveSliderTick(ctx);
        TEST_ASSERT_NEAR(tick.volume, 0.6f, 0.001f, "slider tick volume from hitobject = 60/100");
    }
    {
        // slider tick volume from timing point
        HitSamples s{.volume = 0};
        auto ctx = defaultCtx();
        ctx.timingPointVolume = 40;
        auto tick = s.resolveSliderTick(ctx);
        TEST_ASSERT_NEAR(tick.volume, 0.4f, 0.001f, "slider tick volume from timing point = 40/100");
    }
    {
        // slider tick volume with ignoreSampleVolume
        HitSamples s{.volume = 50};
        auto ctx = defaultCtx();
        ctx.ignoreSampleVolume = true;
        auto tick = s.resolveSliderTick(ctx);
        TEST_ASSERT_NEAR(tick.volume, 1.0f, 0.001f, "slider tick ignores sample volume -> 1.0");
    }
}

}  // namespace Mc::Tests
