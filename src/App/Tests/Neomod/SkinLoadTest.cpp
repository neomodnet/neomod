// Copyright (c) 2026, WH, All rights reserved.
#include "SkinLoadTest.h"

#include "TestMacros.h"
#include "Engine.h"
#include "Resource.h"
#include "ResourceManager.h"

#include <map>
#include "Skin.h"
#include "SkinImage.h"
#include "Sound.h"

namespace Mc::Tests {

SkinLoadTest::SkinLoadTest() {
    logRaw("SkinLoadTest created");
    m_tier1_path = getTestArg("skin_tier1");
    m_tier2_path = getTestArg("skin_tier2");
    m_tier3_path = getTestArg("skin_tier3");
    if(m_tier1_path) logRaw("  skin_tier1: {}", *m_tier1_path);
    if(m_tier2_path) logRaw("  skin_tier2: {}", *m_tier2_path);
    if(m_tier3_path) logRaw("  skin_tier3: {}", *m_tier3_path);
    m_skin = std::make_unique<Skin>("default", MCENGINE_IMAGES_PATH "/default/");
}

void SkinLoadTest::update() {
    switch(m_phase) {
        case WAIT_DEFAULT:
            if(!m_skin->isReady()) return;
            m_phase = TEST_DEFAULT;
            [[fallthrough]];

        case TEST_DEFAULT:
            testDefaultSkin();
            m_skin.reset();
            m_skin = std::make_unique<Skin>("fake", "/tmp/neomod_test_nonexistent_skin_dir/");
            m_phase = WAIT_FAKE;
            return;

        case WAIT_FAKE:
            if(!m_skin->isReady()) return;
            m_phase = TEST_FAKE;
            [[fallthrough]];

        case TEST_FAKE:
            testFakeSkin();
            m_skin.reset();
            advanceToNextPhase();
            return;

        case WAIT_TIER1:
            if(!m_skin->isReady()) return;
            m_phase = TEST_TIER1;
            [[fallthrough]];

        case TEST_TIER1:
            testRealSkin("tier1", *m_tier1_path);
            m_skin.reset();
            advanceToNextPhase();
            return;

        case WAIT_TIER2:
            if(!m_skin->isReady()) return;
            m_phase = TEST_TIER2;
            [[fallthrough]];

        case TEST_TIER2:
            testRealSkin("tier2", *m_tier2_path);
            m_skin.reset();
            advanceToNextPhase();
            return;

        case WAIT_FALLBACK:
            if(!m_skin->isReady()) return;
            m_phase = TEST_FALLBACK;
            [[fallthrough]];

        case TEST_FALLBACK:
            testFallbackTier("fallback(t1+t2)", *m_tier1_path, *m_tier2_path);
            m_skin.reset();
            advanceToNextPhase();
            return;

        case WAIT_SWAP:
            if(!m_skin->isReady()) return;
            m_phase = TEST_SWAP;
            [[fallthrough]];

        case TEST_SWAP:
            testFallbackTier("swap(t2+t1)", *m_tier2_path, *m_tier1_path);
            m_skin.reset();
            advanceToNextPhase();
            return;

        case WAIT_SEQUENTIAL_A:
            if(!m_skin->isReady()) return;
            m_phase = TEST_SEQUENTIAL_A;
            [[fallthrough]];

        case TEST_SEQUENTIAL_A:
            testSequentialLoad();
            advanceToNextPhase();
            return;

        case WAIT_SEQUENTIAL_B:
            if(!m_skin->isReady()) return;
            m_phase = TEST_SEQUENTIAL_B;
            [[fallthrough]];

        case TEST_SEQUENTIAL_B:
            testSequentialLoad();
            m_skin.reset();
            advanceToNextPhase();
            return;

        case HOTSWAP_CREATE_A:
            // create skin A (tier1 primary, tier2 fallback)
            m_skin = std::make_unique<Skin>("tier1", *m_tier1_path + "/", *m_tier2_path + "/");
            m_phase = HOTSWAP_WAIT_A;
            return;

        case HOTSWAP_WAIT_A:
            if(!m_skin->isReady()) return;
            m_phase = HOTSWAP_CREATE_B;
            [[fallthrough]];

        case HOTSWAP_CREATE_B:
            // while skin A is still alive, create skin B (tier2 primary, tier1 fallback)
            m_skin_pending = std::make_unique<Skin>("tier2", *m_tier2_path + "/", *m_tier1_path + "/");
            m_phase = HOTSWAP_WAIT_B;
            return;

        case HOTSWAP_WAIT_B:
            if(!m_skin_pending->isReady()) return;
            m_phase = HOTSWAP_TEST;
            [[fallthrough]];

        case HOTSWAP_TEST:
            testHotSwap();
            m_skin.reset();
            m_skin_pending.reset();
            advanceToNextPhase();
            return;

        case RELOAD_LEAK_WARMUP:
            // first load: populates _DEFAULT caches etc.
            m_skin = std::make_unique<Skin>("tier1", *m_tier1_path + "/", *m_tier2_path + "/");
            m_phase = WAIT_RELOAD_LEAK_WARMUP;
            return;

        case WAIT_RELOAD_LEAK_WARMUP:
            if(!m_skin->isReady()) return;
            // destroy and reload once to warm up all caches
            m_skin.reset();
            m_skin = std::make_unique<Skin>("tier1", *m_tier1_path + "/", *m_tier2_path + "/");
            m_phase = WAIT_RELOAD_LEAK_BASELINE;
            return;

        case WAIT_RELOAD_LEAK_BASELINE:
            if(!m_skin->isReady()) return;
            // snapshot resources after warmup reload is fully loaded
            m_baseline_resource_count = resourceManager->getResources().size();
            m_baseline_paths.clear();
            for(auto *r : resourceManager->getResources()) m_baseline_paths.insert(r->getFilePath());
            m_reload_iteration = 0;
            // destroy and recreate again - this time any growth is a leak
            m_skin.reset();
            m_skin = std::make_unique<Skin>("tier1", *m_tier1_path + "/", *m_tier2_path + "/");
            m_phase = WAIT_RELOAD_LEAK_RELOAD;
            return;

        case WAIT_RELOAD_LEAK_RELOAD:
            if(!m_skin->isReady()) return;
            m_phase = TEST_RELOAD_LEAK;
            [[fallthrough]];

        case TEST_RELOAD_LEAK:
            m_reload_iteration++;
            testReloadLeak();
            if(m_reload_iteration < 3) {
                // do more reload cycles to detect cumulative leaks
                m_skin.reset();
                m_skin = std::make_unique<Skin>("tier1", *m_tier1_path + "/", *m_tier2_path + "/");
                m_phase = WAIT_RELOAD_LEAK_RELOAD;
            } else {
                m_skin.reset();
                advanceToNextPhase();
            }
            return;

        case DONE:
            return;
    }
}

void SkinLoadTest::advanceToNextPhase() {
    if(m_phase == TEST_FAKE && m_tier1_path) {
        m_skin = std::make_unique<Skin>("tier1", *m_tier1_path + "/");
        m_phase = WAIT_TIER1;
    } else if(m_phase == TEST_TIER1 && m_tier2_path) {
        m_skin = std::make_unique<Skin>("tier2", *m_tier2_path + "/");
        m_phase = WAIT_TIER2;
    } else if(m_phase == TEST_TIER2 && m_tier1_path && m_tier2_path) {
        // three-tier: tier1 primary, tier2 fallback, default last
        m_skin = std::make_unique<Skin>("tier1", *m_tier1_path + "/", *m_tier2_path + "/");
        m_phase = WAIT_FALLBACK;
    } else if(m_phase == TEST_FALLBACK) {
        // swap: tier2 primary, tier1 fallback, default last
        m_skin = std::make_unique<Skin>("tier2", *m_tier2_path + "/", *m_tier1_path + "/");
        m_phase = WAIT_SWAP;
    } else if(m_phase == TEST_SWAP) {
        // sequential test phase A: load tier1 alone first
        m_skin = std::make_unique<Skin>("tier1", *m_tier1_path + "/");
        m_phase = WAIT_SEQUENTIAL_A;
    } else if(m_phase == TEST_SEQUENTIAL_A) {
        // sequential test phase B: destroy tier1, load tier2 with tier1 as fallback
        m_skin.reset();
        m_skin = std::make_unique<Skin>("tier2", *m_tier2_path + "/", *m_tier1_path + "/");
        m_phase = WAIT_SEQUENTIAL_B;
    } else if(m_phase == TEST_SEQUENTIAL_B) {
        // hot-swap test: create skin A, then create skin B while A is alive
        m_phase = HOTSWAP_CREATE_A;
    } else if(m_phase == HOTSWAP_TEST && m_tier1_path && m_tier2_path) {
        // reload leak test
        m_phase = RELOAD_LEAK_WARMUP;
    } else {
        m_phase = DONE;
        finish();
    }
}

// --- helpers ---

bool SkinLoadTest::skinElementExists(const std::string &dir, const std::string &elementName) {
    // must use named strings so fileExists(std::string&) is called (case-insensitive)
    std::string path_2x = dir + elementName + "@2x.png";
    std::string path_1x = dir + elementName + ".png";
    return env->fileExists(path_2x) || env->fileExists(path_1x);
}

bool SkinLoadTest::soundElementExists(const std::string &dir, const std::string &elementName) {
    for(auto ext : {".wav", ".mp3", ".ogg", ".flac"}) {
        std::string path = dir + elementName + ext;
        if(env->fileExists(path)) return true;
    }
    return false;
}

std::string SkinLoadTest::expectedImageSource(const std::string &elementName) {
    for(const auto &dir : m_skin->search_dirs) {
        if(skinElementExists(dir, elementName)) return dir;
    }
    return {};
}

std::string SkinLoadTest::expectedSoundSource(const std::string &elementName) {
    for(const auto &dir : m_skin->search_dirs) {
        if(soundElementExists(dir, elementName)) return dir;
    }
    return {};
}

void SkinLoadTest::verifyImageSource(const Image *img, const std::string &elementName, const std::string &label) {
    std::string expected = expectedImageSource(elementName);
    if(expected.empty()) {
        TEST_ASSERT(img == MISSING_TEXTURE, label + " " + elementName + " should be missing");
        return;
    }
    TEST_ASSERT(img != MISSING_TEXTURE, label + " " + elementName + " should be loaded");
    if(img && img != MISSING_TEXTURE) {
        const auto &path = img->getFilePath();
        TEST_ASSERT(path.starts_with(expected),
                    label + " " + elementName + " from " + expected + " (got " + path + ")");
    }
}

void SkinLoadTest::verifySoundSource(const Sound *snd, const std::string &elementName, const std::string &label) {
    std::string expected = expectedSoundSource(elementName);
    if(expected.empty()) return;  // sounds may still have NULL ref on missing
    if(!snd) return;              // NULL sound = not loaded, can't check path
    const auto &path = snd->getFilePath();
    TEST_ASSERT(path.starts_with(expected), label + " " + elementName + " from " + expected + " (got " + path + ")");
}

// --- test phases ---

void SkinLoadTest::testDefaultSkin() {
    TEST_SECTION("default skin: search_dirs");
    {
        TEST_ASSERT_EQ((int)m_skin->search_dirs.size(), 1, "default skin has 1 search dir");
        TEST_ASSERT_EQ(m_skin->search_dirs[0], std::string(MCENGINE_IMAGES_PATH "/default/"),
                       "default skin search dir is the default path");
        TEST_ASSERT(m_skin->is_default, "default skin flag is set");
    }

    TEST_SECTION("default skin: core images");
    {
        TEST_ASSERT(m_skin->i_hitcircle.img != MISSING_TEXTURE, "hitcircle loaded");
        TEST_ASSERT(m_skin->i_approachcircle.img != MISSING_TEXTURE, "approachcircle loaded");
        TEST_ASSERT(m_skin->i_cursor.img != MISSING_TEXTURE, "cursor loaded");
        TEST_ASSERT(m_skin->i_star.img != MISSING_TEXTURE, "star loaded");
        TEST_ASSERT(m_skin->i_loading_spinner.img != MISSING_TEXTURE, "loading-spinner loaded");
        TEST_ASSERT(m_skin->resources.size() > 0, "resources vector is populated");
    }

    TEST_SECTION("default skin: core sounds");
    {
        TEST_ASSERT(m_skin->s_normal_hitnormal != nullptr, "normal-hitnormal loaded");
        TEST_ASSERT(m_skin->s_combobreak != nullptr, "combobreak loaded");
        TEST_ASSERT(m_skin->sounds.size() > 0, "sounds vector is populated");
    }

    TEST_SECTION("default skin: SkinImage elements");
    {
        TEST_ASSERT(m_skin->i_hitcircleoverlay != nullptr, "hitcircleoverlay SkinImage created");
        TEST_ASSERT(!m_skin->i_hitcircleoverlay->isMissingTexture(), "hitcircleoverlay not missing");
    }

    TEST_SECTION("default skin: parametrized update()");
    {
        m_skin->update(false, false, 0);
        m_skin->update(true, true, 1000);
        m_skin->update(true, false, 500);
        TEST_ASSERT(true, "update() with various args doesn't crash");
    }
}

void SkinLoadTest::testFakeSkin() {
    TEST_SECTION("fake skin: search_dirs");
    {
        TEST_ASSERT_EQ((int)m_skin->search_dirs.size(), 2, "non-default skin has 2 search dirs");
        TEST_ASSERT_EQ(m_skin->search_dirs[0], std::string("/tmp/neomod_test_nonexistent_skin_dir/"),
                       "first search dir is user skin dir");
        TEST_ASSERT_EQ(m_skin->search_dirs[1], std::string(MCENGINE_IMAGES_PATH "/default/"),
                       "second search dir is default path");
        TEST_ASSERT(!m_skin->is_default, "default skin flag is not set");
    }

    TEST_SECTION("fake skin: fallback to default for images");
    {
        TEST_ASSERT(m_skin->i_hitcircle.img != MISSING_TEXTURE, "hitcircle falls back to default");
        TEST_ASSERT(m_skin->i_approachcircle.img != MISSING_TEXTURE, "approachcircle falls back to default");
        TEST_ASSERT(m_skin->i_cursor.img != MISSING_TEXTURE, "cursor falls back to default");
    }

    TEST_SECTION("fake skin: fallback to default for sounds");
    {
        TEST_ASSERT(m_skin->s_normal_hitnormal != nullptr, "normal-hitnormal falls back to default");
    }

    TEST_SECTION("fake skin: ignoreDefaultSkin elements are missing");
    {
        TEST_ASSERT(m_skin->i_slider_start_circle.img == MISSING_TEXTURE,
                    "sliderstartcircle is missing (ignoreDefaultSkin)");
        TEST_ASSERT(m_skin->i_slider_end_circle.img == MISSING_TEXTURE,
                    "sliderendcircle is missing (ignoreDefaultSkin)");
        TEST_ASSERT(m_skin->i_particle50.img == MISSING_TEXTURE, "particle50 is missing (ignoreDefaultSkin)");
        TEST_ASSERT(m_skin->i_particle100.img == MISSING_TEXTURE, "particle100 is missing (ignoreDefaultSkin)");
        TEST_ASSERT(m_skin->i_particle300.img == MISSING_TEXTURE, "particle300 is missing (ignoreDefaultSkin)");
    }

    TEST_SECTION("fake skin: SkinImage fallback");
    {
        TEST_ASSERT(m_skin->i_sliderb != nullptr, "sliderb SkinImage created");
        TEST_ASSERT(!m_skin->i_sliderb->isMissingTexture(), "sliderb falls back to default");
        TEST_ASSERT(m_skin->i_sliderb->isFromDefaultSkin(), "sliderb reports from default skin");
    }

    TEST_SECTION("fake skin: menu-back DEFAULTSKIN hack");
    {
        TEST_ASSERT(m_skin->i_menu_back2_DEFAULTSKIN != nullptr, "menu-back DEFAULTSKIN created");
        TEST_ASSERT(!m_skin->i_menu_back2_DEFAULTSKIN->isMissingTexture(), "menu-back DEFAULTSKIN not missing");
    }
}

void SkinLoadTest::testRealSkin(const std::string &label, const std::string &skinPath) {
    TEST_SECTION(label + " skin: search_dirs");
    {
        TEST_ASSERT_EQ((int)m_skin->search_dirs.size(), 2, label + " has 2 search dirs");
        TEST_ASSERT_EQ(m_skin->search_dirs[0], skinPath + "/", label + " primary dir is skin path");
        TEST_ASSERT_EQ(m_skin->search_dirs[1], std::string(MCENGINE_IMAGES_PATH "/default/"),
                       label + " fallback dir is default path");
        TEST_ASSERT(!m_skin->is_default, label + " is not default skin");
    }

    TEST_SECTION(label + " skin: core images loaded");
    {
        TEST_ASSERT(m_skin->i_hitcircle.img != MISSING_TEXTURE, label + " hitcircle loaded");
        TEST_ASSERT(m_skin->i_approachcircle.img != MISSING_TEXTURE, label + " approachcircle loaded");
        TEST_ASSERT(m_skin->i_cursor.img != MISSING_TEXTURE, label + " cursor loaded");
        TEST_ASSERT(m_skin->resources.size() > 0, label + " resources populated");
    }

    TEST_SECTION(label + " skin: source verification (images)");
    {
        verifyImageSource(m_skin->i_hitcircle.img, "hitcircle", label);
        verifyImageSource(m_skin->i_approachcircle.img, "approachcircle", label);
        verifyImageSource(m_skin->i_cursor.img, "cursor", label);
        verifyImageSource(m_skin->i_reversearrow.img, "reversearrow", label);
        verifyImageSource(m_skin->i_slider_gradient.img, "slidergradient", label);
    }

    TEST_SECTION(label + " skin: source verification (sounds)");
    {
        verifySoundSource(m_skin->s_normal_hitnormal, "normal-hitnormal", label);
        verifySoundSource(m_skin->s_combobreak, "combobreak", label);
    }

    TEST_SECTION(label + " skin: SkinImage elements");
    {
        TEST_ASSERT(m_skin->i_hitcircleoverlay != nullptr, label + " hitcircleoverlay created");
        TEST_ASSERT(!m_skin->i_hitcircleoverlay->isMissingTexture(), label + " hitcircleoverlay loaded");

        if(env->fileExists(skinPath + "/hitcircleoverlay.png") ||
           env->fileExists(skinPath + "/hitcircleoverlay@2x.png")) {
            TEST_ASSERT(!m_skin->i_hitcircleoverlay->isFromDefaultSkin(),
                        label + " hitcircleoverlay is from user skin");
        }

        TEST_ASSERT(m_skin->i_sliderb != nullptr, label + " sliderb created");
        TEST_ASSERT(!m_skin->i_sliderb->isMissingTexture(), label + " sliderb loaded");
        if(env->fileExists(skinPath + "/sliderb0.png") || env->fileExists(skinPath + "/sliderb0@2x.png") ||
           env->fileExists(skinPath + "/sliderb.png") || env->fileExists(skinPath + "/sliderb@2x.png")) {
            TEST_ASSERT(!m_skin->i_sliderb->isFromDefaultSkin(), label + " sliderb is from user skin");
        }
    }

    TEST_SECTION(label + " skin: skin.ini parsed");
    {
        if(env->fileExists(skinPath + "/skin.ini")) {
            TEST_ASSERT(true, label + " skin.ini exists");
            TEST_ASSERT(m_skin->version > 0.f, label + " version is positive");
        }
    }

    TEST_SECTION(label + " skin: update() doesn't crash");
    {
        m_skin->update(false, false, 0);
        m_skin->update(true, true, 1000);
        TEST_ASSERT(true, label + " update() ok");
    }
}

void SkinLoadTest::testFallbackTier(const std::string &label, const std::string &primaryPath,
                                    const std::string &fallbackPath) {
    const std::string primaryDir = primaryPath + "/";
    const std::string fallbackDir = fallbackPath + "/";
    const std::string defaultDir{MCENGINE_IMAGES_PATH "/default/"};

    TEST_SECTION(label + ": search_dirs");
    {
        TEST_ASSERT_EQ((int)m_skin->search_dirs.size(), 3, label + " has 3 search dirs");
        TEST_ASSERT_EQ(m_skin->search_dirs[0], primaryDir, label + " [0] is primary");
        TEST_ASSERT_EQ(m_skin->search_dirs[1], fallbackDir, label + " [1] is fallback");
        TEST_ASSERT_EQ(m_skin->search_dirs[2], defaultDir, label + " [2] is default");
        TEST_ASSERT(!m_skin->is_default, label + " is not default skin");
    }

    TEST_SECTION(label + ": image source verification");
    {
        // for each element, verify it comes from the first dir that has it
        verifyImageSource(m_skin->i_hitcircle.img, "hitcircle", label);
        verifyImageSource(m_skin->i_approachcircle.img, "approachcircle", label);
        verifyImageSource(m_skin->i_cursor.img, "cursor", label);
        verifyImageSource(m_skin->i_reversearrow.img, "reversearrow", label);
        verifyImageSource(m_skin->i_slider_gradient.img, "slidergradient", label);
        verifyImageSource(m_skin->i_spinner_bg.img, "spinner-background", label);
        verifyImageSource(m_skin->i_star.img, "star", label);
    }

    TEST_SECTION(label + ": sound source verification");
    {
        verifySoundSource(m_skin->s_normal_hitnormal, "normal-hitnormal", label);
        verifySoundSource(m_skin->s_combobreak, "combobreak", label);
        verifySoundSource(m_skin->s_applause, "applause", label);
    }

    TEST_SECTION(label + ": isFromDefaultSkin correctness");
    {
        // SkinImage should only report isFromDefaultSkin if the image actually came from the default dir
        if(m_skin->i_sliderb && !m_skin->i_sliderb->isMissingTexture()) {
            bool primaryHas = skinElementExists(primaryDir, "sliderb") || skinElementExists(primaryDir, "sliderb0");
            bool fallbackHas = skinElementExists(fallbackDir, "sliderb") || skinElementExists(fallbackDir, "sliderb0");

            if(primaryHas || fallbackHas) {
                TEST_ASSERT(!m_skin->i_sliderb->isFromDefaultSkin(),
                            label + " sliderb NOT from default (primary or fallback has it)");
            } else {
                TEST_ASSERT(m_skin->i_sliderb->isFromDefaultSkin(),
                            label + " sliderb IS from default (neither primary nor fallback has it)");
            }
        }

        if(m_skin->i_hitcircleoverlay && !m_skin->i_hitcircleoverlay->isMissingTexture()) {
            bool primaryHas = skinElementExists(primaryDir, "hitcircleoverlay");
            bool fallbackHas = skinElementExists(fallbackDir, "hitcircleoverlay");

            if(primaryHas || fallbackHas) {
                TEST_ASSERT(!m_skin->i_hitcircleoverlay->isFromDefaultSkin(),
                            label + " hitcircleoverlay NOT from default");
            } else {
                TEST_ASSERT(m_skin->i_hitcircleoverlay->isFromDefaultSkin(),
                            label + " hitcircleoverlay IS from default");
            }
        }
    }

    TEST_SECTION(label + ": update() doesn't crash");
    {
        m_skin->update(false, false, 0);
        m_skin->update(true, true, 1000);
        TEST_ASSERT(true, label + " update() ok");
    }
}

void SkinLoadTest::testSequentialLoad() {
    if(m_phase == TEST_SEQUENTIAL_A) {
        TEST_SECTION("sequential: phase A (tier1 alone)");
        {
            TEST_ASSERT(m_skin->i_hitcircle.img != MISSING_TEXTURE, "seq A: hitcircle loaded");
            TEST_ASSERT(m_skin->i_cursor.img != MISSING_TEXTURE, "seq A: cursor loaded");

            // save filepaths for comparison after the swap
            if(m_skin->i_hitcircle.img && m_skin->i_hitcircle.img != MISSING_TEXTURE)
                m_seq_hitcircle_path = m_skin->i_hitcircle.img->getFilePath();
            if(m_skin->i_cursor.img && m_skin->i_cursor.img != MISSING_TEXTURE)
                m_seq_cursor_path = m_skin->i_cursor.img->getFilePath();

            // verify source
            verifyImageSource(m_skin->i_hitcircle.img, "hitcircle", "seq A");
            verifyImageSource(m_skin->i_cursor.img, "cursor", "seq A");
        }
    } else {
        TEST_SECTION("sequential: phase B (tier2+tier1 fallback, after destroying tier1)");
        {
            TEST_ASSERT(m_skin->i_hitcircle.img != MISSING_TEXTURE, "seq B: hitcircle loaded");
            TEST_ASSERT(m_skin->i_cursor.img != MISSING_TEXTURE, "seq B: cursor loaded");
            TEST_ASSERT_EQ((int)m_skin->search_dirs.size(), 3, "seq B: 3 search dirs");

            // verify source comes from the right place now
            verifyImageSource(m_skin->i_hitcircle.img, "hitcircle", "seq B");
            verifyImageSource(m_skin->i_cursor.img, "cursor", "seq B");

            // if tier2 has its own hitcircle but tier1 also did, the filepath should have changed
            // (unless both skins have the same file, which is unlikely but possible)
            if(!m_seq_hitcircle_path.empty() && m_skin->i_hitcircle.img != MISSING_TEXTURE) {
                const auto &newPath = m_skin->i_hitcircle.img->getFilePath();
                bool tier2HasHitcircle = skinElementExists(*m_tier2_path + "/", "hitcircle");
                bool tier1HasHitcircle = skinElementExists(*m_tier1_path + "/", "hitcircle");

                if(tier2HasHitcircle && tier1HasHitcircle) {
                    // tier2 is now primary, so hitcircle should come from tier2 (different path)
                    TEST_ASSERT(newPath != m_seq_hitcircle_path,
                                "seq B: hitcircle path changed (now from tier2 primary, was tier1)");
                } else if(!tier2HasHitcircle && tier1HasHitcircle) {
                    // tier1 is now fallback, hitcircle should still come from tier1 (same path)
                    TEST_ASSERT(newPath == m_seq_hitcircle_path,
                                "seq B: hitcircle path unchanged (still from tier1 as fallback)");
                }
            }

            // verify resources from old skin were cleaned up (new skin has its own resources)
            TEST_ASSERT(m_skin->resources.size() > 0, "seq B: new skin has its own resources");
        }
    }
}

void SkinLoadTest::testHotSwap() {
    const std::string t1Dir = *m_tier1_path + "/";
    const std::string t2Dir = *m_tier2_path + "/";

    TEST_SECTION("hot-swap: both skins coexist");
    {
        TEST_ASSERT(m_skin != nullptr, "skin A exists");
        TEST_ASSERT(m_skin_pending != nullptr, "skin B exists");
        TEST_ASSERT(m_skin->isReady(), "skin A is ready");
        TEST_ASSERT(m_skin_pending->isReady(), "skin B is ready");

        // skin A = tier1 primary + tier2 fallback
        TEST_ASSERT_EQ((int)m_skin->search_dirs.size(), 3, "skin A has 3 search dirs");
        TEST_ASSERT_EQ(m_skin->search_dirs[0], t1Dir, "skin A primary is tier1");
        TEST_ASSERT_EQ(m_skin->search_dirs[1], t2Dir, "skin A fallback is tier2");

        // skin B = tier2 primary + tier1 fallback
        TEST_ASSERT_EQ((int)m_skin_pending->search_dirs.size(), 3, "skin B has 3 search dirs");
        TEST_ASSERT_EQ(m_skin_pending->search_dirs[0], t2Dir, "skin B primary is tier2");
        TEST_ASSERT_EQ(m_skin_pending->search_dirs[1], t1Dir, "skin B fallback is tier1");

        // both skins should have valid core images
        TEST_ASSERT(m_skin->i_hitcircle.img != MISSING_TEXTURE, "skin A hitcircle loaded");
        TEST_ASSERT(m_skin_pending->i_hitcircle.img != MISSING_TEXTURE, "skin B hitcircle loaded");
        TEST_ASSERT(m_skin->i_cursor.img != MISSING_TEXTURE, "skin A cursor loaded");
        TEST_ASSERT(m_skin_pending->i_cursor.img != MISSING_TEXTURE, "skin B cursor loaded");
    }

    // save skin B paths for post-swap verification
    std::string skinB_hitcircle_path;
    if(m_skin_pending->i_hitcircle.img && m_skin_pending->i_hitcircle.img != MISSING_TEXTURE)
        skinB_hitcircle_path = m_skin_pending->i_hitcircle.img->getFilePath();

    std::string skinB_cursor_path;
    if(m_skin_pending->i_cursor.img && m_skin_pending->i_cursor.img != MISSING_TEXTURE)
        skinB_cursor_path = m_skin_pending->i_cursor.img->getFilePath();

    TEST_SECTION("hot-swap: perform swap (destroy A, adopt B)");
    {
        // this is what the real game does: destroy old skin, adopt new one
        m_skin = std::move(m_skin_pending);

        TEST_ASSERT(m_skin != nullptr, "m_skin now holds skin B");
        TEST_ASSERT(m_skin_pending == nullptr, "m_skin_pending is null after release");
    }

    TEST_SECTION("hot-swap: skin B works after A destroyed");
    {
        TEST_ASSERT(m_skin->isReady(), "skin B still ready after swap");
        TEST_ASSERT(m_skin->i_hitcircle.img != MISSING_TEXTURE, "skin B hitcircle still valid");
        TEST_ASSERT(m_skin->i_cursor.img != MISSING_TEXTURE, "skin B cursor still valid");

        // verify skin B paths are unchanged (skin A destruction didn't corrupt them)
        if(!skinB_hitcircle_path.empty() && m_skin->i_hitcircle.img != MISSING_TEXTURE) {
            TEST_ASSERT_EQ(m_skin->i_hitcircle.img->getFilePath(), skinB_hitcircle_path,
                           "skin B hitcircle path unchanged after swap");
        }
        if(!skinB_cursor_path.empty() && m_skin->i_cursor.img != MISSING_TEXTURE) {
            TEST_ASSERT_EQ(m_skin->i_cursor.img->getFilePath(), skinB_cursor_path,
                           "skin B cursor path unchanged after swap");
        }

        // verify the search dirs are skin B's dirs
        TEST_ASSERT_EQ((int)m_skin->search_dirs.size(), 3, "post-swap has 3 search dirs");
        TEST_ASSERT_EQ(m_skin->search_dirs[0], t2Dir, "post-swap primary is tier2");
        TEST_ASSERT_EQ(m_skin->search_dirs[1], t1Dir, "post-swap fallback is tier1");

        // update should still work
        m_skin->update(false, false, 0);
        m_skin->update(true, true, 1000);
        TEST_ASSERT(true, "post-swap update() ok");
    }
}

void SkinLoadTest::testReloadLeak() {
    size_t current_count = resourceManager->getResources().size();

    TEST_SECTION(fmt::format("reload leak: iteration {} resource count", m_reload_iteration));
    logRaw("  baseline: {} resources, after reload: {}", m_baseline_resource_count, current_count);
    TEST_ASSERT_EQ((int)current_count, (int)m_baseline_resource_count,
                   fmt::format("reload {}: resource count unchanged", m_reload_iteration));

    if(current_count != m_baseline_resource_count) {
        // diff: find paths that appear more times now than in baseline
        std::multiset<std::string> current_paths;
        for(auto *r : resourceManager->getResources()) current_paths.insert(r->getFilePath());

        logRaw("  delta: {} resource(s):", (int)current_count - (int)m_baseline_resource_count);
        for(auto &path : current_paths) {
            auto cur = current_paths.count(path);
            auto base = m_baseline_paths.count(path);
            if(cur > base) {
                logRaw("    LEAKED path=\"{}\" (was {}, now {})", path, base, cur);
            }
        }
    }
}

void SkinLoadTest::finish() {
    TEST_PRINT_RESULTS("SkinLoadTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
