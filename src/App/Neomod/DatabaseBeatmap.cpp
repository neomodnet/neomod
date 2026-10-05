// Copyright (c) 2020, PG, All rights reserved.
#include "DatabaseBeatmap.h"
#include "BeatmapFile.h"
#include "DifficultyCalculator.h"

#include "Parsing.h"

#include <array>
#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <utility>

#include "fmt/format.h"

#include "BeatmapInterface.h"
#include "OsuConVars.h"
#include "Database.h"  // DB_TIMINGPOINT, for the getBPM explicit instantiations
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

using namespace neomod;
using namespace DBType;

bool DatabaseBeatmap::prefer_cjk_names() { return cv::prefer_cjk.getBool(); }

// out-of-line to keep fmt format string checking out of the header
std::string DatabaseBeatmap::getFullSoundFilePath() const {
    return fmt::format("{:s}{:s}", this->getFolder(), this->getAudioFileName());
}

std::string DatabaseBeatmap::getFullBackgroundImageFilePath() const {
    return fmt::format("{:s}{:s}", this->getFolder(), this->getBackgroundImageFileName());
}

DatabaseBeatmap::LOAD_GAMEPLAY_RESULT::LOAD_GAMEPLAY_RESULT() = default;
DatabaseBeatmap::LOAD_GAMEPLAY_RESULT::~LOAD_GAMEPLAY_RESULT() = default;

DatabaseBeatmap::LOAD_GAMEPLAY_RESULT::LOAD_GAMEPLAY_RESULT(DatabaseBeatmap::LOAD_GAMEPLAY_RESULT &&) noexcept =
    default;
DatabaseBeatmap::LOAD_GAMEPLAY_RESULT &DatabaseBeatmap::LOAD_GAMEPLAY_RESULT::operator=(
    DatabaseBeatmap::LOAD_GAMEPLAY_RESULT &&) noexcept = default;

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
    const auto &firstDiff = *diffs[0];
    this->sFolder = firstDiff.sFolder;

    this->sTitle = firstDiff.sTitle;
    this->sTitleUnicode = firstDiff.sTitleUnicode;
    this->has_unicode_title = firstDiff.has_unicode_title;
    this->sArtist = firstDiff.sArtist;
    this->sArtistUnicode = firstDiff.sArtistUnicode;
    this->has_unicode_artist = firstDiff.has_unicode_artist;
    this->sCreator = firstDiff.sCreator;
    this->sBackgroundImageFileName = firstDiff.sBackgroundImageFileName;
    this->iSetID = firstDiff.iSetID;

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
    this->last_play_time = 0;

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
        if(diff->last_play_time > this->last_play_time) this->last_play_time = diff->last_play_time;
    }
}

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

namespace {

// the limits the game reads maps with
Primitives::Limits limitsFromConVars() {
    return {.maxHitObjects = cv::beatmap_max_num_hitobjects.getVal<u32>(),
            .maxSliderScoringTimes = cv::beatmap_max_num_slider_scoringtimes.getInt(),
            .sliderCurveMaxLength = cv::slider_curve_max_length.getFloat(),
            .sliderEndInsideCheckOffset = cv::slider_end_inside_check_offset.getInt(),
            .sliderMaxRepeats = cv::slider_max_repeats.getInt(),
            .sliderMaxTicks = cv::slider_max_ticks.getInt()};
}

void logSkippedLines(std::string_view osuFilePath, const Primitives::PRIMITIVE_CONTAINER &c) {
    if(c.skippedLines.empty()) return;
    debugLog("File: {} no hit object from {} line(s), the first is line {}", osuFilePath, c.skippedLines.size(),
             c.skippedLines.front());
}

}  // namespace

Primitives::PRIMITIVE_CONTAINER DatabaseBeatmap::loadPrimitiveObjects(std::string_view osuFilePath,
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

    Primitives::PRIMITIVE_CONTAINER c = Primitives::loadPrimitiveObjectsFromData(fileBuffer, limitsFromConVars(), dead);
    logSkippedLines(osuFilePath, c);
    return c;
}

