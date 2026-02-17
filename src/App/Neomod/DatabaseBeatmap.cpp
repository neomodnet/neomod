// Copyright (c) 2020, PG, All rights reserved.
#include "DatabaseBeatmap.h"
#include "DifficultyCalculator.h"

#include "GameRules.h"
#include "Parsing.h"

#include <source_location>
#include <utility>
#include <cassert>

#ifndef BUILD_TOOLS_ONLY

#include "BeatmapInterface.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "File.h"
#include "HitObjects.h"
#include "Environment.h"
#include "Osu.h"
#include "Skin.h"
#include "Logging.h"
#include "SongBrowser.h"
#include "AsyncIOHandler.h"
#include "crypto.h"

#include <algorithm>
#include <sys/stat.h>

#define WANT_PDQSORT
#include "Sorting.h"

#define BEATMAP_MAX_NUM_HITOBJECTS cv::beatmap_max_num_hitobjects.getVal<u32>()
#define BEATMAP_MAX_NUM_SLIDER_SCORINGTIMES cv::beatmap_max_num_slider_scoringtimes.getInt()
#define SLIDER_CURVE_MAX_LENGTH cv::slider_curve_max_length.getFloat()
#define SLIDER_END_INSIDE_CHECK_OFFSET cv::slider_end_inside_check_offset.getInt()
#define SLIDER_MAX_REPEATS cv::slider_max_repeats.getInt()
#define SLIDER_MAX_TICKS cv::slider_max_ticks.getInt()
#define STARS_STACKING cv::stars_stacking.getBool()

#define PDQSORT_RANGE srt::pdqsort

#else

#include <print>
#define debugLog(...) std::println(__VA_ARGS__)

#define PDQSORT_RANGE std::ranges::sort

enum class HitObjectType : uint8_t {
    CIRCLE,
    SLIDER,
    SPINNER,
};

namespace PpyHitObjectType {
enum {
    CIRCLE = (1 << 0),
    SLIDER = (1 << 1),
    NEW_COMBO = (1 << 2),
    SPINNER = (1 << 3),
    // 4, 5, 6: 3-bit integer specifying how many combo colors to skip (if NEW_COMBO is set)
    MANIA_HOLD_NOTE = (1 << 7),
};
}

#define BEATMAP_MAX_NUM_HITOBJECTS (u32)40000
#define BEATMAP_MAX_NUM_SLIDER_SCORINGTIMES (i32)32768
#define SLIDER_CURVE_MAX_LENGTH (65536.f / 2.f)
#define SLIDER_END_INSIDE_CHECK_OFFSET (i32)36
#define SLIDER_MAX_REPEATS (i32)9000
#define SLIDER_MAX_TICKS (i32)2048
#define STARS_STACKING true

#define rgb(r, g, b) ((Color)((((255) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff)))

#endif  // BUILD_TOOLS_ONLY

// defined here to avoid including diffcalc things in DatabaseBeatmap.h
DatabaseBeatmap::LOAD_DIFFOBJ_RESULT::LOAD_DIFFOBJ_RESULT() = default;
DatabaseBeatmap::LOAD_DIFFOBJ_RESULT::~LOAD_DIFFOBJ_RESULT() = default;

DatabaseBeatmap::LOAD_DIFFOBJ_RESULT::LOAD_DIFFOBJ_RESULT(DatabaseBeatmap::LOAD_DIFFOBJ_RESULT &&) noexcept = default;
DatabaseBeatmap::LOAD_DIFFOBJ_RESULT &DatabaseBeatmap::LOAD_DIFFOBJ_RESULT::operator=(
    DatabaseBeatmap::LOAD_DIFFOBJ_RESULT &&) noexcept = default;

u32 DatabaseBeatmap::LOAD_DIFFOBJ_RESULT::getMaxComboAtIndex(uSz index) const {
    assert(maxComboAtIndex.size() > 0);
    if(index < maxComboAtIndex.size()) {
        return maxComboAtIndex[index];
    }
    // otherwise return total
    return maxComboAtIndex.back();
}

DatabaseBeatmap::SLIDER::SLIDER() noexcept = default;
DatabaseBeatmap::SLIDER::~SLIDER() noexcept = default;

DatabaseBeatmap::SLIDER::SLIDER(const SLIDER &) noexcept = default;
DatabaseBeatmap::SLIDER &DatabaseBeatmap::SLIDER::operator=(const SLIDER &) noexcept = default;
DatabaseBeatmap::SLIDER::SLIDER(SLIDER &&) noexcept = default;
DatabaseBeatmap::SLIDER &DatabaseBeatmap::SLIDER::operator=(SLIDER &&) noexcept = default;

#ifndef BUILD_TOOLS_ONLY

bool DatabaseBeatmap::prefer_cjk_names() { return cv::prefer_cjk.getBool(); }

DatabaseBeatmap::LOAD_GAMEPLAY_RESULT::LOAD_GAMEPLAY_RESULT() = default;
DatabaseBeatmap::LOAD_GAMEPLAY_RESULT::~LOAD_GAMEPLAY_RESULT() = default;

DatabaseBeatmap::LOAD_GAMEPLAY_RESULT::LOAD_GAMEPLAY_RESULT(DatabaseBeatmap::LOAD_GAMEPLAY_RESULT &&) noexcept =
    default;
DatabaseBeatmap::LOAD_GAMEPLAY_RESULT &DatabaseBeatmap::LOAD_GAMEPLAY_RESULT::operator=(
    DatabaseBeatmap::LOAD_GAMEPLAY_RESULT &&) noexcept = default;

DatabaseBeatmap::DatabaseBeatmap() = default;
DatabaseBeatmap::~DatabaseBeatmap() = default;

DatabaseBeatmap::DatabaseBeatmap(std::string filePath, std::string folder, BeatmapType type)
    : sFolder(std::move(folder)), sFilePath(std::move(filePath)), type(type) {
    this->iVersion = cv::beatmap_version.getInt();
}

DatabaseBeatmap::DatabaseBeatmap(std::unique_ptr<DiffContainer> &&difficulties, BeatmapType type)
    : DatabaseBeatmap("", "", type) {
    this->difficulties = std::move(difficulties);

    assert(this->difficulties && !this->difficulties->empty() &&
           "DatabaseBeatmap: tried to construct a beatmapset with 0 difficulties");
    auto &diffs = *this->difficulties;

    // set parent for difficulties
    for(auto &diff : diffs) {
        diff->parentSet = this;
    }

    // set representative values for this container (i.e. use values from first difficulty)
    this->sFolder = diffs[0]->sFolder;

    this->sTitle = diffs[0]->sTitle;
    this->sTitleUnicode = diffs[0]->sTitleUnicode;
    this->bEmptyTitleUnicode = diffs[0]->bEmptyTitleUnicode;
    this->sArtist = diffs[0]->sArtist;
    this->sArtistUnicode = diffs[0]->sArtistUnicode;
    this->bEmptyArtistUnicode = diffs[0]->bEmptyArtistUnicode;
    this->sCreator = diffs[0]->sCreator;
    this->sBackgroundImageFileName = diffs[0]->sBackgroundImageFileName;
    this->iSetID = diffs[0]->iSetID;

    // also calculate largest representative values
    this->updateRepresentativeValues();
}

void DatabaseBeatmap::updateRepresentativeValues() noexcept {
    if(this->getDifficulties().empty()) return;  // we are a difficulty

    auto &diffs = this->getDifficulties();

    this->iLengthMS = 0;
    this->fCS = 99.f;
    this->fAR = 0.0f;
    this->fOD = 0.0f;
    this->fHP = 0.0f;
    this->iMinBPM = 9001;
    this->iMaxBPM = 0;
    this->iMostCommonBPM = 0;
    this->last_modification_time = 0;

    for(const auto &diff : diffs) {
        if(diff->getLengthMS() > this->iLengthMS) this->iLengthMS = diff->getLengthMS();
        if(diff->getCS() < this->fCS) this->fCS = diff->getCS();
        if(diff->getAR() > this->fAR) this->fAR = diff->getAR();
        if(diff->getHP() > this->fHP) this->fHP = diff->getHP();
        if(diff->getOD() > this->fOD) this->fOD = diff->getOD();
        if(diff->getMinBPM() < this->iMinBPM) this->iMinBPM = diff->getMinBPM();
        if(diff->getMaxBPM() > this->iMaxBPM) this->iMaxBPM = diff->getMaxBPM();
        if(diff->getMostCommonBPM() > this->iMostCommonBPM) this->iMostCommonBPM = diff->getMostCommonBPM();
        if(diff->last_modification_time > this->last_modification_time)
            this->last_modification_time = diff->last_modification_time;
    }
}

void swap(DatabaseBeatmap &a, DatabaseBeatmap &b) noexcept {
    // "swap field" temp macro, trying to avoid bloating line count for this file with these ctors/operators
    // clang-format off
#define SF(fieldname) std::swap(a.fieldname, b.fieldname);
    SF(sMD5Hash)           SF(difficulties)  SF(parentSet)                SF(timingpoints)    SF(sFolder)           SF(sFilePath)         SF(last_modification_time)
    SF(sTitle)             SF(sTitleUnicode) SF(sArtist)                  SF(sArtistUnicode)  SF(sCreator)          SF(sDifficultyName)
    SF(sSource)            SF(sTags)         SF(sBackgroundImageFileName) SF(sAudioFileName)  SF(iID)               SF(iLengthMS)
    SF(iLocalOffset)       SF(iOnlineOffset) SF(iSetID)                   SF(iPreviewTime)    SF(fAR)               SF(fCS)
    SF(fHP)                SF(fOD)           SF(fStackLeniency)           SF(fSliderTickRate) SF(fSliderMultiplier) SF(ppv2Version)
    SF(fStarsNomod)        SF(star_ratings)  SF(iMinBPM)                  SF(iMaxBPM)         SF(iMostCommonBPM)    SF(iNumCircles)
    SF(iNumSliders)        SF(iNumSpinners)  SF(last_queried_sr)          SF(last_queried_sr_idx)
    SF(iVersion)           SF(type)          SF(bEmptyArtistUnicode)
    SF(bEmptyTitleUnicode) SF(do_not_store)  SF(draw_background)
#undef SF
        // clang-format on

        auto tmp_loudness = a.loudness.load(std::memory_order_relaxed);
    a.loudness.store(b.loudness.load(std::memory_order_relaxed), std::memory_order_relaxed);
    b.loudness.store(tmp_loudness, std::memory_order_relaxed);

    auto tmp_md5 = a.md5_init.load(std::memory_order_relaxed);
    a.md5_init.store(b.md5_init.load(std::memory_order_relaxed), std::memory_order_relaxed);
    b.md5_init.store(tmp_md5, std::memory_order_relaxed);
}

