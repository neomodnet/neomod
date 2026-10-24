#include "GameplayJudge.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Bancho.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmap.h"
#include "GameRules.h"
#include "HitObjects.h"
#include "HitSounds.h"
#include "HUD.h"
#include "Logging.h"
#include "Matrices.h"
#include "OsuConVars.h"
#include "Replay.h"
#include "Timing.h"
#include "UI.h"
#include "score.h"

using namespace neomod;
using namespace flags::operators;

void GameplayJudge::Listener::playHitSound(DatabaseBeatmapTypes::HITSAMPLE_BITS /*samples*/, vec2 /*rawPos*/,
                                           i32 /*delta*/, i32 /*timeMS*/) {}
void GameplayJudge::Listener::playSliderTickSound(DatabaseBeatmapTypes::HITSAMPLE_BITS /*samples*/, vec2 /*rawPos*/,
                                                  i32 /*timeMS*/) {}
std::vector<HitSoundUtils::Set_Slider_Hit> GameplayJudge::Listener::updateSliderSlideSounds(
    bool /*sliding*/, DatabaseBeatmapTypes::HITSAMPLE_BITS /*samples*/, vec2 /*rawPos*/,
    const std::vector<HitSoundUtils::Set_Slider_Hit> & /*started*/) {
    return {};
}
void GameplayJudge::Listener::stopSliderSounds(const std::vector<HitSoundUtils::Set_Slider_Hit> & /*started*/) {}
void GameplayJudge::Listener::playSpinnerSpinSound(f32 /*ratio*/, DatabaseBeatmapTypes::HITSAMPLE_BITS /*samples*/) {}
void GameplayJudge::Listener::stopSpinnerSpinSound() {}
void GameplayJudge::Listener::playSpinnerBonusSound(DatabaseBeatmapTypes::HITSAMPLE_BITS /*samples*/) {}
void GameplayJudge::Listener::addTargetHit(f32 /*delta*/, f32 /*angle*/) {}
void GameplayJudge::Listener::onComboBreak() {}
void GameplayJudge::Listener::onHealthGain(f64 /*healthBefore*/) {}
bool GameplayJudge::Listener::onFail(bool /*byMods*/) { return true; }

GameplayJudge::GameplayJudge(DatabaseBeatmap *map, LiveScore &score, const PlayfieldView *view, Listener *listener)
    : beatmap(map), score(score), view(view), listener(listener) {
    this->nb_hitobjects = map->getNumObjects();
    this->updateHitobjectMetrics();
}

GameplayJudge::~GameplayJudge() { this->unload(); }

Primitives::LoadError GameplayJudge::load() {
    this->unload();
    this->resetScore();

    // some hitobjects already need this information to be up-to-date before their constructor is called
    this->updateHitobjectMetrics();

    // the play goes by what it reads from the file now (objects ask for the map's settings while they're made)
    this->mapData = std::make_unique<PlayedMap>(DatabaseBeatmap::readPlayedMap(this->beatmap->getFilePath()));
    DatabaseBeatmap::LOAD_GAMEPLAY_RESULT result = DatabaseBeatmap::loadGameplay(*this->mapData, this, this->view);
    if(result.error.errc) {
        this->mapData.reset();
        return result.error;
    }
    this->nb_hitobjects = this->mapData->primitives.getNumObjects();
    this->hitobjects = std::move(result.hitobjects);
    this->breaks = std::move(result.breaks);
    this->comboColors = std::move(result.combocolors);
    this->defaultSampleSet = result.defaultSampleSet;

    // the drawing order is different from the playing/input order.
    // for drawing, if multiple hitobjects occupy the exact same time (duration) then they get drawn on top of the
    // active hitobject
    this->hitobjectsSortedByEndTime.reserve(this->hitobjects.size());
    for(const auto &unq : this->hitobjects) {
        this->hitobjectsSortedByEndTime.push_back(unq.get());
    }
    Mc::ranges::stable_sort(this->hitobjectsSortedByEndTime, {}, &HitObject::getEndTime);

    // after the hitobjects have been loaded we can calculate the stacks
    this->calculateStacks();
    this->computeDrainRate();

    return result.error;
}

void GameplayJudge::unload() {
    this->mapData.reset();
    this->currentHitObject = nullptr;
    this->iCurrentHitObjectIndex = 0;
    this->iCurrentNumCircles = 0;
    this->iCurrentNumSliders = 0;
    this->iCurrentNumSpinners = 0;
    this->hitobjectsSortedByEndTime.clear();
    this->misaimObjects.clear();
    this->hitobjects.clear();
    this->breaks.clear();
    this->clicks.clear();
}

void GameplayJudge::reset(i32 curPos) {
    this->resetScore();
    for(auto &hitobject : this->hitobjects) {
        hitobject->onReset(curPos);
        hitobject->update(curPos, 0.0);
        hitobject->onReset(curPos);
    }
    this->iCurMusicPos = curPos;
}

void GameplayJudge::resetScore() {
    this->current_keys = 0;
    this->current_frame_idx = 0;
    this->clicks.clear();
    this->fHealth = 1.0;
    this->bFailed = false;
    this->bTempSeekNF = false;
    this->holding_slider = false;
    this->bLastComboPerfect = true;
    this->bBreakPassed = true;
    this->iNumLongBreaksPassed = 0;
    this->iNumLongBreaks = 0;
    this->score.reset();
}

void GameplayJudge::step(const LegacyReplay::Frame &frame, f64 frameTimeSecs, u8 clickKeys) {
    this->bNeedsReplayFrame = false;

    // replays have both K1 and M1 set when K1 is pressed (unless NoKeylock mod is on)
    u8 keys = frame.key_flags;
    if(!this->getMods().has(ModFlags::NoKeylock)) {
        if(keys & LegacyReplay::K1) keys &= ~LegacyReplay::M1;
        if(keys & LegacyReplay::K2) keys &= ~LegacyReplay::M2;
    }

    const vec2 cursorPos{frame.x, frame.y};
    const bool countsKeys = !this->bInBreak && !this->bIsInSkippableSection &&
                            (this->hitobjects.empty() || frame.cur_music_pos >= this->hitobjects[0]->getClickTime());
    for(auto key : {LegacyReplay::K1, LegacyReplay::K2, LegacyReplay::M1, LegacyReplay::M2}) {
        if(!(clickKeys & key)) continue;
        this->lastPressedKey = key;
        this->clicks.push_back(Click{
            .timestampNS = Timing::getTicksNS(),
            .cursorPos = cursorPos,
            .musicPosMS = frame.cur_music_pos,
        });
        if(countsKeys) this->score.addKeyCount(key);
    }

    this->current_keys = keys;
    this->vCursorPos = cursorPos;
    this->iCurMusicPos = frame.cur_music_pos;

    this->update(frameTimeSecs);
}

void GameplayJudge::simulate_to(i32 musicPos) {
    if(this->replay.size() < 2) return;

    const bool keylock = !this->getMods().has(ModFlags::NoKeylock);
    const auto keylocked = [keylock](u8 keys) -> u8 {
        if(keylock) {
            if(keys & LegacyReplay::K1) keys &= ~LegacyReplay::M1;
            if(keys & LegacyReplay::K2) keys &= ~LegacyReplay::M2;
        }
        return keys;
    };

    // NOTE: the last frame is processed as well (unlike watch playback which interpolates towards it), so that
    //       objects ending after the second-to-last frame still get judged
    while(this->current_frame_idx + 1 < (i32)this->replay.size() &&
          this->replay[this->current_frame_idx + 1].cur_music_pos <= musicPos) {
        const LegacyReplay::Frame &prev_frame = this->replay[this->current_frame_idx];
        this->current_frame_idx++;
        const LegacyReplay::Frame &frame = this->replay[this->current_frame_idx];

        const u8 lastKeys = keylocked(prev_frame.key_flags);
        const u8 keys = keylocked(frame.key_flags);
        u8 clickKeys = keys & ~lastKeys;
        if(this->getMods().has(ModFlags::DKS)) clickKeys |= lastKeys & ~keys;

        this->step(frame, (f64)(frame.cur_music_pos - prev_frame.cur_music_pos) / 1000.0, clickKeys);
    }
}

