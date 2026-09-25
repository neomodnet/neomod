// Copyright (c) 2026, WH, All rights reserved.
#include "SkinLoadTest.h"

#include "TestMacros.h"
#include "Archival.h"
#include "Engine.h"
#include "File.h"
#include "OsuConVars.h"
#include "Paths.h"
#include "Resource.h"
#include "ResourceManager.h"
#include "crypto.h"

#include <algorithm>
#include <iterator>
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
    m_skin = std::make_unique<Skin>("default", Mc::Paths::materials() + "/default/");
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

        case EXPORT_LOAD:
            if(!m_skin->isReady()) return;
            m_original_default.clear();
            m_original_look = loadedLook(*m_skin, &m_original_default);
            m_export =
                SkinArchive::submit_export(*m_skin, m_tmp_dir + "export1", m_round_trips[m_round_trip].export_name,
                                           m_round_trips[m_round_trip].kind == RoundTrip::Kind::FixtureWithDefault);
            if(m_round_trips[m_round_trip].kind == RoundTrip::Kind::Fixture) {
                m_export_twin = SkinArchive::submit_export(*m_skin, m_tmp_dir + "export1");
            }
            m_phase = EXPORT_FIRST;
            return;

        case EXPORT_FIRST:
            if(!m_export.is_ready() || (m_export_twin.valid() && !m_export_twin.is_ready())) return;
            checkFirstExport();
            return;

        case EXPORT_IMPORT:
            if(!m_skin->isReady()) return;
            checkImportedSkin();
            return;

        case EXPORT_SECOND:
            if(!m_export.is_ready()) return;
            checkSecondExport();
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
        makeExportFixtures();
        startRoundTrip();
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

SkinLoadTest::ArchiveFiles SkinLoadTest::archiveFiles(const std::string &path) {
    ArchiveFiles files;
    Archive::Reader archive(path, "CP932");
    for(const auto &entry : archive.getAllEntries()) {
        if(entry.isFile()) files[entry.getFilename()] = crypto::hash::sha256(entry.getUncompressedData());
    }
    return files;
}

// what a skin shows and plays: the file behind every image (frame) and sound it loaded
SkinLoadTest::Look SkinLoadTest::loadedLook(Skin &skin, Look *fromDefault) {
    const std::string defaultDir{Mc::Paths::materials() + "/default/"};
    Look look;
    const auto add = [&](const Resource *res) {
        if(!res || res == MISSING_TEXTURE) return;
        const std::string &path = res->getFilePath();
        const auto element = std::make_pair(crypto::hash::sha256_file(path).value_or(std::array<u8, 32>{}),
                                            env->getFileNameFromFilePath(path));
        if(fromDefault && path.starts_with(defaultDir)) fromDefault->insert(element);
        look.insert(element);
    };

    for(const BasicSkinImage *img : skin.basic_images) add(img->img);
    for(SkinImage *img : skin.skin_images) {
        std::vector<const Image *> frames;
        for(int i = 0; i < img->getNumImages(); i++) {
            img->setAnimationFrameForce(i);
            frames.push_back(img->getImageForCurrentFrame().img);
            add(frames.back());
        }
        // (the still image of an animated one, if it has one of its own)
        if(const Image *still = img->getImageForCurrentFrame(false).img; !std::ranges::contains(frames, still)) {
            add(still);
        }
    }
    for(const Sound *snd : skin.sounds) add(snd);
    return look;
}