// temp shorthand macros
#define COPYOTHER(field) field(other.field)
#define MOVEOTHER(field) field(std::move(other.field))
#define ATOMICOTHER(field) field(other.field.load(std::memory_order_relaxed))

DatabaseBeatmap::DatabaseBeatmap(const DatabaseBeatmap &other)
    // clang-format off
    : COPYOTHER(sMD5Hash),            COPYOTHER(parentSet),                COPYOTHER(timingpoints),     COPYOTHER(sFolder),
      COPYOTHER(sFilePath),           COPYOTHER(last_modification_time),   COPYOTHER(sTitle),
      COPYOTHER(sTitleUnicode),       COPYOTHER(sArtist),                  COPYOTHER(sArtistUnicode),
      COPYOTHER(sCreator),            COPYOTHER(sDifficultyName),          COPYOTHER(sSource),
      COPYOTHER(sTags),               COPYOTHER(sBackgroundImageFileName), COPYOTHER(sAudioFileName),
      COPYOTHER(iID),                 COPYOTHER(iLengthMS),                COPYOTHER(iLocalOffset),
      COPYOTHER(iOnlineOffset),       COPYOTHER(iSetID),                   COPYOTHER(iPreviewTime),
      COPYOTHER(fAR),                 COPYOTHER(fCS),                      COPYOTHER(fHP),
      COPYOTHER(fOD),                 COPYOTHER(fStackLeniency),           COPYOTHER(fSliderTickRate),
      COPYOTHER(fSliderMultiplier),   COPYOTHER(ppv2Version),              COPYOTHER(fStarsNomod),
      COPYOTHER(star_ratings),        COPYOTHER(iMinBPM),                  COPYOTHER(iMaxBPM),
      COPYOTHER(iMostCommonBPM),      COPYOTHER(iNumCircles),
      COPYOTHER(iNumSliders),         COPYOTHER(iNumSpinners),             ATOMICOTHER(loudness),
      COPYOTHER(last_queried_sr),     COPYOTHER(last_queried_sr_idx),
      COPYOTHER(iVersion),            ATOMICOTHER(md5_init),
      COPYOTHER(type),                COPYOTHER(bEmptyArtistUnicode),      COPYOTHER(bEmptyTitleUnicode),
      COPYOTHER(do_not_store),        COPYOTHER(draw_background) {
    // clang-format on

    if(other.difficulties) {
        this->difficulties = std::make_unique<DiffContainer>();
        for(const auto &diff : *other.difficulties) {
            assert(diff != nullptr);
            this->difficulties->emplace_back(std::make_unique<BeatmapDifficulty>(*diff));
        }
    }
}

DatabaseBeatmap::DatabaseBeatmap(DatabaseBeatmap &&other) noexcept
    // clang-format off
    : COPYOTHER(sMD5Hash),           MOVEOTHER(difficulties),        COPYOTHER(parentSet),        MOVEOTHER(timingpoints),
      MOVEOTHER(sFolder),            MOVEOTHER(sFilePath),           MOVEOTHER(last_modification_time),
      MOVEOTHER(sTitle),             MOVEOTHER(sTitleUnicode),       MOVEOTHER(sArtist),
      MOVEOTHER(sArtistUnicode),     MOVEOTHER(sCreator),            MOVEOTHER(sDifficultyName),
      MOVEOTHER(sSource),            MOVEOTHER(sTags),               MOVEOTHER(sBackgroundImageFileName),
      MOVEOTHER(sAudioFileName),     COPYOTHER(iID),                 COPYOTHER(iLengthMS),
      COPYOTHER(iLocalOffset),       COPYOTHER(iOnlineOffset),       COPYOTHER(iSetID),
      COPYOTHER(iPreviewTime),       COPYOTHER(fAR),                 COPYOTHER(fCS),
      COPYOTHER(fHP),                COPYOTHER(fOD),                 COPYOTHER(fStackLeniency),
      COPYOTHER(fSliderTickRate),    COPYOTHER(fSliderMultiplier),   COPYOTHER(ppv2Version),
      COPYOTHER(fStarsNomod),        COPYOTHER(star_ratings),        COPYOTHER(iMinBPM),
      COPYOTHER(iMaxBPM),            COPYOTHER(iMostCommonBPM),      COPYOTHER(iNumCircles),
      COPYOTHER(iNumSliders),        COPYOTHER(iNumSpinners),        ATOMICOTHER(loudness),
      COPYOTHER(last_queried_sr),    COPYOTHER(last_queried_sr_idx),
      COPYOTHER(iVersion),           ATOMICOTHER(md5_init),
      COPYOTHER(type),               COPYOTHER(bEmptyArtistUnicode), COPYOTHER(bEmptyTitleUnicode),
      COPYOTHER(do_not_store),       COPYOTHER(draw_background) {
    // clang-format on

    other.difficulties.reset();
    other.timingpoints.clear();
}

#undef COPYOTHER
#undef ATOMICOTHER
#undef MOVEOTHER

bool DatabaseBeatmap::operator==(const DatabaseBeatmap &other) const {
    // we are both BeatmapDifficulties
    if(!this->difficulties && !other.difficulties) {
        // unlikely, but make sure we both have real md5 hashes loaded
        return (this->md5_init.load(std::memory_order_acquire) && other.md5_init.load(std::memory_order_acquire)) &&
               getMD5() == other.getMD5();
    }
    // we are both BeatmapSets, compare contained difficulties
    if(!!this->difficulties && !!other.difficulties) {
        // quick size check
        size_t numDiffs = this->difficulties->size();
        if(numDiffs != other.difficulties->size()) return false;
        for(size_t i = 0; i < numDiffs; i++) {
            // could recurse but msvc complains
            const auto &ourdiff = *(*this->difficulties)[i];
            const auto &theirdiff = *(*other.difficulties)[i];
            if(!((ourdiff.md5_init.load(std::memory_order_acquire) &&
                  theirdiff.md5_init.load(std::memory_order_acquire)) &&
                 ourdiff.getMD5() == theirdiff.getMD5())) {
                return false;
            }
        }
        // all equal
        return true;
    }
    // one is a set, one is a difficulty
    return false;
}

#endif  // BUILD_TOOLS_ONLY

namespace {  // internal helpers

bool parse_timing_point(std::string_view curLine, DatabaseBeatmap::TIMINGPOINT &out) {
    // old beatmaps: Offset, Milliseconds per Beat
    // old new beatmaps: Offset, Milliseconds per Beat, Meter, sampleSet, sampleIndex, Volume,
    // !Inherited new new beatmaps: Offset, Milliseconds per Beat, Meter, sampleSet, sampleIndex,
    // Volume, !Inherited, Kiai Mode

    f64 tpOffset;
    f64 tpMSPerBeat;
    i32 tpMeter;
    i32 tpSampleSet, tpSampleIndex;
    i32 tpVolume;
    i32 tpUninherited;
    i32 tpKiai = 0;  // optional

    if(Parsing::parse(curLine, &tpOffset, ',', &tpMSPerBeat, ',', &tpMeter, ',', &tpSampleSet, ',', &tpSampleIndex, ',',
                      &tpVolume, ',', &tpUninherited, ',', &tpKiai) ||
       Parsing::parse(curLine, &tpOffset, ',', &tpMSPerBeat, ',', &tpMeter, ',', &tpSampleSet, ',', &tpSampleIndex, ',',
                      &tpVolume, ',', &tpUninherited)) {
        out.offset = std::round(tpOffset);
        out.msPerBeat = tpMSPerBeat;
        out.sampleSet = tpSampleSet;
        out.sampleIndex = tpSampleIndex;
        out.volume = std::clamp(tpVolume, 0, 100);
        out.uninherited = tpUninherited == 1;
        out.kiai = tpKiai > 0;
        return true;
    }

    if(Parsing::parse(curLine, &tpOffset, ',', &tpMSPerBeat)) {
        out.offset = std::round(tpOffset);
        out.msPerBeat = tpMSPerBeat;
        out.sampleSet = 0;
        out.sampleIndex = 0;
        out.volume = 100;
        out.uninherited = true;
        out.kiai = false;
        return true;
    }

    return false;
}

// parse a sample set value with lenient handling, matching lazer behavior:
// values outside 0-3 default to Normal (1)
// see: https://github.com/ppy/osu/blob/56ef5eae1409622518fbc19872d5e3477abe90a2/osu.Game/Rulesets/Objects/Legacy/ConvertHitObjectParser.cs#L203
forceinline u8 parse_sampleset_value(std::string_view str) {
    i32 val = Parsing::strto<i32>(str);
    return (val >= 0 && val <= 3) ? static_cast<u8>(val) : static_cast<u8>(SampleSetType::NORMAL);
}

// hitSamples are colon-separated optional components (up to 5), and not all 5 have to be specified
void parse_hitsamples(std::vector<std::string_view> &parts, std::string_view hitSampleStr, HitSamples &samples) {
    if(hitSampleStr.empty()) return;

    SString::split(parts, hitSampleStr, ':');

    // Parse available components, using defaults for missing ones
    if(parts.size() >= 1) {
        samples.normalSet = parse_sampleset_value(parts[0]);
    }
    if(parts.size() >= 2) {
        samples.additionSet = parse_sampleset_value(parts[1]);
    }
    if(parts.size() >= 3) {
        samples.index = Parsing::strto<i32>(parts[2]);
    }
    if(parts.size() >= 4) {
        i32 volume{};
        volume = Parsing::strto<i32>(parts[3]);  // for some reason this can be negative
        samples.volume = std::clamp<u8>(volume, 0, 100);
    }
    if(parts.size() >= 5) {
        samples.filename = parts[4];  // filename can be empty
    }
};

bool sliderScoringTimeComparator(const SLIDER_SCORING_TIME &a, const SLIDER_SCORING_TIME &b) {
    if(a.time != b.time) return a.time < b.time;
    if(a.type != b.type) return static_cast<i32>(a.type) < static_cast<i32>(b.type);
    return false;  // equivalent
};

bool timingPointSortComparator(const DatabaseBeatmap::TIMINGPOINT &a, const DatabaseBeatmap::TIMINGPOINT &b) {
    if(a.offset != b.offset) return a.offset < b.offset;

    // uninherited timingpoints go before inherited timingpoints
    const bool a_uninherited = a.msPerBeat >= 0;
    const bool b_uninherited = b.msPerBeat >= 0;
    if(a_uninherited != b_uninherited) return a_uninherited;

    if(a.sampleSet != b.sampleSet) return a.sampleSet < b.sampleSet;
    if(a.sampleIndex != b.sampleIndex) return a.sampleIndex < b.sampleIndex;
    if(a.kiai != b.kiai) return a.kiai;

    return false;  // equivalent
}

}  // namespace