void GameplayJudge::animate(i32 curPos) {
    const i32 pvs = this->getPVS();
    for(auto &hitobject : this->hitobjects) {
        if(hitobject->isFinished() && curPos - pvs > hitobject->getEndTime()) continue;
        if(hitobject->getClickTime() > curPos + pvs) break;
        hitobject->animate(curPos);
    }
}

void GameplayJudge::updateAll(i32 curPos, f64 frameTimeSecs) {
    for(auto &hitobject : this->hitobjects) {
        hitobject->update(curPos, frameTimeSecs);
    }
}

void GameplayJudge::fail(bool byMods) {
    if(this->bFailed) return;
    this->bFailed = this->listener == nullptr || this->listener->onFail(byMods);
}

void GameplayJudge::cancelFailing() { this->bFailed = false; }

f32 GameplayJudge::getMapAR() const { return this->mapData ? this->mapData->primitives.AR : this->beatmap->getAR(); }
f32 GameplayJudge::getMapCS() const { return this->mapData ? this->mapData->primitives.CS : this->beatmap->getCS(); }
f32 GameplayJudge::getMapOD() const { return this->mapData ? this->mapData->primitives.OD : this->beatmap->getOD(); }
f32 GameplayJudge::getMapHP() const { return this->mapData ? this->mapData->primitives.HP : this->beatmap->getHP(); }
f32 GameplayJudge::getMapStackLeniency() const {
    return this->mapData ? this->mapData->primitives.stackLeniency : this->beatmap->getStackLeniency();
}
i32 GameplayJudge::getMapVersion() const {
    return this->mapData ? this->mapData->primitives.version : this->beatmap->getVersion();
}
const Primitives::TimingPoints &GameplayJudge::getMapTimingPoints() const {
    return this->mapData ? this->mapData->primitives.timingpoints : this->beatmap->getTimingpoints();
}

const Replay::Mods &GameplayJudge::getMods() const { return this->score.mods; }

f32 GameplayJudge::getRawAR() const {
    const auto &mods = this->getMods();
    const f32 multiplier = mods.has(ModFlags::HardRock) ? 1.4f : mods.has(ModFlags::Easy) ? 0.5f : 1.0f;
    return std::clamp<f32>(this->getMapAR() * multiplier, 0.0f, 10.0f);
}

f32 GameplayJudge::getAR() const {
    const auto &mods = this->getMods();

    f32 AR = this->getRawAR();
    if(mods.ar_override >= 0.0f) AR = mods.ar_override;
    if(mods.ar_overridenegative < 0.0f) AR = mods.ar_overridenegative;

    if(mods.has(ModFlags::AROverrideLock)) AR = GameRules::arWithSpeed(AR, 1.f / this->getSpeedMultiplier());

    if(mods.has(ModFlags::ARTimewarp) && !this->hitobjects.empty()) {
        const f32 percent =
            1.0f - ((f64)(this->iCurMusicPos - this->hitobjects[0]->getClickTime()) /
                    (f64)(this->hitobjects.back()->getClickTime() + this->hitobjects.back()->getDuration() -
                          this->hitobjects[0]->getClickTime())) *
                       (1.0f - mods.artimewarp_multiplier);
        AR *= percent;
    }

    if(mods.has(ModFlags::ARWobble)) {
        AR += std::sin((this->iCurMusicPos / 1000.0f) * mods.arwobble_interval) * mods.arwobble_strength;
    }

    return AR;
}

f32 GameplayJudge::getCS() const {
    const auto &mods = this->getMods();
    const f32 multiplier = mods.has(ModFlags::HardRock) ? 1.3f : mods.has(ModFlags::Easy) ? 0.5f : 1.0f;

    f32 CS = std::clamp<f32>(this->getMapCS() * multiplier, 0.0f, 10.0f);
    if(mods.cs_override >= 0.0f) CS = mods.cs_override;
    if(mods.cs_overridenegative < 0.0f) CS = mods.cs_overridenegative;

    if(mods.has(ModFlags::Minimize) && !this->hitobjects.empty()) {
        const f32 percent =
            1.0f + ((f64)(this->iCurMusicPos - this->hitobjects[0]->getClickTime()) /
                    (f64)(this->hitobjects.back()->getClickTime() + this->hitobjects.back()->getDuration() -
                          this->hitobjects[0]->getClickTime())) *
                       mods.minimize_multiplier;
        CS *= percent;
    }

    if(cv::cs_cap_sanity.getBool()) CS = std::min(CS, 12.1429f);

    return CS;
}

f32 GameplayJudge::getHP() const {
    const auto &mods = this->getMods();
    const f32 multiplier = mods.has(ModFlags::HardRock) ? 1.4f : mods.has(ModFlags::Easy) ? 0.5f : 1.0f;

    f32 HP = std::clamp<f32>(this->getMapHP() * multiplier, 0.0f, 10.0f);
    if(mods.hp_override >= 0.0f) HP = mods.hp_override;

    return HP;
}

f32 GameplayJudge::getRawOD() const {
    const auto &mods = this->getMods();
    const f32 multiplier = mods.has(ModFlags::HardRock) ? 1.4f : mods.has(ModFlags::Easy) ? 0.5f : 1.0f;
    return std::clamp<f32>(this->getMapOD() * multiplier, 0.0f, 10.0f);
}

f32 GameplayJudge::getOD() const {
    const auto &mods = this->getMods();

    f32 OD = this->getRawOD();
    if(mods.od_override >= 0.0f) OD = mods.od_override;

    if(mods.has(ModFlags::ODOverrideLock)) OD = GameRules::odWithSpeed(OD, 1.f / this->getSpeedMultiplier());

    return OD;
}

f32 GameplayJudge::getApproachTime() const {
    return this->getMods().has(ModFlags::Mafham)
               ? this->getLength() * 2
               : GameRules::mapDifficultyRange(this->getAR(), GameRules::getMinApproachTime(),
                                               GameRules::getMidApproachTime(), GameRules::getMaxApproachTime());
}

f32 GameplayJudge::getRawApproachTime() const {
    return this->getMods().has(ModFlags::Mafham)
               ? this->getLength() * 2
               : GameRules::mapDifficultyRange(this->getRawAR(), GameRules::getMinApproachTime(),
                                               GameRules::getMidApproachTime(), GameRules::getMaxApproachTime());
}

f32 GameplayJudge::getSpeedMultiplier() const {
    const f32 speed = this->getMods().speed;
    return speed >= 0.05f ? speed : 1.f;
}

f32 GameplayJudge::getPitchMultiplier() const {
    // if pitch compensation is off, keep pitch constant (do not apply daycore/nightcore pitch)
    if(!cv::snd_speed_compensate_pitch.getBool()) return 1.f;

    // TODO: re-add actual nightcore/daycore mods
    const f32 speedMult = this->getSpeedMultiplier();
    if(this->getMods().has(ModFlags::NoPitchCorrection) && speedMult != 1.f) {
        return speedMult < 1.f ? 0.92f /* daycore */ : 1.1166f /* nightcore */;
    }

    return 1.f;
}

f64 GameplayJudge::getHitWindow300() const {
    return GameRules::mapDifficultyRange<f64>(this->getOD(), GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                              GameRules::MAX_HITWINDOW_300);
}

f32 GameplayJudge::getRawHitWindow300() const {
    return GameRules::mapDifficultyRange(this->getRawOD(), GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                         GameRules::MAX_HITWINDOW_300);
}

f64 GameplayJudge::getHitWindow100() const {
    return GameRules::mapDifficultyRange<f64>(this->getOD(), GameRules::MIN_HITWINDOW_100, GameRules::MID_HITWINDOW_100,
                                              GameRules::MAX_HITWINDOW_100);
}

f64 GameplayJudge::getHitWindow50() const {
    return GameRules::mapDifficultyRange<f64>(this->getOD(), GameRules::MIN_HITWINDOW_50, GameRules::MID_HITWINDOW_50,
                                              GameRules::MAX_HITWINDOW_50);
}

f32 GameplayJudge::getApproachRateForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getApproachTime() * (1.0f / this->getSpeedMultiplier()),
                                            GameRules::getMinApproachTime(), GameRules::getMidApproachTime(),
                                            GameRules::getMaxApproachTime());
}

