// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"

#include <memory>
#include <optional>
#include <string>

class Image;
class Sound;
struct Skin;

namespace Mc::Tests {

class SkinLoadTest : public App {
    NOCOPY_NOMOVE(SkinLoadTest)
   public:
    SkinLoadTest();
    ~SkinLoadTest() override = default;

    void update() override;

   private:
    void testDefaultSkin();
    void testFakeSkin();
    void testRealSkin(const std::string &label, const std::string &skinPath);
    void testFallbackTier(const std::string &label, const std::string &primaryPath, const std::string &fallbackPath);
    void testSequentialLoad();
    void testHotSwap();
    void advanceToNextPhase();
    void finish();

    // helpers
    static bool skinElementExists(const std::string &dir, const std::string &elementName);
    static bool soundElementExists(const std::string &dir, const std::string &elementName);
    std::string expectedImageSource(const std::string &elementName);
    std::string expectedSoundSource(const std::string &elementName);
    void verifyImageSource(const Image *img, const std::string &elementName, const std::string &label);
    void verifySoundSource(const Sound *snd, const std::string &elementName, const std::string &label);

    enum Phase : u8 {
        WAIT_DEFAULT,
        TEST_DEFAULT,
        WAIT_FAKE,
        TEST_FAKE,
        WAIT_TIER1,
        TEST_TIER1,
        WAIT_TIER2,
        TEST_TIER2,
        WAIT_FALLBACK,
        TEST_FALLBACK,
        WAIT_SWAP,
        TEST_SWAP,
        WAIT_SEQUENTIAL_A,
        TEST_SEQUENTIAL_A,
        WAIT_SEQUENTIAL_B,
        TEST_SEQUENTIAL_B,
        HOTSWAP_CREATE_A,
        HOTSWAP_WAIT_A,
        HOTSWAP_CREATE_B,
        HOTSWAP_WAIT_B,
        HOTSWAP_TEST,
        DONE
    };
    Phase m_phase{WAIT_DEFAULT};

    std::unique_ptr<Skin> m_skin;
    std::unique_ptr<Skin> m_skin_pending;  // second skin for hot-swap test

    // saved filepaths from sequential test phase A for comparison in phase B
    std::string m_seq_hitcircle_path;
    std::string m_seq_cursor_path;

    std::optional<std::string> m_tier1_path;
    std::optional<std::string> m_tier2_path;
    std::optional<std::string> m_tier3_path;

    int m_passes = 0;
    int m_failures = 0;
};

}  // namespace Mc::Tests