#ifndef BUILD_TOOLS_ONLY
DatabaseBeatmap::PRIMITIVE_CONTAINER DatabaseBeatmap::loadPrimitiveObjects(std::string_view osuFilePath,
                                                                           const Sync::stop_token &dead) {
    // open osu file for parsing
    std::vector<u8> fileBuffer;
    uSz beatmapFileSize = 0;
    {
        File file(osuFilePath);
        if(file.canRead()) {
            beatmapFileSize = file.getFileSize();
            file.readToVector(fileBuffer);
        }
        if(!beatmapFileSize || fileBuffer.empty()) {
            beatmapFileSize = 0;
        }
        // close the file here
    }

    return loadPrimitiveObjectsFromData(fileBuffer, osuFilePath, dead);
}

#endif  // BUILD_TOOLS_ONLY

DatabaseBeatmap::PRIMITIVE_CONTAINER DatabaseBeatmap::loadPrimitiveObjectsFromData(const std::vector<u8> &fileBuffer,
                                                                                   std::string_view osuFilePath,
                                                                                   const Sync::stop_token &dead) {
    thread_local std::vector<std::string_view> spbuf1, spbuf2, spbuf3, spbuf4, spbuf5,
        hitsamplebuf;  // to avoid reallocations; "spbuf" == SString::split buffer

    PRIMITIVE_CONTAINER c{};

    if(dead.stop_requested()) {
        c.error.errc = LoadError::LOAD_INTERRUPTED;
        return c;
    }
    if(fileBuffer.empty()) {
        c.error.errc = LoadError::FILE_LOAD;
        return c;
    }

    std::string_view beatmapFile = {reinterpret_cast<const char *>(fileBuffer.data()),
                                    reinterpret_cast<const char *>(fileBuffer.data() + fileBuffer.size())};

    const float sliderSanityRange = SLIDER_CURVE_MAX_LENGTH;  // infinity sanity check, same as before
    const int sliderMaxRepeatRange =
        SLIDER_MAX_REPEATS;  // NOTE: osu! will refuse to play any beatmap which has sliders with more than
                             // 9000 repeats, here we just clamp it instead

    std::array<std::optional<Color>, 8> tempColors;
    std::vector<DatabaseBeatmap::TIMINGPOINT> tempTimingpoints;

    // load the actual beatmap
    int hitobjectsWithoutSpinnerCounter = 0;
    int colorCounter = 1;
    int colorOffset = 0;
    int comboNumber = 1;
    BlockId curBlock{BlockId::Sentinel};

    std::vector<MetadataBlock> blocksUnseen{metadataBlocks.begin(), metadataBlocks.end()};

    using enum BlockId;

    for(const auto curLine : SString::split_newlines(beatmapFile)) {
        if(dead.stop_requested()) {
            c.error.errc = LoadError::LOAD_INTERRUPTED;
            return c;
        }

        // ignore comments, but only if at the beginning of a line (e.g. allow Artist:DJ'TEKINA//SOMETHING)
        if(curLine.empty() || SString::is_comment(curLine)) continue;

        // skip the for loop on the first go-around, the header has to be at the start
        if(curBlock == Sentinel) {
            curBlock = Header;
        } else {
            if(auto it = std::ranges::find(blocksUnseen, curLine, [](const auto &block) { return block.str; });
               it != blocksUnseen.end()) {
                curBlock = it->id;
                blocksUnseen.erase(it);
            }
        }

        // we don't care here
        if(curBlock == Metadata) continue;

        switch(curBlock) {
            case Sentinel:
            case Metadata: {  // already handled above, shut up clang-tidy
                std::unreachable();
                break;
            }

            // (e.g. "osu file format v12")
            case Header: {
                Parsing::parse(curLine, "osu file format v", &c.version);
                break;
            }

            case General: {
                std::string sampleSet;
                if(Parsing::parse(curLine, "SampleSet", ':', &sampleSet)) {
                    SString::lower_inplace(sampleSet);
                    if(sampleSet == "normal") {
                        c.defaultSampleSet = SampleSetType::NORMAL;
                    } else if(sampleSet == "soft") {
                        c.defaultSampleSet = SampleSetType::SOFT;
                    } else if(sampleSet == "drum") {
                        c.defaultSampleSet = SampleSetType::DRUM;
                    }
                }

                Parsing::parse(curLine, "StackLeniency", ':', &c.stackLeniency);
                break;
            }

            case Difficulty: {
                Parsing::parse(curLine, "SliderMultiplier", ':', &c.sliderMultiplier);
                Parsing::parse(curLine, "SliderTickRate", ':', &c.sliderTickRate);
                break;
            }

            case Events: {
                i64 type, startTime, endTime;
                if(Parsing::parse(curLine, &type, ',', &startTime, ',', &endTime)) {
                    if(type == 2) {
                        BREAK b{.startTime = startTime, .endTime = endTime};
                        c.breaks.push_back(b);
                    }
                }
                break;
            }

            case TimingPoints: {
                DatabaseBeatmap::TIMINGPOINT t{};
                if(parse_timing_point(curLine, t)) {
                    tempTimingpoints.push_back(t);
                }
                break;
            }

            case Colours: {
                u8 comboNum;
                u8 r, g, b;

                if(Parsing::parse(curLine, "Combo", &comboNum, ':', &r, ',', &g, ',', &b)) {
                    if(comboNum >= 1 && comboNum <= 8) {  // bare minimum validation effort
                        tempColors[comboNum - 1] = rgb(r, g, b);
                    }
                }

                break;
            }

            case HitObjects: {
                size_t err_line = 0;

                auto upd_last_error = [&err_line](bool parse_result_bad,
                                                  size_t line = std::source_location::current().line()) -> void {
                    if(err_line) {  // already got error
                        return;
                    } else {
                        if(parse_result_bad) {
                            err_line = line;
                        }
                    }
                };

                // circles:
                // x,y,time,type,hitSounds,hitSamples
                // sliders:
                // x,y,time,type,hitSounds,sliderType|curveX:curveY|...,repeat,pixelLength,edgeHitsound,edgeSets,hitSamples
                // spinners:
                // x,y,time,type,hitSounds,endTime,hitSamples

                // NOTE: calculating combo numbers and color offsets based on the parsing order is dangerous.
                // maybe the hitobjects are not sorted by time in the file; these values should be calculated
                // after sorting just to be sure?

                f32 x{}, y{};
                i32 time;
                i32 hitSounds;
                // this actually should be initialized since we use it unconditionally after trying to parse it
                u8 type = 0;

                std::vector<std::string_view> &csvs = spbuf1;
                SString::split(csvs, curLine, ',');

                if(csvs.size() < 5) break;
                upd_last_error(!Parsing::parse(csvs[0], &x) || !std::isfinite(x) || std::isnan(x));
                upd_last_error(!Parsing::parse(csvs[1], &y) || !std::isfinite(y) || std::isnan(y));
                upd_last_error(!Parsing::parse(csvs[2], &time));
                upd_last_error(!Parsing::parse(csvs[3], &type));
                upd_last_error(!Parsing::parse(csvs[4], &hitSounds));
                upd_last_error((type & PpyHitObjectType::SLIDER) && (csvs.size() < 8));
                upd_last_error((type & PpyHitObjectType::SPINNER) && (csvs.size() < 6));
                upd_last_error((type & PpyHitObjectType::MANIA_HOLD_NOTE));
                if(err_line) {
                    debugLog("File: {} Invalid hit object (error on line {}): {}", osuFilePath, err_line, curLine);
                    break;
                }

                if(!(type & PpyHitObjectType::SPINNER)) hitobjectsWithoutSpinnerCounter++;

                if(type & PpyHitObjectType::NEW_COMBO) {
                    comboNumber = 1;

                    // special case 1: if the current object is a spinner, then the raw color counter is not
                    // increased (but the offset still is!)
                    // special case 2: the first (non-spinner) hitobject in a beatmap is always a new combo,
                    // therefore the raw color counter is not increased for it (but the offset still is!)
                    if(!(type & PpyHitObjectType::SPINNER) && hitobjectsWithoutSpinnerCounter > 1) colorCounter++;

                    // special case 3: "Bits 4-6 (16, 32, 64) form a 3-bit number (0-7) that chooses how many combo colours to skip."
                    colorOffset += (type >> 4) & 0b111;
                }

                if(type & PpyHitObjectType::CIRCLE) {
                    HITCIRCLE h{};
                    h.x = x;
                    h.y = y;
                    h.time = time;
                    h.number = comboNumber++;
                    h.colorCounter = colorCounter;
                    h.colorOffset = colorOffset;
                    h.clicked = false;
                    h.samples.hitSounds = (hitSounds & HitSoundType::VALID_HITSOUNDS);

                    if(csvs.size() > 5) {
                        // ignore errors, use defaults
                        parse_hitsamples(hitsamplebuf, csvs[5], h.samples);
                    }

                    c.hitcircles.push_back(std::move(h));
                } else if(type & PpyHitObjectType::SLIDER) {
                    SLIDER slider{};
                    slider.colorCounter = colorCounter;
                    slider.colorOffset = colorOffset;
                    slider.time = time;
                    slider.hoverSamples.hitSounds = (hitSounds & HitSoundType::VALID_SLIDER_HITSOUNDS);

                    std::vector<std::string_view> &curves = spbuf2;
                    SString::split(curves, csvs[5], '|');

                    slider.type = curves[0][0];
                    curves.erase(curves.begin());
                    for(const auto &curvePoints : curves) {
                        f32 cpX{}, cpY{};
                        // just skip infinite/invalid curve points (https://osu.ppy.sh/b/1029976)
                        const bool valid = Parsing::parse(curvePoints, &cpX, ':', &cpY) &&  //
                                           std::isfinite(cpX) && !std::isnan(cpX) &&        //
                                           std::isfinite(cpY) && !std::isnan(cpY);          //

                        if(!valid) continue;

                        slider.points.emplace_back(std::clamp(cpX, -sliderSanityRange, sliderSanityRange),
                                                   std::clamp(cpY, -sliderSanityRange, sliderSanityRange));
                    }

                    upd_last_error(!Parsing::parse(csvs[6], &slider.repeat));
                    if(err_line) {
                        debugLog("File: {} Invalid slider: (error on line {}): {}", osuFilePath, err_line, curLine);
                        break;
                    }
                    upd_last_error(!Parsing::parse(csvs[7], &slider.pixelLength));
                    if(err_line && !csvs[7].empty()) {
                        // fix up infinite pixelLength
                        if(SString::contains_ncase(csvs[7], "e+")) {
                            if(csvs[7].starts_with('-')) {
                                slider.pixelLength = -sliderSanityRange;
                            } else {
                                slider.pixelLength = sliderSanityRange;
                            }
                            err_line = 0;
                        }
                    }
                    if(err_line) {
                        debugLog("File: {} Invalid slider pixel length: {} slider.pixelLength: {}", osuFilePath,
                                 csvs[7], slider.pixelLength);
                        break;
                    }

                    // special case: osu! logic for handling the hitobject point vs the controlpoints (since
                    // sliders have both, and older beatmaps store the start point inside the control
                    // points)
                    vec2 xy = vec2(std::clamp(x, -sliderSanityRange, sliderSanityRange),
                                   std::clamp(y, -sliderSanityRange, sliderSanityRange));
                    if(slider.points.size() > 0) {
                        if(slider.points[0] != xy) slider.points.insert(slider.points.begin(), xy);
                    } else {
                        slider.points.push_back(xy);
                    }

                    // partially allow bullshit sliders (add second point to make valid)
                    // e.g. https://osu.ppy.sh/beatmapsets/791900#osu/1676490
                    if(slider.points.size() == 1) slider.points.push_back(xy);

                    std::vector<std::string_view> &edgeSounds = spbuf3;
                    if(csvs.size() > 8)
                        SString::split(edgeSounds, csvs[8], '|');
                    else
                        edgeSounds.clear();

                    std::vector<std::string_view> &edgeSets = spbuf4;
                    if(csvs.size() > 9)
                        SString::split(edgeSets, csvs[9], '|');
                    else
                        edgeSets.clear();

                    for(i32 i = 0; i < edgeSounds.size(); i++) {
                        HitSamples samples;
                        // ignore parse errors, default hitSounds to 0
                        (void)Parsing::parse(edgeSounds[i], &samples.hitSounds);
                        samples.hitSounds &= HitSoundType::VALID_HITSOUNDS;

                        if(!edgeSets.empty() && i < edgeSets.size()) {
                            std::vector<std::string_view> &parts = spbuf5;
                            SString::split(parts, edgeSets[i], ':');
                            if(parts.size() >= 1) samples.normalSet = parse_sampleset_value(parts[0]);
                            if(parts.size() >= 2) samples.additionSet = parse_sampleset_value(parts[1]);
                        }

                        slider.edgeSamples.push_back(std::move(samples));
                    }

                    // No start sample specified, use default
                    if(slider.edgeSamples.empty()) slider.edgeSamples.emplace_back();

                    // No end sample specified, use the same as start
                    if(slider.edgeSamples.size() == 1) slider.edgeSamples.push_back(slider.edgeSamples.front());

                    if(csvs.size() > 10) {
                        parse_hitsamples(hitsamplebuf, csvs[10], slider.hoverSamples);
                    }

                    slider.x = xy.x;
                    slider.y = xy.y;
                    slider.repeat = std::clamp(slider.repeat, 0, sliderMaxRepeatRange);
                    slider.pixelLength = std::clamp(slider.pixelLength, -sliderSanityRange, sliderSanityRange);
                    slider.number = comboNumber++;
                    c.sliders.push_back(std::move(slider));
                } else if(type & PpyHitObjectType::SPINNER) {
                    i32 endTime{0};
                    upd_last_error(!Parsing::parse(csvs[5], &endTime));

                    if(err_line) {
                        debugLog("File: {} Invalid spinner (error on line {}): {}", osuFilePath, err_line, curLine);
                        break;
                    }

                    SPINNER s{.x = (i32)x,
                              .y = (i32)y,
                              .time = time,
                              .endTime = endTime,
                              .samples = {.hitSounds = (u8)(hitSounds & HitSoundType::VALID_HITSOUNDS)}};

                    if(csvs.size() > 6) {
                        parse_hitsamples(hitsamplebuf, csvs[6], s.samples);
                    }

                    c.spinners.push_back(std::move(s));
                }

                break;
            }
        }
    }

    // late bail if too many hitobjects would run out of memory and crash
    if(c.getNumObjects() > BEATMAP_MAX_NUM_HITOBJECTS) {
        c.error.errc = LoadError::TOOMANY_HITOBJECTS;
        return c;
    }

    for(const auto &tempCol : tempColors) {
        if(tempCol.has_value()) {
            c.combocolors.push_back(tempCol.value());
        }
    }

    // calculate total break duration
    for(const auto &brk : c.breaks) {
        c.totalBreakDuration += (u32)(brk.endTime - brk.startTime);
    }

    if(!tempTimingpoints.empty()) {
        // sort timingpoints by time
        if(tempTimingpoints.size() > 1) PDQSORT_RANGE(tempTimingpoints, timingPointSortComparator);
        c.timingpoints = std::move(tempTimingpoints);
    }

    return c;
}