f32 GameplayJudge::getRawARForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getRawApproachTime() * (1.0f / this->getSpeedMultiplier()),
                                            GameRules::getMinApproachTime(), GameRules::getMidApproachTime(),
                                            GameRules::getMaxApproachTime());
}

f32 GameplayJudge::getConstantApproachRateForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getRawApproachTime() * this->getSpeedMultiplier(),
                                            GameRules::getMinApproachTime(), GameRules::getMidApproachTime(),
                                            GameRules::getMaxApproachTime());
}

f32 GameplayJudge::getOverallDifficultyForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getHitWindow300() * (1.0f / this->getSpeedMultiplier()),
                                            GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                            GameRules::MAX_HITWINDOW_300);
}

f32 GameplayJudge::getRawODForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getRawHitWindow300() * (1.0f / this->getSpeedMultiplier()),
                                            GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                            GameRules::MAX_HITWINDOW_300);
}

f32 GameplayJudge::getConstantOverallDifficultyForSpeedMultiplier() const {
    return GameRules::mapDifficultyRangeInv((f32)this->getRawHitWindow300() * this->getSpeedMultiplier(),
                                            GameRules::MIN_HITWINDOW_300, GameRules::MID_HITWINDOW_300,
                                            GameRules::MAX_HITWINDOW_300);
}

u32 GameplayJudge::getScoreV1DifficultyMultiplier() const {
    // NOTE: We intentionally get CS/HP/OD from beatmap data, not "real" CS/HP/OD
    //       Since this multiplier is only used for ScoreV1
    u32 breakTimeMS = this->getBreakDurationTotal();
    f32 drainLength =
        std::max(this->getLengthPlayable() - std::min(breakTimeMS, this->getLengthPlayable()), (u32)1000) / 1000;
    return std::round((this->beatmap->getCS() + this->beatmap->getHP() + this->beatmap->getOD() +
                       std::clamp<f32>((f32)this->beatmap->getNumObjects() / drainLength * 8.0f, 0.0f, 16.0f)) /
                      38.0f * 5.0f);
}

LiveHitResult GameplayJudge::getHitResult(i32 delta) const {
    // "stable-like" hit windows, see https://github.com/ppy/osu/pull/33882
    const f32 window300 = std::floor(this->getHitWindow300()) - 0.5f;
    const f32 window100 = std::floor(this->getHitWindow100()) - 0.5f;
    const f32 window50 = std::floor(this->getHitWindow50()) - 0.5f;
    const f32 fDelta = std::abs((f32)delta);

    // We are 400ms away from the hitobject, don't count this as a miss
    if(fDelta > GameRules::HITWINDOW_MISS) {
        return LiveHitResult::HIT_NULL;
    }

    const auto modFlags = this->getMods().flags;

    // mod_halfwindow only allows early hits
    // mod_halfwindow_allow_300s also allows "late" perfect hits
    if(flags::has<ModFlags::HalfWindow>(modFlags) && delta > 0) {
        if(fDelta > window300 || !flags::has<ModFlags::HalfWindowAllow300s>(modFlags)) {
            return LiveHitResult::HIT_MISS;
        }
    }

    if(fDelta < window300) return LiveHitResult::HIT_300;
    if(fDelta < window100 && !(flags::has<ModFlags::No100s>(modFlags) || flags::has<ModFlags::Ming3012>(modFlags)))
        return LiveHitResult::HIT_100;
    if(fDelta < window50 && !(flags::has<ModFlags::No100s>(modFlags) || flags::has<ModFlags::No50s>(modFlags)))
        return LiveHitResult::HIT_50;
    return LiveHitResult::HIT_MISS;
}

u32 GameplayJudge::getLength() const { return this->beatmap->getLengthMS(); }

u32 GameplayJudge::getLengthPlayable() const {
    if(!this->hitobjects.empty())
        return (u32)(this->hitobjects.back()->getEndTime() - this->hitobjects.front()->getClickTime());
    else
        return this->getLength();
}

u32 GameplayJudge::getBreakDurationTotal() const {
    u32 breakDurationTotal = 0;
    for(const auto &i : this->breaks) {
        breakDurationTotal += (u32)(i.endTime - i.startTime);
    }

    return breakDurationTotal;
}

DBType::BREAK GameplayJudge::getBreakForTimeRange(i32 startMS, i32 positionMS, i32 endMS) const {
    DBType::BREAK curBreak{.startTime = -1, .endTime = -1};

    for(const auto &i : this->breaks) {
        if(i.startTime >= startMS && i.endTime <= endMS) {
            if(positionMS >= curBreak.startTime) curBreak = i;
        }
    }

    return curBreak;
}

i32 GameplayJudge::getPVS() const {
    // this is an approximation with generous boundaries, it doesn't need to be exact (just good enough to filter 10000
    // hitobjects down to a few hundred or so) it will be used in both positive and negative directions (previous and
    // future hitobjects) to speed up loops which iterate over all hitobjects
    return this->fCachedApproachTimeForUpdate + GameRules::getFadeInTime() + (i32)GameRules::HITWINDOW_MISS +
           1500;  // sanity
}

vec2 GameplayJudge::getCursorPos() const { return this->vCursorPos; }

bool GameplayJudge::isClickHeld() const { return this->getKeys() & ~LegacyReplay::Smoke; }

vec2 GameplayJudge::osuCoords2Pixels(vec2 coords) const {
    const auto &mods = this->getMods();
    if(mods.has(ModFlags::HardRock)) coords.y = GameRules::OSU_COORD_HEIGHT - coords.y;

    // wobble
    const f32 speedMultiplierCompensation = 1.0f / this->getSpeedMultiplier();
    if(mods.has(ModFlags::Wobble1)) {
        coords.x += std::sin((this->iCurMusicPos / 1000.0f) * 5 * speedMultiplierCompensation * mods.wobble_frequency) *
                    mods.wobble_strength;
        coords.y += std::sin((this->iCurMusicPos / 1000.0f) * 4 * speedMultiplierCompensation * mods.wobble_frequency) *
                    mods.wobble_strength;
    }

    // wobble2
    if(mods.has(ModFlags::Wobble2)) {
        vec2 centerDelta = coords - vec2(GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT) / 2.f;
        coords.x += centerDelta.x * 0.25f *
                    std::sin((this->iCurMusicPos / 1000.0f) * 5 * speedMultiplierCompensation * mods.wobble_frequency) *
                    mods.wobble_strength;
        coords.y += centerDelta.y * 0.25f *
                    std::sin((this->iCurMusicPos / 1000.0f) * 3 * speedMultiplierCompensation * mods.wobble_frequency) *
                    mods.wobble_strength;
    }

    // wobble rotation
    if(this->fPlayfieldRotation != 0.0f) {
        coords.x -= GameRules::OSU_COORD_WIDTH / 2;
        coords.y -= GameRules::OSU_COORD_HEIGHT / 2;

        vec3 coords3 = vec3(coords.x, coords.y, 0);
        Matrix4 rot;
        rot.rotateZ(this->fPlayfieldRotation);

        coords3 = coords3 * rot;
        coords.x = coords3.x + GameRules::OSU_COORD_WIDTH / 2;
        coords.y = coords3.y + GameRules::OSU_COORD_HEIGHT / 2;
    }

    // if wobble, clamp coordinates
    if(mods.has(ModFlags::Wobble1) || mods.has(ModFlags::Wobble2)) {
        coords.x = std::clamp<f32>(coords.x, 0.0f, GameRules::OSU_COORD_WIDTH);
        coords.y = std::clamp<f32>(coords.y, 0.0f, GameRules::OSU_COORD_HEIGHT);
    }

    return coords;
}