DiffCalc::LOAD_DIFFOBJ_RESULT DatabaseBeatmap::loadDifficultyHitObjects(std::string_view osuFilePath, float AR,
                                                                        float CS, float speedMultiplier, bool hardRock,
                                                                        const Sync::stop_token &dead) {
    // load primitive arrays
    Primitives::PRIMITIVE_CONTAINER c = loadPrimitiveObjects(osuFilePath, dead);
    return DiffCalc::loadDifficultyHitObjects(c, AR, CS, speedMultiplier, hardRock, dead);
}

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

Mc::Registration DatabaseBeatmap::getMapFileAsync(MapFileReadDoneCallback data_callback) const {
    // don't want to include AsyncIOHandler.h in DatabaseBeatmap.h
    static_assert(std::is_same_v<MapFileReadDoneCallback, AsyncIOHandler::ReadCallback>);
    return io->read(this->getFilePath(), std::move(data_callback));
}

DatabaseBeatmap::LOAD_META_RESULT DatabaseBeatmap::loadMetadata(bool compute_md5) {
    if(this->difficulties) {
        return {.fileData = {},
                .error = {Primitives::LoadError::LOADMETADATA_ON_BEATMAPSET}};  // we are a beatmapset, not a difficulty
    }

    logIf(cv::debug_osu.getBool() || cv::debug_db.getBool(), "loading {:s}", this->getFilePath());

    std::vector<u8> fileBuffer;
    size_t beatmapFileSize{0};

    {
        File file(this->getFilePath());
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

    const auto ret = [&](Primitives::LoadError::code retcode) -> DatabaseBeatmap::LOAD_META_RESULT {
        return {.fileData = std::move(fileBuffer), .error = {retcode}};
    };

    if(fileBuffer.empty() || !beatmapFileSize) {
        debugLog("Osu Error: Couldn't read file {}", this->getFilePath());
        return ret(Primitives::LoadError::FILE_LOAD);
    }

    // compute MD5 hash (very slow)
    if(compute_md5 && !this->md5_init.load(std::memory_order_acquire)) {
        this->writeMD5(crypto::hash::md5(fileBuffer));
    }

    // reset
    this->timingpoints = {};

    using Kind = BeatmapFile::SectionKind;
    const BeatmapFile file{beatmapFile};

    // (e.g. "osu file format v12")
    if(const auto version = file.getVersion(); version && *version >= 0 && *version <= 255) {
        this->iVersion = static_cast<u8>(*version);
        if(this->iVersion > cv::beatmap_version.getInt()) {
            debugLog("Ignoring unknown/invalid beatmap version {:d}", this->iVersion);
            return ret(Primitives::LoadError::UNKNOWN_VERSION);
        }
    }

    BeatmapFile::KeyValue kv;

    for(const auto line : file.getEntries(Kind::GENERAL)) {
        if(!BeatmapFile::parse(line.text, kv)) continue;
        if(kv.key == "Mode") {
            // early return for non-std
            if(u8 gamemode; Parsing::parse(kv.value, &gamemode) && gamemode != 0) {
                logIfCV(debug_osu, "ignoring non-std gamemode {} for {}", gamemode, this->getFilePath());
                return ret(Primitives::LoadError::NON_STD_GAMEMODE);
            }
        } else if(kv.key == "AudioFilename") {
            this->sAudioFileName = kv.value;
        } else if(kv.key == "StackLeniency") {
            Parsing::parse(kv.value, &this->fStackLeniency);
        } else if(kv.key == "PreviewTime") {
            Parsing::parse(kv.value, &this->iPreviewTime);
        }
    }

    std::string tempArtistUnicode;
    std::string tempTitleUnicode;
    for(const auto line : file.getEntries(Kind::METADATA)) {
        if(!BeatmapFile::parse(line.text, kv)) continue;
        if(kv.key == "Title") {
            this->sTitle = kv.value;
        } else if(kv.key == "TitleUnicode") {
            tempTitleUnicode = kv.value;
        } else if(kv.key == "Artist") {
            this->sArtist = kv.value;
        } else if(kv.key == "ArtistUnicode") {
            tempArtistUnicode = kv.value;
        } else if(kv.key == "Creator") {
            this->sCreator = kv.value;
        } else if(kv.key == "Version") {
            this->sDifficultyName = kv.value;
        } else if(kv.key == "Source") {
            this->sSource = kv.value;
        } else if(kv.key == "Tags") {
            this->sTags = kv.value;
        } else if(kv.key == "BeatmapID") {
            Parsing::parse(kv.value, &this->iID);
        } else if(kv.key == "BeatmapSetID") {
            Parsing::parse(kv.value, &this->iSetID);
        }
    }

    bool foundAR = false;
    for(const auto line : file.getEntries(Kind::DIFFICULTY)) {
        if(!BeatmapFile::parse(line.text, kv)) continue;
        if(kv.key == "CircleSize") {
            Parsing::parse(kv.value, &this->fCS);
        } else if(kv.key == "ApproachRate") {
            foundAR |= Parsing::parse(kv.value, &this->fAR);
        } else if(kv.key == "HPDrainRate") {
            Parsing::parse(kv.value, &this->fHP);
        } else if(kv.key == "OverallDifficulty") {
            Parsing::parse(kv.value, &this->fOD);
        } else if(kv.key == "SliderMultiplier") {
            Parsing::parse(kv.value, &this->fSliderMultiplier);
        } else if(kv.key == "SliderTickRate") {
            Parsing::parse(kv.value, &this->fSliderTickRate);
        }
    }

    BeatmapFile::Event event;
    for(const auto line : file.getEntries(Kind::EVENTS)) {
        // short-circuit if we already have a stored filename
        if(this->getBackgroundImageFileName().length() > 2) break;
        if(BeatmapFile::parse(line.text, event) && event.kind == BeatmapFile::Event::Kind::BACKGROUND) {
            this->sBackgroundImageFileName = event.file;
        }
    }

    Primitives::TimingPoints tempTimingpoints = Primitives::readTimingPoints(file);

    if(!SString::is_wspace_only(tempTitleUnicode)) {
        this->sTitleUnicode = std::move(tempTitleUnicode);
        this->has_unicode_title = true;
    } else {
        this->sTitleUnicode.clear();
        this->has_unicode_title = false;
    }
    if(!SString::is_wspace_only(tempArtistUnicode)) {
        this->sArtistUnicode = std::move(tempArtistUnicode);
        this->has_unicode_artist = true;
    } else {
        this->sArtistUnicode.clear();
        this->has_unicode_artist = false;
    }

    // general sanity checks
    if(tempTimingpoints.size() < 1) {
        logIfCV(debug_osu, "no timingpoints in beatmap!");
        return ret(Primitives::LoadError::NO_TIMINGPOINTS);  // nothing more to do here
    }

    this->timingpoints = std::move(tempTimingpoints);

    // calculate BPM range
    if(this->iMostCommonBPM <= 0) {
        logIfCV(debug_osu, "calculating BPM range ...");
        BPMCalc::BPMInfo bpm{};
        std::vector<BPMCalc::BPMTuple> bpm_calculation_buffer;
        bpm = BPMCalc::getBPM(this->timingpoints, bpm_calculation_buffer);
        this->iMinBPM = bpm.min;
        this->iMaxBPM = bpm.max;
        this->iMostCommonBPM = bpm.most_common;
    }

    // special case: old beatmaps have AR = OD, there is no ApproachRate stored
    if(!foundAR) this->fAR = this->fOD;

    return ret(Primitives::LoadError::NONE);
}

DatabaseBeatmap::LOAD_GAMEPLAY_RESULT DatabaseBeatmap::loadGameplay(BeatmapDifficulty *databaseBeatmap,
                                                                    AbstractBeatmapInterface *beatmap,
                                                                    const PlayfieldView *view,
                                                                    LOAD_META_RESULT preloadedMetadata) {
    LOAD_GAMEPLAY_RESULT result = LOAD_GAMEPLAY_RESULT();
    Primitives::PRIMITIVE_CONTAINER c;

    {
        // NOTE: reload metadata (force ensures that all necessary data is ready for creating hitobjects and playing etc.,
        // also if beatmap file is changed manually in the meantime)
        // XXX: file io, md5 calc, all on main thread!!
        auto metaRes = std::move(preloadedMetadata);

        if(metaRes.fileData.empty() || metaRes.error) {
            logIf(cv::debug_osu.getBool() || cv::debug_db.getBool(), "reloading metadata for {} because {}",
                  databaseBeatmap->getFilePath(),
                  metaRes.fileData.empty() ? "metadata file data was empty" : metaRes.error.error_string());
            metaRes = databaseBeatmap->loadMetadata();
        }

        result.error = metaRes.error;
        if(result.error.errc) {
            return result;
        }

        // load primitives, put in temporary container
        c = Primitives::loadPrimitiveObjectsFromData(metaRes.fileData, limitsFromConVars());
        logSkippedLines(databaseBeatmap->getFilePath(), c);
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
        result.error.errc = Primitives::LoadError::NO_TIMINGPOINTS;
        return result;
    }

    // update numObjects
    databaseBeatmap->iNumCircles = c.hitcircles.size();
    databaseBeatmap->iNumSliders = c.sliders.size();
    databaseBeatmap->iNumSpinners = c.spinners.size();

    // check if we have any hitobjects at all
    if(databaseBeatmap->getNumObjects() < 1) {
        result.error.errc = Primitives::LoadError::NO_OBJECTS;
        return result;
    }

    // calculate sliderTimes, and build slider clicks and ticks
    Primitives::LoadError sliderTimeCalcResult = Primitives::calculateSliderTimesClicksTicks(
        c.version, c.sliders, databaseBeatmap->timingpoints, databaseBeatmap->fSliderMultiplier,
        databaseBeatmap->fSliderTickRate, c.limits);
    if(sliderTimeCalcResult.errc != Primitives::LoadError::NONE) {
        result.error.errc = sliderTimeCalcResult.errc;
        return result;
    }

    // build hitobjects from the primitive data we loaded from the osu file, with the mods that change them
    {
        // also calculate max possible combo
        int maxPossibleCombo = 0;
        maxPossibleCombo += c.hitcircles.size();

        for(auto &s : c.sliders) {
            if(cv::mod_strict_tracking.getBool() && cv::mod_strict_tracking_remove_slider_ticks.getBool())
                s.ticks.clear();

            if(cv::mod_reverse_sliders.getBool()) std::ranges::reverse(s.points);

            const int repeats = std::max((s.repeat - 1), 0);
            maxPossibleCombo += 2 + repeats + (repeats + 1) * s.ticks.size();  // start/end + repeat arrow + ticks
        }

        maxPossibleCombo += c.spinners.size();

        beatmap->iMaxPossibleCombo = maxPossibleCombo;

        result.hitobjects = HitObjects::create(c, beatmap, view);
    }

    // update beatmap length stat
    if(databaseBeatmap->iLengthMS == 0 && result.hitobjects.size() > 0)
        databaseBeatmap->iLengthMS = result.hitobjects.back()->getClickTime() + result.hitobjects.back()->getDuration();

    // precalculate Score v2 combo portion maximum
    if(beatmap != nullptr) {
        u32 scoreV2ComboPortionMaximum = 1;

        if(result.hitobjects.size() > 0) {
            scoreV2ComboPortionMaximum = 0;
        }

        uSz combo = 0;
        for(const auto &currentHitObject : result.hitobjects) {
            uSz scoreComboMultiplier = combo == 0 ? 0 : combo - 1;

            if(currentHitObject->getType() == HitObjectType::CIRCLE ||
               currentHitObject->getType() == HitObjectType::SPINNER) {
                scoreV2ComboPortionMaximum += (u32)(300.0 * (1.0 + (double)scoreComboMultiplier / 10.0));
                combo++;
            } else if(currentHitObject->getType() == HitObjectType::SLIDER) {
                combo += 1 + static_cast<const Slider *>(currentHitObject.get())->getClicks().size();
                scoreComboMultiplier = combo == 0 ? 0 : combo - 1;
                scoreV2ComboPortionMaximum += (u32)(300.0 * (1.0 + (double)scoreComboMultiplier / 10.0));
                combo++;
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
                if(currentHitObject->getType() != HitObjectType::SPINNER) {
                    currentHitObject->setComboNumber(comboNumber);
                    comboNumber++;
                }
            }
        }

        const int numberMax = cv::number_max.getInt();
        if(numberMax > 0) {
            for(const auto &currentHitObject : result.hitobjects) {
                const int currentComboNumber = currentHitObject->getComboNumber();
                const int newComboNumber = (currentComboNumber % numberMax);

                currentHitObject->setComboNumber((newComboNumber == 0) ? numberMax : newComboNumber);
            }
        }
    }

    debugLog("loaded {:d} hitobjects", result.hitobjects.size());

    return result;
}

MapOverrides DatabaseBeatmap::get_overrides() const {
    return {.background_image_filename{this->getBackgroundImageFileName()},
            .last_played = this->last_play_time,
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

TIMING_INFO DatabaseBeatmap::getTimingInfoForTime(i32 positionMS) const {
    return this->timingpoints.getTimingInfo(positionMS);
}

namespace neomod::BPMCalc {

template <typename T>
BPMInfo getBPM(const T &timing_points, std::vector<BPMTuple> &bpm_buffer)
    requires((std::is_same_v<T, std::vector<DB_TIMINGPOINT>> || std::is_same_v<T, std::vector<DBType::TIMINGPOINT>>) ||
             (std::is_same_v<T, FixedSizeArray<DB_TIMINGPOINT>> || std::is_same_v<T, Primitives::TimingPoints>))
{
    if(timing_points.empty()) {
        return {};
    }

    bpm_buffer.clear();  // reuse existing buffer
    bpm_buffer.reserve(timing_points.size());

    double lastTime = timing_points.back().offset;
    for(size_t i = 0; i < timing_points.size(); i++) {
        const auto &t = timing_points[i];
        if(t.offset > lastTime) continue;
        if(t.msPerBeat <= 0.0 || std::isnan(t.msPerBeat)) continue;

        // "osu-stable forced the first control point to start at 0."
        // "This is reproduced here to maintain compatibility around osu!mania scroll speed and song
        // select display."
        double currentTime = (i == 0 ? 0 : t.offset);
        double nextTime = (i == timing_points.size() - 1 ? lastTime : timing_points[i + 1].offset);

        i32 bpm = (i32)std::round(std::min(60000.0 / t.msPerBeat, 9001.0));
        double duration = std::max(nextTime - currentTime, 0.0);

        bool found = false;
        for(auto &tuple : bpm_buffer) {
            if(tuple.bpm == bpm) {
                tuple.duration += duration;
                found = true;
                break;
            }
        }

        if(!found) {
            bpm_buffer.push_back(BPMTuple{
                .bpm = bpm,
                .duration = duration,
            });
        }
    }

    i32 min = 9001;
    i32 max = 0;
    i32 mostCommonBPM = 0;
    double longestDuration = 0;
    for(const auto &tuple : bpm_buffer) {
        if(tuple.bpm > max) max = tuple.bpm;
        if(tuple.bpm < min) min = tuple.bpm;
        if(tuple.duration > longestDuration || (tuple.duration == longestDuration && tuple.bpm > mostCommonBPM)) {
            longestDuration = tuple.duration;
            mostCommonBPM = tuple.bpm;
        }
    }
    if(min > max) min = max;

    return BPMInfo{
        .min = min,
        .max = max,
        .most_common = mostCommonBPM,
    };
}

// keep in sync with the requires clause in DatabaseBeatmap.h
template BPMInfo getBPM(const std::vector<DB_TIMINGPOINT> &, std::vector<BPMTuple> &);
template BPMInfo getBPM(const std::vector<DBType::TIMINGPOINT> &, std::vector<BPMTuple> &);
template BPMInfo getBPM(const FixedSizeArray<DB_TIMINGPOINT> &, std::vector<BPMTuple> &);
template BPMInfo getBPM(const Primitives::TimingPoints &, std::vector<BPMTuple> &);

}  // namespace neomod::BPMCalc