DatabaseBeatmap::LoadError DatabaseBeatmap::calculateSliderTimesClicksTicks(
    int beatmapVersion, std::vector<SLIDER> &sliders, FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> &timingpoints,
    float sliderMultiplier, float sliderTickRate) {
    return calculateSliderTimesClicksTicks(beatmapVersion, sliders, timingpoints, sliderMultiplier, sliderTickRate,
                                           alwaysFalseStopPred);
}

DatabaseBeatmap::LoadError DatabaseBeatmap::calculateSliderTimesClicksTicks(
    int beatmapVersion, std::vector<SLIDER> &sliders, FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> &timingpoints,
    float sliderMultiplier, float sliderTickRate, const Sync::stop_token &dead) {
    LoadError r;

    if(timingpoints.size() < 1) {
        r.errc = LoadError::NO_TIMINGPOINTS;
        return r;
    }

    struct SliderHelper {
        static float getSliderTickDistance(float sliderMultiplier, float sliderTickRate) {
            return ((100.0f * sliderMultiplier) / sliderTickRate);
        }

        static float getSliderTimeForSlider(const SLIDER &slider, const TIMING_INFO &timingInfo,
                                            float sliderMultiplier) {
            const float duration = timingInfo.beatLength * (slider.pixelLength / sliderMultiplier) / 100.0f;
            return duration >= 1.0f ? duration : 1.0f;  // sanity check
        }

        static float getSliderVelocity(const TIMING_INFO &timingInfo, float sliderMultiplier, float sliderTickRate) {
            const float beatLength = timingInfo.beatLength;
            if(beatLength > 0.0f)
                return (getSliderTickDistance(sliderMultiplier, sliderTickRate) * sliderTickRate *
                        (1000.0f / beatLength));
            else
                return getSliderTickDistance(sliderMultiplier, sliderTickRate) * sliderTickRate;
        }

        static float getTimingPointMultiplierForSlider(const TIMING_INFO &timingInfo)  // needed for slider ticks
        {
            float beatLengthBase = timingInfo.beatLengthBase;
            if(beatLengthBase == 0.0f)  // sanity check
                beatLengthBase = 1.0f;

            return timingInfo.beatLength / beatLengthBase;
        }
    };

    for(auto &s : sliders) {
        if(dead.stop_requested()) {
            r.errc = LoadError::LOAD_INTERRUPTED;
            return r;
        }

        // sanity reset
        s.ticks.clear();
        s.scoringTimesForStarCalc.clear();

        // calculate duration
        const TIMING_INFO timingInfo = getTimingInfoForTimeAndTimingPoints(s.time, timingpoints);
        s.sliderTimeWithoutRepeats = SliderHelper::getSliderTimeForSlider(s, timingInfo, sliderMultiplier);
        s.sliderTime = s.sliderTimeWithoutRepeats * s.repeat;

        // calculate ticks
        int brk = 0;
        // don't generate ticks for NaN timingpoints and infinite values
        while(!brk++ && !timingInfo.isNaN && !std::isnan(s.pixelLength) && std::isfinite(s.pixelLength)) {
            const float minTickPixelDistanceFromEnd =
                0.01f * SliderHelper::getSliderVelocity(timingInfo, sliderMultiplier, sliderTickRate);
            const float tickPixelLength =
                (beatmapVersion < 8 ? SliderHelper::getSliderTickDistance(sliderMultiplier, sliderTickRate)
                                    : SliderHelper::getSliderTickDistance(sliderMultiplier, sliderTickRate) /
                                          SliderHelper::getTimingPointMultiplierForSlider(timingInfo));

            if(std::isnan(tickPixelLength) || !std::isfinite(tickPixelLength)) break;

            const float tickDurationPercentOfSliderLength =
                tickPixelLength / (s.pixelLength == 0.0f ? 1.0f : s.pixelLength);
            const int max_ticks = SLIDER_MAX_TICKS;
            const int tickCount = std::min((int)std::ceil(s.pixelLength / tickPixelLength) - 1,
                                           max_ticks);  // NOTE: hard sanity limit number of ticks per slider

            if(tickCount > 0) {
                const float tickTOffset = tickDurationPercentOfSliderLength;
                float pixelDistanceToEnd = s.pixelLength;
                float t = tickTOffset;
                for(int i = 0; i < tickCount; i++, t += tickTOffset) {
                    // skip ticks which are too close to the end of the slider
                    pixelDistanceToEnd -= tickPixelLength;
                    if(pixelDistanceToEnd <= minTickPixelDistanceFromEnd) break;

                    s.ticks.push_back(t);
                }
            }
        }

        // bail if too many predicted heuristic scoringTimes would run out of memory and crash
        if((size_t)std::abs(s.repeat) * s.ticks.size() > (size_t)BEATMAP_MAX_NUM_SLIDER_SCORINGTIMES) {
            r.errc = LoadError::TOOMANY_HITOBJECTS;
            return r;
        }

        // calculate s.scoringTimesForStarCalc, which should include every point in time where the cursor must be within
        // the followcircle radius and at least one key must be pressed: see
        // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Difficulty/Preprocessing/OsuDifficultyHitObject.cs
        const i32 osuSliderEndInsideCheckOffset = (i32)SLIDER_END_INSIDE_CHECK_OFFSET;

        // 1) "skip the head circle"

        // 2) add repeat times (either at slider begin or end)
        for(int i = 0; i < (s.repeat - 1); i++) {
            const f32 time = s.time + (s.sliderTimeWithoutRepeats * (i + 1));  // see Slider.cpp
            s.scoringTimesForStarCalc.push_back(SLIDER_SCORING_TIME{
                .type = SLIDER_SCORING_TIME::TYPE::REPEAT,
                .time = time,
            });
        }

        // 3) add tick times (somewhere within slider, repeated for every repeat)
        for(int i = 0; i < s.repeat; i++) {
            for(int t = 0; t < s.ticks.size(); t++) {
                const float tickPercentRelativeToRepeatFromStartAbs =
                    (((i + 1) % 2) != 0 ? s.ticks[t] : 1.0f - s.ticks[t]);  // see Slider.cpp
                const f32 time =
                    s.time + (s.sliderTimeWithoutRepeats * i) +
                    (tickPercentRelativeToRepeatFromStartAbs * s.sliderTimeWithoutRepeats);  // see Slider.cpp
                s.scoringTimesForStarCalc.push_back(SLIDER_SCORING_TIME{
                    .type = SLIDER_SCORING_TIME::TYPE::TICK,
                    .time = time,
                });
            }
        }

        // 4) add slider end (potentially before last tick for bullshit sliders, but sorting takes care of that)
        // see https://github.com/ppy/osu/pull/4193#issuecomment-460127543
        const f32 time =
            std::max(static_cast<f32>(s.time) + s.sliderTime / 2.0f,
                     (static_cast<f32>(s.time) + s.sliderTime) - static_cast<f32>(osuSliderEndInsideCheckOffset));
        s.scoringTimesForStarCalc.push_back(SLIDER_SCORING_TIME{
            .type = SLIDER_SCORING_TIME::TYPE::END,
            .time = time,
        });

        if(dead.stop_requested()) {
            r.errc = LoadError::LOAD_INTERRUPTED;
            return r;
        }

        // 5) sort scoringTimes from earliest to latest
        if(s.scoringTimesForStarCalc.size() > 1) {
            PDQSORT_RANGE(s.scoringTimesForStarCalc, sliderScoringTimeComparator);
        }
    }

    return r;
}

