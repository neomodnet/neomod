// shared between DiffCalcTool.cpp (single-map + batch) and DiffCalcSuite.cpp (test + crosscheck)
#pragma once

#if __has_include("config.h")
#include "config.h"
#endif

#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"
#include "ModFlags.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neomod::DiffCalcTool {

// usage strings shared between the entrypoint and the subcommand arg errors (the main binary
// reaches all of this through "neomod -diffcalc ...")
#ifdef BUILD_TOOLS_ONLY
inline constexpr std::string_view USAGE_PREFIX = "";
#else
inline constexpr std::string_view USAGE_PREFIX = "-diffcalc ";
#endif
inline constexpr std::string_view USAGE_TEST = "test [--suite <dir>] [--record] [--tolerance <rel>]";
inline constexpr std::string_view USAGE_CROSSCHECK = "crosscheck [--suite <dir>]";

// default fixture/golden location, relative to the repo root
inline constexpr std::string_view DEFAULT_SUITE_DIR = "tools/diffcalc/tests";

std::string modsStringFromMods(ModFlags mods, float speed);

// inverse of modsStringFromMods, keep the two in sync
std::pair<ModFlags, float> modStringToModFlag(std::string_view CSVs);

// everything computed for one (map, mods, speed) run, so output formatting is separate from calculation
struct OneMapResult {
    enum class ErrorStage : uint8_t { NONE, READ_FILE, PRIMITIVES, DIFFOBJECTS };

    std::string map;  // identity, path as given on input
    ModFlags modFlags{};
    float speedMultiplier{1.f};

    ErrorStage errorStage{ErrorStage::NONE};
    std::string error;

    // [Difficulty] settings as parsed into the PRIMITIVE_CONTAINER (incl. the AR = OD fallback)
    float AR{5.f}, CS{5.f}, OD{5.f}, HP{5.f};
    int version{14};
    float stackLeniency{.7f};
    float sliderMultiplier{1.f};
    float sliderTickRate{1.f};
    uint32_t numCircles{}, numSliders{}, numSpinners{}, numObjects{};
    uint32_t maxCombo{}, maxComboAtMid{};
    uint32_t playableLength{}, breakDuration{};

    double totalStars{};
    DiffCalc::DifficultyAttributes attrs{};
    DiffCalc::RawDifficultyValues raw{};
    std::vector<double> aimStrains{}, speedStrains{};

    double ppSS{}, ppImperfect{}, ppLowAcc{}, ppMcosuImperfect{};
    DiffCalc::PPv2CalcParams ssParams{};  // post-calculatePPv2 (resolved -1 sentinels, adjusted AR/OD)

    [[nodiscard]] bool failed() const { return errorStage != ErrorStage::NONE; }
};

// reads and parses the .osu file; on failure returns the failed stage and fills error
OneMapResult::ErrorStage loadPrimitivesFromPath(const std::string &path, DatabaseBeatmap::PRIMITIVE_CONTAINER &out,
                                                std::string &error);

// star calc + pp for one already-loaded map with one (mods, speed) config. the container can be
// reused across configs (slider times are only computed once), same as the game's mod sweeps.
OneMapResult computeOneConfig(DatabaseBeatmap::PRIMITIVE_CONTAINER &primitives, const std::string &mapIdentity,
                              ModFlags modFlags, float speedMultiplier);

OneMapResult computeOneMap(const std::string &osuFilePath, ModFlags modFlags, float speedMultiplier);

std::string writeJsonLine(const OneMapResult &r, bool dumpStrains);

// exhaustive per-field serializers (compile error when the structs gain/lose fields), also used
// by the crosscheck mode for bit-exact comparisons with field-level diff messages
std::string jsonDifficultyAttributes(const DiffCalc::DifficultyAttributes &attrs);
std::string jsonRawDifficultyValues(const DiffCalc::RawDifficultyValues &raw);

struct BatchConfig {
    ModFlags flags{};
    float speed{1.f};
};

// all config lines for one map, in config order. the primitives container is loaded once and
// reused for every config. identity is what ends up in the "map" field (batch passes the input
// path, the test suite passes the bare filename so goldens are location independent).
std::string processMapForBatch(const std::string &path, const std::string &identity,
                               const std::vector<BatchConfig> &configs);

// "test" subcommand (DiffCalcSuite.cpp): fixture maps x fixed config matrix vs golden jsonl files
int runSuiteTest(const std::vector<std::string> &argv);

// "crosscheck" subcommand (DiffCalcSuite.cpp): strain-reuse/partial-calc self-consistency checks
// that one-shot runs never exercise
int runCrosscheck(const std::vector<std::string> &argv);

}  // namespace neomod::DiffCalcTool
