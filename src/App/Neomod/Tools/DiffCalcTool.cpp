// standalone PP/SR calculator for .osu files
#if __has_include("config.h")
#include "config.h"
#endif

#ifndef BUILD_TOOLS_ONLY
#include "DiffCalcTool.h"
#endif

#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"
#include "ModFlags.h"
#include "SString.h"
#include "Parsing.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

using namespace neomod;

namespace {  // static

struct LiteFile {
    std::ifstream m_ifstream;
    size_t m_filesize;

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

    [[nodiscard]] std::string readLine() {
        if(!canRead()) return "";
        std::string line;
        if(std::getline(m_ifstream, line)) {
            if(!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        return "";
    }

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

struct BeatmapSettings {
    float AR = 5.0f;
    float CS = 5.0f;
    float OD = 5.0f;
    float HP = 5.0f;
};

BeatmapSettings parseDifficultySettings(LiteFile &file) {
    BeatmapSettings settings;

    bool foundAR = false;
    bool inDifficulty = false;

    for(auto line = file.readLine(); !line.empty() || file.canRead(); line = file.readLine()) {
        if(line.empty() || SString::is_comment(line)) continue;

        if(line.contains("[Difficulty]")) {
            inDifficulty = true;
            continue;
        }

        if(line.starts_with('[') && inDifficulty) {
            break;
        }
#define TRY_PARSE(...) \
    if(Parsing::parse(__VA_ARGS__)) continue;
        if(inDifficulty) {
            TRY_PARSE(line, "CircleSize", ':', &settings.CS);
            if(Parsing::parse(line, "ApproachRate", ':', &settings.AR)) {
                foundAR = true;
                continue;
            }
            TRY_PARSE(line, "HPDrainRate", ':', &settings.HP);
            TRY_PARSE(line, "OverallDifficulty", ':', &settings.OD);
        }
#undef TRY_PARSE
    }

    // old beatmaps: AR = OD
    if(!foundAR) {
        settings.AR = settings.OD;
    }

    return settings;
}

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

}  // namespace

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
    constexpr std::string_view usage =
        "<osu_file> [speed] [[mod flags bitmask (0xHEX)] OR [comma separated mods (HR,NF)]]";
#else
    constexpr std::string_view usage =
        "-diffcalc <osu_file> [speed] [[mod flags bitmask (0xHEX)] OR [comma separated mods (HR,NF)]]";
#endif

    size_t argc = argv.size();

    if(argc < 3) {
        std::cerr << "usage: " << argv[0] << usage << '\n';
        return 1;
    }

    std::string osuFilePath = argv[2];

    LiteFile file(osuFilePath);
    if(!file.canRead() || (file.getFileSize() == 0)) {
        std::cerr << "error: could not read file" << osuFilePath << '\n';
        return 1;
    }

    // parse difficulty settings from file
    BeatmapSettings settings = parseDifficultySettings(file);

    std::vector<uint8_t> fileBuffer;
    file.readToVector(fileBuffer);

    // load primitive hitobjects
    DatabaseBeatmap::PRIMITIVE_CONTAINER primitives =
        DatabaseBeatmap::loadPrimitiveObjectsFromData(fileBuffer, osuFilePath);
    if(primitives.error.errc) {
        std::cerr << "error loading beatmap primitives: " << primitives.error.error_string() << '\n';
        return 1;
    }

    bool hadStandaloneSpeed = false;
    float speedMultiplier = 1.0f;
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

    ModFlags modFlags = {};
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

    // load difficulty hitobjects for star calculation
    DatabaseBeatmap::LOAD_DIFFOBJ_RESULT diffResult =
        DatabaseBeatmap::loadDifficultyHitObjects(primitives, settings.AR, settings.CS, speedMultiplier);

    if(diffResult.error.errc) {
        std::cerr << "error loading difficulty objects: " << diffResult.error.error_string() << '\n';
        return 1;
    }

    // calculate star rating
    DiffCalc::BeatmapDiffcalcData diffcalcData{.sortedHitObjects = diffResult.diffobjects,
                                               .CS = settings.CS,
                                               .HP = settings.HP,
                                               .AR = settings.AR,
                                               .OD = settings.OD,
                                               .hidden = flags::has<ModFlags::Hidden>(modFlags),
                                               .relax = flags::has<ModFlags::Relax>(modFlags),
                                               .autopilot = flags::has<ModFlags::Autopilot>(modFlags),
                                               .touchDevice = flags::has<ModFlags::TouchDevice>(modFlags),
                                               .speedMultiplier = speedMultiplier,
                                               .breakDuration = diffResult.totalBreakDuration,
                                               .playableLength = diffResult.playableLength};

    DiffCalc::DifficultyAttributes outAttrs{};
    DiffCalc::RawDifficultyValues rawDiff{};
    std::vector<double> aimStrains;
    std::vector<double> speedStrains;

    DiffCalc::StarCalcParams starParams{
        .outAttributes = outAttrs,
        .beatmapData = diffcalcData,
        .outAimStrains = &aimStrains,
        .outSpeedStrains = &speedStrains,
        .upToObjectIndex = -1,
        .cancelCheck = {},
        .outRawDifficulty = &rawDiff,
        .strainState = &diffResult.strainState,
    };

    const double totalStars = DiffCalc::calculateStarDiffForHitObjects(starParams);

    const double aim = outAttrs.AimDifficulty;
    const double speed = outAttrs.SpeedDifficulty;

    // calculate PP for SS play
    const int numHitObjects = static_cast<int>(primitives.getNumObjects());
    const int maxCombo = static_cast<int>(diffResult.getTotalMaxCombo());

    DiffCalc::PPv2CalcParams ppParams{.attributes = outAttrs,
                                      .modFlags = modFlags,
                                      .timescale = speedMultiplier,
                                      .ar = settings.AR,
                                      .od = settings.OD,
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

    const double pp = DiffCalc::calculatePPv2(ppParams);

    // and for a fixed imperfect play (combo drop + misses/100s/50s + legacy score), to also
    // exercise the miss penalty, estimated sliderbreak and scorev1 misscount estimation paths
    DiffCalc::PPv2CalcParams imperfectPPParams{.attributes = outAttrs,
                                               .modFlags = modFlags,
                                               .timescale = speedMultiplier,
                                               .ar = settings.AR,
                                               .od = settings.OD,
                                               .numHitObjects = numHitObjects,
                                               .numCircles = static_cast<int>(primitives.hitcircles.size()),
                                               .numSliders = static_cast<int>(primitives.sliders.size()),
                                               .numSpinners = static_cast<int>(primitives.spinners.size()),
                                               .maxPossibleCombo = maxCombo,
                                               .combo = (maxCombo * 9) / 10,
                                               .misses = numHitObjects / 50,
                                               .c300 = -1,
                                               .c100 = numHitObjects / 20,
                                               .c50 = numHitObjects / 100,
                                               .legacyTotalScore = outAttrs.MaximumLegacyComboScore / 2,
                                               .isMcOsuImported = false};

    const double imperfectPP = DiffCalc::calculatePPv2(imperfectPPParams);

    // output results (with round-trip float precision, for exact comparisons between builds)
    std::cout << std::setprecision(17);
    std::cout << "star rating: " << totalStars << '\n';
    std::cout << "  aim: " << aim << '\n';
    std::cout << "  speed: " << speed << '\n';
    std::cout << "raw difficulty values:\n";
    std::cout << "  aimNoSliders: " << rawDiff.aimNoSliders << '\n';
    std::cout << "  aim: " << rawDiff.aim << '\n';
    std::cout << "  speed: " << rawDiff.speed << '\n';
    {
        // strain digests (count + order-dependent sum), enough to pin the section strain outputs
        double aimStrainSum = 0.0;
        for(const double s : aimStrains) aimStrainSum += s;
        double speedStrainSum = 0.0;
        for(const double s : speedStrains) speedStrainSum += s;
        std::cout << "aim strains: " << aimStrains.size() << " sum: " << aimStrainSum << '\n';
        std::cout << "speed strains: " << speedStrains.size() << " sum: " << speedStrainSum << '\n';
    }
    std::cout << "pp (SS): " << pp << '\n';
    std::cout << "pp (imperfect): " << imperfectPP << '\n';
    std::cout << "attributes (post-SS-calc):\n" << DiffCalc::PPv2CalcParamsToString(ppParams) << '\n';
    std::cout << '\n';
    std::cout << "map info:\n";
    std::cout << "  mods: " << modsStringFromMods(modFlags, speedMultiplier) << '\n';
    std::cout << "  timescale: " << speedMultiplier << '\n';
    std::cout << "  AR: " << settings.AR << '\n';
    std::cout << "  CS: " << settings.CS << '\n';
    std::cout << "  OD: " << settings.OD << '\n';
    std::cout << "  HP: " << settings.HP << '\n';
    std::cout << "  objects: " << primitives.getNumObjects() << " (" << primitives.hitcircles.size() << "c + "
              << primitives.sliders.size() << "s + " << primitives.spinners.size() << "sp)\n";
    std::cout << "  max combo: " << diffResult.getTotalMaxCombo() << '\n';
    std::cout << "  max combo at middle object: " << diffResult.getMaxComboAtIndex(primitives.getNumObjects() / 2)
              << '\n';
    std::cout << "  playable length: " << diffResult.playableLength << '\n';
    std::cout << "  break duration: " << diffResult.totalBreakDuration << '\n';

    return 0;
}