LiveHitResult GameplayJudge::addHitResult(HitObject *hitObject, LiveHitResult hit, i32 delta, bool isEndOfCombo,
                                          bool ignoreOnHitErrorBar, bool hitErrorBarOnly, bool ignoreCombo,
                                          bool ignoreScore, bool ignoreHealth) {
    using enum LiveHitResult;
    const auto &mods = this->getMods();

    // key presses always get a frame; this is for slider parts, misses, or relax hits
    this->bNeedsReplayFrame = true;

    // handle perfect & sudden death
    if(mods.has(ModFlags::Perfect)) {
        if(!hitErrorBarOnly && hit != HIT_300 && hit != HIT_300G && hit != HIT_SLIDER10 && hit != HIT_SLIDER30 &&
           hit != HIT_SPINNERSPIN && hit != HIT_SPINNERBONUS) {
            this->fail(true);
            return HIT_MISS;
        }
    } else if(mods.has(ModFlags::SuddenDeath)) {
        if(hit == HIT_MISS) {
            this->fail(true);
            return HIT_MISS;
        }
    }

    if(hit == HIT_MISS && this->listener) this->listener->onComboBreak();

    // score
    this->score.addHitResult(this, hitObject, hit, delta, ignoreOnHitErrorBar, hitErrorBarOnly, ignoreCombo,
                             ignoreScore);

    // health
    LiveHitResult returnedHit = HIT_MISS;
    if(!ignoreHealth) {
        this->addHealth(this->score.getHealthIncrease(this, hit), true);

        // geki/katu handling
        if(isEndOfCombo) {
            // (a combo end without a hit result, see Slider::onHit(), awards nothing)
            const int comboEndBitmask = hit == HIT_MISS_SLIDERBREAK ? 2 : this->score.getComboEndBitmask();

            if(comboEndBitmask == 0) {
                returnedHit = HIT_300G;
                this->addHealth(this->score.getHealthIncrease(this, returnedHit), true);
                this->score.addHitResultComboEnd(returnedHit);
            } else if((comboEndBitmask & 2) == 0) {
                if(hit == HIT_100) {
                    returnedHit = HIT_100K;
                    this->addHealth(this->score.getHealthIncrease(this, returnedHit), true);
                    this->score.addHitResultComboEnd(returnedHit);
                } else if(hit == HIT_300) {
                    returnedHit = HIT_300K;
                    this->addHealth(this->score.getHealthIncrease(this, returnedHit), true);
                    this->score.addHitResultComboEnd(returnedHit);
                }
            } else if(hit != HIT_MISS && hit != HIT_MISS_SLIDERBREAK)
                this->addHealth(this->score.getHealthIncrease(this, HIT_MU), true);

            this->score.setComboEndBitmask(0);
            this->bLastComboPerfect = returnedHit == HIT_300G;
        }
    }

    return returnedHit;
}

bool GameplayJudge::isPassing() const {
    if(this->hitobjects.empty() || this->iCurMusicPos < this->hitobjects.front()->getClickTime()) return true;
    if(this->iCurMusicPos > this->hitobjectsSortedByEndTime.back()->getEndTime()) {
        // a strict majority of the long breaks' results and the health at the end
        const i32 passed = this->iNumLongBreaksPassed + (this->fHealth >= 0.5 ? 1 : 0);
        return passed * 2 > this->iNumLongBreaks + 1;
    }
    return this->bInBreak ? this->bBreakPassed : this->bLastComboPerfect;
}

void GameplayJudge::addSliderBreak() {
    // handle perfect & sudden death
    if(this->getMods().has(ModFlags::Perfect) || this->getMods().has(ModFlags::SuddenDeath)) {
        this->fail(true);
        return;
    }

    if(this->listener) this->listener->onComboBreak();
    this->score.addSliderBreak();
}

void GameplayJudge::addScorePoints(int points, bool isSpinner) { this->score.addPoints(points, isSpinner); }

void GameplayJudge::playHitSound(DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 delta, i32 timeMS) {
    if(this->listener) this->listener->playHitSound(samples, rawPos, delta, timeMS);
}

void GameplayJudge::playSliderTickSound(DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 timeMS) {
    if(this->listener) this->listener->playSliderTickSound(samples, rawPos, timeMS);
}

std::vector<HitSoundUtils::Set_Slider_Hit> GameplayJudge::updateSliderSlideSounds(
    bool sliding, DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos,
    const std::vector<HitSoundUtils::Set_Slider_Hit> &started) {
    if(!this->listener) return {};
    return this->listener->updateSliderSlideSounds(sliding, samples, rawPos, started);
}

void GameplayJudge::stopSliderSounds(const std::vector<HitSoundUtils::Set_Slider_Hit> &started) {
    if(this->listener) this->listener->stopSliderSounds(started);
}

void GameplayJudge::playSpinnerSpinSound(f32 ratio, DatabaseBeatmapTypes::HITSAMPLE_BITS samples) {
    if(this->listener) this->listener->playSpinnerSpinSound(ratio, samples);
}

void GameplayJudge::stopSpinnerSpinSound() {
    if(this->listener) this->listener->stopSpinnerSpinSound();
}

void GameplayJudge::playSpinnerBonusSound(DatabaseBeatmapTypes::HITSAMPLE_BITS samples) {
    if(this->listener) this->listener->playSpinnerBonusSound(samples);
}

void GameplayJudge::addTargetHit(f32 delta, f32 angle) {
    if(this->listener) this->listener->addTargetHit(delta, angle);
}

void GameplayJudge::addHealth(f64 percent, bool isFromHitResult) {
    // never drain before first hitobject (or after seeking, or if drain is disabled)
    if(this->bTempSeekNF || this->getMods().has(ModFlags::NoHP) || this->hitobjects.empty() ||
       this->iCurMusicPos < this->hitobjects[0]->getClickTime())
        return;

    // never drain after last hitobject
    if(this->iCurMusicPos > this->hitobjectsSortedByEndTime.back()->getEndTime()) return;

    if(this->bFailed) {
        this->fHealth = 0.0;
        return;
    }

    if(isFromHitResult && percent > 0.0 && this->listener) this->listener->onHealthGain(this->fHealth);

    this->fHealth = std::clamp<f64>(this->fHealth + percent, 0.0, 1.0);

    // handle generic fail state (2)
    const bool isDead = this->fHealth < 0.001;
    if(isDead && !this->getMods().has(ModFlags::NoFail)) {
        if(this->getMods().has(ModFlags::Easy) && this->score.getNumEZRetries() > 0)  // retries with ez
        {
            this->score.setNumEZRetries(this->score.getNumEZRetries() - 1);

            // special case: set health to 160/200 (osu!stable behavior, seems fine for all drains)
            this->fHealth = 160.0f / 200.f;
        } else if(isFromHitResult && percent < 0.0) {
            // judgement fail
            this->fail();
        }
    }
}

