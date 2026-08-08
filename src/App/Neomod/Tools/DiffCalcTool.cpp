// standalone PP/SR calculator for .osu files
#if __has_include("config.h")
#include "config.h"
#endif

#ifndef BUILD_TOOLS_ONLY
#include "DiffCalcTool.h"
#endif

#include "DatabaseBeatmap.h"
#include "DiffCalcToolShared.h"
#include "DifficultyCalculator.h"
#include "ModFlags.h"
#include "SString.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <utility>

#if defined(__SSE__) || (defined(_M_IX86_FP) && (_M_IX86_FP > 0))
// check if x86 for sse include
#include <xmmintrin.h>
#endif

#include <cfloat>  // for _controlfp

using namespace neomod;
using namespace neomod::DiffCalcTool;

namespace {  // static

// copied from src/Engine/Thread.cpp, use the same FPU register tweaks as the main neomod build
void initFPUFlags() {
#ifdef _MCW_DN
    // flush denormals
    _controlfp(_DN_FLUSH, _MCW_DN);
#endif

#if defined(__SSE__) || (defined(_M_IX86_FP) && (_M_IX86_FP > 0))
    // denorm clear to zero (CTZ) and denorms are zero (DAZ) for x86 sse
    _mm_setcsr(_mm_getcsr() | 0x8040);
#elif !(defined(_MSC_VER) && !defined(__clang__))  // idk how to do this with msvc and no inline assembly
// flush-to-zero for arm
// arm32
#if defined(__arm__) || defined(_ARM_)
    __asm__ __volatile__("vmsr fpscr,%0" ::"r"(1 << 24));
#endif
// arm64
#if defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__)
    uint64_t fpcr;  // NOLINT
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);  // FZ
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif

#endif  // !(defined(_MSC_VER) && !defined(__clang__))
}

struct LiteFile {
    std::ifstream m_ifstream;
    size_t m_filesize{};

    LiteFile(const std::string &path) : m_ifstream() {
        m_ifstream.open(path, std::ios::in | std::ios::binary);
        if(canRead()) {
            std::streampos fsize = 0;
            fsize = m_ifstream.tellg();
            m_ifstream.seekg(0, std::ios::end);
            fsize = m_ifstream.tellg() - fsize;
            m_ifstream.seekg(0, std::ios::beg);
            m_filesize = fsize;
        }
    }

    [[nodiscard]] bool canRead() const { return m_ifstream.good(); }
    [[nodiscard]] size_t getFileSize() const { return m_filesize; }

    void readToVector(std::vector<uint8_t> &out) {
        if(!canRead()) {
            out.clear();
            return;
        }
        out.resize(m_filesize);
        m_ifstream.seekg(0, std::ios::beg);
        if(m_ifstream.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(m_filesize)).good()) {
            return;
        }
        out.clear();
        return;
    }
};

// json helpers: shortest round-trip float formatting (std::to_chars underneath, string equality
// implies bit equality), nan/inf as quoted strings to stay valid json
std::string jnum(double v) {
    if(!std::isfinite(v)) return std::format("\"{}\"", v);
    return std::format("{}", v);
}

std::string jnum(float v) {
    if(!std::isfinite(v)) return std::format("\"{}\"", v);
    return std::format("{}", v);
}

std::string jstr(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for(const char c : s) {
        switch(c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if(static_cast<unsigned char>(c) < 0x20)
                    out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
                else
                    out += c;
        }
    }
    out += '"';
    return out;
}

