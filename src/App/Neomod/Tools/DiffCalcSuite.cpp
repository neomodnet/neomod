// golden-value test suite for the standalone diffcalc tool ("test" subcommand)
#if __has_include("config.h")
#include "config.h"
#endif

#include "DiffCalcToolShared.h"
#include "ModFlags.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace neomod;
using namespace neomod::DiffCalcTool;

namespace {  // static

// fixed config matrix for the golden suite. covers every star-calc-relevant flag (HD/RX/AP/TD),
// the pp-only mods (HR/EZ/FL and combinations), dt/ht rates and a non-standard rate.
// changing this table invalidates the goldens, re-record and review the diff.
struct TestConfig {
    std::string_view mods;  // modStringToModFlag CSV form, empty = nomod
    float speed;
};

constexpr auto TEST_MATRIX = std::to_array<TestConfig>({
    {.mods = "", .speed = 1.0f},
    {.mods = "HD", .speed = 1.0f},
    {.mods = "HR", .speed = 1.0f},
    {.mods = "EZ", .speed = 1.0f},
    {.mods = "", .speed = 1.5f},
    {.mods = "", .speed = 0.75f},
    {.mods = "HD,HR", .speed = 1.0f},
    {.mods = "HD", .speed = 1.5f},
    {.mods = "RX", .speed = 1.0f},
    {.mods = "AP", .speed = 1.0f},
    {.mods = "TD", .speed = 1.0f},
    {.mods = "FL", .speed = 1.0f},
    {.mods = "HD,FL", .speed = 1.0f},
    {.mods = "", .speed = 1.25f},
});

std::string configLabel(const TestConfig &cfg) {
    return std::format("{}@{}", cfg.mods.empty() ? "NM" : cfg.mods, cfg.speed);
}

// sorted .osu filenames in <suite>/maps; a set ec means the directory walk itself failed
std::vector<std::string> listFixtures(const std::filesystem::path &mapsDir, std::error_code &ec) {
    namespace fs = std::filesystem;
    std::vector<std::string> fixtures;
    for(auto it = fs::directory_iterator(mapsDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if(!it->is_regular_file(ec) || ec) continue;
        std::string name = it->path().filename().string();
        if(name.ends_with(".osu")) fixtures.push_back(std::move(name));
    }
    std::ranges::sort(fixtures);
    return fixtures;
}

// flattens one json line into (dotted key, raw value token) pairs in writer order. only handles
// what writeJsonLine emits into goldens: nested objects and scalar values, no arrays.
bool parseFlatJson(std::string_view line, std::vector<std::pair<std::string, std::string>> &out) {
    size_t pos = 0;
    std::vector<std::string> prefixStack;
    std::string prefix;
    auto rebuildPrefix = [&prefixStack, &prefix]() {
        prefix.clear();
        for(const auto &p : prefixStack) {
            prefix += p;
            prefix += '.';
        }
    };

    if(pos >= line.size() || line[pos] != '{') return false;
    pos++;
    while(pos < line.size()) {
        if(line[pos] == '}') {
            pos++;
            if(prefixStack.empty()) return pos == line.size();
            prefixStack.pop_back();
            rebuildPrefix();
            if(pos < line.size() && line[pos] == ',') pos++;
            continue;
        }

        if(line[pos] != '"') return false;
        const size_t keyStart = ++pos;
        while(pos < line.size() && line[pos] != '"') pos++;  // keys never contain escapes
        if(pos >= line.size()) return false;
        std::string key{line.substr(keyStart, pos - keyStart)};
        pos++;
        if(pos >= line.size() || line[pos] != ':') return false;
        pos++;
        if(pos >= line.size()) return false;

        if(line[pos] == '{') {
            pos++;
            prefixStack.push_back(std::move(key));
            rebuildPrefix();
            continue;
        }

        const size_t valStart = pos;
        if(line[pos] == '"') {
            pos++;
            while(pos < line.size() && line[pos] != '"') pos += (line[pos] == '\\') ? 2 : 1;
            if(pos >= line.size()) return false;
            pos++;
        } else if(line[pos] == '[') {
            return false;  // arrays never appear in golden lines
        } else {
            while(pos < line.size() && line[pos] != ',' && line[pos] != '}') pos++;
        }
        out.emplace_back(prefix + key, std::string{line.substr(valStart, pos - valStart)});
        if(pos < line.size() && line[pos] == ',') pos++;
    }
    return false;  // ran off the end without closing the root object
}

// relTol <= 0 means exact. strings, bools and integer-form numbers always compare exactly,
// float-form numbers use a relative tolerance with a small absolute floor.
bool valuesEqual(const std::string &a, const std::string &b, double relTol) {
    if(a == b) return true;
    if(relTol <= 0.0) return false;
    if(a.starts_with('"') || b.starts_with('"') || a == "true" || a == "false" || b == "true" || b == "false")
        return false;
    // "-0" only ever comes from float formatting, let it take the float path (vs an integer "0")
    auto isIntForm = [](const std::string &v) { return v != "-0" && v.find_first_of(".eE") == std::string::npos; };
    if(isIntForm(a) && isIntForm(b)) return false;
    double da = 0.0;
    double db = 0.0;
    auto ra = std::from_chars(a.data(), a.data() + a.size(), da);
    auto rb = std::from_chars(b.data(), b.data() + b.size(), db);
    if(ra.ec != std::errc() || rb.ec != std::errc()) return false;
    return std::fabs(da - db) <= std::max(relTol * std::max(std::fabs(da), std::fabs(db)), 1e-12);
}

struct LineDiff {
    std::string field, expected, got;
};

std::string truncated(const std::string &s) { return s.length() > 60 ? s.substr(0, 57) + "..." : s; }

std::optional<LineDiff> compareLine(const std::string &goldenLine, const std::string &actualLine, double relTol) {
    if(goldenLine == actualLine) return std::nullopt;

    std::vector<std::pair<std::string, std::string>> golden;
    std::vector<std::pair<std::string, std::string>> actual;
    if(!parseFlatJson(goldenLine, golden) || !parseFlatJson(actualLine, actual))
        return LineDiff{.field = "<unparseable>", .expected = truncated(goldenLine), .got = truncated(actualLine)};
    if(golden.size() != actual.size())
        return LineDiff{.field = "<field count>",
                        .expected = std::format("{}", golden.size()),
                        .got = std::format("{}", actual.size())};
    for(size_t i = 0; i < golden.size(); i++) {
        if(golden[i].first != actual[i].first)
            return LineDiff{.field = "<key order>", .expected = golden[i].first, .got = actual[i].first};
        if(!valuesEqual(golden[i].second, actual[i].second, relTol))
            return LineDiff{.field = golden[i].first, .expected = golden[i].second, .got = actual[i].second};
    }
    if(relTol <= 0.0)  // fields compare equal but bytes differ, should not happen with our writer
        return LineDiff{.field = "<line>", .expected = truncated(goldenLine), .got = truncated(actualLine)};
    return std::nullopt;
}

std::vector<std::string> splitLines(const std::string &block) {
    std::vector<std::string> lines;
    size_t start = 0;
    while(start < block.size()) {
        size_t end = block.find('\n', start);
        if(end == std::string::npos) end = block.size();
        lines.emplace_back(block.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// crosscheck helpers: one full star calc captured for bit-exact comparison. attrs/raw reuse the
// exhaustive json serializers so a new field can never silently escape the comparison.
struct CalcSnapshot {
    double stars{};
    DiffCalc::DifficultyAttributes attrs{};
    DiffCalc::RawDifficultyValues raw{};
    std::vector<double> aimStrains{}, speedStrains{};
};

struct CrosscheckSetup {
    float AR{5.f}, CS{5.f}, OD{5.f}, HP{5.f};
    ModFlags modFlags{};
    float speedMultiplier{1.f};
    float odOverride{-1.f};         // < 0 = use OD
    bool autopilotOverride{false};  // xor'd onto the mod flag
};

CalcSnapshot calcOnce(DatabaseBeatmap::LOAD_DIFFOBJ_RESULT &loaded, const CrosscheckSetup &setup, int upToObjectIndex,
                      DiffCalc::StrainComputeState *strainState) {
    CalcSnapshot snap{};
    const bool autopilot = flags::has<ModFlags::Autopilot>(setup.modFlags) != setup.autopilotOverride;
    DiffCalc::BeatmapDiffcalcData diffcalcData{.sortedHitObjects = loaded.diffobjects,
                                               .CS = setup.CS,
                                               .HP = setup.HP,
                                               .AR = setup.AR,
                                               .OD = setup.odOverride < 0.f ? setup.OD : setup.odOverride,
                                               .hidden = flags::has<ModFlags::Hidden>(setup.modFlags),
                                               .relax = flags::has<ModFlags::Relax>(setup.modFlags),
                                               .autopilot = autopilot,
                                               .touchDevice = flags::has<ModFlags::TouchDevice>(setup.modFlags),
                                               .flashlight = flags::has<ModFlags::Flashlight>(setup.modFlags),
                                               .speedMultiplier = setup.speedMultiplier,
                                               .breakDuration = loaded.totalBreakDuration,
                                               .playableLength = loaded.playableLength};
    DiffCalc::StarCalcParams starParams{
        .outAttributes = snap.attrs,
        .beatmapData = diffcalcData,
        .outAimStrains = &snap.aimStrains,
        .outSpeedStrains = &snap.speedStrains,
        .upToObjectIndex = upToObjectIndex,
        .cancelCheck = {},
        .outRawDifficulty = &snap.raw,
        .strainState = strainState,
    };
    snap.stars = DiffCalc::calculateStarDiffForHitObjects(starParams);
    return snap;
}

std::optional<LineDiff> compareSnapshots(const CalcSnapshot &expected, const CalcSnapshot &got) {
    if(expected.stars != got.stars)
        return LineDiff{
            .field = "stars", .expected = std::format("{}", expected.stars), .got = std::format("{}", got.stars)};

    // bit-exact via the shortest-round-trip serializers, field-level message via the flat parser
    const std::string expectedAttrs = jsonDifficultyAttributes(expected.attrs);
    const std::string gotAttrs = jsonDifficultyAttributes(got.attrs);
    if(expectedAttrs != gotAttrs) {
        auto diff = compareLine(expectedAttrs, gotAttrs, 0.0);
        if(diff.has_value())
            return LineDiff{.field = "attrs." + diff->field, .expected = diff->expected, .got = diff->got};
    }
    const std::string expectedRaw = jsonRawDifficultyValues(expected.raw);
    const std::string gotRaw = jsonRawDifficultyValues(got.raw);
    if(expectedRaw != gotRaw) {
        auto diff = compareLine(expectedRaw, gotRaw, 0.0);
        if(diff.has_value())
            return LineDiff{.field = "raw." + diff->field, .expected = diff->expected, .got = diff->got};
    }

    auto compareStrains = [](std::string_view name, const std::vector<double> &e,
                             const std::vector<double> &g) -> std::optional<LineDiff> {
        if(e.size() != g.size())
            return LineDiff{.field = std::format("{}.count", name),
                            .expected = std::format("{}", e.size()),
                            .got = std::format("{}", g.size())};
        for(size_t i = 0; i < e.size(); i++) {
            if(e[i] != g[i])
                return LineDiff{.field = std::format("{}[{}]", name, i),
                                .expected = std::format("{}", e[i]),
                                .got = std::format("{}", g[i])};
        }
        return std::nullopt;
    };
    if(auto diff = compareStrains("aimStrains", expected.aimStrains, got.aimStrains)) return diff;
    if(auto diff = compareStrains("speedStrains", expected.speedStrains, got.speedStrains)) return diff;
    return std::nullopt;
}

}  // namespace

namespace neomod::DiffCalcTool {

int runSuiteTest(const std::vector<std::string> &argv) {
    namespace fs = std::filesystem;

    std::string suiteDir{DEFAULT_SUITE_DIR};
    bool record = false;
    double tolerance = 0.0;  // exact

    auto argError = [&argv](std::string_view message) {
        std::cerr << "error: " << message << '\n';
        std::cerr << "usage: " << argv[0] << ' ' << USAGE_PREFIX << USAGE_TEST << '\n';
        return 1;
    };

    for(size_t i = 3; i < argv.size(); i++) {
        const std::string &arg = argv[i];
        const bool hasValue = (i + 1) < argv.size();
        if(arg == "--suite" && hasValue) {
            suiteDir = argv[++i];
        } else if(arg == "--record") {
            record = true;
        } else if(arg == "--tolerance" && hasValue) {
            const std::string &token = argv[++i];
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), tolerance);
            if(ec != std::errc() || ptr != token.data() + token.size() || tolerance <= 0.0)
                return argError(std::format("invalid tolerance '{}'", token));
        } else {
            return argError(std::format("unknown argument '{}'", arg));
        }
    }
    if(record && tolerance > 0.0) return argError("--record and --tolerance are mutually exclusive");

    const fs::path mapsDir = fs::path(suiteDir) / "maps";
    const fs::path goldenDir = fs::path(suiteDir) / "golden";

    std::error_code ec;
    const std::vector<std::string> fixtures = listFixtures(mapsDir, ec);
    if(ec) return argError(std::format("could not read fixtures from '{}': {}", mapsDir.string(), ec.message()));
    if(fixtures.empty()) return argError(std::format("no fixture maps in '{}'", mapsDir.string()));

    std::vector<BatchConfig> configs;
    configs.reserve(TEST_MATRIX.size());
    for(const auto &cfg : TEST_MATRIX)
        configs.push_back({.flags = modStringToModFlag(cfg.mods).first, .speed = cfg.speed});

    if(record) {
        fs::create_directories(goldenDir, ec);
        if(ec) return argError(std::format("could not create '{}': {}", goldenDir.string(), ec.message()));
    }

    int passed = 0;
    int failed = 0;
    size_t recordedLines = 0;
    for(const std::string &fixture : fixtures) {
        const std::string stem = fixture.substr(0, fixture.length() - 4);
        const fs::path goldenFile = goldenDir / (stem + ".jsonl");
        const std::string block = processMapForBatch((mapsDir / fixture).string(), fixture, configs);
        const std::vector<std::string> lines = splitLines(block);

        if(record) {
            std::ofstream out(goldenFile, std::ios::out | std::ios::binary | std::ios::trunc);
            if(!out.good()) return argError(std::format("could not write '{}'", goldenFile.string()));
            out << block;
            recordedLines += lines.size();
            std::cout << "RECORDED " << fixture << " (" << lines.size() << " lines)\n";
            continue;
        }

        std::ifstream goldenIn(goldenFile, std::ios::in | std::ios::binary);
        if(!goldenIn.good()) {
            std::cout << "FAIL " << fixture << ": missing golden file " << goldenFile.string()
                      << " (run with --record to create)\n";
            failed++;
            continue;
        }
        std::vector<std::string> goldenLines;
        for(std::string line; std::getline(goldenIn, line);) {
            if(!line.empty() && line.back() == '\r') line.pop_back();
            goldenLines.push_back(std::move(line));
        }

        if(goldenLines.size() != lines.size()) {
            std::cout << "FAIL " << fixture << ": golden has " << goldenLines.size() << " lines, produced "
                      << lines.size() << " (matrix changed? re-record)\n";
            failed++;
            continue;
        }

        int lineFailures = 0;
        for(size_t i = 0; i < lines.size(); i++) {
            const auto diff = compareLine(goldenLines[i], lines[i], tolerance);
            if(!diff.has_value()) continue;
            lineFailures++;
            if(lineFailures <= 10) {
                std::cout << "FAIL " << fixture << ' ' << configLabel(TEST_MATRIX[i]) << ' ' << diff->field
                          << ": expected " << diff->expected << " got " << diff->got << '\n';
            }
        }
        if(lineFailures > 10)
            std::cout << "     (" << (lineFailures - 10) << " more failing lines in " << fixture << ")\n";

        if(lineFailures == 0) {
            std::cout << "PASS " << fixture << '\n';
            passed++;
        } else {
            failed++;
        }
    }

    // goldens without a matching fixture are stale identities: pruned on record, loud failures
    // on test
    std::vector<fs::path> orphans;
    for(auto it = fs::directory_iterator(goldenDir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if(!it->is_regular_file(ec) || ec) continue;
        const std::string name = it->path().filename().string();
        if(!name.ends_with(".jsonl")) continue;
        if(!std::ranges::binary_search(fixtures, name.substr(0, name.length() - 6) + ".osu"))
            orphans.push_back(it->path());
    }
    for(const fs::path &orphan : orphans) {
        if(record) {
            fs::remove(orphan, ec);
            std::cout << "PRUNED " << orphan.filename().string() << " (no matching fixture map)\n";
        } else {
            std::cout << "FAIL " << orphan.filename().string() << ": golden has no matching fixture map\n";
            failed++;
        }
    }

    if(record) {
        std::cout << "RECORDED " << fixtures.size() << " fixtures (" << recordedLines << " lines)\n";
        return 0;
    }

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}

int runCrosscheck(const std::vector<std::string> &argv) {
    namespace fs = std::filesystem;

    // exercises the reuse paths one-shot runs never hit: (a) same-key strainState skip, (b) key
    // mismatch triggers a recompute over the used vector, (c) growing-prefix partial calcs via
    // upToObjectIndex against fresh truncated calcs, (d) recompute (pass >= 2) stability.
    // ordering matters: on >5000-slider maps pass-1 leaves a couple of slider curves unallocated
    // (deferred McKay alloc), so pass-1 results are only ever compared against pass-1 results.
    constexpr auto CROSSCHECK_CONFIGS = std::to_array<TestConfig>({
        {.mods = "", .speed = 1.0f},
        {.mods = "HD,HR", .speed = 1.5f},
    });

    std::string suiteDir{DEFAULT_SUITE_DIR};

    auto argError = [&argv](std::string_view message) {
        std::cerr << "error: " << message << '\n';
        std::cerr << "usage: " << argv[0] << ' ' << USAGE_PREFIX << USAGE_CROSSCHECK << '\n';
        return 1;
    };

    for(size_t i = 3; i < argv.size(); i++) {
        const std::string &arg = argv[i];
        if(arg == "--suite" && (i + 1) < argv.size()) {
            suiteDir = argv[++i];
        } else {
            return argError(std::format("unknown argument '{}'", arg));
        }
    }

    const fs::path mapsDir = fs::path(suiteDir) / "maps";
    std::error_code ec;
    const std::vector<std::string> fixtures = listFixtures(mapsDir, ec);
    if(ec) return argError(std::format("could not read fixtures from '{}': {}", mapsDir.string(), ec.message()));
    if(fixtures.empty()) return argError(std::format("no fixture maps in '{}'", mapsDir.string()));

    int passed = 0;
    int failed = 0;
    int skipped = 0;
    for(const std::string &fixture : fixtures) {
        DatabaseBeatmap::PRIMITIVE_CONTAINER primitives;
        std::string loadError;
        if(loadPrimitivesFromPath((mapsDir / fixture).string(), primitives, loadError) !=
           OneMapResult::ErrorStage::NONE) {
            std::cout << "SKIP " << fixture << " (" << loadError << ")\n";
            skipped++;
            continue;
        }
        const bool bigMap = primitives.sliders.size() > 5000;

        int fixtureFailures = 0;
        auto check = [&fixture, &fixtureFailures](const std::string &label, std::string_view checkName,
                                                  const CalcSnapshot &expected, const CalcSnapshot &got) {
            const auto diff = compareSnapshots(expected, got);
            if(!diff.has_value()) return;
            fixtureFailures++;
            std::cout << "FAIL " << fixture << ' ' << label << ' ' << checkName << ' ' << diff->field << ": expected "
                      << diff->expected << " got " << diff->got << '\n';
        };

        for(const TestConfig &cfg : CROSSCHECK_CONFIGS) {
            const std::string label = configLabel(cfg);
            const CrosscheckSetup setup{.AR = primitives.AR,
                                        .CS = primitives.CS,
                                        .OD = primitives.OD,
                                        .HP = primitives.HP,
                                        .modFlags = modStringToModFlag(cfg.mods).first,
                                        .speedMultiplier = cfg.speed};
            const bool autopilot = flags::has<ModFlags::Autopilot>(setup.modFlags);

            auto load = [&primitives, &cfg]() {
                return DatabaseBeatmap::loadDifficultyHitObjects(primitives, primitives.AR, primitives.CS, cfg.speed);
            };

            // (a) same-key skip == fresh compute (both pass-1)
            DatabaseBeatmap::LOAD_DIFFOBJ_RESULT reused = load();
            if(reused.error.errc) {
                std::cout << "SKIP " << fixture << ' ' << label << " (" << reused.error.error_string() << ")\n";
                continue;
            }
            // <2 objects (or a lone non-slider) takes the 0-star early return before any state
            // stamping, so the state-key checks below do not apply there
            const bool computeRuns = reused.diffobjects.size() >= 2 ||
                                     (reused.diffobjects.size() == 1 &&
                                      reused.diffobjects[0].type == DiffCalc::DifficultyHitObject::TYPE::SLIDER);
            const CalcSnapshot fresh = calcOnce(reused, setup, -1, &reused.strainState);
            const CalcSnapshot skippedCalc = calcOnce(reused, setup, -1, &reused.strainState);
            check(label, "skip-reuse", fresh, skippedCalc);

            // (c) growing-prefix partial calcs on the reused (still pass-1) vector against fresh
            // truncated calcs, at uneven strides
            const int n = static_cast<int>(reused.diffobjects.size());
            std::vector<int> prefixes = {0, 1, 2, n / 13, n / 7, n / 3, n / 2, (2 * n) / 3, n - 2, n - 1};
            std::erase_if(prefixes, [n](int k) { return k < 0 || k >= n; });
            std::ranges::sort(prefixes);
            prefixes.erase(std::ranges::unique(prefixes).begin(), prefixes.end());
            for(const int k : prefixes) {
                DatabaseBeatmap::LOAD_DIFFOBJ_RESULT freshLoad = load();
                const CalcSnapshot truncatedFresh = calcOnce(freshLoad, setup, k, &freshLoad.strainState);
                const CalcSnapshot truncatedReused = calcOnce(reused, setup, k, &reused.strainState);
                check(label, std::format("prefix@{}", k), truncatedFresh, truncatedReused);
            }

            // (b) od key mismatch forces a recompute over the used vector; reference is a fresh
            // vector recomputed the same number of times (both sides pass-2)
            CrosscheckSetup odSetup = setup;
            odSetup.odOverride = setup.OD < 9.f ? setup.OD + 1.f : setup.OD - 1.f;
            const CalcSnapshot odRecomputed = calcOnce(reused, odSetup, -1, &reused.strainState);
            DatabaseBeatmap::LOAD_DIFFOBJ_RESULT reference = load();
            (void)calcOnce(reference, odSetup, -1, &reference.strainState);
            const CalcSnapshot odReference = calcOnce(reference, odSetup, -1, nullptr);
            check(label, "od-recompute", odReference, odRecomputed);
            if(computeRuns && !reused.strainState.matches(setup.CS, odSetup.odOverride, setup.AR, cfg.speed,
                                                          flags::has<ModFlags::Relax>(setup.modFlags), autopilot,
                                                          flags::has<ModFlags::TouchDevice>(setup.modFlags),
                                                          flags::has<ModFlags::Flashlight>(setup.modFlags))) {
                fixtureFailures++;
                std::cout << "FAIL " << fixture << ' ' << label
                          << " state-key: strainState does not record the recomputed od\n";
            }

            // (b2) autopilot key mismatch, same idea (both sides pass >= 2)
            CrosscheckSetup apSetup = odSetup;
            apSetup.autopilotOverride = true;
            const CalcSnapshot apRecomputed = calcOnce(reused, apSetup, -1, &reused.strainState);
            const CalcSnapshot apReference = calcOnce(reference, apSetup, -1, nullptr);
            check(label, "ap-recompute", apReference, apRecomputed);
            if(computeRuns && !reused.strainState.matches(setup.CS, odSetup.odOverride, setup.AR, cfg.speed,
                                                          flags::has<ModFlags::Relax>(setup.modFlags), !autopilot,
                                                          flags::has<ModFlags::TouchDevice>(setup.modFlags),
                                                          flags::has<ModFlags::Flashlight>(setup.modFlags))) {
                fixtureFailures++;
                std::cout << "FAIL " << fixture << ' ' << label
                          << " state-key: strainState does not record the recomputed autopilot\n";
            }

            // (d) recompute stability: pass-2 == pass-3 always; pass-1 == pass-2 only when all
            // slider curves were allocated up front (<= 5000 sliders)
            DatabaseBeatmap::LOAD_DIFFOBJ_RESULT stability = load();
            const CalcSnapshot pass1 = calcOnce(stability, setup, -1, &stability.strainState);
            const CalcSnapshot pass2 = calcOnce(stability, setup, -1, nullptr);
            const CalcSnapshot pass3 = calcOnce(stability, setup, -1, nullptr);
            check(label, "pass-stability", pass2, pass3);
            if(!bigMap) {
                check(label, "pass1-vs-pass2", pass1, pass2);
            } else {
                const bool differs = compareSnapshots(pass1, pass2).has_value();
                std::cout << "INFO " << fixture << ' ' << label << ": pass-1 "
                          << (differs ? "!= pass-2 (expected, >5000 sliders defer curve allocs)"
                                      : "== pass-2 (deferred curve alloc did not shift values here)")
                          << '\n';
            }
        }

        if(fixtureFailures == 0) {
            std::cout << "PASS " << fixture << '\n';
            passed++;
        } else {
            failed++;
        }
    }

    std::cout << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
    return failed > 0 ? 1 : 0;
}

}  // namespace neomod::DiffCalcTool