#ifndef BUILD_TOOLS_ONLY

DatabaseBeatmap::LOAD_DIFFOBJ_RESULT DatabaseBeatmap::loadDifficultyHitObjects(std::string_view osuFilePath, float AR,
                                                                               float CS, float speedMultiplier,
                                                                               bool calculateStarsInaccurately,
                                                                               const Sync::stop_token &dead) {
    // load primitive arrays
    PRIMITIVE_CONTAINER c = loadPrimitiveObjects(osuFilePath, dead);
    return loadDifficultyHitObjects(c, AR, CS, speedMultiplier, calculateStarsInaccurately, dead);
}

#endif

DatabaseBeatmap::LOAD_DIFFOBJ_RESULT DatabaseBeatmap::loadDifficultyHitObjects(PRIMITIVE_CONTAINER &c, float AR,
                                                                               float CS, float speedMultiplier,
                                                                               bool calculateStarsInaccurately,
                                                                               const Sync::stop_token &dead) {
    LOAD_DIFFOBJ_RESULT result{};

    // build generalized OsuDifficultyHitObjects from the vectors (hitcircles, sliders, spinners)
    // the OsuDifficultyHitObject class is the one getting used in all pp/star calculations, it encompasses every object
    // type for simplicity

    if(c.error.errc) {
        result.error.errc = c.error.errc;
        return result;
    }

    // save break duration (for pp calc)
    result.totalBreakDuration = c.totalBreakDuration;

    // calculate sliderTimes, and build slider clicks and ticks (only if not already done)
    if(!c.sliderTimesCalculated) {
        LoadError sliderTimeCalcResult = calculateSliderTimesClicksTicks(c.version, c.sliders, c.timingpoints,
                                                                         c.sliderMultiplier, c.sliderTickRate, dead);
        if(sliderTimeCalcResult.errc) {
            result.error.errc = sliderTimeCalcResult.errc;
            return result;
        }
        c.sliderTimesCalculated = true;
    }

    // and generate the difficultyhitobjects
    result.diffobjects.reserve(c.hitcircles.size() + c.sliders.size() + c.spinners.size());

    for(auto &hitcircle : c.hitcircles) {
        result.diffobjects.emplace_back(DifficultyHitObject::TYPE::CIRCLE, vec2(hitcircle.x, hitcircle.y),
                                        (i32)hitcircle.time);
    }

    const bool calculateSliderCurveInConstructor =
        (c.sliders.size() < 5000);  // NOTE: for explanation see OsuDifficultyHitObject constructor
    for(const auto &slider : c.sliders) {
        if(dead.stop_requested()) {
            result.error.errc = LoadError::LOAD_INTERRUPTED;
            return result;
        }

        if(!calculateStarsInaccurately) {
            result.diffobjects.emplace_back(
                DifficultyHitObject::TYPE::SLIDER, vec2(slider.x, slider.y), slider.time,
                slider.time + (i32)slider.sliderTime, slider.sliderTimeWithoutRepeats, slider.type, slider.points,
                slider.pixelLength, slider.scoringTimesForStarCalc, slider.repeat, calculateSliderCurveInConstructor);
        } else {
            result.diffobjects.emplace_back(DifficultyHitObject::TYPE::SLIDER, vec2(slider.x, slider.y), slider.time,
                                            slider.time + (i32)slider.sliderTime, slider.sliderTimeWithoutRepeats,
                                            slider.type,
                                            std::vector<vec2>(),  // NOTE: ignore curve when calculating inaccurately
                                            slider.pixelLength,
                                            std::vector<SLIDER_SCORING_TIME>(),  // NOTE: ignore curve when calculating
                                                                                 // inaccurately
                                            slider.repeat,
                                            false);  // NOTE: ignore curve when calculating inaccurately
        }
    }

    for(const auto &spinner : c.spinners) {
        result.diffobjects.emplace_back(DifficultyHitObject::TYPE::SPINNER, vec2(spinner.x, spinner.y),
                                        (i32)spinner.time, (i32)spinner.endTime);
    }

    if(dead.stop_requested()) {
        result.error.errc = LoadError::LOAD_INTERRUPTED;
        return result;
    }

    if(result.diffobjects.size() > 1) {
        // sort hitobjects by time
        static constexpr auto diffHitObjectSortComparator =
            +[](const DifficultyHitObject &a, const DifficultyHitObject &b) -> bool {
            if(a.time != b.time) return a.time < b.time;
            if(a.type != b.type) return static_cast<int>(a.type) < static_cast<int>(b.type);
            if(a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
            if(a.pos.y != b.pos.y) return a.pos.y < b.pos.y;
            return false;  // equivalent
        };

        PDQSORT_RANGE(result.diffobjects, diffHitObjectSortComparator);
    }

    if(dead.stop_requested()) {
        result.error.errc = LoadError::LOAD_INTERRUPTED;
        return result;
    }

    // calculate stacks
    // see Beatmap.cpp
    // NOTE: this must be done before the speed multiplier is applied!
    // HACKHACK: code duplication ffs
    if(STARS_STACKING && !calculateStarsInaccurately)  // NOTE: ignore stacking when calculating inaccurately
    {
        const float finalAR = AR;
        const float finalCS = CS;
        const float rawHitCircleDiameter = GameRules::getRawHitCircleDiameter(finalCS);

        const float STACK_LENIENCE = 3.0f;

        const float approachTime = GameRules::getApproachTimeForStacking(finalAR);

        if(c.version > 5) {
            // peppy's algorithm
            // https://gist.github.com/peppy/1167470

            for(int i = result.diffobjects.size() - 1; i >= 0; i--) {
                int n = i;

                DifficultyHitObject *objectI = &result.diffobjects[i];

                const bool isSpinner = (objectI->type == DifficultyHitObject::TYPE::SPINNER);

                if(objectI->stack != 0 || isSpinner) continue;

                const bool isHitCircle = (objectI->type == DifficultyHitObject::TYPE::CIRCLE);
                const bool isSlider = (objectI->type == DifficultyHitObject::TYPE::SLIDER);

                if(isHitCircle) {
                    while(--n >= 0) {
                        DifficultyHitObject *objectN = &result.diffobjects[n];

                        const bool isSpinnerN = (objectN->type == DifficultyHitObject::TYPE::SPINNER);

                        if(isSpinnerN) continue;

                        if(objectI->time - (approachTime * c.stackLeniency) > (objectN->endTime)) break;

                        vec2 objectNEndPosition = objectN->getOriginalRawPosAt(objectN->time + objectN->getDuration());
                        if(objectN->getDuration() != 0 &&
                           vec::length(objectNEndPosition - objectI->getOriginalRawPosAt(objectI->time)) <
                               STACK_LENIENCE) {
                            int offset = objectI->stack - objectN->stack + 1;
                            for(int j = n + 1; j <= i; j++) {
                                if(vec::length(objectNEndPosition - result.diffobjects[j].getOriginalRawPosAt(
                                                                        result.diffobjects[j].time)) < STACK_LENIENCE)
                                    result.diffobjects[j].stack = (result.diffobjects[j].stack - offset);
                            }

                            break;
                        }

                        if(vec::length(objectN->getOriginalRawPosAt(objectN->time) -
                                       objectI->getOriginalRawPosAt(objectI->time)) < STACK_LENIENCE) {
                            objectN->stack = (objectI->stack + 1);
                            objectI = objectN;
                        }
                    }
                } else if(isSlider) {
                    while(--n >= 0) {
                        DifficultyHitObject *objectN = &result.diffobjects[n];

                        const bool isSpinner = (objectN->type == DifficultyHitObject::TYPE::SPINNER);

                        if(isSpinner) continue;

                        if(objectI->time - (approachTime * c.stackLeniency) > objectN->time) break;

                        if(vec::length((objectN->getDuration() != 0
                                            ? objectN->getOriginalRawPosAt(objectN->time + objectN->getDuration())
                                            : objectN->getOriginalRawPosAt(objectN->time)) -
                                       objectI->getOriginalRawPosAt(objectI->time)) < STACK_LENIENCE) {
                            objectN->stack = (objectI->stack + 1);
                            objectI = objectN;
                        }
                    }
                }
            }
        } else  // version < 6
        {
            // old stacking algorithm for old beatmaps
            // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Beatmaps/OsuBeatmapProcessor.cs

            for(int i = 0; i < result.diffobjects.size(); i++) {
                DifficultyHitObject *currHitObject = &result.diffobjects[i];

                const bool isSlider = (currHitObject->type == DifficultyHitObject::TYPE::SLIDER);

                if(currHitObject->stack != 0 && !isSlider) continue;

                i32 startTime = currHitObject->time + currHitObject->getDuration();
                int sliderStack = 0;

                for(int j = i + 1; j < result.diffobjects.size(); j++) {
                    DifficultyHitObject *objectJ = &result.diffobjects[j];

                    if(objectJ->time - (approachTime * c.stackLeniency) > startTime) break;

                    // "The start position of the hitobject, or the position at the end of the path if the hitobject is
                    // a slider"
                    vec2 position2 =
                        isSlider
                            ? currHitObject->getOriginalRawPosAt(currHitObject->time + currHitObject->getDuration())
                            : currHitObject->getOriginalRawPosAt(currHitObject->time);

                    if(vec::length(objectJ->getOriginalRawPosAt(objectJ->time) -
                                   currHitObject->getOriginalRawPosAt(currHitObject->time)) < 3) {
                        currHitObject->stack++;
                        startTime = objectJ->time + objectJ->getDuration();
                    } else if(vec::length(objectJ->getOriginalRawPosAt(objectJ->time) - position2) < 3) {
                        // "Case for sliders - bump notes down and right, rather than up and left."
                        sliderStack++;
                        objectJ->stack -= sliderStack;
                        startTime = objectJ->time + objectJ->getDuration();
                    }
                }
            }
        }

        // update hitobject positions
        float stackOffset = rawHitCircleDiameter / 128.0f / GameRules::broken_gamefield_rounding_allowance * 6.4f;
        for(int i = 0; i < result.diffobjects.size(); i++) {
            if(dead.stop_requested()) {
                result.error.errc = LoadError::LOAD_INTERRUPTED;
                return result;
            }

            if(result.diffobjects[i].curve && result.diffobjects[i].stack != 0)
                result.diffobjects[i].updateStackPosition(stackOffset);
        }
    }

    // apply speed multiplier (if present)
    if(speedMultiplier != 1.0f && speedMultiplier > 0.0f) {
        const double invSpeedMultiplier = 1.0 / (double)speedMultiplier;
        for(int i = 0; i < result.diffobjects.size(); i++) {
            if(dead.stop_requested()) {
                result.error.errc = LoadError::LOAD_INTERRUPTED;
                return result;
            }

            result.diffobjects[i].time = (i32)((double)result.diffobjects[i].time * invSpeedMultiplier);
            result.diffobjects[i].endTime = (i32)((double)result.diffobjects[i].endTime * invSpeedMultiplier);

            if(!calculateStarsInaccurately)  // NOTE: ignore slider curves when calculating inaccurately
            {
                result.diffobjects[i].spanDuration = (double)result.diffobjects[i].spanDuration * invSpeedMultiplier;
                for(auto &scoringTime : result.diffobjects[i].scoringTimes) {
                    scoringTime.time = ((f64)scoringTime.time * invSpeedMultiplier);
                }
            }
        }
    }

    // calculate playable length (for pp calc)
    if(!result.diffobjects.empty()) {
        result.playableLength = (u32)(result.diffobjects.back().baseEndTime - result.diffobjects[0].baseTime);
    }

    // calculate cumulative max combo per object
    if(!calculateStarsInaccurately && !result.diffobjects.empty()) {
        result.maxComboAtIndex.clear();  // remove dummy 0

        result.maxComboAtIndex.reserve(result.diffobjects.size());
        u32 runningCombo = 0;
        for(const auto &obj : result.diffobjects) {
            if(obj.type == DifficultyHitObject::TYPE::SLIDER)
                runningCombo += 1 + (u32)obj.scoringTimes.size();
            else
                runningCombo += 1;
            result.maxComboAtIndex.push_back(runningCombo);
        }
    } else {
        result.maxComboAtIndex.clear();

        // for inaccurate calculation, just store the total (scoringTimes is empty)
        u32 totalCombo = (u32)c.hitcircles.size() + (u32)c.spinners.size();
        for(const auto &s : c.sliders) {
            const int repeats = std::max((s.repeat - 1), 0);
            totalCombo += 2 + repeats + (repeats + 1) * s.ticks.size();
        }
        result.maxComboAtIndex.push_back(totalCombo);
    }

    if(result.diffobjects.empty()) {
        result.error.errc = LoadError::NO_OBJECTS;
    }

    return result;
}

DatabaseBeatmap::TIMING_INFO DatabaseBeatmap::getTimingInfoForTimeAndTimingPoints(
    i32 positionMS, const FixedSizeArray<DatabaseBeatmap::TIMINGPOINT> &timingpoints) {
    static TIMING_INFO default_info{
        .offset = 0,
        .beatLengthBase = 1,
        .beatLength = 1,
        .sampleSet = 0,
        .sampleIndex = 0,
        .volume = 100,
        .isNaN = false,
    };

    if(timingpoints.size() < 1) return default_info;

    TIMING_INFO ti{default_info};

    // initial values
    ti.offset = timingpoints[0].offset;
    ti.volume = timingpoints[0].volume;
    ti.sampleSet = timingpoints[0].sampleSet;
    ti.sampleIndex = timingpoints[0].sampleIndex;

    // new (peppy's algorithm)
    // (correctly handles aspire & NaNs)
    {
        const bool allowMultiplier = true;

        int point = 0;
        int samplePoint = 0;
        int audioPoint = 0;

        for(int i = -1; const auto &tp : timingpoints) {
            // timingpoints are sorted by offset
            if(tp.offset > positionMS) break;
            ++i;

            audioPoint = i;

            if(tp.uninherited)
                point = i;
            else
                samplePoint = i;
        }

        const f32 mult = (allowMultiplier && samplePoint > point && timingpoints[samplePoint].msPerBeat < 0)
                             ? std::clamp<f32>((f32)-timingpoints[samplePoint].msPerBeat, 10.0f, 1000.0f) / 100.0f
                             : 1.f;

        ti.beatLengthBase = timingpoints[point].msPerBeat;
        ti.offset = timingpoints[point].offset;

        ti.isNaN = std::isnan(timingpoints[samplePoint].msPerBeat) || std::isnan(timingpoints[point].msPerBeat);
        ti.beatLength = ti.beatLengthBase * mult;

        ti.volume = timingpoints[audioPoint].volume;
        ti.sampleSet = timingpoints[audioPoint].sampleSet;
        ti.sampleIndex = timingpoints[audioPoint].sampleIndex;
    }

    return ti;
}

#ifndef BUILD_TOOLS_ONLY

f32 DatabaseBeatmap::getStarRating(u8 idx) const {
    if(idx == this->last_queried_sr_idx && this->last_queried_sr > 0.f) {
        return this->last_queried_sr;
    }

    assert(idx < StarPrecalc::NUM_PRECALC_RATINGS);
    f32 ret = 0.f;

    if(this->difficulties) {  // we are a beatmapset, get max sr of child difficulty
        f32 maxdiff = 0.f;
        f32 max_cached_sr = -1.f;
        for(const auto &d : *this->difficulties) {
            if(f32 diffsr = d->getStarRating(idx); diffsr > maxdiff) {
                maxdiff = diffsr;
                // check if we cached it
                if(d->last_queried_sr_idx == idx && d->last_queried_sr == diffsr) {
                    max_cached_sr = diffsr;
                }
            }
        }

        ret = maxdiff;

        // cache max child diff sr if the max child already had it cached
        if(max_cached_sr == maxdiff) {
            this->last_queried_sr = ret;
            this->last_queried_sr_idx = idx;
        }
    } else if(this->star_ratings) {
        const f32 sr_array_stars{(*this->star_ratings)[idx]};

        // cache the result if we had a valid one (and they aren't outdated)
        if(sr_array_stars > 0.f) {
            ret = sr_array_stars;
            if(this->ppv2Version == DiffCalc::PP_ALGORITHM_VERSION) {
                this->last_queried_sr = ret;
                this->last_queried_sr_idx = idx;
            }
        } else {
            // fall back to nomod stars
            // TODO: return "closest computed" SR for queries while calculating
            ret = this->fStarsNomod;
            this->last_queried_sr_idx = 0xFF;
            this->last_queried_sr = 0.f;
        }
    } else {
        ret = this->fStarsNomod;
    }

    return ret;
}

bool DatabaseBeatmap::getMapFileAsync(MapFileReadDoneCallback data_callback) {
    // don't want to include AsyncIOHandler.h in DatabaseBeatmap.h
    static_assert(std::is_same_v<MapFileReadDoneCallback, AsyncIOHandler::ReadCallback>);
    if(!Environment::fileExists(std::string_view{this->sFilePath})) return false;
    return io->read(this->sFilePath, std::move(data_callback));
}

// XXX: code duplication (see loadPrimitiveObjects)
DatabaseBeatmap::LOAD_META_RESULT DatabaseBeatmap::loadMetadata(bool compute_md5) {
    if(this->difficulties) {
        return {.fileData = {},
                .error = {LoadError::LOADMETADATA_ON_BEATMAPSET}};  // we are a beatmapset, not a difficulty
    }

    logIf(cv::debug_osu.getBool() || cv::debug_db.getBool(), "loading {:s}", this->sFilePath);

    std::vector<u8> fileBuffer;
    size_t beatmapFileSize{0};

    {
        File file(this->sFilePath);
        if(file.canRead()) {
            beatmapFileSize = file.getFileSize();
            file.readToVector(fileBuffer);
        }
        if(!beatmapFileSize || fileBuffer.empty()) {
            beatmapFileSize = 0;
        }

        // should already be non-zero if the map was added from db,
        // but if we're adding a new beatmap then it will be 0
        if(beatmapFileSize > 0 && this->last_modification_time <= 0) {
            this->last_modification_time = file.getModificationTime();
        }
        // close the file here
    }

    std::string_view beatmapFile = {reinterpret_cast<char *>(fileBuffer.data()),
                                    reinterpret_cast<char *>(fileBuffer.data() + beatmapFileSize)};

    const auto ret = [&](LoadError::code retcode) -> DatabaseBeatmap::LOAD_META_RESULT {
        return {.fileData = std::move(fileBuffer), .error = {retcode}};
    };

    if(fileBuffer.empty() || !beatmapFileSize) {
        debugLog("Osu Error: Couldn't read file {}", this->sFilePath);
        return ret(LoadError::FILE_LOAD);
    }

    // compute MD5 hash (very slow)
    if(compute_md5 && !this->md5_init.load(std::memory_order_acquire)) {
        MD5Hash out;
        crypto::hash::md5(fileBuffer.data(), fileBuffer.size(), out.data());
        if(!out.empty()) {
            this->writeMD5(out);
        } else {
            debugLog("WARNING: failed to compute MD5 for {}", this->sFilePath);
        }
    }

    // reset
    this->timingpoints.clear();

    std::vector<DatabaseBeatmap::TIMINGPOINT> tempTimingpoints;

    // load metadata
    bool foundAR = false;

    BlockId curBlock{BlockId::Sentinel};
    std::vector<MetadataBlock> blocksUnseen{metadataBlocks.begin(), metadataBlocks.end()};

    using enum BlockId;

    for(const auto curLine : SString::split_newlines(beatmapFile)) {
        // ignore comments, but only if at the beginning of a line (e.g. allow Artist:DJ'TEKINA//SOMETHING)
        if(curLine.empty() || SString::is_comment(curLine)) continue;

        // skip the for loop on the first go-around, the header has to be at the start
        if(curBlock == Sentinel) {
            curBlock = Header;
        } else {
            if(auto it = std::ranges::find(blocksUnseen, curLine, [](const auto &block) { return block.str; });
               it != blocksUnseen.end()) {
                curBlock = it->id;
                blocksUnseen.erase(it);
            }
        }

        // NOTE: stop early (don't parse "HitObjects" or "Colours" sections here)
        if(curBlock == HitObjects || curBlock == Colours) break;

        switch(curBlock) {
            case Colours:
            case Sentinel:  // already handled above, shut up clang-tidy
            case HitObjects: {
                std::unreachable();
                break;
            }

// to go to the next line after we successfully parse a line
#define PARSE_LINE(...) \
    if(!!(Parsing::parse(curLine, __VA_ARGS__))) break;

            // (e.g. "osu file format v12")
            case Header: {
                if(Parsing::parse(curLine, "osu file format v", &this->iVersion)) {
                    if(this->iVersion > cv::beatmap_version.getInt()) {
                        debugLog("Ignoring unknown/invalid beatmap version {:d}", this->iVersion);
                        return ret(LoadError::UNKNOWN_VERSION);
                    }
                }
                break;
            }

            case General: {
                // early return for non-std
                u8 gamemode{(u8)-1};
                if(Parsing::parse(curLine, "Mode", ':', &gamemode) && gamemode != 0) {
                    logIfCV(debug_osu, "ignoring non-std gamemode {} for {}", gamemode, this->sFilePath);
                    return ret(LoadError::NON_STD_GAMEMODE);
                }
                //PARSE_LINE("Mode", ':', &this->iGameMode);
                PARSE_LINE("AudioFilename", ':', &this->sAudioFileName);
                PARSE_LINE("StackLeniency", ':', &this->fStackLeniency);
                PARSE_LINE("PreviewTime", ':', &this->iPreviewTime);
                break;
            }

            case Metadata: {
                PARSE_LINE("Title", ':', &this->sTitle);
                PARSE_LINE("TitleUnicode", ':', &this->sTitleUnicode);
                PARSE_LINE("Artist", ':', &this->sArtist);
                PARSE_LINE("ArtistUnicode", ':', &this->sArtistUnicode);
                PARSE_LINE("Creator", ':', &this->sCreator);
                PARSE_LINE("Version", ':', &this->sDifficultyName);
                PARSE_LINE("Source", ':', &this->sSource);
                PARSE_LINE("Tags", ':', &this->sTags);
                PARSE_LINE("BeatmapID", ':', &this->iID);
                PARSE_LINE("BeatmapSetID", ':', &this->iSetID);
                break;
            }

            case Difficulty: {
                PARSE_LINE("CircleSize", ':', &this->fCS);
                if(Parsing::parse(curLine, "ApproachRate", ':', &this->fAR)) {
                    foundAR = true;
                    break;
                }
                PARSE_LINE("HPDrainRate", ':', &this->fHP);
                PARSE_LINE("OverallDifficulty", ':', &this->fOD);
                PARSE_LINE("SliderMultiplier", ':', &this->fSliderMultiplier);
                PARSE_LINE("SliderTickRate", ':', &this->fSliderTickRate);
                break;
            }
#undef PARSE_LINE

            case Events: {
                // short-circuit if we already have a stored filename
                bool haveFilename = this->sBackgroundImageFileName.length() > 2;

                std::string str;
                i64 type{-1};
                if(!haveFilename &&
                   Parsing::parse(curLine, &type, ',', Parsing::skip<i64> /* skip start time */, ',', &str) &&
                   (type == 0)) {
                    this->sBackgroundImageFileName = str;
                    haveFilename = true;
                }

                break;
            }

            case TimingPoints: {
                DatabaseBeatmap::TIMINGPOINT t{};
                if(parse_timing_point(curLine, t)) {
                    tempTimingpoints.push_back(t);
                }
                break;
            }
        }
    }

    // im not actually sure if we need to do this again here
    if(SString::is_wspace_only(this->sTitleUnicode)) {
        this->bEmptyTitleUnicode = true;
    }
    if(SString::is_wspace_only(this->sArtistUnicode)) {
        this->bEmptyArtistUnicode = true;
    }

    // general sanity checks
    if(tempTimingpoints.size() < 1) {
        logIfCV(debug_osu, "no timingpoints in beatmap!");
        return ret(LoadError::NO_TIMINGPOINTS);  // nothing more to do here
    }

    this->timingpoints = std::move(tempTimingpoints);

    // sort timingpoints and calculate BPM range
    if(this->timingpoints.size() > 0) {
        // sort timingpoints by time
        if(this->timingpoints.size() > 1) {
            PDQSORT_RANGE(this->timingpoints, timingPointSortComparator);
        }

        if(this->iMostCommonBPM <= 0) {
            logIfCV(debug_osu, "calculating BPM range ...");
            BPMInfo bpm{};
            std::vector<BPMTuple> bpm_calculation_buffer;
            bpm = getBPM(this->timingpoints, bpm_calculation_buffer);
            this->iMinBPM = bpm.min;
            this->iMaxBPM = bpm.max;
            this->iMostCommonBPM = bpm.most_common;
        }
    }

    // special case: old beatmaps have AR = OD, there is no ApproachRate stored
    if(!foundAR) this->fAR = this->fOD;

    return ret(LoadError::NONE);
}

DatabaseBeatmap::LOAD_GAMEPLAY_RESULT DatabaseBeatmap::loadGameplay(BeatmapDifficulty *databaseBeatmap,
                                                                    AbstractBeatmapInterface *beatmap,
                                                                    LOAD_META_RESULT preloadedMetadata,
                                                                    PRIMITIVE_CONTAINER *outPrimitivesCopy) {
    LOAD_GAMEPLAY_RESULT result = LOAD_GAMEPLAY_RESULT();
    PRIMITIVE_CONTAINER c;

    {
        // NOTE: reload metadata (force ensures that all necessary data is ready for creating hitobjects and playing etc.,
        // also if beatmap file is changed manually in the meantime)
        // XXX: file io, md5 calc, all on main thread!!
        auto metaRes = std::move(preloadedMetadata);

        if(metaRes.fileData.empty() || metaRes.error) {
            logIf(cv::debug_osu.getBool() || cv::debug_db.getBool(), "reloading metadata for {} because {}",
                  databaseBeatmap->sFilePath,
                  metaRes.fileData.empty() ? "metadata file data was empty" : metaRes.error.error_string());
            metaRes = databaseBeatmap->loadMetadata();
        }

        result.error = metaRes.error;
        if(result.error.errc) {
            return result;
        }

        // load primitives, put in temporary container
        c = loadPrimitiveObjectsFromData(metaRes.fileData, databaseBeatmap->sFilePath, alwaysFalseStopPred);
        if(outPrimitivesCopy) {
            *outPrimitivesCopy = c;
        }
    }

    if(c.error.errc) {
        result.error.errc = c.error.errc;
        return result;
    }

    result.breaks = std::move(c.breaks);
    result.combocolors = std::move(c.combocolors);
    result.defaultSampleSet = c.defaultSampleSet;

    // override some values with data from primitive load, even though they should already be loaded from metadata
    // (sanity)
    databaseBeatmap->timingpoints = std::move(c.timingpoints);
    databaseBeatmap->fSliderMultiplier = c.sliderMultiplier;
    databaseBeatmap->fSliderTickRate = c.sliderTickRate;
    databaseBeatmap->fStackLeniency = c.stackLeniency;
    databaseBeatmap->iVersion = c.version;

    // check if we have any timingpoints at all
    if(databaseBeatmap->timingpoints.size() == 0) {
        result.error.errc = LoadError::NO_TIMINGPOINTS;
        return result;
    }

    // update numObjects
    databaseBeatmap->iNumCircles = c.hitcircles.size();
    databaseBeatmap->iNumSliders = c.sliders.size();
    databaseBeatmap->iNumSpinners = c.spinners.size();

    // check if we have any hitobjects at all
    if(databaseBeatmap->getNumObjects() < 1) {
        result.error.errc = LoadError::NO_OBJECTS;
        return result;
    }

    // calculate sliderTimes, and build slider clicks and ticks
    LoadError sliderTimeCalcResult =
        calculateSliderTimesClicksTicks(c.version, c.sliders, databaseBeatmap->timingpoints,
                                        databaseBeatmap->fSliderMultiplier, databaseBeatmap->fSliderTickRate);
    if(sliderTimeCalcResult.errc != LoadError::NONE) {
        result.error.errc = sliderTimeCalcResult.errc;
        return result;
    }

    // build hitobjects from the primitive data we loaded from the osu file
    {
        // also calculate max possible combo
        int maxPossibleCombo = 0;

        for(auto &h : c.hitcircles) {
            result.hitobjects.push_back(std::make_unique<Circle>(h.x, h.y, h.time, h.samples, h.number, false,
                                                                 h.colorCounter, h.colorOffset, beatmap));
        }
        maxPossibleCombo += c.hitcircles.size();

        for(auto &s : c.sliders) {
            if(cv::mod_strict_tracking.getBool() && cv::mod_strict_tracking_remove_slider_ticks.getBool())
                s.ticks.clear();

            if(cv::mod_reverse_sliders.getBool()) std::ranges::reverse(s.points);

            result.hitobjects.push_back(std::make_unique<Slider>(
                s.type, s.repeat, s.pixelLength, s.points, s.ticks, s.sliderTime, s.sliderTimeWithoutRepeats, s.time,
                s.hoverSamples, s.edgeSamples, s.number, false, s.colorCounter, s.colorOffset, beatmap));

            const int repeats = std::max((s.repeat - 1), 0);
            maxPossibleCombo += 2 + repeats + (repeats + 1) * s.ticks.size();  // start/end + repeat arrow + ticks
        }

        for(auto &s : c.spinners) {
            result.hitobjects.push_back(
                std::make_unique<Spinner>(s.x, s.y, s.time, s.samples, false, s.endTime, beatmap));
        }
        maxPossibleCombo += c.spinners.size();

        beatmap->iMaxPossibleCombo = maxPossibleCombo;
    }

    // sort hitobjects by starttime
    if(result.hitobjects.size() > 1) {
        static constexpr auto hobjsorter =
            +[](const std::unique_ptr<HitObject> &a, const std::unique_ptr<HitObject> &b) -> bool {
            return BeatmapInterface::sortHitObjectByStartTimeComp(a.get(), b.get());
        };
        PDQSORT_RANGE(result.hitobjects, hobjsorter);
    }

    // update beatmap length stat
    if(databaseBeatmap->iLengthMS == 0 && result.hitobjects.size() > 0)
        databaseBeatmap->iLengthMS = result.hitobjects.back()->click_time + result.hitobjects.back()->duration;

    // set isEndOfCombo + precalculate Score v2 combo portion maximum
    if(beatmap != nullptr) {
        u32 scoreV2ComboPortionMaximum = 1;

        if(result.hitobjects.size() > 0) scoreV2ComboPortionMaximum = 0;

        uSz combo = 0;
        for(size_t i = 0; i < result.hitobjects.size(); i++) {
            HitObject *currentHitObject = result.hitobjects[i].get();
            const HitObject *nextHitObject =
                (i + 1 < result.hitobjects.size() ? result.hitobjects[i + 1].get() : nullptr);

            uSz scoreComboMultiplier = combo == 0 ? 0 : combo - 1;

            if(currentHitObject->type == HitObjectType::CIRCLE || currentHitObject->type == HitObjectType::SPINNER) {
                scoreV2ComboPortionMaximum += (u32)(300.0 * (1.0 + (double)scoreComboMultiplier / 10.0));
                combo++;
            } else if(currentHitObject->type == HitObjectType::SLIDER) {
                combo += 1 + static_cast<const Slider *>(currentHitObject)->getClicks().size();
                scoreComboMultiplier = combo == 0 ? 0 : combo - 1;
                scoreV2ComboPortionMaximum += (u32)(300.0 * (1.0 + (double)scoreComboMultiplier / 10.0));
                combo++;
            }

            if(nextHitObject == nullptr || nextHitObject->combo_number == 1) {
                currentHitObject->is_end_of_combo = true;
            }
        }

        beatmap->iScoreV2ComboPortionMaximum = scoreV2ComboPortionMaximum;
    }

    // special rule for first hitobject (for 1 approach circle with HD)
    if(cv::show_approach_circle_on_first_hidden_object.getBool()) {
        if(result.hitobjects.size() > 0) result.hitobjects[0]->setForceDrawApproachCircle(true);
    }

    // custom override for forcing a hard number cap and/or sequence (visually only)
    // NOTE: this is done after we have already calculated/set isEndOfCombos
    {
        if(cv::ignore_beatmap_combo_numbers.getBool()) {
            // NOTE: spinners don't increment the combo number
            int comboNumber = 1;
            for(const auto &currentHitObject : result.hitobjects) {
                if(currentHitObject->type != HitObjectType::SPINNER) {
                    currentHitObject->combo_number = comboNumber;
                    comboNumber++;
                }
            }
        }

        const int numberMax = cv::number_max.getInt();
        if(numberMax > 0) {
            for(const auto &currentHitObject : result.hitobjects) {
                const int currentComboNumber = currentHitObject->combo_number;
                const int newComboNumber = (currentComboNumber % numberMax);

                currentHitObject->combo_number = (newComboNumber == 0) ? numberMax : newComboNumber;
            }
        }
    }

    debugLog("DatabaseBeatmap::loadGameplay() loaded {:d} hitobjects", result.hitobjects.size());

    return result;
}

MapOverrides DatabaseBeatmap::get_overrides() const {
    return {.background_image_filename = this->sBackgroundImageFileName,
            .ppv2_version = this->ppv2Version,
            .star_rating = this->fStarsNomod,
            .loudness = this->loudness.load(std::memory_order_relaxed),
            .min_bpm = this->iMinBPM,
            .max_bpm = this->iMaxBPM,
            .avg_bpm = this->iMostCommonBPM,
            .local_offset = this->iLocalOffset,
            .online_offset = this->iOnlineOffset,
            .nb_circles = static_cast<u16>(this->iNumCircles),
            .nb_sliders = static_cast<u16>(this->iNumSliders),
            .nb_spinners = static_cast<u16>(this->iNumSpinners),
            .draw_background = this->draw_background};
}

DatabaseBeatmap::TIMING_INFO DatabaseBeatmap::getTimingInfoForTime(i32 positionMS) const {
    return getTimingInfoForTimeAndTimingPoints(positionMS, this->timingpoints);
}

#endif  // BUILD_TOOLS_ONLY