std::string jsonPPv2CalcParams(const DiffCalc::PPv2CalcParams &pars) {
    // exhaustive, same idea as jsonDifficultyAttributes
    const auto &[attributes, modFlags, timescale, ar, od, numHitObjects, numCircles, numSliders, numSpinners,
                 maxPossibleCombo, combo, misses, c300, c100, c50, legacyTotalScore, isMcOsuImported] = pars;
    return std::format(
        R"({{"attrs":{},"modFlags":"{:#x}","timescale":{},"ar":{},"od":{},"numHitObjects":{},)"
        R"("numCircles":{},"numSliders":{},"numSpinners":{},"maxPossibleCombo":{},"combo":{},"misses":{},)"
        R"("c300":{},"c100":{},"c50":{},"legacyTotalScore":{},"isMcOsuImported":{}}})",
        jsonDifficultyAttributes(attributes), static_cast<uint64_t>(modFlags), jnum(timescale), jnum(ar), jnum(od),
        numHitObjects, numCircles, numSliders, numSpinners, maxPossibleCombo, combo, misses, c300, c100, c50,
        legacyTotalScore, isMcOsuImported);
}

// output results (with round-trip float precision, for exact comparisons between builds)
void printHumanText(const OneMapResult &r) {
    switch(r.errorStage) {
        case OneMapResult::ErrorStage::READ_FILE:
            std::cerr << "error: could not read file " << r.map << '\n';
            return;
        case OneMapResult::ErrorStage::PRIMITIVES:
            std::cerr << "error loading beatmap primitives: " << r.error << '\n';
            return;
        case OneMapResult::ErrorStage::DIFFOBJECTS:
            std::cerr << "error loading difficulty objects: " << r.error << '\n';
            return;
        case OneMapResult::ErrorStage::NONE:
            break;
    }

    std::cout << std::setprecision(17);
    std::cout << "star rating: " << r.totalStars << '\n';
    std::cout << "  aim: " << r.attrs.AimDifficulty << '\n';
    std::cout << "  speed: " << r.attrs.SpeedDifficulty << '\n';
    std::cout << "raw difficulty values:\n";
    std::cout << "  aimNoSliders: " << r.raw.aimNoSliders << '\n';
    std::cout << "  aim: " << r.raw.aim << '\n';
    std::cout << "  speed: " << r.raw.speed << '\n';
    std::cout << "  readingNoHidden: " << r.raw.readingNoHidden << '\n';
    std::cout << "  readingHidden: " << r.raw.readingHidden << '\n';
    std::cout << "  flashlightNoHidden: " << r.raw.flashlightNoHidden << '\n';
    std::cout << "  flashlightHidden: " << r.raw.flashlightHidden << '\n';
    {
        // strain digests (count + order-dependent sum), enough to pin the section strain outputs
        double aimStrainSum = 0.0;
        for(const double s : r.aimStrains) aimStrainSum += s;
        double speedStrainSum = 0.0;
        for(const double s : r.speedStrains) speedStrainSum += s;
        std::cout << "aim strains: " << r.aimStrains.size() << " sum: " << aimStrainSum << '\n';
        std::cout << "speed strains: " << r.speedStrains.size() << " sum: " << speedStrainSum << '\n';
    }
    std::cout << "pp (SS): " << r.ppSS << '\n';
    std::cout << "pp (imperfect): " << r.ppImperfect << '\n';
    std::cout << "attributes (post-SS-calc):\n" << DiffCalc::PPv2CalcParamsToString(r.ssParams) << '\n';
    std::cout << '\n';
    std::cout << "map info:\n";
    std::cout << "  mods: " << modsStringFromMods(r.modFlags, r.speedMultiplier) << '\n';
    std::cout << "  timescale: " << r.speedMultiplier << '\n';
    std::cout << "  AR: " << r.AR << '\n';
    std::cout << "  CS: " << r.CS << '\n';
    std::cout << "  OD: " << r.OD << '\n';
    std::cout << "  HP: " << r.HP << '\n';
    std::cout << "  objects: " << r.numObjects << " (" << r.numCircles << "c + " << r.numSliders << "s + "
              << r.numSpinners << "sp)\n";
    std::cout << "  max combo: " << r.maxCombo << '\n';
    std::cout << "  max combo at middle object: " << r.maxComboAtMid << '\n';
    std::cout << "  playable length: " << r.playableLength << '\n';
    std::cout << "  break duration: " << r.breakDuration << '\n';
}

constexpr std::string_view USAGE =
    "<osu_file> [speed] [[mod flags bitmask (0xHEX)] | [comma separated mods (HR,NF)]] [--json [--dump-strains]]";
constexpr std::string_view USAGE_BATCH =
    "batch (--list <file> | --dir <path> | -) [--mods <combos>] [--speeds <rates>] [--jobs N] [-o <file>]";

// batch mod combos are concatenated 2-letter codes ("HDHR"), "NM" = nomod. rates are not mods
// here (dt/nc/ht are rejected), they belong to --speeds.
std::optional<ModFlags> parseBatchModCombo(std::string_view token) {
    using namespace flags::operators;

    const std::string lower = SString::to_lower(token);
    if(lower == "nm") return ModFlags{};

    ModFlags combined{};
    if(lower.length() < 2 || (lower.length() % 2) != 0) return std::nullopt;
    for(size_t i = 0; i < lower.length(); i += 2) {
        const auto [chunkFlags, chunkSpeed] = modStringToModFlag(std::string_view{lower}.substr(i, 2));
        if(chunkSpeed != 1.0f || chunkFlags == ModFlags{}) return std::nullopt;  // rate mod or unknown code
        combined |= chunkFlags;
    }
    return combined;
}

int runBatch(const std::vector<std::string> &argv) {
    std::string listFile;
    std::string dirPath;
    bool stdinList = false;
    std::string outFile;
    std::vector<std::string> modTokens = {"NM", "HD", "HR", "EZ", "HDHR"};
    std::vector<float> speeds = {0.75f, 1.0f, 1.5f};
    unsigned int jobs = std::thread::hardware_concurrency();

    auto argError = [&argv](std::string_view message) {
        std::cerr << "error: " << message << '\n';
        std::cerr << "usage: " << argv[0] << ' ' << USAGE_PREFIX << USAGE_BATCH << '\n';
        return 1;
    };

    for(size_t i = 3; i < argv.size(); i++) {
        const std::string &arg = argv[i];
        const bool hasValue = (i + 1) < argv.size();
        if(arg == "-") {
            stdinList = true;
        } else if(arg == "--list" && hasValue) {
            listFile = argv[++i];
        } else if(arg == "--dir" && hasValue) {
            dirPath = argv[++i];
        } else if(arg == "-o" && hasValue) {
            outFile = argv[++i];
        } else if(arg == "--mods" && hasValue) {
            modTokens.clear();
            for(const auto token : SString::split(argv[++i], ',')) modTokens.emplace_back(token);
        } else if(arg == "--speeds" && hasValue) {
            speeds.clear();
            for(const auto token : SString::split(argv[++i], ',')) {
                float speed = 0.f;
                auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), speed);
                if(ec != std::errc() || ptr != token.data() + token.size() || speed < 0.01f || speed > 3.f)
                    return argError(std::format("invalid speed '{}' (expected 0.01-3)", token));
                speeds.push_back(speed);
            }
        } else if(arg == "--jobs" && hasValue) {
            const std::string &token = argv[++i];
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), jobs);
            if(ec != std::errc() || ptr != token.data() + token.size() || jobs < 1)
                return argError(std::format("invalid job count '{}'", token));
        } else {
            return argError(std::format("unknown argument '{}'", arg));
        }
    }

    if((static_cast<int>(!listFile.empty()) + static_cast<int>(!dirPath.empty()) + static_cast<int>(stdinList)) != 1)
        return argError("exactly one of --list, --dir or - is required");
    if(modTokens.empty() || speeds.empty()) return argError("--mods and --speeds must not be empty");

    // mods x speeds cross product, mods outer
    std::vector<BatchConfig> configs;
    configs.reserve(modTokens.size() * speeds.size());
    for(const auto &token : modTokens) {
        const auto parsedFlags = parseBatchModCombo(token);
        if(!parsedFlags.has_value())
            return argError(
                std::format("invalid mod combo '{}' (2-letter codes like HDHR, rates go to --speeds)", token));
        for(const float speed : speeds) configs.push_back({.flags = *parsedFlags, .speed = speed});
    }

    std::vector<std::string> paths;
    if(dirPath.empty()) {
        std::ifstream listStream;
        if(!stdinList) {
            listStream.open(listFile, std::ios::in);
            if(!listStream.good()) return argError(std::format("could not read list file '{}'", listFile));
        }
        std::istream &list = stdinList ? std::cin : listStream;
        for(std::string line; std::getline(list, line);) {
            if(!line.empty() && line.back() == '\r') line.pop_back();
            if(!line.empty()) paths.push_back(line);
        }
    } else {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto it = fs::recursive_directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec);
        if(ec) return argError(std::format("could not open directory '{}': {}", dirPath, ec.message()));
        for(; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if(ec) return argError(std::format("directory walk failed: {}", ec.message()));
            if(!it->is_regular_file(ec) || ec) continue;
            std::string path = it->path().string();
            if(path.length() > 4 && SString::to_lower(std::string_view{path}.substr(path.length() - 4)) == ".osu")
                paths.push_back(std::move(path));
        }
        std::ranges::sort(paths);  // byte-wise lexicographic, matches LC_ALL=C sort
    }

    if(paths.empty()) return argError("no maps to process");

    std::ofstream outStream;
    if(!outFile.empty()) {
        // binary so output bytes are identical across platforms
        outStream.open(outFile, std::ios::out | std::ios::binary | std::ios::trunc);
        if(!outStream.good()) return argError(std::format("could not open output file '{}'", outFile));
    }
    std::ostream &out = outFile.empty() ? std::cout : outStream;

    const size_t numMaps = paths.size();
    jobs = std::max(1U, std::min(jobs, static_cast<unsigned int>(numMaps)));

    // each worker takes the next unprocessed map; the main thread emits finished blocks strictly
    // in input order, so the output is byte-identical no matter the job count
    std::vector<std::string> blocks(numMaps);
    std::vector<uint8_t> blockReady(numMaps, 0);
    std::mutex blockMutex;
    std::condition_variable blockReadyCV;
    std::atomic<size_t> nextMapIndex{0};

    std::vector<std::thread> workers;
    workers.reserve(jobs);
    for(unsigned int t = 0; t < jobs; t++) {
        workers.emplace_back(
            [&paths, &configs, &blocks, &blockReady, &blockMutex, &blockReadyCV, &nextMapIndex, numMaps]() {
                initFPUFlags();  // FTZ/DAZ are per-thread state, keep workers identical to single-map runs
                while(true) {
                    const size_t i = nextMapIndex.fetch_add(1);
                    if(i >= numMaps) break;
                    std::string block = processMapForBatch(paths[i], paths[i], configs);
                    {
                        const std::scoped_lock lock(blockMutex);
                        blocks[i] = std::move(block);
                        blockReady[i] = 1;
                    }
                    blockReadyCV.notify_all();
                }
            });
    }

    for(size_t i = 0; i < numMaps; i++) {
        {
            std::unique_lock lock(blockMutex);
            blockReadyCV.wait(lock, [&blockReady, i]() { return blockReady[i] != 0; });
        }
        out << blocks[i];
        std::string().swap(blocks[i]);
        if(((i + 1) % 1000) == 0 || (i + 1) == numMaps)
            std::cerr << "processed " << (i + 1) << "/" << numMaps << " maps\n";
    }

    for(auto &worker : workers) worker.join();
    out.flush();
    return out.good() ? 0 : 1;
}

}  // namespace

