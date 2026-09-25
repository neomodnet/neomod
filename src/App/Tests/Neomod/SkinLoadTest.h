// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "App.h"
#include "AsyncFuture.h"
#include "SkinArchive.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    void testReloadLeak();
    void advanceToNextPhase();
    void finish();

    // skin export round trips: export a skin, import the export like a dropped .osk, and export that again
    struct RoundTrip {
        enum class Kind : u8 {
            Fixture,                // fixture skin + fallback: exact expectations
            FixtureWithDefault,     // the same, packing what the default skin fills in too
            FixtureLegacySettings,  // skin_hd 0 + skin_use_skin_hitsounds 0 must not change the export
            RandomSkin,             // skin_random with the default skin selected, picking one of the fixture skins
            RandomElements,         // skin_random_elements from the fixture skins
            Given,                  // skin_tier1 + skin_tier2
        };
        Kind kind;
        std::string label;
        std::string name;
        std::string skin_dir;
        std::string fallback_dir;
        std::string export_name;  // (empty to let the export make one up)
    };
    using ArchiveFiles = std::map<std::string, std::array<u8, 32>>;  // name -> sha256 of the contents
    using Look = std::multimap<std::array<u8, 32>, std::string>;     // sha256 of the contents -> file name

    void makeExportFixtures();
    void startRoundTrip();
    void checkFirstExport();
    void checkImportedSkin();
    void checkSecondExport();
    void checkFixtureExport(const ArchiveFiles &files, bool withDefault);
    void nextRoundTrip();

    // helpers
    static bool skinElementExists(const std::string &dir, const std::string &elementName);
    static bool soundElementExists(const std::string &dir, const std::string &elementName);
    static ArchiveFiles archiveFiles(const std::string &path);
    [[nodiscard]] std::string fixtureDir(std::string_view name) const;
    static Look loadedLook(Skin &skin, Look *fromDefault = nullptr);
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
        RELOAD_LEAK_WARMUP,
        WAIT_RELOAD_LEAK_WARMUP,
        WAIT_RELOAD_LEAK_BASELINE,
        WAIT_RELOAD_LEAK_RELOAD,
        TEST_RELOAD_LEAK,
        EXPORT_LOAD,
        EXPORT_FIRST,
        EXPORT_IMPORT,
        EXPORT_SECOND,
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

    // reload leak test
    size_t m_baseline_resource_count = 0;
    std::multiset<std::string> m_baseline_paths;
    int m_reload_iteration = 0;

    // export round trips
    std::string m_tmp_dir;
    std::vector<RoundTrip> m_round_trips;
    size_t m_round_trip = 0;
    Async::Future<SkinArchive::ExportResult> m_export;
    Async::Future<SkinArchive::ExportResult> m_export_twin;  // (the fixture's export, once more at the same time)
    std::string m_first_export_path;
    ArchiveFiles m_first_export;    // without the log
    ArchiveFiles m_fixture_export;  // the plain fixture round trip's, for the legacy settings one
    Look m_original_look;
    Look m_original_default;  // the part of it that comes from the default skin
    bool m_saved_skin_hd = true;
    bool m_saved_skin_hitsounds = true;

    int m_passes = 0;
    int m_failures = 0;
};

}  // namespace Mc::Tests