void GameplayJudge::update(f64 frameTimeSecs) {
    if(this->hitobjects.empty()) return;

    const auto &mods = this->getMods();

    // live update hitobject metrics (the circle size can change over time)
    this->updateHitobjectMetrics();

    // wobble mod
    if(mods.has(ModFlags::Wobble1)) {
        const f32 speedMultiplierCompensation = 1.0f / this->getSpeedMultiplier();
        this->fPlayfieldRotation =
            (this->iCurMusicPos / 1000.0f) * 30.0f * speedMultiplierCompensation * mods.wobble_rotation_speed;
        this->fPlayfieldRotation = std::fmod(this->fPlayfieldRotation, 360.0f);
    } else {
        this->fPlayfieldRotation = 0.0f;
    }

    // for performance reasons, a lot of operations are crammed into 1 loop over all hitobjects:
    // update all hitobjects,
    // handle click events,
    // also get the time of the next/previous hitobject and their indices for later,
    // and get the current hitobject,
    // also calculate notes per second (nps) and note density,
    // also handle note blocking
    this->currentHitObject = nullptr;
    this->iNextHitObjectTime = 0;
    this->iPreviousHitObjectTime = 0;
    this->iPreviousFollowPointObjectIndex = 0;
    this->iNPS = 0;
    this->iND = 0;
    this->iCurrentNumCircles = 0;
    this->iCurrentNumSliders = 0;
    this->iCurrentNumSpinners = 0;
    bool spinner_active = false;
    {
        bool blockNextNotes = false;

        // to avoid recalculating in HitObject::update every call
        this->fCachedApproachTimeForUpdate = this->getApproachTime();

        const i32 pvs = [&]() -> i32 {
            if(mods.has(ModFlags::Mafham)) {
                const i32 objIdx =
                    std::clamp<i32>(this->iCurrentHitObjectIndex + cv::mod_mafham_render_livesize.getInt() + 1, 0,
                                    (i32)this->hitobjects.size() - 1);
                return this->hitobjects[objIdx]->getClickTime() - this->iCurMusicPos + 1500;
            }
            if(mods.has(ModFlags::FreezeFrame)) {
                // For freeze frame, we want to extend PVS to encompass the whole combo group
                // Find the next combo group, then get the difference from our current combo group
                const auto &current_hitobject = this->hitobjects[this->iCurrentHitObjectIndex];
                for(int i = this->iCurrentHitObjectIndex; i < this->hitobjects.size(); i++) {
                    const auto &future_hitobject = this->hitobjects[i];
                    if(future_hitobject->getComboStartTime() > current_hitobject->getComboStartTime()) {
                        return this->getPVS() +
                               (future_hitobject->getClickTime() - current_hitobject->getComboStartTime());
                    }
                }

                // Oops! We are on the last combo group.
                const auto &last_hitobject = this->hitobjects.back();
                return this->getPVS() + (last_hitobject->getClickTime() - current_hitobject->getComboStartTime());
            }

            return this->getPVS();
        }();
        const bool usePVS = cv::pvs.getBool();

        const int notelockType = mods.notelock_type;
        const i32 tolerance2B = (i32)cv::notelock_stable_tolerance2b.getInt();

        this->iCurrentHitObjectIndex = 0;  // reset below here, since it's needed for mafham pvs

        for(int i = 0; i < this->hitobjects.size(); i++) {
            HitObject *curHobj = this->hitobjects[i].get();
            // the order must be like this:
            // 0) miscellaneous stuff (minimal performance impact)
            // 1) prev + next time vars
            // 2) PVS optimization
            // 3) note blocking
            // 4) click events
            // 5) main hitobject update
            //
            // (clicks go before the update so that a press landing on the same frame as an object end still hits,
            // like osu!stable's order)

            const bool isCircle = curHobj->isCircle();
            const bool isSlider = curHobj->isSlider();
            const bool isSpinner = curHobj->isSpinner();

            // determine previous & next object time, used for auto + followpoints + warning arrows + empty section
            // skipping
            if(this->iNextHitObjectTime == 0) {
                if(curHobj->getClickTime() > this->iCurMusicPos) {
                    this->iNextHitObjectTime = curHobj->getClickTime();
                } else {
                    this->currentHitObject = curHobj;
                    this->iPreviousHitObjectTime = curHobj->getEndTime();

                    if(this->iCurMusicPos >
                       this->iPreviousHitObjectTime + (i32)cv::followpoints_prevfadetime.getFloat())
                        this->iPreviousFollowPointObjectIndex = i;
                }
            }

            // PVS optimization
            if(usePVS) {
                if(curHobj->isFinished() && (this->iCurMusicPos - pvs > curHobj->getEndTime()))  // past objects
                {
                    if(isCircle) this->iCurrentNumCircles++;
                    if(isSlider) this->iCurrentNumSliders++;
                    if(isSpinner) this->iCurrentNumSpinners++;

                    this->iCurrentHitObjectIndex = i;
                    continue;
                }
                if(curHobj->getClickTime() > this->iCurMusicPos + pvs)  // future objects
                    break;
            }

            if(this->iCurMusicPos >= curHobj->getEndTime()) this->iCurrentHitObjectIndex = i;

            // spinner visibility detection
            // XXX: there might be a "better" way to do it?
            if(isSpinner) {
                bool spinner_started = this->iCurMusicPos >= curHobj->getClickTime();
                bool spinner_ended = this->iCurMusicPos > curHobj->getEndTime();
                spinner_active |= (spinner_started && !spinner_ended);
            }

            // note blocking / notelock (1)
            const Slider *currentSliderPointer = isSlider ? static_cast<Slider *>(curHobj) : nullptr;
            if(notelockType > 0) {
                curHobj->setBlocked(blockNextNotes);

                if(notelockType == 1)  // neomod
                {
                    // (nothing, handled in (2) block)
                } else if(notelockType == 2)  // osu!stable
                {
                    if(!curHobj->isFinished()) {
                        blockNextNotes = true;

                        // Sliders are "finished" after they end
                        // Extra handling for simultaneous/2b hitobjects, as these would otherwise get blocked
                        // NOTE: this will still unlock some simultaneous/2b patterns too early
                        //       (slider slider circle [circle]), but nobody from that niche has complained so far
                        if(isSlider || isSpinner) {
                            if((i + 1) < this->hitobjects.size()) {
                                if((isSpinner || currentSliderPointer->isStartCircleFinished()) &&
                                   (this->hitobjects[i + 1]->getClickTime() <= (curHobj->getEndTime() + tolerance2B)))
                                    blockNextNotes = false;
                            }
                        }
                    }
                } else if(notelockType == 3)  // osu!lazer 2020
                {
                    if(!curHobj->isFinished()) {
                        // spinners are completely ignored (transparent)
                        if(!isSpinner) {
                            blockNextNotes = (this->iCurMusicPos <= curHobj->getClickTime());

                            // sliders are "finished" after their startcircle
                            if(isSlider && currentSliderPointer->isStartCircleFinished()) {
                                // sliders with finished startcircles do not block
                                blockNextNotes = false;
                            }
                        }
                    }
                }
            } else {
                curHobj->setBlocked(false);
            }

            // click events (this also handles hitsounds!)
            const bool isCurrentHitObjectASliderAndHasItsStartCircleFinishedBeforeClickEvents =
                (currentSliderPointer != nullptr && currentSliderPointer->isStartCircleFinished());
            const bool isCurrentHitObjectFinishedBeforeClickEvents = curHobj->isFinished();
            if(!this->clicks.empty()) curHobj->onClickEvent(this->clicks);
            const bool isCurrentHitObjectFinishedAfterClickEvents = curHobj->isFinished();
            const bool isCurrentHitObjectASliderAndHasItsStartCircleFinishedAfterClickEvents =
                (currentSliderPointer != nullptr && currentSliderPointer->isStartCircleFinished());

            // note blocking / notelock (2.1)
            if(!isCurrentHitObjectASliderAndHasItsStartCircleFinishedBeforeClickEvents &&
               isCurrentHitObjectASliderAndHasItsStartCircleFinishedAfterClickEvents) {
                // in here if a slider had its startcircle clicked successfully in this update iteration

                if(notelockType == 2)  // osu!stable
                {
                    // edge case: frame perfect double tapping on overlapping sliders would incorrectly eat the second
                    // input, because the isStartCircleFinished() 2b edge case check handling happens before
                    // m_hitobjects[i]->onClickEvent(this->clicks); so, we check if the currentSliderPointer got its
                    // isStartCircleFinished() within this m_hitobjects[i]->onClickEvent(this->clicks); and unlock
                    // blockNextNotes if that is the case note that we still only unlock within duration + tolerance2B
                    // (same as in (1))
                    if((i + 1) < this->hitobjects.size()) {
                        if((this->hitobjects[i + 1]->getClickTime() <= (curHobj->getEndTime() + tolerance2B)))
                            blockNextNotes = false;
                    }
                }
            }

            // note blocking / notelock (2.2)
            if(!isCurrentHitObjectFinishedBeforeClickEvents && isCurrentHitObjectFinishedAfterClickEvents) {
                // in here if a hitobject has been clicked (and finished completely) successfully in this update
                // iteration

                blockNextNotes = false;

                if(notelockType == 1)  // neomod
                {
                    // auto miss all previous unfinished hitobjects, always
                    // (can stop reverse iteration once we get to the first finished hitobject)

                    for(int m = i - 1; m >= 0; m--) {
                        if(this->hitobjects[m]->isFinished()) break;

                        // spinners are completely ignored (transparent)
                        if(this->hitobjects[m]->isSpinner()) continue;

                        // NOTE: 2b exception. only force miss if objects are not overlapping.
                        if(curHobj->getClickTime() > this->hitobjects[m]->getEndTime()) {
                            this->hitobjects[m]->miss(this->iCurMusicPos);
                        }
                    }
                } else if(notelockType == 2)  // osu!stable
                {
                    // (nothing, handled in (1) and (2.1) blocks)
                } else if(notelockType == 3)  // osu!lazer 2020
                {
                    // auto miss all previous unfinished hitobjects if the current music time is > their time (center)
                    // (can stop reverse iteration once we get to the first finished hitobject)

                    for(int m = i - 1; m >= 0; m--) {
                        if(this->hitobjects[m]->isFinished()) break;

                        // spinners are completely ignored (transparent)
                        if(this->hitobjects[m]->isSpinner()) continue;

                        if(this->iCurMusicPos > this->hitobjects[m]->getClickTime()) {
                            // NOTE: 2b exception. only force miss if objects are not overlapping.
                            if(curHobj->getClickTime() > this->hitobjects[m]->getEndTime()) {
                                this->hitobjects[m]->miss(this->iCurMusicPos);
                            }
                        }
                    }
                }
            }

            // main hitobject update (after click events, so a press on the same frame as an object end still hits)
            curHobj->update(this->iCurMusicPos, frameTimeSecs);

            if(curHobj->isFinished()) {
                if(isCircle) this->iCurrentNumCircles++;
                if(isSlider) this->iCurrentNumSliders++;
                if(isSpinner) this->iCurrentNumSpinners++;

                this->iCurrentHitObjectIndex = i;
            }

            // notes per second
            const i32 npsHalfGateSizeMS = (i32)(500.0f * this->getSpeedMultiplier());
            if(curHobj->getClickTime() > this->iCurMusicPos - npsHalfGateSizeMS &&
               curHobj->getClickTime() < this->iCurMusicPos + npsHalfGateSizeMS)
                this->iNPS++;

            // note density
            if(curHobj->isVisible()) this->iND++;
        }

        // miss hiterrorbar slots
        // this gets the closest previous unfinished hitobject, as well as all following hitobjects which are in 50
        // range and could be clicked
        if(cv::hiterrorbar_misaims.getBool()) {
            this->misaimObjects.clear();
            HitObject *lastUnfinishedHitObject = nullptr;
            const i32 hitWindow50 = (i32)this->getHitWindow50();
            for(const auto &hitobject : this->hitobjects)  // this shouldn't hurt performance too much, since no
                                                           // expensive operations are happening within the loop
            {
                if(!hitobject->isFinished()) {
                    if(this->iCurMusicPos >= hitobject->getClickTime())
                        lastUnfinishedHitObject = hitobject.get();
                    else if(std::abs(hitobject->getClickTime() - this->iCurMusicPos) < hitWindow50)
                        this->misaimObjects.push_back(hitobject.get());
                    else
                        break;
                }
            }
            if(lastUnfinishedHitObject != nullptr &&
               std::abs(lastUnfinishedHitObject->getClickTime() - this->iCurMusicPos) < hitWindow50)
                this->misaimObjects.insert(this->misaimObjects.begin(), lastUnfinishedHitObject);

            // now, go through the remaining clicks, and go through the unfinished hitobjects.
            // handle misaim clicks sequentially (setting the misaim flag on the hitobjects to only allow 1 entry in the
            // hiterrorbar for misses per object) clicks don't have to be consumed here, as they are deleted below
            // anyway
            for(const auto &click : this->clicks) {
                for(auto *misaimObject : this->misaimObjects) {
                    if(misaimObject->hasMisAimed())  // only 1 slot per object!
                        continue;

                    misaimObject->misAimed();
                    const i32 delta = click.musicPosMS - (i32)misaimObject->getClickTime();
                    if(!this->score.simulating) ui->getHUD()->addHitError(delta, false, true);

                    break;  // the current click has been dealt with (and the hitobject has been misaimed)
                }
            }
        }

        // all remaining clicks which have not been consumed by any hitobjects can safely be deleted
        if(!this->clicks.empty()) {
            // nightmare mod: extra clicks = sliderbreak
            bool break_on_extra_click = mods.has(ModFlags::StrictClicks);
            break_on_extra_click &= !this->bIsInSkippableSection && !this->bInBreak && !spinner_active;
            break_on_extra_click &= this->iCurrentHitObjectIndex > 0;
            if(break_on_extra_click) {
                this->addSliderBreak();
                this->addHitResult(nullptr, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                   false);  // only decrease health
            }

            this->clicks.clear();
        }
    }

    // empty section detection
    {
        const i32 legacyOffset = (this->iPreviousHitObjectTime < this->hitobjects[0]->getClickTime() ? 0 : 1000);  // Mc
        const i32 nextHitObjectDelta = this->iNextHitObjectTime - this->iCurMusicPos;
        // (before the first one there's no previous one, so a storyboard's lead-in before 0 counts too)
        const bool beforeFirst = this->iCurMusicPos < this->hitobjects[0]->getClickTime();
        if(nextHitObjectDelta > 0 && nextHitObjectDelta > (i32)cv::skip_time.getInt() &&
           (beforeFirst || this->iCurMusicPos > (this->iPreviousHitObjectTime + legacyOffset)))
            this->bIsInSkippableSection = true;
        else if(!cv::end_skip.getBool() && nextHitObjectDelta < 0)
            this->bIsInSkippableSection = true;
        else
            this->bIsInSkippableSection = false;

        // nothing can be skipped after the start in multiplayer rooms
        if(BanchoState::is_playing_a_multi_map() && this->iCurrentHitObjectIndex > 0) {
            this->bIsInSkippableSection = false;
        }
    }

    // break time detection
    const DBType::BREAK breakEvent =
        this->getBreakForTimeRange(this->iPreviousHitObjectTime, this->iCurMusicPos, this->iNextHitObjectTime);
    const bool inBreak = (this->iCurMusicPos >= breakEvent.startTime && this->iCurMusicPos <= breakEvent.endTime);
    if(inBreak && !this->bInBreak) {
        this->bBreakPassed = this->fHealth >= 0.5;
        // (a break long enough to show its own result)
        if(breakEvent.endTime - breakEvent.startTime > 2880) {
            this->iNumLongBreaks++;
            if(this->bBreakPassed) this->iNumLongBreaksPassed++;
        }
    }
    this->bInBreak = inBreak;

    // hp drain & failing
    // handle constant drain
    if(this->fDrainRate > 0.0 && !this->bInBreak && !this->bIsInSkippableSection) {
        // special case: break drain edge cases
        bool drainAfterLastHitobjectBeforeBreakStart = (this->getMapVersion() < 8);

        const bool isBetweenHitobjectsAndBreak = this->iPreviousHitObjectTime <= breakEvent.startTime &&
                                                 this->iNextHitObjectTime >= breakEvent.endTime &&
                                                 this->iCurMusicPos > this->iPreviousHitObjectTime;
        const bool isLastHitobjectBeforeBreakStart =
            isBetweenHitobjectsAndBreak && this->iCurMusicPos <= breakEvent.startTime;

        if(!isBetweenHitobjectsAndBreak ||
           (drainAfterLastHitobjectBeforeBreakStart && isLastHitobjectBeforeBreakStart)) {
            // special case: spinner nerf
            f64 spinnerDrainNerf = this->bIsSpinnerActive ? 0.25 : 1.0;
            this->addHealth(-this->fDrainRate * frameTimeSecs * (f64)this->getSpeedMultiplier() * spinnerDrainNerf,
                            false);
        }
    }

    // revive in mp
    if(this->fHealth > 0.999 && this->score.isDead()) this->score.setDead(false);

    // update auto (after having updated the hitobjects)
    if(flags::any<ModFlags::Autoplay | ModFlags::Autopilot>(mods.flags)) this->updateAutoCursorPos();

    // spinner detection (used by osu!stable drain, and by HUD for not drawing the hiterrorbar)
    if(this->currentHitObject != nullptr) {
        this->bIsSpinnerActive = this->currentHitObject->isSpinner() &&
                                 this->iCurMusicPos > this->currentHitObject->getClickTime() &&
                                 this->iCurMusicPos < this->currentHitObject->getEndTime();
    }
}