namespace neomod::DiffCalcTool {

std::string modsStringFromMods(ModFlags mods, float speed) {
    using enum ModFlags;
    using namespace flags::operators;

    std::string modsString;

    // only for exact values
    const bool nc = speed == 1.5f && flags::has<NoPitchCorrection>(mods);
    const bool dt = speed == 1.5f && !nc;  // only show dt/nc, not both
    const bool ht = speed == 0.75f;

    const bool pf = flags::has<Perfect>(mods);
    const bool sd = !pf && flags::has<SuddenDeath>(mods);

    if(flags::has<NoFail>(mods)) modsString.append("NF,");
    if(flags::has<Easy>(mods)) modsString.append("EZ,");
    if(flags::has<TouchDevice>(mods)) modsString.append("TD,");
    if(flags::has<Hidden>(mods)) modsString.append("HD,");
    if(flags::has<HardRock>(mods)) modsString.append("HR,");
    if(flags::has<FreezeFrame>(mods)) modsString.append("FR,");
    if(flags::has<Traceable>(mods)) modsString.append("TC,");
    if(sd) modsString.append("SD,");
    if(dt) modsString.append("DT,");
    if(nc) modsString.append("NC,");
    if(flags::has<DKS>(mods)) modsString.append("DKS,");
    if(flags::has<Relax>(mods)) modsString.append("Relax,");
    if(ht) modsString.append("HT,");
    if(flags::has<Flashlight>(mods)) modsString.append("FL,");
    if(flags::has<SpunOut>(mods)) modsString.append("SO,");
    if(flags::has<Autopilot>(mods)) modsString.append("AP,");
    if(pf) modsString.append("PF,");
    if(flags::has<ScoreV2>(mods)) modsString.append("v2,");
    if(flags::has<Target>(mods)) modsString.append("Target,");
    if(flags::any<MirrorHorizontal | MirrorVertical>(mods)) modsString.append("Mirror,");
    if(flags::has<FPoSu>(mods)) modsString.append("FPoSu,");
    if(flags::has<Singletap>(mods)) modsString.append("1K,");
    if(flags::has<NoKeylock>(mods)) modsString.append("4K,");

    if(modsString.length() > 0) modsString.pop_back();  // remove trailing comma

    return modsString;
}

// inverse of modsStringFromMods, keep the two in sync
std::pair<ModFlags, float> modStringToModFlag(std::string_view CSVs) {
    using enum ModFlags;
    using namespace flags::operators;

    ModFlags retFlags = None;
    float retSpeed = 1.0f;
    for(const auto modString : SString::split(SString::to_lower(CSVs), ',')) {
        if(modString == "nf") {
            retFlags |= NoFail;
        } else if(modString == "ez") {
            retFlags |= Easy;
        } else if(modString == "td") {
            retFlags |= TouchDevice;
        } else if(modString == "hd") {
            retFlags |= Hidden;
        } else if(modString == "hr") {
            retFlags |= HardRock;
        } else if(modString == "fr") {
            retFlags |= FreezeFrame;
        } else if(modString == "tc") {
            retFlags |= Traceable;
        } else if(modString == "sd") {
            retFlags |= SuddenDeath;
        } else if(modString == "dt") {
            retSpeed = 1.5f;
        } else if(modString == "nc") {
            retFlags |= NoPitchCorrection;
            retSpeed = 1.5f;  // nightcore is doubletime + pitch
        } else if(modString == "dks") {
            retFlags |= DKS;
        } else if(modString == "relax" || modString == "rx") {
            retFlags |= Relax;
        } else if(modString == "ht") {
            retSpeed = 0.75f;
        } else if(modString == "fl") {
            retFlags |= Flashlight;
        } else if(modString == "so") {
            retFlags |= SpunOut;
        } else if(modString == "ap") {
            retFlags |= Autopilot;
        } else if(modString == "pf") {
            retFlags |= Perfect;
        } else if(modString == "v2") {
            retFlags |= ScoreV2;
        } else if(modString == "target") {
            retFlags |= Target;
        } else if(modString == "mirror") {
            retFlags |= MirrorHorizontal;  // modsStringFromMods prints "Mirror" for either axis
        } else if(modString == "fposu") {
            retFlags |= FPoSu;
        } else if(modString == "1k") {
            retFlags |= Singletap;
        } else if(modString == "4k") {
            retFlags |= NoKeylock;
        }
    }
    return {retFlags, retSpeed};
}

OneMapResult::ErrorStage loadPrimitivesFromPath(const std::string &path, DatabaseBeatmap::PRIMITIVE_CONTAINER &out,
                                                std::string &error) {
    std::vector<uint8_t> fileBuffer;
    {
        LiteFile file(path);
        if(!file.canRead() || (file.getFileSize() == 0)) {
            error = "could not read file";
            return OneMapResult::ErrorStage::READ_FILE;
        }
        file.readToVector(fileBuffer);
        // don't need to keep the file open anymore
    }

    out = DatabaseBeatmap::loadPrimitiveObjectsFromData(fileBuffer, path);
    if(out.error.errc) {
        error = out.error.error_string();
        return OneMapResult::ErrorStage::PRIMITIVES;
    }
    return OneMapResult::ErrorStage::NONE;
}

// star calc + pp for one already-loaded map with one (mods, speed) config. the container can be
// reused across configs (slider times are only computed once), same as the game's mod sweeps.
OneMapResult computeOneConfig(DatabaseBeatmap::PRIMITIVE_CONTAINER &primitives, const std::string &mapIdentity,
                              ModFlags modFlags, float speedMultiplier) {
    OneMapResult r{};
    r.map = mapIdentity;
    r.modFlags = modFlags;
    r.speedMultiplier = speedMultiplier;
    r.AR = primitives.AR;
    r.CS = primitives.CS;
    r.OD = primitives.OD;
    r.HP = primitives.HP;

    r.version = primitives.version;
    r.stackLeniency = primitives.stackLeniency;
    r.sliderMultiplier = primitives.sliderMultiplier;
    r.sliderTickRate = primitives.sliderTickRate;
    r.numCircles = static_cast<uint32_t>(primitives.hitcircles.size());
    r.numSliders = static_cast<uint32_t>(primitives.sliders.size());
    r.numSpinners = static_cast<uint32_t>(primitives.spinners.size());
    r.numObjects = primitives.getNumObjects();

    // load difficulty hitobjects for star calculation
    DatabaseBeatmap::LOAD_DIFFOBJ_RESULT diffResult =
        DatabaseBeatmap::loadDifficultyHitObjects(primitives, r.AR, r.CS, speedMultiplier);

    if(diffResult.error.errc) {
        r.errorStage = OneMapResult::ErrorStage::DIFFOBJECTS;
        r.error = diffResult.error.error_string();
        return r;
    }

    r.maxCombo = diffResult.getTotalMaxCombo();
    r.maxComboAtMid = diffResult.getMaxComboAtIndex(primitives.getNumObjects() / 2);
    r.playableLength = diffResult.playableLength;
    r.breakDuration = diffResult.totalBreakDuration;

    // calculate star rating
    DiffCalc::BeatmapDiffcalcData diffcalcData{.sortedHitObjects = diffResult.diffobjects,
                                               .CS = r.CS,
                                               .HP = r.HP,
                                               .AR = r.AR,
                                               .OD = r.OD,
                                               .hidden = flags::has<ModFlags::Hidden>(modFlags),
                                               .relax = flags::has<ModFlags::Relax>(modFlags),
                                               .autopilot = flags::has<ModFlags::Autopilot>(modFlags),
                                               .touchDevice = flags::has<ModFlags::TouchDevice>(modFlags),
                                               .flashlight = flags::has<ModFlags::Flashlight>(modFlags),
                                               .speedMultiplier = speedMultiplier,
                                               .breakDuration = diffResult.totalBreakDuration,
                                               .playableLength = diffResult.playableLength};

    DiffCalc::StarCalcParams starParams{
        .outAttributes = r.attrs,
        .beatmapData = diffcalcData,
        .outAimStrains = &r.aimStrains,
        .outSpeedStrains = &r.speedStrains,
        .upToObjectIndex = -1,
        .cancelCheck = {},
        .outRawDifficulty = &r.raw,
        .strainState = &diffResult.strainState,
    };

    r.totalStars = DiffCalc::calculateStarDiffForHitObjects(starParams);

    // pp for a fixed set of score states. calculatePPv2 resolves the -1 sentinels and adjusts
    // AR/OD in place, so every state gets its own copy taken before any of them runs.
    const int numHitObjects = static_cast<int>(primitives.getNumObjects());
    const int maxCombo = static_cast<int>(diffResult.getTotalMaxCombo());

    // SS play
    DiffCalc::PPv2CalcParams ssParams{.attributes = r.attrs,
                                      .modFlags = modFlags,
                                      .timescale = speedMultiplier,
                                      .ar = r.AR,
                                      .od = r.OD,
                                      .numHitObjects = numHitObjects,
                                      .numCircles = static_cast<int>(primitives.hitcircles.size()),
                                      .numSliders = static_cast<int>(primitives.sliders.size()),
                                      .numSpinners = static_cast<int>(primitives.spinners.size()),
                                      .maxPossibleCombo = maxCombo,
                                      .combo = -1,
                                      .misses = 0,
                                      .c300 = -1,
                                      .c100 = 0,
                                      .c50 = 0,
                                      .legacyTotalScore = 0,
                                      .isMcOsuImported = false};

    // a fixed imperfect play (combo drop + misses/100s/50s + legacy score), to also exercise the
    // miss penalty, estimated sliderbreak and scorev1 misscount estimation paths
    DiffCalc::PPv2CalcParams imperfectParams = ssParams;
    imperfectParams.combo = (maxCombo * 9) / 10;
    imperfectParams.misses = numHitObjects / 50;
    imperfectParams.c100 = numHitObjects / 20;
    imperfectParams.c50 = numHitObjects / 100;
    imperfectParams.legacyTotalScore = r.attrs.MaximumLegacyComboScore / 2;

    // a low accuracy play without legacy score, for the combo-based misscount estimation path
    DiffCalc::PPv2CalcParams lowAccParams = ssParams;
    lowAccParams.combo = maxCombo / 3;
    lowAccParams.misses = numHitObjects / 10;
    lowAccParams.c100 = numHitObjects / 5;
    lowAccParams.c50 = numHitObjects / 25;

    // and the imperfect play again as an mcosu-imported score, for the mcosu scorev1 path
    DiffCalc::PPv2CalcParams mcosuParams = imperfectParams;
    mcosuParams.legacyTotalScore = r.attrs.MaximumLegacyComboScore / 3;
    mcosuParams.isMcOsuImported = true;

    r.ppSS = DiffCalc::calculatePPv2(ssParams);
    r.ssParams = ssParams;
    r.ppImperfect = DiffCalc::calculatePPv2(imperfectParams);
    r.ppLowAcc = DiffCalc::calculatePPv2(lowAccParams);
    r.ppMcosuImperfect = DiffCalc::calculatePPv2(mcosuParams);

    return r;
}

OneMapResult computeOneMap(const std::string &osuFilePath, ModFlags modFlags, float speedMultiplier) {
    DatabaseBeatmap::PRIMITIVE_CONTAINER primitives;
    std::string error;
    const OneMapResult::ErrorStage errorStage = loadPrimitivesFromPath(osuFilePath, primitives, error);
    if(errorStage != OneMapResult::ErrorStage::NONE) {
        OneMapResult r{};
        r.map = osuFilePath;
        r.modFlags = modFlags;
        r.speedMultiplier = speedMultiplier;
        r.errorStage = errorStage;
        r.error = std::move(error);
        return r;
    }

    return computeOneConfig(primitives, osuFilePath, modFlags, speedMultiplier);
}

std::string jsonDifficultyAttributes(const DiffCalc::DifficultyAttributes &attrs) {
    // exhaustive: this structured binding fails to compile when DifficultyAttributes gains or
    // loses a field, extend the json below when it does
    const auto &[aimDifficulty, aimDifficultSliderCount, speedDifficulty, speedNoteCount, readingDifficulty,
                 readingDifficultNoteCount, flashlightDifficulty, sliderFactor, aimTopWeightedSliderFactor,
                 speedTopWeightedSliderFactor, aimDifficultStrainCount, speedDifficultStrainCount, nestedScorePerObject,
                 legacyScoreBaseMultiplier, sliderCount, maximumLegacyComboScore, approachRate, overallDifficulty] =
        attrs;
    return std::format(
        R"({{"aimDifficulty":{},"aimDifficultSliderCount":{},"speedDifficulty":{},"speedNoteCount":{},)"
        R"("readingDifficulty":{},"readingDifficultNoteCount":{},"flashlightDifficulty":{},)"
        R"("sliderFactor":{},"aimTopWeightedSliderFactor":{},"speedTopWeightedSliderFactor":{},)"
        R"("aimDifficultStrainCount":{},"speedDifficultStrainCount":{},"nestedScorePerObject":{},)"
        R"("legacyScoreBaseMultiplier":{},"sliderCount":{},"maximumLegacyComboScore":{},"approachRate":{},)"
        R"("overallDifficulty":{}}})",
        jnum(aimDifficulty), jnum(aimDifficultSliderCount), jnum(speedDifficulty), jnum(speedNoteCount),
        jnum(readingDifficulty), jnum(readingDifficultNoteCount), jnum(flashlightDifficulty), jnum(sliderFactor),
        jnum(aimTopWeightedSliderFactor), jnum(speedTopWeightedSliderFactor), jnum(aimDifficultStrainCount),
        jnum(speedDifficultStrainCount), jnum(nestedScorePerObject), jnum(legacyScoreBaseMultiplier), sliderCount,
        maximumLegacyComboScore, jnum(approachRate), jnum(overallDifficulty));
}

std::string jsonRawDifficultyValues(const DiffCalc::RawDifficultyValues &raw) {
    // exhaustive, same idea as jsonDifficultyAttributes
    const auto &[aimNoSliders, aim, speed, readingNoHidden, readingHidden, flashlightNoHidden, flashlightHidden] = raw;
    return std::format(R"({{"aimNoSliders":{},"aim":{},"speed":{},"readingNoHidden":{},"readingHidden":{},)"
                       R"("flashlightNoHidden":{},"flashlightHidden":{}}})",
                       jnum(aimNoSliders), jnum(aim), jnum(speed), jnum(readingNoHidden), jnum(readingHidden),
                       jnum(flashlightNoHidden), jnum(flashlightHidden));
}

std::string writeJsonLine(const OneMapResult &r, bool dumpStrains) {
    std::string out = std::format(R"({{"schema":1,"algo":{},"map":{},"mods":{},"flags":"{:#x}","speed":{})",
                                  DiffCalc::PP_ALGORITHM_VERSION, jstr(r.map),
                                  jstr(modsStringFromMods(r.modFlags, r.speedMultiplier)),
                                  static_cast<uint64_t>(r.modFlags), jnum(r.speedMultiplier));

    if(r.failed()) {
        out += std::format(R"(,"error":{}}})", jstr(r.error));
        return out;
    }

    out +=
        std::format(R"(,"beatmap":{{"version":{},"ar":{},"cs":{},"od":{},"hp":{},"stackLeniency":{},)"
                    R"("sliderMultiplier":{},"sliderTickRate":{},"circles":{},"sliders":{},"spinners":{},"objects":{},)"
                    R"("maxCombo":{},"maxComboAtMid":{},"playableLength":{},"breakDuration":{}}})",
                    r.version, jnum(r.AR), jnum(r.CS), jnum(r.OD), jnum(r.HP), jnum(r.stackLeniency),
                    jnum(r.sliderMultiplier), jnum(r.sliderTickRate), r.numCircles, r.numSliders, r.numSpinners,
                    r.numObjects, r.maxCombo, r.maxComboAtMid, r.playableLength, r.breakDuration);

    out += std::format(R"(,"stars":{{"total":{},"aim":{},"speed":{}}})", jnum(r.totalStars),
                       jnum(r.attrs.AimDifficulty), jnum(r.attrs.SpeedDifficulty));

    out += ",\"attrs\":";
    out += jsonDifficultyAttributes(r.attrs);
    out += ",\"raw\":";
    out += jsonRawDifficultyValues(r.raw);

    {
        // same digests as the text output (order-dependent sums), plus the peak
        double aimStrainSum = 0.0;
        double aimStrainMax = 0.0;
        for(const double s : r.aimStrains) {
            aimStrainSum += s;
            if(s > aimStrainMax) aimStrainMax = s;
        }
        double speedStrainSum = 0.0;
        double speedStrainMax = 0.0;
        for(const double s : r.speedStrains) {
            speedStrainSum += s;
            if(s > speedStrainMax) speedStrainMax = s;
        }
        out += std::format(R"(,"strains":{{"aimCount":{},"aimSum":{},"aimMax":{},"speedCount":{},)"
                           R"("speedSum":{},"speedMax":{})",
                           r.aimStrains.size(), jnum(aimStrainSum), jnum(aimStrainMax), r.speedStrains.size(),
                           jnum(speedStrainSum), jnum(speedStrainMax));
        if(dumpStrains) {
            out += R"(,"aim":[)";
            for(size_t i = 0; i < r.aimStrains.size(); ++i) {
                if(i > 0) out += ',';
                out += jnum(r.aimStrains[i]);
            }
            out += R"(],"speed":[)";
            for(size_t i = 0; i < r.speedStrains.size(); ++i) {
                if(i > 0) out += ',';
                out += jnum(r.speedStrains[i]);
            }
            out += ']';
        }
        out += '}';
    }

    out += std::format(R"(,"pp":{{"ss":{},"imperfect":{},"lowAcc":{},"mcosuImperfect":{}}})", jnum(r.ppSS),
                       jnum(r.ppImperfect), jnum(r.ppLowAcc), jnum(r.ppMcosuImperfect));

    out += R"(,"ssParams":)";
    out += jsonPPv2CalcParams(r.ssParams);
    out += '}';

    return out;
}

// all config lines for one map, in config order. the primitives container is loaded once and
// reused for every config. identity is what ends up in the "map" field (batch passes the input
// path, the test suite passes the bare filename so goldens are location independent).
std::string processMapForBatch(const std::string &path, const std::string &identity,
                               const std::vector<BatchConfig> &configs) {
    DatabaseBeatmap::PRIMITIVE_CONTAINER primitives;
    std::string error;
    const OneMapResult::ErrorStage errorStage = loadPrimitivesFromPath(path, primitives, error);

    std::string block;
    if(errorStage != OneMapResult::ErrorStage::NONE) {
        for(const auto &cfg : configs) {
            OneMapResult r{};
            r.map = identity;
            r.modFlags = cfg.flags;
            r.speedMultiplier = cfg.speed;
            r.errorStage = errorStage;
            r.error = error;
            block += writeJsonLine(r, false);
            block += '\n';
        }
        return block;
    }

    for(const auto &cfg : configs) {
        block += writeJsonLine(computeOneConfig(primitives, identity, cfg.flags, cfg.speed), false);
        block += '\n';
    }
    return block;
}

}  // namespace neomod::DiffCalcTool

#ifdef BUILD_TOOLS_ONLY
#define entrypoint main
#else
#define entrypoint NEOMOD_run_diffcalc
#endif

int entrypoint(int argc_, char *argv_[]) {
    auto argv = std::vector<std::string>(argv_, argv_ + argc_);

#ifdef BUILD_TOOLS_ONLY
    // lazy
    argv.insert(argv.begin() + 1, "-");
#endif

    // for consistency with the neomod build, set the same FPU registers/state
    initFPUFlags();

    // subcommands (anything else falls through to the single-map flow below)
    if(argv.size() > 2 && argv[2] == "batch") {
        return runBatch(argv);
    }
    if(argv.size() > 2 && argv[2] == "test") {
        return DiffCalcTool::runSuiteTest(argv);
    }
    if(argv.size() > 2 && argv[2] == "crosscheck") {
        return DiffCalcTool::runCrosscheck(argv);
    }

    // pull out flag-style args first so the positional parsing below is unaffected by them
    bool jsonOutput = false;
    bool dumpStrains = false;
    std::erase_if(argv, [&](const std::string &arg) {
        if(arg == "--json") {
            jsonOutput = true;
            return true;
        }
        if(arg == "--dump-strains") {  // implies --json
            jsonOutput = true;
            dumpStrains = true;
            return true;
        }
        return false;
    });

    size_t argc = argv.size();

    if(argc < 3) {
        std::cerr << "usage: " << argv[0] << ' ' << USAGE_PREFIX << USAGE << '\n';
        std::cerr << "       " << argv[0] << ' ' << USAGE_PREFIX << USAGE_BATCH << '\n';
        std::cerr << "       " << argv[0] << ' ' << USAGE_PREFIX << USAGE_TEST << '\n';
        std::cerr << "       " << argv[0] << ' ' << USAGE_PREFIX << USAGE_CROSSCHECK << '\n';
        return 1;
    }

    std::string osuFilePath = argv[2];

    ModFlags modFlags = {};
    float speedMultiplier = 1.0f;
    {
        bool hadStandaloneSpeed = false;
        if(argc > 3) {
            std::string_view cur{argv[3]};
            float speedTemp = 1.f;
            auto [ptr, ec] = std::from_chars(cur.data(), cur.data() + cur.size(), speedTemp);
            // require the whole argument to be consumed, otherwise a mod string like "1K" parses as 1.0
            if(ec == std::errc() && ptr == cur.data() + cur.size() && speedTemp >= 0.01f && speedTemp <= 3.f) {
                hadStandaloneSpeed = true;
                speedMultiplier = speedTemp;
            }
        }

        const size_t modsArgsPos = hadStandaloneSpeed ? 4 : 3;
        const bool hasPossibleModsArgs = argc > modsArgsPos;
        bool hasHexFlags = false;
        if(hasPossibleModsArgs) {
            int base = 10;
            uint64_t flagsValue = 0;
            std::string_view cur{argv[modsArgsPos]};
            if(cur.starts_with("0x") || cur.starts_with("0X")) {
                base = 16;
                cur = cur.substr(2);
                hasHexFlags = true;
            }
            auto [ptr, ec] = std::from_chars(cur.data(), cur.data() + cur.size(), flagsValue, base);
            // require the whole argument to be consumed, otherwise a mod string like "4K" parses as 4
            if(ec == std::errc() && ptr == cur.data() + cur.size()) modFlags = static_cast<ModFlags>(flagsValue);
        }
        // try parsing as CSVs
        if(hasPossibleModsArgs && !hasHexFlags && modFlags == ModFlags{}) {
            const auto [parsedFlags, parsedDTorHTSpeed] = modStringToModFlag(argv[modsArgsPos]);
            modFlags = parsedFlags;
            if(!hadStandaloneSpeed) {
                speedMultiplier = parsedDTorHTSpeed;
            }
        }
    }

    OneMapResult result = computeOneMap(osuFilePath, modFlags, speedMultiplier);

    if(jsonOutput) {
        std::cout << writeJsonLine(result, dumpStrains) << '\n';
    } else {
        printHumanText(result);
    }

    return result.failed() ? 1 : 0;
}