// (in an osu! skins folder, for random skins to be picked from)
std::string SkinLoadTest::fixtureDir(std::string_view name) const {
    return fmt::format("{}osu/Skins/{}/", m_tmp_dir, name);
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
        TEST_ASSERT_EQ(m_skin->search_dirs[0], Mc::Paths::materials() + "/default/",
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
        TEST_ASSERT(m_skin->basic_images.size() > 0, "resources vector is populated");
    }

    TEST_SECTION("default skin: core sounds");
    {
        TEST_ASSERT(m_skin->s_normal_hitnormal != nullptr, "normal-hitnormal loaded");
        TEST_ASSERT(m_skin->s_combobreak != nullptr, "combobreak loaded");
        TEST_ASSERT(m_skin->sounds.size() > 0, "sounds vector is populated");
    }

    TEST_SECTION("default skin: SkinImage elements");
    {
        TEST_ASSERT(!m_skin->i_hitcircleoverlay.isMissingTexture(), "hitcircleoverlay not missing");
    }

    TEST_SECTION("default skin: export");
    {
        auto exported = SkinArchive::submit_export(*m_skin, Mc::Paths::cache() + "/.tmp/skinloadtest/unused");
        TEST_ASSERT(exported.is_ready(), "the default skin is turned down right away");
        TEST_ASSERT(exported.get().status == SkinArchive::ExportResult::Status::DefaultSkin,
                    "the default skin is only exported with include_default");
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
        TEST_ASSERT_EQ(m_skin->search_dirs[1], Mc::Paths::materials() + "/default/",
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
        TEST_ASSERT(!m_skin->i_sliderb.isMissingTexture(), "sliderb falls back to default");
        TEST_ASSERT(m_skin->i_sliderb.isFromDefaultSkin(), "sliderb reports from default skin");
    }

    TEST_SECTION("fake skin: menu-back DEFAULTSKIN hack");
    {
        TEST_ASSERT(!m_skin->i_menu_back2_DEFAULTSKIN.isMissingTexture(), "menu-back DEFAULTSKIN not missing");
    }
}

void SkinLoadTest::testRealSkin(const std::string &label, const std::string &skinPath) {
    TEST_SECTION(label + " skin: search_dirs");
    {
        TEST_ASSERT_EQ((int)m_skin->search_dirs.size(), 2, label + " has 2 search dirs");
        TEST_ASSERT_EQ(m_skin->search_dirs[0], skinPath + "/", label + " primary dir is skin path");
        TEST_ASSERT_EQ(m_skin->search_dirs[1], Mc::Paths::materials() + "/default/",
                       label + " fallback dir is default path");
        TEST_ASSERT(!m_skin->is_default, label + " is not default skin");
    }

    TEST_SECTION(label + " skin: core images loaded");
    {
        TEST_ASSERT(m_skin->i_hitcircle.img != MISSING_TEXTURE, label + " hitcircle loaded");
        TEST_ASSERT(m_skin->i_approachcircle.img != MISSING_TEXTURE, label + " approachcircle loaded");
        TEST_ASSERT(m_skin->i_cursor.img != MISSING_TEXTURE, label + " cursor loaded");
        TEST_ASSERT(m_skin->basic_images.size() > 0, label + " resources populated");
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
        TEST_ASSERT(!m_skin->i_hitcircleoverlay.isMissingTexture(), label + " hitcircleoverlay loaded");

        if(env->fileExists(skinPath + "/hitcircleoverlay.png") ||
           env->fileExists(skinPath + "/hitcircleoverlay@2x.png")) {
            TEST_ASSERT(!m_skin->i_hitcircleoverlay.isFromDefaultSkin(), label + " hitcircleoverlay is from user skin");
        }

        TEST_ASSERT(!m_skin->i_sliderb.isMissingTexture(), label + " sliderb loaded");
        if(env->fileExists(skinPath + "/sliderb0.png") || env->fileExists(skinPath + "/sliderb0@2x.png") ||
           env->fileExists(skinPath + "/sliderb.png") || env->fileExists(skinPath + "/sliderb@2x.png")) {
            TEST_ASSERT(!m_skin->i_sliderb.isFromDefaultSkin(), label + " sliderb is from user skin");
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
    const std::string defaultDir{Mc::Paths::materials() + "/default/"};

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
        if(!m_skin->i_sliderb.isMissingTexture()) {
            bool primaryHas = skinElementExists(primaryDir, "sliderb") || skinElementExists(primaryDir, "sliderb0");
            bool fallbackHas = skinElementExists(fallbackDir, "sliderb") || skinElementExists(fallbackDir, "sliderb0");

            if(primaryHas || fallbackHas) {
                TEST_ASSERT(!m_skin->i_sliderb.isFromDefaultSkin(),
                            label + " sliderb NOT from default (primary or fallback has it)");
            } else {
                TEST_ASSERT(m_skin->i_sliderb.isFromDefaultSkin(),
                            label + " sliderb IS from default (neither primary nor fallback has it)");
            }
        }

        if(!m_skin->i_hitcircleoverlay.isMissingTexture()) {
            bool primaryHas = skinElementExists(primaryDir, "hitcircleoverlay");
            bool fallbackHas = skinElementExists(fallbackDir, "hitcircleoverlay");

            if(primaryHas || fallbackHas) {
                TEST_ASSERT(!m_skin->i_hitcircleoverlay.isFromDefaultSkin(),
                            label + " hitcircleoverlay NOT from default");
            } else {
                TEST_ASSERT(m_skin->i_hitcircleoverlay.isFromDefaultSkin(),
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
            TEST_ASSERT(m_skin->basic_images.size() > 0, "seq B: new skin has its own resources");
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

// --- export round trips ---

void SkinLoadTest::makeExportFixtures() {
    m_tmp_dir = fmt::format("{}/.tmp/skinloadtest/", Mc::Paths::cache());
    env->deletePathsRecursive(m_tmp_dir, 8);

    // made of the default skin's files under other names, so that where an element comes from shows
    const std::string def = Mc::Paths::materials() + "/default/";
    const std::string skin = fixtureDir("exp_skin");
    const std::string fallback = fixtureDir("exp_fallback");
    bool ok = env->createDirectory(skin + "日本語") && env->createDirectory(skin + "extras 🎨") &&
              env->createDirectory(fallback);
    const auto copy = [&](std::string_view from, const std::string &to) {
        ok &= File::copy(def + std::string{from}, to);
    };
    const auto write = [&](const std::string &path, std::string_view text) {
        File file(path, File::MODE::WRITE);
        ok &= file.write({reinterpret_cast<const u8 *>(text.data()), text.size()});
    };

    write(skin + "skin.ini",
          "[General]\nName: exp_skin\nAuthor: SkinLoadTest\n\n"
          "[Fonts]\nHitCirclePrefix: 日本語\\num\nComboPrefix: hide\n");
    copy("approachcircle.png", skin + "hitcircle.png");
    copy("sliderb0.png", skin + "sliderb0.png");
    copy("hit0.png", skin + "sliderb1.png");
    copy("normal-hitclap.flac", skin + "normal-hitnormal.flac");
    copy("hit50.png", skin + "mania-key1.png");
    for(int i = 0; i < 10; i++) copy(fmt::format("default-{}.png", 9 - i), fmt::format("{}日本語/num-{}.png", skin, i));
    write(skin + "drum-hitclap.wav", "");  // (muted)
    write(skin + "extras 🎨/readme.txt", "no shift-jis for this one\n");
    write(skin + ".DS_Store", "clutter");
    write(skin + "Thumbs.db", "clutter");
    write(skin + std::string{SkinArchive::LOG_NAME}, "log of an earlier export\n");

    write(fallback + "skin.ini",
          "[General]\nName: exp_fallback\n\n[Fonts]\nHitCirclePrefix: fbnum\nComboPrefix: fbcombo\n");
    copy("hitcircleoverlay@2x.png", fallback + "approachcircle@2x.png");
    copy("cursor-ripple.png", fallback + "cursor.png");
    copy("cursor-ripple@2x.png", fallback + "cursor@2x.png");
    copy("hit100.png", fallback + "hit300-0.png");
    copy("hit50.png", fallback + "hit300-1.png");
    copy("hit0.png", fallback + "hit300.png");
    copy("hitcircle.png", fallback + "hitcircle.png");
    copy("sliderb0.png", fallback + "sliderb.png");
    copy("check-on.flac", fallback + "combobreak.flac");
    copy("combobreak.mp3", fallback + "normal-hitnormal.mp3");
    copy("hit50.png", fallback + "mania-note1.png");
    copy("default-0.png", fallback + "fbnum-0.png");
    for(int i = 0; i < 10; i++) copy(fmt::format("score-{}.png", i), fmt::format("{}fbcombo-{}.png", fallback, i));

    TEST_SECTION("export fixtures");
    TEST_ASSERT(ok, "fixture skins made in " + m_tmp_dir);

    using enum RoundTrip::Kind;
    m_round_trips = {
        {.kind = Fixture,
         .label = "export(fixture)",
         .name = "exp_skin",
         .skin_dir = skin,
         .fallback_dir = fallback,
         .export_name = ""},
        {.kind = FixtureWithDefault,
         .label = "export(fixture, with default, as \"Default.osk\")",
         .name = "exp_skin",
         .skin_dir = skin,
         .fallback_dir = fallback,
         .export_name = "Default.osk"},
        {.kind = FixtureLegacySettings,
         .label = "export(fixture, skin_hd 0, skin_use_skin_hitsounds 0, as \"legacy: settings\")",
         .name = "exp_skin",
         .skin_dir = skin,
         .fallback_dir = fallback,
         .export_name = "legacy: settings"},
        {.kind = RandomSkin,
         .label = "export(skin_random, with the default skin selected)",
         .name = "default",
         .skin_dir = def,
         .fallback_dir = "",
         .export_name = ""},
        {.kind = RandomElements,
         .label = "export(skin_random_elements)",
         .name = "exp_skin",
         .skin_dir = skin,
         .fallback_dir = fallback,
         .export_name = ""},
    };
    if(m_tier1_path && m_tier2_path) {
        m_round_trips.push_back({.kind = Given,
                                 .label = "export(t1+t2)",
                                 .name = env->getFileNameFromFilePath(*m_tier1_path),
                                 .skin_dir = *m_tier1_path + "/",
                                 .fallback_dir = *m_tier2_path + "/",
                                 .export_name = ""});
    }
    m_round_trip = 0;
}

void SkinLoadTest::startRoundTrip() {
    if(m_round_trip >= m_round_trips.size()) {
        env->deletePathsRecursive(m_tmp_dir, 8);
        m_phase = DONE;
        finish();
        return;
    }

    const auto &rt = m_round_trips[m_round_trip];
    if(rt.kind == RoundTrip::Kind::FixtureLegacySettings) {
        m_saved_skin_hd = cv::skin_hd.getBool();
        m_saved_skin_hitsounds = cv::skin_use_skin_hitsounds.getBool();
        cv::skin_hd.setValue(false);
        cv::skin_use_skin_hitsounds.setValue(false);
    }

    // (random skins get picked from the osu! skins folder while the skin loads)
    using enum RoundTrip::Kind;
    const bool random = rt.kind == RandomSkin || rt.kind == RandomElements;
    const std::string saved_osu_folder{cv::osu_folder.getString()};
    const std::string saved_sub_skins{cv::osu_folder_sub_skins.getString()};
    const bool saved_random = cv::skin_random.getBool(), saved_random_elements = cv::skin_random_elements.getBool();
    if(random) {
        cv::osu_folder.setValue(m_tmp_dir + "osu/");
        cv::osu_folder_sub_skins.setValue("Skins/");
        cv::skin_random.setValue(rt.kind == RandomSkin);
        cv::skin_random_elements.setValue(rt.kind == RandomElements);
    }
    m_skin = std::make_unique<Skin>(rt.name, rt.skin_dir, rt.fallback_dir);
    if(random) {
        cv::osu_folder.setValue(saved_osu_folder);
        cv::osu_folder_sub_skins.setValue(saved_sub_skins);
        cv::skin_random.setValue(saved_random);
        cv::skin_random_elements.setValue(saved_random_elements);
    }
    m_phase = EXPORT_LOAD;
}

void SkinLoadTest::nextRoundTrip() {
    m_skin.reset();
    for(const auto *dir : {"export1", "export2", "imported"}) env->deletePathsRecursive(m_tmp_dir + dir, 8);
    m_round_trip++;
    startRoundTrip();
}

void SkinLoadTest::checkFirstExport() {
    using enum RoundTrip::Kind;
    const auto &rt = m_round_trips[m_round_trip];
    const auto res = m_export.get();
    if(rt.kind == FixtureLegacySettings) {
        cv::skin_hd.setValue(m_saved_skin_hd);
        cv::skin_use_skin_hitsounds.setValue(m_saved_skin_hitsounds);
    }

    TEST_SECTION(rt.label + ": export");
    TEST_ASSERT(res.status == SkinArchive::ExportResult::Status::Exported, rt.label + " exported to " + res.path);
    if(res.status != SkinArchive::ExportResult::Status::Exported) return nextRoundTrip();

    TEST_ASSERT_EQ((int)env->getFilesInFolder(m_tmp_dir + "export1").size(), m_export_twin.valid() ? 2 : 1,
                   "only the .osk is left behind");
    auto files = archiveFiles(res.path);
    TEST_ASSERT_EQ((int)files.erase(std::string{SkinArchive::LOG_NAME}), 1, rt.label + " has the export log");
    const std::string fileName = env->getFileNameFromFilePath(res.path);

    switch(rt.kind) {
        case Fixture: {
            // (the export that went at the same time can't take its name, nor replace it)
            const auto twin = m_export_twin.get();
            auto twinFiles = archiveFiles(twin.path);
            twinFiles.erase(std::string{SkinArchive::LOG_NAME});
            TEST_ASSERT((std::set<std::string>{fileName, env->getFileNameFromFilePath(twin.path)} ==
                         std::set<std::string>{"exp_skin + exp_fallback.osk", "exp_skin + exp_fallback (1).osk"}),
                        "named after both skins, and numbered when that's taken");
            TEST_ASSERT(twinFiles == files, "the same files in either");
            checkFixtureExport(files, false);
            m_fixture_export = files;
            break;
        }
        case FixtureWithDefault:
            TEST_ASSERT_EQ(fileName, std::string{PACKAGE_NAME " default.osk"},
                           "not named like the default skin, which a skin folder can't be");
            checkFixtureExport(files, true);
            break;
        case FixtureLegacySettings:
            TEST_ASSERT_EQ(fileName, std::string{"legacy_ settings.osk"}, "a given name works as a file name");
            TEST_ASSERT(files == m_fixture_export, rt.label + " is the same as without those settings");
            // (loaded with those, the skin doesn't look like its export does on its own)
            return nextRoundTrip();
        case RandomSkin:
            TEST_ASSERT(!m_skin->is_default && m_skin->search_dirs.back() == Mc::Paths::materials() + "/default/",
                        "the picked skin falls back to the default one, like any other skin");
            TEST_ASSERT(m_skin->name == "exp_skin" || m_skin->name == "exp_fallback",
                        "a fixture skin got picked: " + m_skin->name);
            TEST_ASSERT_EQ(fileName, m_skin->name + ".osk", "named after the picked skin");
            break;
        case RandomElements: {
            TEST_ASSERT(fileName.starts_with("random mix "), "named as a random mix: " + fileName);
            const auto ini = files.find("skin.ini");
            TEST_ASSERT(
                ini != files.end() && ini->second == crypto::hash::sha256_file(fixtureDir("exp_skin") + "skin.ini"),
                "the selected skin's skin.ini");
            // (only what the elements got loaded from, not the rest of a folder, nor the default skin's)
            for(const std::string name : {"extras 🎨/readme.txt", "mania-key1.png", "followpoint-0.png"}) {
                TEST_ASSERT(!files.contains(name), "no " + name);
            }
            break;
        }
        case Given:
            TEST_ASSERT(!files.empty(), rt.label + " has files");
            break;
    }
    m_first_export = std::move(files);
    m_first_export_path = res.path;

    // import it, like an .osk dropped on the window
    m_skin.reset();
    std::string name = env->getFileNameFromFilePath(res.path);
    name.erase(name.size() - 4);
    TEST_ASSERT(SkinArchive::unpack(res.path, m_tmp_dir + "imported"), rt.label + " unpacks");
    m_skin = std::make_unique<Skin>(name, fmt::format("{}imported/{}/", m_tmp_dir, name));
    m_phase = EXPORT_IMPORT;
}

void SkinLoadTest::checkImportedSkin() {
    const auto &rt = m_round_trips[m_round_trip];
    TEST_SECTION(rt.label + ": imported");

    // on its own, the export has to load the same files (by contents, as digits can get renamed). only what the default
    // skin filled in can go missing when it isn't packed, e.g. the still image of an animation from the fallback skin
    const Look look = loadedLook(*m_skin);
    constexpr auto contents = &Look::value_type::first;
    Look lost, gained;
    std::ranges::set_difference(m_original_look, look, std::inserter(lost, lost.end()), {}, contents, contents);
    std::ranges::set_difference(look, m_original_look, std::inserter(gained, gained.end()), {}, contents, contents);
    if(rt.kind != RoundTrip::Kind::FixtureWithDefault) {
        std::erase_if(lost, [this](const auto &element) { return m_original_default.contains(element.first); });
    }
    TEST_ASSERT(lost.empty() && gained.empty(), rt.label + " loads the same files");
    for(const auto &element : lost) logRaw("    lost: {}", element.second);
    for(const auto &element : gained) logRaw("    gained: {}", element.second);

    m_export =
        SkinArchive::submit_export(*m_skin, m_tmp_dir + "export2", {}, rt.kind == RoundTrip::Kind::FixtureWithDefault);
    m_phase = EXPORT_SECOND;
}

void SkinLoadTest::checkSecondExport() {
    const auto &rt = m_round_trips[m_round_trip];
    const auto res = m_export.get();

    TEST_SECTION(rt.label + ": export of the import");
    TEST_ASSERT(res.status == SkinArchive::ExportResult::Status::Exported, rt.label + " exported again to " + res.path);
    if(res.status == SkinArchive::ExportResult::Status::Exported) {
        TEST_ASSERT_EQ(env->getFileNameFromFilePath(res.path), env->getFileNameFromFilePath(m_first_export_path),
                       rt.label + " keeps its name");
        auto files = archiveFiles(res.path);
        TEST_ASSERT_EQ((int)files.erase(std::string{SkinArchive::LOG_NAME}), 1, rt.label + " has a new export log");
        TEST_ASSERT(files == m_first_export, rt.label + " exports the same files again");
        if(files != m_first_export) {
            for(const auto &[name, hash] : m_first_export) {
                if(const auto it = files.find(name); it == files.end() || it->second != hash) {
                    logRaw("    lost/changed: {}", name);
                }
            }
            for(const auto &[name, hash] : files) {
                if(!m_first_export.contains(name)) logRaw("    gained: {}", name);
            }
        }
    }
    nextRoundTrip();
}

void SkinLoadTest::checkFixtureExport(const ArchiveFiles &files, bool withDefault) {
    const std::string def = Mc::Paths::materials() + "/default/";
    const std::string skin = fixtureDir("exp_skin");
    const std::string fallback = fixtureDir("exp_fallback");
    const auto has = [&files](const std::string &name, const std::string &source) {
        const auto it = files.find(name);
        return it != files.end() && it->second == crypto::hash::sha256_file(source);
    };

    // all of the skin's own files, whether the loader knows them or not
    std::vector<std::string> own{"skin.ini",       "hitcircle.png",    "sliderb0.png",         "sliderb1.png",
                                 "mania-key1.png", "drum-hitclap.wav", "extras 🎨/readme.txt", "normal-hitnormal.flac"};
    for(int i = 0; i < 10; i++) own.push_back(fmt::format("日本語/num-{}.png", i));
    for(const auto &name : own) TEST_ASSERT(has(name, skin + name), "the skin's own " + name);

    // the elements the fallback skin fills in
    const std::array<std::string, 7> filledIn{"approachcircle@2x.png", "cursor.png", "cursor@2x.png",  "hit300-0.png",
                                              "hit300-1.png",          "hit300.png", "combobreak.flac"};
    for(const auto &name : filledIn) TEST_ASSERT(has(name, fallback + name), "the fallback's " + name);

    // the skin's combo digits ("hide") are nowhere, so they come from the fallback skin's (under the skin's name)
    for(int i = 0; i < 10; i++) {
        TEST_ASSERT(has(fmt::format("hide-{}.png", i), fmt::format("{}fbcombo-{}.png", fallback, i)),
                    fmt::format("the fallback's combo digit {} as hide-{}.png", i, i));
        TEST_ASSERT(!files.contains(fmt::format("fbcombo-{}.png", i)), fmt::format("no fbcombo-{}.png", i));
    }

    // clutter, what the skin has itself (in any variant), and what the loader doesn't know from the fallback
    for(const std::string name :
        {".DS_Store", "Thumbs.db", "normal-hitnormal.mp3", "sliderb.png", "mania-note1.png", "fbnum-0.png"}) {
        TEST_ASSERT(!files.contains(name), "no " + name);
    }

    // the default skin's elements only when asked for, and never another variant of one that a skin has
    TEST_ASSERT_EQ(has("followpoint-0.png", def + "followpoint-0.png"), withDefault, "the default followpoint-0.png");
    TEST_ASSERT_EQ(has("hitcircleoverlay@2x.png", def + "hitcircleoverlay@2x.png"), withDefault,
                   "the default hitcircleoverlay@2x.png");
    TEST_ASSERT(!files.contains("hitcircle@2x.png"), "no default hitcircle@2x.png next to the skin's hitcircle.png");
    TEST_ASSERT(!files.contains("approachcircle.png"), "no default approachcircle.png next to the fallback's @2x");
    if(!withDefault) TEST_ASSERT_EQ((int)files.size(), (int)(own.size() + filledIn.size() + 10), "nothing else");
}

void SkinLoadTest::finish() {
    TEST_PRINT_RESULTS("SkinLoadTest");
    engine->shutdown();
}

}  // namespace Mc::Tests