void GameplayJudge::updateAutoCursorPos() {
    const vec2 playfieldCenter{GameRules::OSU_COORD_WIDTH / 2, GameRules::OSU_COORD_HEIGHT / 2};
    this->vAutoCursorPos = playfieldCenter;
    this->vAutoCursorPos.y *= 2.5f;  // start moving in offscreen from bottom

    if(this->hitobjects.empty()) {
        this->vAutoCursorPos = playfieldCenter;
        return;
    }

    const auto &mods = this->getMods();
    const bool autopilot = mods.has(ModFlags::Autopilot);
    const i32 curMusicPos = this->iCurMusicPos;

    // general
    i32 prevTime = 0;
    i32 nextTime = this->hitobjects[0]->getClickTime();
    vec2 prevPos = this->vAutoCursorPos;
    vec2 curPos = this->vAutoCursorPos;
    vec2 nextPos = this->vAutoCursorPos;
    bool haveCurPos = false;

    // dance
    int nextPosIndex = 0;

    if(this->hitobjects[0]->getClickTime() < cv::early_note_time.getInt())
        prevTime = (int)std::roundf((float)-cv::early_note_time.getInt() * this->getSpeedMultiplier());

    if(!autopilot) {
        bool autoDanceOverride = false;
        for(int i = 0; i < this->hitobjects.size(); i++) {
            HitObject *o = this->hitobjects[i].get();

            // get previous object
            if(o->getClickTime() <= curMusicPos) {
                prevTime = o->getEndTime();
                prevPos = o->getAutoCursorPos(curMusicPos);
                if(o->getDuration() > 0 && curMusicPos - o->getClickTime() <= o->getDuration()) {
                    if(cv::auto_cursordance.getBool()) {
                        auto *sliderPointer = o->isSlider() ? static_cast<Slider *>(o) : nullptr;
                        if(sliderPointer != nullptr) {
                            const std::vector<Slider::SLIDERCLICK> &clicks = sliderPointer->getClicks();

                            // start
                            prevTime = o->getClickTime();
                            prevPos = this->osuCoords2Pixels(o->getRawPosAt(prevTime));

                            i32 biggestPrevious = 0;
                            i32 smallestNext = (std::numeric_limits<i32>::max)();
                            bool allFinished = true;
                            i32 endTime = 0;

                            // middle clicks
                            for(const auto &click : clicks) {
                                // get previous click
                                if(click.timeMS <= curMusicPos && click.timeMS > biggestPrevious) {
                                    biggestPrevious = click.timeMS;
                                    prevTime = click.timeMS;
                                    prevPos = this->osuCoords2Pixels(o->getRawPosAt(prevTime));
                                }

                                // get next click
                                if(click.timeMS > curMusicPos && click.timeMS < smallestNext) {
                                    smallestNext = click.timeMS;
                                    nextTime = click.timeMS;
                                    nextPos = this->osuCoords2Pixels(o->getRawPosAt(nextTime));
                                }

                                // end hack
                                if(!click.finished)
                                    allFinished = false;
                                else if(click.timeMS > endTime)
                                    endTime = click.timeMS;
                            }

                            // end
                            if(allFinished) {
                                // hack for slider without middle clicks
                                if(endTime == 0) endTime = o->getClickTime();

                                prevTime = endTime;
                                prevPos = this->osuCoords2Pixels(o->getRawPosAt(prevTime));
                                nextTime = o->getEndTime();
                                nextPos = this->osuCoords2Pixels(o->getRawPosAt(nextTime));
                            }

                            haveCurPos = false;
                            autoDanceOverride = true;
                            break;
                        }
                    }

                    haveCurPos = true;
                    curPos = prevPos;
                    break;
                }
            }

            // get next object
            if(o->getClickTime() > curMusicPos) {
                nextPosIndex = i;
                if(!autoDanceOverride) {
                    nextPos = o->getAutoCursorPos(curMusicPos);
                    nextTime = o->getClickTime();
                }
                break;
            }
        }
    } else {
        for(int i = 0; i < this->hitobjects.size(); i++) {
            HitObject *o = this->hitobjects[i].get();

            // get previous object
            if(o->isFinished() ||
               (curMusicPos > o->getEndTime() + (i32)(this->getHitWindow50() * mods.autopilot_lenience))) {
                prevTime = o->getEndTime() + o->getAutopilotDelta();
                prevPos = o->getAutoCursorPos(curMusicPos);
            } else if(!o->isFinished())  // get next object
            {
                nextPosIndex = i;
                nextPos = o->getAutoCursorPos(curMusicPos);
                nextTime = o->getClickTime();

                // wait for the user to click
                if(curMusicPos >= nextTime + o->getDuration()) {
                    haveCurPos = true;
                    curPos = nextPos;

                    o->setAutopilotDelta(curMusicPos - (nextTime + o->getDuration()));
                } else if(o->getDuration() > 0 && curMusicPos >= nextTime)  // handle objects with duration
                {
                    haveCurPos = true;
                    curPos = nextPos;
                    o->setAutopilotDelta(0);
                }

                break;
            }
        }
    }

    if(haveCurPos)  // in active hitObject
        this->vAutoCursorPos = curPos;
    else {
        // interpolation
        f32 percent = 1.0f;
        if((nextTime == 0 && prevTime == 0) || (nextTime - prevTime) == 0)
            percent = 1.0f;
        else
            percent = (f32)((i32)curMusicPos - prevTime) / (f32)(nextTime - prevTime);

        percent = std::clamp<f32>(percent, 0.0f, 1.0f);

        // osu!px, like the diameter
        f32 distance = vec::length((nextPos - prevPos));
        if(distance > this->fHitcircleDiameter * 1.05f)  // snap only if not in a stream (heuristic)
        {
            int numIterations = std::clamp<int>(
                autopilot ? cv::autopilot_snapping_strength.getInt() : cv::auto_snapping_strength.getInt(), 0, 42);
            for(int i = 0; i < numIterations; i++) {
                percent = (-percent) * (percent - 2.0f);
            }
        } else  // in a stream
        {
            this->iAutoCursorDanceIndex = nextPosIndex;
        }

        this->vAutoCursorPos = prevPos + (nextPos - prevPos) * percent;

        if(cv::auto_cursordance.getBool() && !autopilot) {
            vec3 dir = vec3(nextPos.x, nextPos.y, 0) - vec3(prevPos.x, prevPos.y, 0);
            vec3 center = dir * 0.5f;
            Matrix4 worldMatrix;
            worldMatrix.translate(center);
            worldMatrix.rotate((1.0f - percent) * 180.0f * (this->iAutoCursorDanceIndex % 2 == 0 ? 1 : -1), 0, 0, 1);
            vec3 fancyAutoCursorPos = worldMatrix * center;
            this->vAutoCursorPos =
                prevPos + (nextPos - prevPos) * 0.5f + vec2(fancyAutoCursorPos.x, fancyAutoCursorPos.y);
        }
    }
}

void GameplayJudge::updateHitobjectMetrics() {
    // in osu!px like the positions it's judged against (osuCoords2Pixels and the cursor aren't scaled)
    this->fRawHitcircleDiameter = GameRules::getRawHitCircleDiameter(this->getCS());
    this->fHitcircleDiameter = this->fRawHitcircleDiameter;

    const auto &mods = this->getMods();
    const f32 sliderFollowCircleDiameterMultiplier =
        mods.has(ModFlags::PreciseSliders)
            ? (1.0f * (1.0f - mods.jigsaw_followcircle_radius_factor) +
               mods.jigsaw_followcircle_radius_factor * GameRules::SLIDER_FOLLOW_CIRCLE_MULTIPLIER)
            : GameRules::SLIDER_FOLLOW_CIRCLE_MULTIPLIER;
    this->fSliderFollowCircleDiameter = this->fHitcircleDiameter * sliderFollowCircleDiameterMultiplier;
}

void GameplayJudge::calculateStacks() {
    this->updateHitobjectMetrics();

    debugLog("Beatmap: Calculating stacks ...");

    HitObjects::stack(this->hitobjects, this->getAR(), this->getMapVersion(), this->getMapStackLeniency(),
                      this->fRawHitcircleDiameter, this->getMods().has(ModFlags::HardRock));
}

void GameplayJudge::computeDrainRate() {
    this->fDrainRate = 0.0;
    this->fHpMultiplierNormal = 1.0;
    this->fHpMultiplierComboEnd = 1.0;

    if(this->hitobjects.empty()) return;

    debugLog("Beatmap: Calculating drain ...");

    {
        // see https://github.com/ppy/osu-iPhone/blob/master/Classes/OsuPlayer.m
        // see calcHPDropRate() @ https://github.com/ppy/osu-iPhone/blob/master/Classes/OsuFiletype.m#L661

        // NOTE: all drain changes between 2014 and today have been fixed here (the link points to an old version of the
        // algorithm!) these changes include: passive spinner nerf (drain * 0.25 while spinner is active), and clamping
        // the object length drain to 0 + an extra check for that (see maxLongObjectDrop) see
        // https://osu.ppy.sh/home/changelog/stable40/20190513.2

        struct TestPlayer {
            TestPlayer(f64 hpBarMaximum) {
                this->hpBarMaximum = hpBarMaximum;

                this->hpMultiplierNormal = 1.0;
                this->hpMultiplierComboEnd = 1.0;

                this->resetHealth();
            }

            void resetHealth() {
                this->health = this->hpBarMaximum;
                this->healthUncapped = this->hpBarMaximum;
            }

            void increaseHealth(f64 amount) {
                this->healthUncapped += amount;
                this->health += amount;

                if(this->health > this->hpBarMaximum) this->health = this->hpBarMaximum;

                if(this->health < 0.0) this->health = 0.0;

                if(this->healthUncapped < 0.0) this->healthUncapped = 0.0;
            }

            void decreaseHealth(f64 amount) {
                this->health -= amount;

                if(this->health < 0.0) this->health = 0.0;

                if(this->health > this->hpBarMaximum) this->health = this->hpBarMaximum;

                this->healthUncapped -= amount;

                if(this->healthUncapped < 0.0) this->healthUncapped = 0.0;
            }

            f64 hpBarMaximum;

            f64 health;
            f64 healthUncapped;

            f64 hpMultiplierNormal;
            f64 hpMultiplierComboEnd;
        };
        TestPlayer testPlayer(200.0);

        const f64 HP = this->getHP();
        const int version = this->getMapVersion();

        f64 testDrop = 0.05;

        const f64 lowestHpEver = GameRules::mapDifficultyRange(HP, 195.0, 160.0, 60.0);
        const f64 lowestHpComboEnd = GameRules::mapDifficultyRange(HP, 198.0, 170.0, 80.0);
        const f64 lowestHpEnd = GameRules::mapDifficultyRange(HP, 198.0, 180.0, 80.0);
        const f64 HpRecoveryAvailable = GameRules::mapDifficultyRange(HP, 8.0, 4.0, 0.0);

        bool fail = false;

        do {
            testPlayer.resetHealth();

            f64 lowestHp = testPlayer.health;
            int lastTime = (int)(this->hitobjects[0]->getClickTime() - (i32)this->getApproachTime());
            fail = false;

            const int breakCount = this->breaks.size();
            int breakNumber = 0;

            int comboTooLowCount = 0;

            for(int i = 0; i < this->hitobjects.size(); i++) {
                const HitObject *h = this->hitobjects[i].get();
                const auto *sliderPointer = h->isSlider() ? static_cast<const Slider *>(h) : nullptr;
                const auto *spinnerPointer = h->isSpinner() ? static_cast<const Spinner *>(h) : nullptr;

                const int localLastTime = lastTime;

                int breakTime = 0;
                if(breakCount > 0 && breakNumber < breakCount) {
                    const DBType::BREAK &e = this->breaks[breakNumber];
                    if(e.startTime >= localLastTime && e.endTime <= h->getClickTime()) {
                        // consider break start equal to object end time for version 8+ since drain stops during this
                        // time
                        breakTime = (version < 8) ? (e.endTime - e.startTime) : (e.endTime - localLastTime);
                        breakNumber++;
                    }
                }

                testPlayer.decreaseHealth(testDrop * (h->getClickTime() - lastTime - breakTime));

                lastTime = (int)(h->getEndTime());

                if(testPlayer.health < lowestHp) lowestHp = testPlayer.health;

                if(testPlayer.health > lowestHpEver) {
                    const f64 longObjectDrop = testDrop * (f64)h->getDuration();
                    const f64 maxLongObjectDrop = std::max(0.0, longObjectDrop - testPlayer.health);

                    testPlayer.decreaseHealth(longObjectDrop);

                    // nested hitobjects
                    if(sliderPointer != nullptr) {
                        // startcircle
                        testPlayer.increaseHealth(
                            LiveScore::getHealthIncrease(LiveHitResult::HIT_SLIDER30, HP, testPlayer.hpMultiplierNormal,
                                                         testPlayer.hpMultiplierComboEnd, 1.0));  // slider30

                        // ticks + repeats + repeat ticks
                        const std::vector<Slider::SLIDERCLICK> &clicks = sliderPointer->getClicks();
                        for(const auto &click : clicks) {
                            switch(click.type) {
                                case 0:  // repeat
                                    testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                                        LiveHitResult::HIT_SLIDER30, HP, testPlayer.hpMultiplierNormal,
                                        testPlayer.hpMultiplierComboEnd, 1.0));  // slider30
                                    break;
                                case 1:  // tick
                                    testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                                        LiveHitResult::HIT_SLIDER10, HP, testPlayer.hpMultiplierNormal,
                                        testPlayer.hpMultiplierComboEnd, 1.0));  // slider10
                                    break;
                            }
                        }

                        // endcircle
                        testPlayer.increaseHealth(
                            LiveScore::getHealthIncrease(LiveHitResult::HIT_SLIDER30, HP, testPlayer.hpMultiplierNormal,
                                                         testPlayer.hpMultiplierComboEnd, 1.0));  // slider30
                    } else if(spinnerPointer != nullptr) {
                        const i32 halfSpinsNeeded =
                            GameRules::getSpinnerHalfSpinsRequired(this, spinnerPointer->getDuration());
                        for(i32 r = 0; r < halfSpinsNeeded; r++) {
                            testPlayer.increaseHealth(LiveScore::getHealthIncrease(
                                LiveHitResult::HIT_SPINNERSPIN, HP, testPlayer.hpMultiplierNormal,
                                testPlayer.hpMultiplierComboEnd, 1.0));  // spinnerspin
                        }
                    }

                    if(!(maxLongObjectDrop > 0.0) || (testPlayer.health - maxLongObjectDrop) > lowestHpEver) {
                        // regular hit (for every hitobject)
                        testPlayer.increaseHealth(
                            LiveScore::getHealthIncrease(LiveHitResult::HIT_300, HP, testPlayer.hpMultiplierNormal,
                                                         testPlayer.hpMultiplierComboEnd, 1.0));  // 300

                        // end of combo (new combo starts at next hitobject)
                        if((i == this->hitobjects.size() - 1) || this->hitobjects[i]->isEndOfCombo()) {
                            testPlayer.increaseHealth(
                                LiveScore::getHealthIncrease(LiveHitResult::HIT_300G, HP, testPlayer.hpMultiplierNormal,
                                                             testPlayer.hpMultiplierComboEnd, 1.0));  // geki

                            if(testPlayer.health < lowestHpComboEnd) {
                                if(++comboTooLowCount > 2) {
                                    testPlayer.hpMultiplierComboEnd *= 1.07;
                                    testPlayer.hpMultiplierNormal *= 1.03;
                                    fail = true;
                                    break;
                                }
                            }
                        }

                        continue;
                    }

                    fail = true;
                    testDrop *= 0.96;
                    break;
                }

                fail = true;
                testDrop *= 0.96;
                break;
            }

            if(!fail && testPlayer.health < lowestHpEnd) {
                fail = true;
                testDrop *= 0.94;
                testPlayer.hpMultiplierComboEnd *= 1.01;
                testPlayer.hpMultiplierNormal *= 1.01;
            }

            const f64 recovery = (testPlayer.healthUncapped - testPlayer.hpBarMaximum) / (f64)this->hitobjects.size();
            if(!fail && recovery < HpRecoveryAvailable) {
                fail = true;
                testDrop *= 0.96;
                testPlayer.hpMultiplierComboEnd *= 1.02;
                testPlayer.hpMultiplierNormal *= 1.01;
            }
        } while(fail);

        this->fDrainRate =
            (testDrop / testPlayer.hpBarMaximum) * 1000.0;  // from [0, 200] to [0, 1], and from ms to seconds
        this->fHpMultiplierComboEnd = testPlayer.hpMultiplierComboEnd;
        this->fHpMultiplierNormal = testPlayer.hpMultiplierNormal;
    }
}
