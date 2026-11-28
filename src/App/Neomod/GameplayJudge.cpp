#include "GameplayJudge.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Bancho.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "GameRules.h"
#include "HitObjects.h"
#include "HitSounds.h"
#include "HUD.h"
#include "Logging.h"
#include "Matrices.h"
#include "OsuConVars.h"
#include "PlayfieldView.h"
#include "Replay.h"
#include "Timing.h"
#include "UI.h"
#include "crypto.h"
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
void GameplayJudge::Listener::onHitResult(i32 /*object*/, LiveHitResult /*hit*/, i32 /*delta*/, bool /*timed*/) {}
void GameplayJudge::Listener::onComboBreak() {}
void GameplayJudge::Listener::onExtraClick() {}
void GameplayJudge::Listener::onHealthGain(f64 /*healthBefore*/) {}
bool GameplayJudge::Listener::onFail(bool /*byMods*/) { return true; }

GameplayJudge::GameplayJudge(DatabaseBeatmap *map, LiveScore &score, const PlayfieldView *view, Listener *listener)
    : beatmap(map), score(score), view(view), listener(listener) {
    this->nb_hitobjects = map->getNumObjects();
    this->updateHitobjectMetrics();
}

GameplayJudge::~GameplayJudge() { this->unload(); }

Primitives::LoadError GameplayJudge::load(std::shared_ptr<const std::vector<u8>> fileData) {
    this->unload();
    this->resetScore();

    // some hitobjects already need this information to be up-to-date before their constructor is called
    this->updateHitobjectMetrics();

    // the play goes by what it reads from the file now (objects ask for the map's settings while they're made)
    this->mapData = std::make_unique<PlayedMap>(this->beatmap->readPlayedMap(std::move(fileData)));
    DatabaseBeatmap::LOAD_GAMEPLAY_RESULT result =
        DatabaseBeatmap::loadGameplay(*this->mapData, this->hitobjects, this);
    if(result.error.errc) {
        this->hitobjects.clear();
        this->mapData.reset();
        return result.error;
    }
    this->nb_hitobjects = this->mapData->primitives.getNumObjects();
    this->breaks = std::move(result.breaks);
    this->comboColors = std::move(result.combocolors);
    this->defaultSampleSet = result.defaultSampleSet;

    // the drawing order is different from the playing/input order.
    // for drawing, if multiple hitobjects occupy the exact same time (duration) then they get drawn on top of the
    // active hitobject
    const HitObjects &objs = this->hitobjects;
    const uSz numObjects = objs.size();
    this->hitobjectsSortedByEndTime.resize(numObjects);
    for(uSz i = 0; i < numObjects; i++) this->hitobjectsSortedByEndTime[i] = (u32)i;
    Mc::ranges::stable_sort(this->hitobjectsSortedByEndTime, {}, [&objs](u32 i) { return objs.endTimes[i]; });

    this->latestTimeBefore.assign(numObjects + 1, std::numeric_limits<i32>::min());
    this->latestTimeBeforeByEndTime.assign(numObjects + 1, std::numeric_limits<i32>::min());
    this->numCirclesBefore.assign(numObjects + 1, 0);
    this->numSlidersBefore.assign(numObjects + 1, 0);
    for(uSz i = 0; i < numObjects; i++) {
        const u32 byEnd = this->hitobjectsSortedByEndTime[i];
        this->latestTimeBefore[i + 1] = std::max({this->latestTimeBefore[i], objs.clickTimes[i], objs.endTimes[i]});
        this->latestTimeBeforeByEndTime[i + 1] =
            std::max({this->latestTimeBeforeByEndTime[i], objs.clickTimes[byEnd], objs.endTimes[byEnd]});
        this->numCirclesBefore[i + 1] = this->numCirclesBefore[i] + (objs.types[i] == HitObjectType::CIRCLE ? 1 : 0);
        this->numSlidersBefore[i + 1] = this->numSlidersBefore[i] + (objs.types[i] == HitObjectType::SLIDER ? 1 : 0);
    }
    this->earliestStartFrom.assign(numObjects + 1, std::numeric_limits<i32>::max());
    for(uSz i = numObjects; i-- > 0;) {
        this->earliestStartFrom[i] =
            std::min(this->earliestStartFrom[i + 1], objs.comboStartTimes[this->hitobjectsSortedByEndTime[i]]);
    }

    // after the hitobjects have been loaded we can calculate the stacks
    this->calculateStacks();
    this->computeDrainRate();

    return result.error;
}

void GameplayJudge::unload() {
    // what the objects still play stops with them
    for(const SliderState &slider : this->hitobjects.sliders) this->stopSliderSounds(slider.lastSliderSampleSets);
    for(uSz i = 0; i < this->hitobjects.spinners.size(); i++) this->stopSpinnerSpinSound();
    this->mapData.reset();
    this->currentHitObject = -1;
    this->iCurrentHitObjectIndex = 0;
    this->iCurrentNumCircles = 0;
    this->iCurrentNumSliders = 0;
    this->iCurrentNumSpinners = 0;
    this->hitobjectsSortedByEndTime.clear();
    this->misaimObjects.clear();
    this->hitobjects.clear();
    this->latestTimeBefore.clear();
    this->latestTimeBeforeByEndTime.clear();
    this->numCirclesBefore.clear();
    this->numSlidersBefore.clear();
    this->earliestStartFrom.clear();
    this->numFinishedFirst = 0;
    this->numFinishedFirstByEndTime = 0;
    this->breaks.clear();
    this->clicks.clear();
}

void GameplayJudge::reset(i32 curPos, bool leaveOutStarted) {
    this->resetScore();
    this->numFinishedFirst = 0;
    this->numFinishedFirstByEndTime = 0;
    this->lookTiming = HitObjectDrawing::lookTiming(*this, this->view);
    for(uSz i = 0; i < this->hitobjects.size(); i++) {
        // (past its end, it's done)
        const i32 pos = leaveOutStarted && this->hitobjects.clickTimes[i] <= curPos
                            ? std::max(curPos, this->hitobjects.endTimes[i] + 1)
                            : curPos;
        this->resetObject((u32)i, pos);
        HitObjectDrawing::animate(this->hitobjects, i, pos, this->lookTiming);
        this->updateObject((u32)i, pos, 0.0);
        this->resetObject((u32)i, pos);
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

void GameplayJudge::step(const Replay::Frame &frame, f64 frameTimeSecs, u8 clickKeys) {
    this->bNeedsReplayFrame = false;

    // replays have both K1 and M1 set when K1 is pressed (unless NoKeylock mod is on)
    u8 keys = frame.key_flags;
    if(!this->getMods().has(ModFlags::NoKeylock)) {
        if(keys & Replay::K1) keys &= ~Replay::M1;
        if(keys & Replay::K2) keys &= ~Replay::M2;
    }

    const vec2 cursorPos{frame.x, frame.y};
    const bool countsKeys = !this->bInBreak && !this->bIsInSkippableSection &&
                            (this->hitobjects.empty() || frame.cur_music_pos >= this->hitobjects.clickTimes[0]);
    for(auto key : {Replay::K1, Replay::K2, Replay::M1, Replay::M2}) {
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
            if(keys & Replay::K1) keys &= ~Replay::M1;
            if(keys & Replay::K2) keys &= ~Replay::M2;
        }
        return keys;
    };

    // NOTE: the last frame is processed as well (unlike watch playback which interpolates towards it), so that
    //       objects ending after the second-to-last frame still get judged
    while(this->current_frame_idx + 1 < (i32)this->replay.size() &&
          this->replay[this->current_frame_idx + 1].cur_music_pos <= musicPos) {
        const Replay::Frame &prev_frame = this->replay[this->current_frame_idx];
        this->current_frame_idx++;
        const Replay::Frame &frame = this->replay[this->current_frame_idx];

        const u8 lastKeys = keylocked(prev_frame.key_flags);
        const u8 keys = keylocked(frame.key_flags);
        u8 clickKeys = keys & ~lastKeys;
        // with keylock, K1/M1 and K2/M2 each act as one button, which only clicks when it goes down.
        // a key taking over the hold from the other one of its pair (K1|M1 -> M1) is no new click.
        if(keylock) {
            if(lastKeys & (Replay::K1 | Replay::M1)) clickKeys &= ~(Replay::K1 | Replay::M1);
            if(lastKeys & (Replay::K2 | Replay::M2)) clickKeys &= ~(Replay::K2 | Replay::M2);
        }
        if(this->getMods().has(ModFlags::DKS)) clickKeys |= lastKeys & ~keys;

        // cv::auto_and_relax_block_user_input
        if((this->getMods().has(ModFlags::Autoplay) || this->getMods().has(ModFlags::Relax)) &&
           !this->getMods().has(ModFlags::RelaxInputs)) {
            clickKeys = 0;
        }

        this->step(frame, (f64)(frame.cur_music_pos - prev_frame.cur_music_pos) / 1000.0 / this->getSpeedMultiplier(),
                   clickKeys);
    }
}

void GameplayJudge::animate(f64 curPos) {
    const i32 pvs = this->getPVS();
    const bool freezeFrame = this->getMods().has(ModFlags::FreezeFrame);
    HitObjects &objs = this->hitobjects;
    this->lookTiming = HitObjectDrawing::lookTiming(*this, this->view);
    for(uSz i = this->getFirstLiveObject(curPos - pvs); i < objs.size(); i++) {
        if(objs.finished[i] && curPos - pvs > objs.endTimes[i]) continue;
        if((freezeFrame ? objs.comboStartTimes[i] : objs.clickTimes[i]) > curPos + pvs) break;
        HitObjectDrawing::animate(objs, i, curPos, this->lookTiming);
    }
}

void GameplayJudge::updateAll(i32 curPos, f64 frameTimeSecs) {
    this->lookTiming = HitObjectDrawing::lookTiming(*this, this->view);
    for(uSz i = 0; i < this->hitobjects.size(); i++) {
        HitObjectDrawing::animate(this->hitobjects, i, curPos, this->lookTiming);
        this->updateObject((u32)i, curPos, frameTimeSecs);
    }
}

void GameplayJudge::updateObject(u32 i, i32 curPosMS, f64 frameTimeSecs) {
    switch(this->hitobjects.types[i]) {
        case HitObjectType::CIRCLE:
            this->updateCircle(i, curPosMS);
            break;
        case HitObjectType::SLIDER:
            this->updateSlider(i, curPosMS);
            break;
        case HitObjectType::SPINNER:
            this->updateSpinner(i, curPosMS, frameTimeSecs);
            break;
    }
}

void GameplayJudge::resetObject(u32 i, i32 curPosMS) {
    switch(this->hitobjects.types[i]) {
        case HitObjectType::CIRCLE:
            this->resetCircle(i, curPosMS);
            break;
        case HitObjectType::SLIDER:
            this->resetSlider(i, curPosMS);
            break;
        case HitObjectType::SPINNER:
            this->resetSpinner(i, curPosMS);
            break;
    }
}

bool GameplayJudge::isClickable(u32 i, i32 musicPos, vec2 cursorPos) const {
    switch(this->hitobjects.types[i]) {
        case HitObjectType::CIRCLE:
            return this->isCircleClickable(i, musicPos, cursorPos);
        case HitObjectType::SLIDER:
            return this->isSliderClickable(i, musicPos, cursorPos);
        case HitObjectType::SPINNER:
            break;
    }
    return false;
}

vec2 GameplayJudge::getAutoCursorPos(u32 i, i32 curPosMS) const {
    switch(this->hitobjects.types[i]) {
        case HitObjectType::CIRCLE:
            break;
        case HitObjectType::SLIDER:
            return this->hitobjects.sliderAt(i).curPoint;
        case HitObjectType::SPINNER:
            return this->getSpinnerAutoCursorPos(i, curPosMS);
    }
    return this->osuCoords2Pixels(this->hitobjects.positions[i]);
}

void GameplayJudge::missObject(u32 i, i32 curPosMS) {
    if(this->hitobjects.types[i] == HitObjectType::CIRCLE) this->missCircle(i, curPosMS);
    if(this->hitobjects.types[i] == HitObjectType::SLIDER) this->missSlider(i, curPosMS);
}

void GameplayJudge::addObjectHitResult(u32 i, LiveHitResult result, i32 delta, bool isEndOfCombo, vec2 posRaw,
                                       f32 targetDelta, f32 targetAngle, bool ignoreOnHitErrorBar, bool ignoreCombo,
                                       bool ignoreHealth, bool addObjectDurationToSkinAnimationTimeStartOffset) {
    if(this->getMods().has(ModFlags::Target) && result != LiveHitResult::HIT_MISS && targetDelta >= 0.0f) {
        const f32 p300 = cv::mod_target_300_percent.getFloat();
        const f32 p100 = cv::mod_target_100_percent.getFloat();
        const f32 p50 = cv::mod_target_50_percent.getFloat();

        if(targetDelta < p300 && (result == LiveHitResult::HIT_300 || result == LiveHitResult::HIT_100))
            result = LiveHitResult::HIT_300;
        else if(targetDelta < p100)
            result = LiveHitResult::HIT_100;
        else if(targetDelta < p50)
            result = LiveHitResult::HIT_50;
        else
            result = LiveHitResult::HIT_MISS;

        this->addTargetHit(targetDelta, targetAngle);
    }

    const LiveHitResult returnedHit = this->addHitResult((i32)i, result, delta, isEndOfCombo, ignoreOnHitErrorBar,
                                                         false, ignoreCombo, false, ignoreHealth);
    this->showHitResult(i, returnedHit != LiveHitResult::HIT_MISS ? returnedHit : result, delta, posRaw,
                        addObjectDurationToSkinAnimationTimeStartOffset);
}

void GameplayJudge::showHitResult(u32 i, LiveHitResult result, i32 delta, vec2 posRaw,
                                  bool addObjectDurationToSkinAnimationTimeStartOffset) {
    HitObjects &objs = this->hitobjects;
    HitObjectLook &l = objs.looks[i];
    if(!this->animatesHits()) return;

    HitResultAnim hitresultanim;
    {
        hitresultanim.result = result;
        hitresultanim.rawPos = posRaw;
        hitresultanim.deltaMS = delta;
        hitresultanim.deltaRangePercent = std::clamp<f32>((f32)delta / this->getHitWindow50(), -1.0f, 1.0f);
        hitresultanim.timeSecs = engine->getTime();
        hitresultanim.addObjectDurationToSkinAnimationTimeStartOffset = addObjectDurationToSkinAnimationTimeStartOffset;
    }

    // currently a maximum of 2 simultaneous results are supported (for drawing, per hitobject)
    if(engine->getTime() >
       l.result1.timeSecs + cv::hitresult_duration_max.getFloat() * (1.0f / this->view->getBaseAnimationSpeed()))
        l.result1 = hitresultanim;
    else
        l.result2 = hitresultanim;
}

bool GameplayJudge::animatesHits() const { return this->view != nullptr && !this->getScore().simulating; }

void GameplayJudge::resetCommon(u32 i) {
    HitObjects &objs = this->hitobjects;
    HitObjectLook &l = objs.looks[i];
    objs.misAimed[i] = false;
    objs.autopilotDeltas[i] = 0;

    l.result1.timeSecs = -9999.0f;
    l.result2.timeSecs = -9999.0f;
}

void GameplayJudge::updateCircle(u32 i, i32 curPosMS) {
    HitObjects &objs = this->hitobjects;
    CircleState &s = objs.circleAt(i);
    if(objs.finished[i]) return;

    const ModFlags curIFaceMods = this->getMods().flags;
    const i32 deltaMS = curPosMS - objs.clickTimes[i];

    if(flags::has<ModFlags::Autoplay>(curIFaceMods)) {
        if(curPosMS >= objs.clickTimes[i]) {
            this->hitCircle(i, LiveHitResult::HIT_300, 0);
        }
        return;
    }

    if(flags::has<ModFlags::Relax>(curIFaceMods)) {
        if(curPosMS >= objs.clickTimes[i] + (i32)cv::relax_offset.getInt()) {
            const vec2 pos = this->osuCoords2Pixels(objs.positions[i]);
            const f32 cursorDelta = vec::length(this->getCursorPos() - pos);
            if((cursorDelta < this->fHitcircleDiameter / 2.0f && (flags::has<ModFlags::Relax>(curIFaceMods)))) {
                LiveHitResult result = this->getHitResult(deltaMS);

                if(result != LiveHitResult::HIT_NULL) {
                    const f32 targetDelta = cursorDelta / (this->fHitcircleDiameter / 2.0f);
                    const f32 targetAngle =
                        vec::degrees(std::atan2(this->getCursorPos().y - pos.y, this->getCursorPos().x - pos.x));

                    this->hitCircle(i, result, deltaMS, targetDelta, targetAngle);
                }
            }
        }
    }

    if(deltaMS >= 0) {
        s.waiting = true;

        // if this is a miss after waiting
        if(deltaMS > (i32)this->getHitWindow50()) {
            this->hitCircle(i, LiveHitResult::HIT_MISS, deltaMS);
        }
    } else {
        s.waiting = false;
    }
}

void GameplayJudge::missCircle(u32 i, i32 curPosMS) {
    HitObjects &objs = this->hitobjects;
    if(objs.finished[i]) return;

    const i32 deltaMS = curPosMS - objs.clickTimes[i];

    this->hitCircle(i, LiveHitResult::HIT_MISS, deltaMS);
}

bool GameplayJudge::isCircleClickable(u32 i, i32 music_pos, vec2 cursor_pos) const {
    const HitObjects &objs = this->hitobjects;
    if(objs.finished[i] || objs.blocked[i]) return false;
    if(this->getHitResult(music_pos - objs.clickTimes[i]) == LiveHitResult::HIT_NULL) return false;

    const vec2 pos = this->osuCoords2Pixels(objs.positions[i]);
    const f32 cursorDelta = vec::length(cursor_pos - pos);
    if(cursorDelta >= this->fHitcircleDiameter / 2.0f) return false;

    return true;
}

void GameplayJudge::clickCircle(u32 i) {
    HitObjects &objs = this->hitobjects;
    CircleState &s = objs.circleAt(i);
    if(objs.finished[i]) return;

    const vec2 cursorPos = clicks[0].cursorPos;
    const vec2 pos = this->osuCoords2Pixels(objs.positions[i]);
    const f32 cursorDelta = vec::length(cursorPos - pos);

    if(cursorDelta < this->fHitcircleDiameter / 2.0f) {
        // note blocking & shake
        if(objs.blocked[i]) {
            if(this->animatesHits()) s.shakeAnimation = engine->getTime() + cv::circle_shake_duration.getFloat();
            return;  // ignore click event completely
        }

        const i32 deltaMS = clicks[0].musicPosMS - objs.clickTimes[i];

        LiveHitResult result = this->getHitResult(deltaMS);
        if(result != LiveHitResult::HIT_NULL) {
            const f32 targetDelta = cursorDelta / (this->fHitcircleDiameter / 2.0f);
            const f32 targetAngle = vec::degrees(std::atan2(cursorPos.y - pos.y, cursorPos.x - pos.x));

            clicks.erase(clicks.begin());
            this->hitCircle(i, result, deltaMS, targetDelta, targetAngle);
        }
    }
}

void GameplayJudge::hitCircle(u32 i, LiveHitResult result, i32 delta, f32 targetDelta, f32 targetAngle) {
    HitObjects &objs = this->hitobjects;
    CircleState &s = objs.circleAt(i);
    // sound and hit animation
    if(result != LiveHitResult::HIT_MISS) {
        this->playHitSound(objs.samples[i], objs.positions[i], delta, objs.clickTimes[i]);

        if(this->animatesHits()) {
            s.hitAnimation = 0.001f;  // quickfix for 1 frame missing images
            s.hitAnimation.set(1.0f, GameRules::getFadeOutTime(this->view->getBaseAnimationSpeed()), anim::QuadOut);
        }
    }

    // add it, and we are finished
    this->addObjectHitResult(i, result, delta, (bool)objs.endsCombo[i], objs.positions[i], targetDelta, targetAngle);
    objs.finished[i] = true;
}

void GameplayJudge::resetCircle(u32 i, i32 curPosMS) {
    HitObjects &objs = this->hitobjects;
    CircleState &s = objs.circleAt(i);
    this->resetCommon(i);

    s.waiting = false;
    s.shakeAnimation = 0.0f;

    if(this->view != nullptr) {
        s.hitAnimation.stop();
    }

    if(objs.clickTimes[i] > curPosMS) {
        objs.finished[i] = false;
        s.hitAnimation = 0.0f;
    } else {
        objs.finished[i] = true;
        s.hitAnimation = 1.0f;
    }
}

void GameplayJudge::updateSlider(u32 i, i32 curPosMS) {
    HitObjects &objs = this->hitobjects;
    SliderState &s = objs.sliderAt(i);
    if(!objs.finished[i])
        updateSlide(s, objs.clickTimes[i], curPosMS,
                    HitObjectDrawing::isVisibleAt(objs, i, curPosMS, this->lookTiming));

    // all further calculations are only done while we are active
    if(objs.finished[i]) {
        this->updateSliderTracking(i);  // (the keys still change)
        return;
    }

    const ModFlags curIFaceMods = this->getMods().flags;

    s.curPoint = this->osuCoords2Pixels(s.curPointRaw);

    // No longer ignore keys that were released since entering the slider
    // see isClickHeldSlider()
    s.ignoredKeys &= this->getKeys();

    // handle dynamic followradius
    f32 followRadius = s.cursorLeft ? this->fHitcircleDiameter / 2.0f : this->fSliderFollowCircleDiameter / 2.0f;
    const bool wasTrackingCorrectly = !s.cursorLeft;
    const bool isPlayfieldCursorInside = (vec::length(this->getCursorPos() - s.curPoint) < followRadius);
    const bool isAutoCursorInside =
        ((flags::has<ModFlags::Autoplay>(curIFaceMods)) &&
         (!cv::auto_cursordance.getBool() || (vec::length(this->getCursorPos() - s.curPoint) < followRadius)));
    s.cursorInside = (isAutoCursorInside || isPlayfieldCursorInside);
    s.cursorLeft = !s.cursorInside;
    this->updateSliderTracking(i);

    // don't carry the followcircle state over from the approach phase, the cursor has to be inside the hitcircle first
    if(curPosMS < objs.clickTimes[i]) s.cursorLeft = true;

    // handle slider start
    if(!s.startFinished) {
        if((flags::has<ModFlags::Autoplay>(curIFaceMods))) {
            if(curPosMS >= objs.clickTimes[i]) {
                this->hitSlider(i, LiveHitResult::HIT_300, 0, false);
                this->holding_slider = true;
            }
        } else {
            i32 deltaMS = curPosMS - objs.clickTimes[i];

            if((flags::has<ModFlags::Relax>(curIFaceMods))) {
                if(curPosMS >= objs.clickTimes[i] + (i32)cv::relax_offset.getInt()) {
                    const vec2 pos = this->osuCoords2Pixels(s.pointAt(0.0f));
                    const f32 cursorDelta = vec::length(this->getCursorPos() - pos);
                    if((cursorDelta < this->fHitcircleDiameter / 2.0f && (flags::has<ModFlags::Relax>(curIFaceMods)))) {
                        LiveHitResult result = this->getHitResult(deltaMS);

                        if(result != LiveHitResult::HIT_NULL) {
                            const f32 targetDelta = cursorDelta / (this->fHitcircleDiameter / 2.0f);
                            const f32 targetAngle = vec::degrees(
                                std::atan2(this->getCursorPos().y - pos.y, this->getCursorPos().x - pos.x));

                            s.startResult = result;
                            this->hitSlider(i, s.startResult, deltaMS, false, targetDelta, targetAngle);
                            this->holding_slider = true;
                        }
                    }
                }
            }
        }
    }

    // wait for a startcircle miss: osu!stable checks for it every frame, so a miss window that ended before this update
    // already missed before anything else here, while one that ends with it misses after its repeats, ticks and tail
    const auto missStartCircleAfter = [&](i32 windowMS) {
        if(s.startFinished || flags::has<ModFlags::Autoplay>(curIFaceMods)) return;
        const i32 deltaMS = curPosMS - objs.clickTimes[i];
        if(deltaMS > windowMS) {
            s.startResult = LiveHitResult::HIT_MISS;
            this->hitSlider(i, s.startResult, deltaMS, false);
            this->holding_slider = false;

            // no key tapped the startcircle, so all held keys count as holding the slider (like osu!stable)
            s.ignoredKeys = 0;
        }
    };
    missStartCircleAfter((i32)this->getHitWindow50() + 1);

    // handle slider end, repeats, ticks
    if(!s.endFinished) {
        // NOTE: we have 2 timing conditions after which we start checking for strict tracking: 1) startcircle was
        // clicked, 2) slider has started timing wise it is easily possible to hit the startcircle way before the
        // sliderball would become active, which is why the first check exists. even if the sliderball has not yet
        // started sliding, you will be punished for leaving the (still invisible) followcircle area after having
        // clicked the startcircle, always.
        const bool isTrackingStrictTrackingMod =
            ((s.startFinished || curPosMS >= objs.clickTimes[i]) && flags::has<ModFlags::StrictTracking>(curIFaceMods));

        // slider tail lenience bullshit: see
        // https://github.com/ppy/osu/blob/master/osu.Game.Rulesets.Osu/Objects/Slider.cs#L123 being "inside the slider"
        // (for the end of the slider) is NOT checked at the exact end of the slider, but somewhere random before,
        // because fuck you
        const bool hadTailCheck = s.heldTillEndForLenienceHackCheck;
        const i32 offsetMS = (i32)cv::slider_end_inside_check_offset.getInt();
        const i32 lenienceHackEndTimeMS =
            std::max(objs.clickTimes[i] + (objs.endTimes[i] - objs.clickTimes[i]) / 2, (objs.endTimes[i]) - offsetMS);
        const bool isTrackingCorrectly =
            (this->isSliderHeld(i) || (flags::has<ModFlags::Relax>(curIFaceMods))) && s.cursorInside;

        // like osu!stable, judge at most one point per update, in order: the repeats and ticks, then the tail (even if
        // its check time comes before the last tick)
        const auto nextClick = Mc::ranges::find_if(s.clicks, [](const SliderClick &click) { return !click.finished; });
        const bool tailIsNext = nextClick == s.clicks.end();

        // we should only credit ticks/repeats/tails that passed after tracking began:
        // if tracking starts at the frame we notice a point, that point already passed.
        if(curPosMS >= objs.clickTimes[i] && isTrackingCorrectly && !wasTrackingCorrectly) {
            s.slideStartTimeMS = curPosMS;
            this->markReplayFrame();
        }

        if(isTrackingCorrectly) {
            if(isTrackingStrictTrackingMod) {
                s.strictTrackingModLastClickHeldTime = curPosMS;
                if(s.strictTrackingModLastClickHeldTime ==
                   0)  // (prevent frame perfect inputs from not triggering the strict tracking miss because we use != 0
                       // comparison to detect if tracking correctly at least once)
                    s.strictTrackingModLastClickHeldTime = 1;
            }

            // only check it at the exact point in time ...
            if(tailIsNext && curPosMS >= lenienceHackEndTimeMS) {
                // ... once (like a tick)
                if(!s.heldTillEndForLenienceHackCheck) {
                    // player was correctly clicking/holding inside slider at lenienceHackEndTime
                    s.heldTillEndForLenienceHackCheck = true;
                    if(s.slideStartTimeMS <= lenienceHackEndTimeMS) s.heldTillEndForLenienceHack = true;
                }
            }
        } else {
            // do not allow empty clicks outside of the circle radius to prevent the
            // s.bCursorInside flag from resetting
            s.cursorLeft = true;
        }

        // can't be "inside the slider" after lenienceHackEndTime
        // (even though the slider is still going, which is madness)
        if(tailIsNext && curPosMS >= lenienceHackEndTimeMS) s.heldTillEndForLenienceHackCheck = true;

        // handle strict tracking mod
        if(isTrackingStrictTrackingMod) {
            const bool wasTrackingCorrectlyAtLeastOnce = (s.strictTrackingModLastClickHeldTime != 0);
            if(wasTrackingCorrectlyAtLeastOnce && !isTrackingCorrectly) {
                // no strict tracking miss past the tail check time, even if a later tick or repeat delays the check
                if(curPosMS < lenienceHackEndTimeMS) {
                    // force miss the end once, if it has not already been force missed by notelock
                    if(s.endResult == LiveHitResult::HIT_NULL) {
                        // force miss endcircle
                        this->addSliderBreak();

                        s.heldTillEnd = false;
                        s.heldTillEndForLenienceHack = false;
                        s.heldTillEndForLenienceHackCheck = true;
                        s.endResult = LiveHitResult::HIT_MISS;

                        // end of combo, ignore in hiterrorbar, ignore combo, subtract health
                        const LiveHitResult returnedHit = this->addHitResult(
                            (i32)i, s.endResult, 0, (bool)objs.endsCombo[i], true, false, true, false, false);

                        // scorev2 shows the head result, so only show the miss if it changed the slider result
                        if(!flags::has<ModFlags::ScoreV2>(curIFaceMods) || s.endResult != s.startResult)
                            this->showHitResult(i, returnedHit != LiveHitResult::HIT_MISS ? returnedHit : s.endResult,
                                                0, objs.rawPosAt(i, objs.endTimes[i]), true);
                    }
                }
            }
        }

        // judge the endcircle at its check time like osu!stable's tail point, so that its combo comes before e.g. a
        // late startcircle miss
        if(!hadTailCheck && s.heldTillEndForLenienceHackCheck && s.endResult == LiveHitResult::HIT_NULL) {
            if(s.heldTillEndForLenienceHack || flags::has<ModFlags::Autoplay>(curIFaceMods)) {
                this->addHitResult((i32)i, LiveHitResult::HIT_SLIDER30, 0, false, true, true, false, true,
                                   false);  // increase combo and health
                this->addScorePoints(30);
            } else {
                // special case: missing the endcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
                // NOTE: yes, this will drain twice for the end of a slider (once here, and once for the judgement of
                // the whole slider)
                this->addHitResult((i32)i, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                   false);  // only decrease health
            }
        }

        // handle repeats and ticks
        if(nextClick != s.clicks.end() && curPosMS >= nextClick->timeMS) {
            SliderClick &click = *nextClick;
            click.finished = true;
            click.successful = (this->isSliderHeld(i) && s.cursorInside) ||
                               (flags::has<ModFlags::Autoplay>(curIFaceMods)) ||
                               ((flags::has<ModFlags::Relax>(curIFaceMods)) && s.cursorInside);

            // same as the tail: only count points that passed after tracking began
            if(click.successful && s.slideStartTimeMS > click.timeMS) click.successful = false;

            if(click.type == 0) {
                this->hitSliderRepeat(i, click);
            } else {
                this->hitSliderTick(i, click);
            }
        }

        missStartCircleAfter((i32)this->getHitWindow50());

        // handle auto, and the last circle
        if((flags::has<ModFlags::Autoplay>(curIFaceMods))) {
            if(curPosMS >= objs.endTimes[i]) {
                s.heldTillEnd = true;
                this->hitSlider(i, LiveHitResult::HIT_300, 0, true);
                this->holding_slider = false;
            }
        } else {
            if(curPosMS >= objs.endTimes[i]) {
                // handle leftover startcircle
                {
                    // this may happen (if the slider time is shorter than the miss window of the startcircle)
                    if(s.startResult == LiveHitResult::HIT_NULL) {
                        // we still want to cause a sliderbreak in this case!
                        this->addSliderBreak();

                        // special case: missing the startcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS
                        // health)
                        this->addHitResult((i32)i, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true,
                                           true,
                                           false);  // only decrease health

                        s.startResult = LiveHitResult::HIT_MISS;
                    }
                }

                // handle endcircle
                bool isEndResultComingFromStrictTrackingMod = false;
                if(s.endResult == LiveHitResult::HIT_NULL) {
                    s.heldTillEnd = s.heldTillEndForLenienceHack;
                    s.endResult = s.heldTillEnd ? LiveHitResult::HIT_300 : LiveHitResult::HIT_MISS;

                    // handle total slider result (currently startcircle + repeats + ticks + endcircle)
                    // clicks = (repeats + ticks)
                    const f32 numMaxPossibleHits = 1 + s.clicks.size() + 1;
                    f32 numActualHits = 0;

                    if(s.startResult != LiveHitResult::HIT_MISS) numActualHits++;
                    if(s.endResult != LiveHitResult::HIT_MISS) numActualHits++;

                    for(auto &click : s.clicks) {
                        if(click.successful) numActualHits++;
                    }

                    const f32 percent = numActualHits / numMaxPossibleHits;

                    const bool allow300 = (flags::has<ModFlags::ScoreV2>(curIFaceMods))
                                              ? (s.startResult == LiveHitResult::HIT_300)
                                              : true;
                    const bool allow100 =
                        (flags::has<ModFlags::ScoreV2>(curIFaceMods))
                            ? (s.startResult == LiveHitResult::HIT_300 || s.startResult == LiveHitResult::HIT_100)
                            : true;

                    // rewrite s.endResult as the whole slider result, then use it for the final this->hitSlider(i, )
                    if(percent >= 0.999f && allow300)
                        s.endResult = LiveHitResult::HIT_300;
                    else if(percent >= 0.5f && allow100 && !flags::has<ModFlags::Ming3012>(curIFaceMods) &&
                            !flags::has<ModFlags::No100s>(curIFaceMods))
                        s.endResult = LiveHitResult::HIT_100;
                    else if(percent > 0.0f && !flags::has<ModFlags::No100s>(curIFaceMods) &&
                            !flags::has<ModFlags::No50s>(curIFaceMods))
                        s.endResult = LiveHitResult::HIT_50;
                    else
                        s.endResult = LiveHitResult::HIT_MISS;

                    // debugLog("percent = {:f}", percent);

                    if(!s.heldTillEnd && cv::slider_end_miss_breaks_combo.getBool()) this->addSliderBreak();
                } else
                    isEndResultComingFromStrictTrackingMod = true;

                this->hitSlider(i, s.endResult, 0, true, 0.0f, 0.0f, isEndResultComingFromStrictTrackingMod);
                this->holding_slider = false;
            }
        }

        // handle sliderslide sound
        // TODO @kiwec: move this to draw()
        const ModFlags curGameplayFlags = this->getMods().flags;
        const bool sliding = s.startFinished && !s.endFinished && s.cursorInside && curPosMS >= objs.clickTimes[i]  //
                             && (this->isSliderHeld(i) || (flags::has<ModFlags::Autoplay>(curGameplayFlags)) ||     //
                                 (flags::has<ModFlags::Relax>(curGameplayFlags)));
        s.lastSliderSampleSets =
            this->updateSliderSlideSounds(sliding, objs.samples[i], s.curPointRaw, s.lastSliderSampleSets);
    }
}

void GameplayJudge::missSlider(u32 i, i32 curPosMS) {
    HitObjects &objs = this->hitobjects;
    SliderState &s = objs.sliderAt(i);
    if(objs.finished[i]) return;

    const i32 deltaMS = curPosMS - objs.clickTimes[i];

    // startcircle
    if(!s.startFinished) {
        s.startResult = LiveHitResult::HIT_MISS;
        this->hitSlider(i, s.startResult, deltaMS, false);
        this->holding_slider = false;
    }

    // endcircle, repeats, ticks
    if(!s.endFinished) {
        // repeats, ticks
        {
            for(auto &click : s.clicks) {
                if(!click.finished) {
                    click.finished = true;
                    click.successful = false;

                    if(click.type == 0)
                        this->hitSliderRepeat(i, click);
                    else
                        this->hitSliderTick(i, click);
                }
            }
        }

        // endcircle
        {
            s.heldTillEnd = s.heldTillEndForLenienceHack;

            if(!s.heldTillEnd && cv::slider_end_miss_breaks_combo.getBool()) this->addSliderBreak();

            // (a missed endcircle drains like in update(), unless it was already judged there)
            if(!s.heldTillEndForLenienceHackCheck) {
                s.heldTillEndForLenienceHackCheck = true;
                this->addHitResult((i32)i, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                   false);  // only decrease health
            }

            s.endResult = LiveHitResult::HIT_MISS;
            this->hitSlider(i, s.endResult, 0, true);
            this->holding_slider = false;
        }
    }
}

bool GameplayJudge::isSliderClickable(u32 i, i32 music_pos, vec2 cursor_pos) const {
    const HitObjects &objs = this->hitobjects;
    const SliderState &s = objs.sliderAt(i);
    if(s.numCtrlPoints == 0 || s.startFinished || objs.blocked[i]) return false;
    if(this->getHitResult(music_pos - objs.clickTimes[i]) == LiveHitResult::HIT_NULL) return false;

    const vec2 pos = this->osuCoords2Pixels(s.pointAt(0.0f));
    const f32 cursorDelta = vec::length(cursor_pos - pos);
    if(cursorDelta >= this->fHitcircleDiameter / 2.0f) return false;

    return true;
}

void GameplayJudge::clickSlider(u32 i) {
    HitObjects &objs = this->hitobjects;
    SliderState &s = objs.sliderAt(i);
    if(s.numCtrlPoints == 0 || objs.blocked[i])
        return;  // also handle note blocking here (doesn't need fancy shake logic, since sliders don't shake in
                 // osu!stable)

    if(!s.startFinished) {
        const vec2 cursorPos = clicks[0].cursorPos;
        const vec2 pos = this->osuCoords2Pixels(s.pointAt(0.0f));
        const f32 cursorDelta = vec::length(cursorPos - pos);

        if(cursorDelta < this->fHitcircleDiameter / 2.0f) {
            const i32 deltaMS = clicks[0].musicPosMS - objs.clickTimes[i];

            LiveHitResult result = this->getHitResult(deltaMS);
            if(result != LiveHitResult::HIT_NULL) {
                const f32 targetDelta = cursorDelta / (this->fHitcircleDiameter / 2.0f);
                const f32 targetAngle = vec::degrees(std::atan2(cursorPos.y - pos.y, cursorPos.x - pos.x));

                clicks.erase(clicks.begin());
                s.startResult = result;
                this->hitSlider(i, s.startResult, deltaMS, false, targetDelta, targetAngle);
                this->holding_slider = true;
            }
        }
    }
}

void GameplayJudge::hitSlider(u32 i, LiveHitResult result, i32 delta, bool isEndCircle, f32 targetDelta,
                              f32 targetAngle, bool isEndResultFromStrictTrackingMod) {
    HitObjects &objs = this->hitobjects;
    SliderState &s = objs.sliderAt(i);
    if(s.numCtrlPoints == 0) return;

    // start + end of a slider add +30 points, if successful

    // debugLog("isEndCircle = {:d},    s.iCurRepeat = {:d}", (i32)isEndCircle, iCurRepeat);

    // sound and hit animation and also sliderbreak combo drop
    {
        if(result == LiveHitResult::HIT_MISS) {
            if(!isEndResultFromStrictTrackingMod) this->addSliderBreak();
        } else {
            if(s.edgeSamples.size() > 0) {
                if(isEndCircle) {
                    this->playHitSound(s.edgeSamples.back(), s.curPointRaw, delta, objs.endTimes[i]);
                } else {
                    this->playHitSound(s.edgeSamples[0], s.curPointRaw, delta, objs.clickTimes[i]);
                }
            }

            if(this->animatesHits()) {
                const f32 fadeoutTimeSecs = GameRules::getFadeOutTime(this->view->getBaseAnimationSpeed());

                if(!isEndCircle) {
                    this->addSliderHitAnim(i, SliderHitAnim::HEAD, fadeoutTimeSecs);
                } else {
                    if(s.repeat % 2 != 0) {
                        this->addSliderHitAnim(i, SliderHitAnim::TAIL, fadeoutTimeSecs);
                    } else {
                        this->addSliderHitAnim(i, SliderHitAnim::HEAD, fadeoutTimeSecs);
                    }
                }
            }
        }

        // end body fadeout
        if(isEndCircle) {
            if(this->animatesHits()) {
                s.endSliderBodyFadeAnimation = 0.001f;  // quickfix for 1 frame missing images
                s.endSliderBodyFadeAnimation.set(1.0f,
                                                 GameRules::getFadeOutTime(this->view->getBaseAnimationSpeed()) *
                                                     cv::slider_body_fade_out_time_multiplier.getFloat(),
                                                 anim::QuadOut);
            }
            // debugLog("stopping due to end body fadeout");
            this->stopSliderSounds(s.lastSliderSampleSets);
        }
    }

    // add score, and we are finished
    if(!isEndCircle) {
        // startcircle

        s.startResult = result;
        s.startFinished = true;

        // ignore all keys that were held prior to entering the slider
        // except the one used to tap the slider head (or, "hold into" the slider)
        // see isClickHeldSlider()
        s.ignoredKeys = (this->getKeys() & ~this->lastPressedKey);
        this->updateSliderTracking(i);

        if(flags::has<ModFlags::Target>(this->getMods().flags)) {
            // not end of combo, show in hiterrorbar, use for accuracy, increase combo, increase
            // score, ignore for health, don't add object duration to result anim
            this->addObjectHitResult(i, result, delta, false, s.pointAt(0.0f), targetDelta, targetAngle, false, false,
                                     true, false);
        } else {
            // not end of combo, show in hiterrorbar, ignore for accuracy, increase combo,
            // don't count towards score, depending on scorev2 ignore for health or not
            this->addHitResult((i32)i, result, delta, false, false, true, false, true, true);

            // scorev2 judges the whole slider by its head, so its result is shown as soon as the head is hit
            if(this->getMods().has(ModFlags::ScoreV2)) this->showHitResult(i, result, delta, s.pointAt(0.0f), false);
        }

        // add bonus score + health manually
        if(result != LiveHitResult::HIT_MISS) {
            LiveHitResult resultForHealth = LiveHitResult::HIT_SLIDER30;

            this->addHitResult((i32)i, resultForHealth, 0, false, true, true, true, true,
                               false);  // only increase health
            this->addScorePoints(30);
        } else {
            // special case: missing the startcircle drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
            // (in osu!stable, letting the startcircle of a combo's last slider pass also ends that combo's geki/katu
            // state, so only the results after it count for the slider's own geki/katu)
            this->addHitResult((i32)i, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, (bool)objs.endsCombo[i] && delta > 0,
                               true, true, true, true,
                               false);  // only decrease health
        }
    } else {
        // endcircle

        // the startcircle can be missed without reaching this->hitSlider(i, ), if the slider is shorter than its miss window
        const bool startShown = s.startFinished;
        s.startFinished = true;
        s.endFinished = true;
        objs.finished[i] = true;

        if(!isEndResultFromStrictTrackingMod) {
            // special case: osu!lazer 2020 only returns 1 judgement for the whole slider, but via the startcircle. i.e.
            // we are not allowed to drain again here in mcosu logic (because startcircle judgement is handled at the
            // end here)
            // XXX: remove this
            const bool isLazer2020Drain = false;

            // (the endcircle's bonus score, combo and health were already added at its check time, see update())
            // end of combo, ignore in hiterrorbar, ignore combo, increase score, increase health depending on drain type
            const LiveHitResult returnedHit = this->addHitResult((i32)i, result, delta, (bool)objs.endsCombo[i], true,
                                                                 false, true, false, isLazer2020Drain);

            const bool scoreV2 = this->getMods().has(ModFlags::ScoreV2);
            if(scoreV2 && !startShown) this->showHitResult(i, s.startResult, delta, s.pointAt(0.0f), true);

            // scorev2 shows the head result, so the tail only shows a slider result that the rest of it changed
            if(!scoreV2 || result != s.startResult) {
                this->showHitResult(i, returnedHit != LiveHitResult::HIT_MISS ? returnedHit : result, delta,
                                    objs.rawPosAt(i, objs.endTimes[i]), true);
            }
        }
    }

    s.curRepeatCounterForHitSounds++;
}

void GameplayJudge::hitSliderRepeat(u32 i, const SliderClick &click) {
    HitObjects &objs = this->hitobjects;
    SliderState &s = objs.sliderAt(i);
    if(s.numCtrlPoints == 0) return;

    // repeat hit of a slider adds +30 points, if successful

    // sound and hit animation
    if(!click.successful) {
        this->addSliderBreak();
    } else {
        // NOTE: iCurRepeatCounterForHitSounds starts at 1
        this->playHitSound(s.repeatSamples(s.curRepeatCounterForHitSounds), s.curPointRaw, 0, click.timeMS);

        if(this->animatesHits()) {
            f32 animation_multiplier = this->view->getSpeedAdjustedAnimationSpeed();
            f32 tick_pulse_time = cv::slider_followcircle_tick_pulse_time.getFloat() * animation_multiplier;

            s.followCircleTickAnimationScale = 0.0f;
            s.followCircleTickAnimationScale.set(1.0f, tick_pulse_time, anim::Linear);

            const f32 fadeoutTimeSecs = GameRules::getFadeOutTime(this->view->getBaseAnimationSpeed());

            if(click.sliderend) {
                this->addSliderHitAnim(i, SliderHitAnim::TAIL, fadeoutTimeSecs);
            } else {
                this->addSliderHitAnim(i, SliderHitAnim::HEAD, fadeoutTimeSecs);
            }
        }
    }

    // add score
    if(!click.successful) {
        // add health manually
        // special case: missing a repeat drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
        this->addHitResult((i32)i, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                           false);  // only decrease health
    } else {
        this->addHitResult((i32)i, LiveHitResult::HIT_SLIDER30, 0, false, true, true, false, true,
                           false);  // not end of combo, ignore in hiterrorbar, ignore for accuracy, increase
                                    // combo, don't count towards score, increase health

        // add bonus score manually
        this->addScorePoints(30);
    }

    s.curRepeatCounterForHitSounds++;
}

void GameplayJudge::hitSliderTick(u32 i, const SliderClick &click) {
    HitObjects &objs = this->hitobjects;
    SliderState &s = objs.sliderAt(i);
    if(s.numCtrlPoints == 0) return;

    // tick hit of a slider adds +10 points, if successful

    // tick drawing visibility
    i32 numMissingTickClicks = 0;
    for(const auto &c : s.clicks) {
        if(c.type == 1 && c.tickIndex == click.tickIndex && !c.finished) {
            numMissingTickClicks++;
        }
    }
    if(numMissingTickClicks == 0) {
        s.ticks[click.tickIndex].finished = true;
    }

    // sound and hit animation
    if(!click.successful) {
        this->addSliderBreak();
    } else {
        this->playSliderTickSound(objs.samples[i], s.curPointRaw, click.timeMS);

        if(this->animatesHits()) {
            f32 animation_multiplier = this->view->getSpeedAdjustedAnimationSpeed();
            f32 tick_pulse_time = cv::slider_followcircle_tick_pulse_time.getFloat() * animation_multiplier;

            s.followCircleTickAnimationScale = 0.0f;
            s.followCircleTickAnimationScale.set(1.0f, tick_pulse_time, anim::Linear);
        }
    }

    // add score
    if(!click.successful) {
        // add health manually
        // special case: missing a tick drains HIT_MISS_SLIDERBREAK health (and not HIT_MISS health)
        this->addHitResult((i32)i, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                           false);  // only decrease health
    } else {
        this->addHitResult((i32)i, LiveHitResult::HIT_SLIDER10, 0, false, true, true, false, true,
                           false);  // not end of combo, ignore in hiterrorbar, ignore for accuracy, increase
                                    // combo, don't count towards score, increase health

        // add bonus score manually
        this->addScorePoints(10);
    }
}

void GameplayJudge::resetSlider(u32 i, i32 curPosMS) {
    HitObjects &objs = this->hitobjects;
    SliderState &s = objs.sliderAt(i);
    this->resetCommon(i);

    // debugLog("stopping due to onReset");
    this->stopSliderSounds(s.lastSliderSampleSets);
    if(this->view != nullptr) {
        s.followCircleTickAnimationScale.stop();
        s.endSliderBodyFadeAnimation.stop();
    }
    s.clickAnimations.clear();

    s.lastSliderSampleSets.clear();
    s.strictTrackingModLastClickHeldTime = 0;
    s.ignoredKeys = 0;
    s.slideStartTimeMS = -1;
    s.cursorLeft = true;
    s.heldTillEnd = false;
    s.heldTillEndForLenienceHack = false;
    s.heldTillEndForLenienceHackCheck = false;
    s.startResult = LiveHitResult::HIT_NULL;
    s.endResult = LiveHitResult::HIT_NULL;

    s.curRepeatCounterForHitSounds = 0;

    if(objs.clickTimes[i] > curPosMS) {
        s.startFinished = false;
        s.endFinished = false;
        objs.finished[i] = false;
        s.endSliderBodyFadeAnimation = 0.0f;
    } else if(curPosMS < objs.endTimes[i]) {
        s.startFinished = true;
        s.endFinished = false;
        objs.finished[i] = false;
        s.endSliderBodyFadeAnimation = 0.0f;
    } else {
        s.startFinished = true;
        s.endFinished = true;
        objs.finished[i] = true;
        s.endSliderBodyFadeAnimation = 1.0f;
    }

    // (a tick is finished once none of its clicks is left)
    for(auto &tick : s.ticks) tick.finished = true;
    for(auto &click : s.clicks) {
        if(curPosMS > click.timeMS) {
            click.finished = true;
            click.successful = true;
        } else {
            click.finished = false;
            click.successful = false;
            if(click.type == 1) s.ticks[click.tickIndex].finished = false;
        }
    }
}

void GameplayJudge::addSliderHitAnim(u32 i, u8 typeFlags, f32 duration) {
    HitObjects &objs = this->hitobjects;
    SliderState &s = objs.sliderAt(i);
    // percent = 0.001f: quickfix for 1 frame missing images
    // sanity check, avoid bogus maps with insanely fast buzzsliders overloading animationhandler
    if(s.clickAnimations.size() >= 128) {
        // just overwrite a random one, no one would notice anyways with this many on screen at once
        auto &ret = s.clickAnimations[prand() % 128];
        ret.percent = 0.001f;
        ret.type = decltype(ret.type)(typeFlags);
        ret.percent.set(1.0f, duration, anim::QuadOut);
    } else {
        auto &ret = s.clickAnimations.emplace_back(SliderHitAnim{.percent{0.001f}, .type{typeFlags}});
        ret.percent.set(1.0f, duration, anim::QuadOut);
    }
}

void GameplayJudge::updateSliderTracking(u32 i) {
    SliderState &s = this->hitobjects.sliderAt(i);
    s.tracking = s.cursorInside &&
                 (this->isSliderHeld(i) || flags::any<ModFlags::Autoplay | ModFlags::Relax>(this->getMods().flags));
}

bool GameplayJudge::isSliderHeld(u32 i) const {
    const SliderState &s = this->hitobjects.sliderAt(i);
    // osu! has a weird slider quirk, that I'll explain in detail here.
    // When holding K1 before the slider, tapping K2 on slider head, and releasing K2 later,
    // the slider is no longer considered being "held" until K2 is pressed again, or K1 is released and pressed again.

    // The reason this exists is to prevent people from holding K1 the whole map and tapping with K2.
    // Holding is part of the rhythm flow, and this is a rhythm game right?

    // Note that the restriction only applies to the slider head.
    // Any key pressed *after* entering the slider counts as a hold.

    u8 held_gameplay_keys = this->getKeys() & ~Replay::Smoke;
    return (held_gameplay_keys & ~s.ignoredKeys);
}

void GameplayJudge::updateSpinner(u32 i, i32 curPosMS, f64 frameTimeSecs) {
    HitObjects &objs = this->hitobjects;
    SpinnerState &s = objs.spinnerAt(i);
    // if we have not been clicked yet, check if we are in the timeframe of a miss, also handle auto and relax
    if(!objs.finished[i]) {
        s.halfSpinsNeeded = GameRules::getSpinnerHalfSpinsRequired(this, (objs.endTimes[i] - objs.clickTimes[i]));

        // spinners too short to require a single half rotation are implicitly cleared
        if(s.halfSpinsNeeded <= 0) s.ratio = 1.f;

        // handle spinner ending
        if(curPosMS >= objs.endTimes[i]) {
            this->hitSpinner(i);
            return;
        }

        // Skip calculations
        if(frameTimeSecs == 0.0) {
            return;
        }

        const f32 frameMS = (f32)(frameTimeSecs * 1000.0);
        const f32 AUTO_MULTIPLIER = (1.0f / 20.0f);

        i32 deltaMS = objs.clickTimes[i] - (i32)curPosMS;

        // handle auto, mouse spinning movement
        f32 angleDiff = 0;
        if(flags::any<ModFlags::Autoplay | ModFlags::Autopilot | ModFlags::SpunOut>(this->getMods().flags)) {
            angleDiff = frameMS * AUTO_MULTIPLIER;
        } else {  // user spin
            vec2 mouseDelta = this->getCursorPos() - this->osuCoords2Pixels(objs.positions[i]);
            const auto currentMouseAngle = (f32)std::atan2(mouseDelta.y, mouseDelta.x);
            angleDiff = (currentMouseAngle - s.lastMouseAngle);

            if(std::abs(angleDiff) > 0.001f)
                s.lastMouseAngle = currentMouseAngle;
            else
                angleDiff = 0;

            if(angleDiff < -PI_F)
                angleDiff += 2 * PI_F;
            else if(angleDiff > PI_F)
                angleDiff -= 2 * PI_F;
        }

        // handle spinning
        if(deltaMS <= 0) {
            const ModFlags mods = this->getMods().flags;
            bool isSpinning =
                this->isClickHeld() || flags::any<ModFlags::Autoplay | ModFlags::Relax | ModFlags::SpunOut>(mods);

            s.deltaOverflowMS += frameMS;

            if(isSpinning) s.deltaAngleOverflow += angleDiff;

            // like osu!stable, the spinner velocity chases the cursor velocity in 60 Hz steps (its replay frame rate).
            // the velocity changes by at most maxAccel per ms, which is higher for spinners shorter than 5 seconds.
            // velocities are per real ms, capped at 0.05 rad/ms (477 RPM).
            constexpr f32 STEP_MS = 1000.0f / 60.0f;
            const f32 maxAccel =
                0.00008f + std::max(0.0f, (5000.0f - (f32)(objs.endTimes[i] - objs.clickTimes[i])) / 2000000.0f);
            while(s.deltaOverflowMS >= STEP_MS) {
                const f32 stepAngle = s.deltaAngleOverflow * STEP_MS / s.deltaOverflowMS;
                s.deltaAngleOverflow -= stepAngle;
                s.deltaOverflowMS -= STEP_MS;

                // the cursor velocity drops to a third after one still step, and to 0 after two
                if(stepAngle == 0.0f) {
                    s.theoreticalVelocity = ++s.stillSteps < 2 ? s.theoreticalVelocity / 3.0f : 0.0f;
                } else {
                    s.stillSteps = 0;
                    s.theoreticalVelocity = stepAngle / STEP_MS;
                }

                if(flags::any<ModFlags::SpunOut | ModFlags::Autopilot>(mods)) {
                    s.velocity = 0.03f;
                } else {
                    // relax turns around 4x slower
                    const bool turning = s.theoreticalVelocity > s.velocity ? s.velocity < 0.0f : s.velocity > 0.0f;
                    const f32 accel = maxAccel * STEP_MS / (turning && flags::has<ModFlags::Relax>(mods) ? 4.0f : 1.0f);
                    s.velocity += std::clamp(s.theoreticalVelocity - s.velocity, -accel, accel);
                }
                s.velocity = std::clamp(s.velocity, -0.05f, 0.05f);

                s.RPM = s.RPM * 0.9f + 0.1f * std::abs(s.velocity) * 1000.0f / (2.0f * PI_F) * 60.0f;
            }

            // the spinner turns in music time: spins count 1.5x with DT, 0.75x with HT
            if(s.velocity != 0.0f)
                this->rotateSpinner(i, s.velocity * frameMS * this->getSpeedMultiplier(), curPosMS);
            else
                this->stopSpinnerSpinSound();

            if(s.halfSpinsNeeded > 0) s.ratio = s.rotations / ((f32)s.halfSpinsNeeded * 180.0f);
            if(s.completedTimeMS < 0 && s.ratio >= 1.0f) s.completedTimeMS = curPosMS;
        }
    }
}

void GameplayJudge::resetSpinner(u32 i, i32 curPosMS) {
    HitObjects &objs = this->hitobjects;
    SpinnerState &s = objs.spinnerAt(i);
    this->resetCommon(i);

    this->stopSpinnerSpinSound();

    s.RPM = 0.0f;
    s.drawRot = 0.0f;
    s.rotations = 0.0f;
    s.deltaOverflowMS = 0.0f;
    s.deltaAngleOverflow = 0.0f;
    s.velocity = 0.0f;
    s.theoreticalVelocity = 0.0f;
    s.stillSteps = 0;
    s.ratio = 0.0f;
    s.completedTimeMS = -1;
    s.firstSpinTimeMS = -1;
    s.bonusTimeMS = -1;
    s.bonusSpins = 0;
    s.hitSuccess = false;

    // spinners don't need misaims
    objs.misAimed[i] = true;

    if(curPosMS > objs.endTimes[i])
        objs.finished[i] = true;
    else
        objs.finished[i] = false;
}

void GameplayJudge::hitSpinner(u32 i) {
    HitObjects &objs = this->hitobjects;
    SpinnerState &s = objs.spinnerAt(i);
    // calculate hit result, in half rotations like osu!stable
    const i32 halfSpins = (i32)(s.rotations / 180.0f);
    const auto &mods = this->getMods();
    LiveHitResult result = LiveHitResult::HIT_NULL;
    if(s.halfSpinsNeeded <= 0 || halfSpins >= s.halfSpinsNeeded + 1 || mods.has(ModFlags::Autoplay))
        result = LiveHitResult::HIT_300;
    else if(halfSpins >= s.halfSpinsNeeded - 1 && !mods.has(ModFlags::Ming3012) && !mods.has(ModFlags::No100s))
        result = LiveHitResult::HIT_100;
    else if(halfSpins >= s.halfSpinsNeeded / 4 && !mods.has(ModFlags::No100s) && !mods.has(ModFlags::No50s))
        result = LiveHitResult::HIT_50;
    else
        result = LiveHitResult::HIT_MISS;

    s.hitSuccess = result != LiveHitResult::HIT_MISS;

    // sound
    if(result != LiveHitResult::HIT_MISS) this->playHitSound(objs.samples[i], objs.positions[i], 0);

    // add it, and we are finished
    this->addObjectHitResult(i, result, 0, (bool)objs.endsCombo[i], objs.positions[i], -1.0f, 0.f,
                             /*ignoreOnHitErrorBar=*/true);
    objs.finished[i] = true;

    this->stopSpinnerSpinSound();
}

void GameplayJudge::rotateSpinner(u32 i, f32 rad, i32 curPosMS) {
    HitObjects &objs = this->hitobjects;
    SpinnerState &s = objs.spinnerAt(i);
    s.drawRot += vec::degrees(rad);

    rad = std::abs(rad);
    const f32 newRotations = s.rotations + vec::degrees(rad);

    // stable counts spins in half rotations, the first one is what fades out "SPIN!"
    if(s.firstSpinTimeMS < 0 && newRotations >= 180.0f) s.firstSpinTimeMS = curPosMS;

    // past the first one, every half rotation gives health, and every whole rotation 100 points. from 2.5 rotations
    // past clearing the spinner, every other half rotation gives a 1100 point bonus instead.
    const i32 bonusStart = s.halfSpinsNeeded + 3;
    for(i32 halfSpin = (i32)(s.rotations / 180.0f) + 1; halfSpin <= (i32)(newRotations / 180.0f); halfSpin++) {
        if(halfSpin <= 1) continue;

        if(halfSpin > bonusStart && (halfSpin - bonusStart) % 2 == 0) {
            s.bonusSpins++;
            s.bonusTimeMS = curPosMS;
            this->playSpinnerBonusSound(objs.samples[i]);
            this->addHitResult((i32)i, LiveHitResult::HIT_SPINNERBONUS, 0, false, true, true, true, true,
                               false);  // only increase health
            this->addScorePoints(1100, true);
        } else {
            this->addHitResult((i32)i, LiveHitResult::HIT_SPINNERSPIN, 0, false, true, true, true, true,
                               false);  // only increase health
            if(halfSpin % 2 == 0) this->addScorePoints(100, true);
        }
    }

    // spinner sound
    this->playSpinnerSpinSound(s.ratio, objs.samples[i]);

    s.rotations = newRotations;
}

vec2 GameplayJudge::getSpinnerAutoCursorPos(u32 i, i32 curPosMS) const {
    const HitObjects &objs = this->hitobjects;
    // calculate point
    i32 deltaMS = 0;
    if(curPosMS <= objs.clickTimes[i])
        deltaMS = 0;
    else if(curPosMS >= objs.endTimes[i])
        deltaMS = (objs.endTimes[i] - objs.clickTimes[i]);
    else
        deltaMS = curPosMS - objs.clickTimes[i];

    vec2 actualPos = this->osuCoords2Pixels(objs.positions[i]);
    const f32 AUTO_MULTIPLIER = (1.0f / 20.0f);
    f32 multiplier =
        flags::any<ModFlags::Autoplay | ModFlags::Autopilot>(this->getMods().flags) ? AUTO_MULTIPLIER : 1.0f;
    f32 angle = (deltaMS * multiplier) - PI_F / 2.0f;
    f32 r = GameRules::getPlayfieldSize().y / 10.0f;  // XXX: slow?
    return vec2((f32)(actualPos.x + r * std::cos(angle)), (f32)(actualPos.y + r * std::sin(angle)));
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
        const f32 percent = 1.0f - ((f64)(this->iCurMusicPos - this->hitobjects.clickTimes[0]) /
                                    (f64)(this->hitobjects.endTimes.back() - this->hitobjects.clickTimes[0])) *
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
        const f32 percent = 1.0f + ((f64)(this->iCurMusicPos - this->hitobjects.clickTimes[0]) /
                                    (f64)(this->hitobjects.endTimes.back() - this->hitobjects.clickTimes[0])) *
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
                       std::clamp<f32>((f32)this->nb_hitobjects / drainLength * 8.0f, 0.0f, 16.0f)) /
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
        return (u32)(this->hitobjects.endTimes.back() - this->hitobjects.clickTimes.front());
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

namespace {
// counts how many objects in a row, from the first one, are finished and end before time.
// latestTimeBefore[i] is the latest start or end time among the first i objects.
uSz countBefore(std::span<const i32> latestTimeBefore, uSz numFinished, f64 time) {
    if(latestTimeBefore.empty()) return 0;
    const auto before = latestTimeBefore.first(numFinished + 1);
    return (uSz)(Mc::ranges::lower_bound(before, time) - before.begin()) - 1;
}
}  // namespace

uSz GameplayJudge::getFirstLiveObject(f64 time) const {
    return countBefore(this->latestTimeBefore, this->numFinishedFirst, time);
}

uSz GameplayJudge::getFirstLiveObjectByEndTime(f64 time) const {
    return countBefore(this->latestTimeBeforeByEndTime, this->numFinishedFirstByEndTime, time);
}

uSz GameplayJudge::getFirstFutureObjectByEndTime(f64 time) const {
    return (uSz)(Mc::ranges::upper_bound(this->earliestStartFrom, time) - this->earliestStartFrom.begin());
}

i32 GameplayJudge::getPVS() const {
    // this is an approximation with generous boundaries, it doesn't need to be exact (just good enough to filter 10000
    // hitobjects down to a few hundred or so) it will be used in both positive and negative directions (previous and
    // future hitobjects) to speed up loops which iterate over all hitobjects
    return this->fCachedApproachTimeForUpdate + GameRules::getFadeInTime() + (i32)GameRules::HITWINDOW_MISS +
           1500;  // sanity
}

vec2 GameplayJudge::getCursorPos() const { return this->vCursorPos; }

bool GameplayJudge::isClickHeld() const { return this->getKeys() & ~Replay::Smoke; }

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
        coords = GameRules::rotateAroundPlayfieldCenter(coords, this->fPlayfieldRotation);
    }

    // if wobble, clamp coordinates
    if(mods.has(ModFlags::Wobble1) || mods.has(ModFlags::Wobble2)) {
        coords.x = std::clamp<f32>(coords.x, 0.0f, GameRules::OSU_COORD_WIDTH);
        coords.y = std::clamp<f32>(coords.y, 0.0f, GameRules::OSU_COORD_HEIGHT);
    }

    return coords;
}

LiveHitResult GameplayJudge::addHitResult(i32 object, LiveHitResult hit, i32 delta, bool isEndOfCombo,
                                          bool ignoreOnHitErrorBar, bool hitErrorBarOnly, bool ignoreCombo,
                                          bool ignoreScore, bool ignoreHealth) {
    using enum LiveHitResult;
    const auto &mods = this->getMods();

    // key presses always get a frame; this is for slider parts, misses, or relax hits
    this->markReplayFrame();

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

    if(this->listener) {
        this->listener->onHitResult(object, hit, delta, !ignoreOnHitErrorBar);
        if(hit == HIT_MISS) this->listener->onComboBreak();
    }

    // score
    this->score.addHitResult(this, hit, delta, ignoreOnHitErrorBar, hitErrorBarOnly, ignoreCombo, ignoreScore);

    // health
    LiveHitResult returnedHit = HIT_MISS;
    if(!ignoreHealth) {
        this->addHealth(this->score.getHealthIncrease(this, hit), true);

        // geki/katu handling
        if(isEndOfCombo) {
            // a combo end without a hit result (see hitSlider()) awards nothing
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
    if(this->hitobjects.empty() || this->iCurMusicPos < this->hitobjects.clickTimes.front()) return true;
    if(this->iCurMusicPos > this->hitobjects.endTimes[this->hitobjectsSortedByEndTime.back()]) {
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
       this->iCurMusicPos < this->hitobjects.clickTimes[0])
        return;

    // never drain after last hitobject
    if(this->iCurMusicPos > this->hitobjects.endTimes[this->hitobjectsSortedByEndTime.back()]) return;

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
    HitObjects &objs = this->hitobjects;

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
    this->currentHitObject = -1;
    this->iNextHitObjectTime = 0;
    this->iPreviousHitObjectTime = 0;
    this->iPreviousFollowPointObjectIndex = 0;
    this->iNPS = 0;
    this->iND = 0;
    this->iCurrentNumCircles = 0;
    this->iCurrentNumSliders = 0;
    this->iCurrentNumSpinners = 0;
    {
        bool blockNextNotes = false;

        // to avoid recalculating it for every object
        this->fCachedApproachTimeForUpdate = this->getApproachTime();
        this->lookTiming = HitObjectDrawing::lookTiming(*this, this->view);

        const i32 pvs = [&]() -> i32 {
            if(mods.has(ModFlags::Mafham)) {
                const i32 objIdx =
                    std::clamp<i32>(this->iCurrentHitObjectIndex + cv::mod_mafham_render_livesize.getInt() + 1, 0,
                                    (i32)this->hitobjects.size() - 1);
                return objs.clickTimes[objIdx] - this->iCurMusicPos + 1500;
            }

            return this->getPVS();
        }();
        const bool usePVS = cv::pvs.getBool();
        const bool freezeFrame = mods.has(ModFlags::FreezeFrame);

        const int notelockType = mods.notelock_type;
        const i32 tolerance2B = (i32)cv::notelock_stable_tolerance2b.getInt();

        this->iCurrentHitObjectIndex = 0;  // reset below here, since it's needed for mafham pvs

        const i32 followPointFadeTime = (i32)cv::followpoints_prevfadetime.getFloat();
        const uSz firstLive =
            usePVS ? this->getFirstLiveObject(std::min(this->iCurMusicPos - pvs, this->iCurMusicPos)) : 0;
        if(firstLive > 0) {
            this->currentHitObject = (i32)firstLive - 1;
            this->iPreviousHitObjectTime = objs.endTimes[firstLive - 1];
            for(uSz i = firstLive; i-- > 0;) {
                if(this->iCurMusicPos > objs.endTimes[i] + followPointFadeTime) {
                    this->iPreviousFollowPointObjectIndex = (i32)i;
                    break;
                }
            }
            this->iCurrentNumCircles = this->numCirclesBefore[firstLive];
            this->iCurrentNumSliders = this->numSlidersBefore[firstLive];
            this->iCurrentNumSpinners = (i32)firstLive - this->iCurrentNumCircles - this->iCurrentNumSliders;
            this->iCurrentHitObjectIndex = (i32)firstLive - 1;
        }

        for(int i = (int)firstLive; i < this->hitobjects.size(); i++) {
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

            const bool isCircle = objs.types[i] == HitObjectType::CIRCLE;
            const bool isSlider = objs.types[i] == HitObjectType::SLIDER;
            const bool isSpinner = objs.types[i] == HitObjectType::SPINNER;

            // determine previous & next object time, used for auto + followpoints + warning arrows + empty section
            // skipping
            if(this->iNextHitObjectTime == 0) {
                if(objs.clickTimes[i] > this->iCurMusicPos) {
                    this->iNextHitObjectTime = objs.clickTimes[i];
                } else {
                    this->currentHitObject = i;
                    this->iPreviousHitObjectTime = objs.endTimes[i];

                    if(this->iCurMusicPos > this->iPreviousHitObjectTime + followPointFadeTime)
                        this->iPreviousFollowPointObjectIndex = i;
                }
            }

            // PVS optimization
            if(usePVS) {
                if(objs.finished[i] && (this->iCurMusicPos - pvs > objs.endTimes[i]))  // past objects
                {
                    if(isCircle) this->iCurrentNumCircles++;
                    if(isSlider) this->iCurrentNumSliders++;
                    if(isSpinner) this->iCurrentNumSpinners++;

                    this->iCurrentHitObjectIndex = i;
                    continue;
                }
                // freeze frame shows a whole combo once it starts
                if((freezeFrame ? objs.comboStartTimes[i] : objs.clickTimes[i]) >
                   this->iCurMusicPos + pvs)  // future objects
                    break;
            }

            if(this->iCurMusicPos >= objs.endTimes[i]) this->iCurrentHitObjectIndex = i;

            // note blocking / notelock (1)
            const SliderState *currentSliderPointer = isSlider ? &objs.sliderAt(i) : nullptr;
            if(notelockType > 0) {
                objs.blocked[i] = blockNextNotes;

                if(notelockType == 1)  // neomod
                {
                    // (nothing, handled in (2) block)
                } else if(notelockType == 2)  // osu!stable
                {
                    if(!objs.finished[i]) {
                        blockNextNotes = true;

                        // Sliders are "finished" after they end
                        // Extra handling for simultaneous/2b hitobjects, as these would otherwise get blocked
                        // NOTE: this will still unlock some simultaneous/2b patterns too early
                        //       (slider slider circle [circle]), but nobody from that niche has complained so far
                        if(isSlider || isSpinner) {
                            if((i + 1) < this->hitobjects.size()) {
                                if((isSpinner || currentSliderPointer->startFinished) &&
                                   (objs.clickTimes[i + 1] <= (objs.endTimes[i] + tolerance2B)))
                                    blockNextNotes = false;
                            }
                        }
                    }
                } else if(notelockType == 3)  // osu!lazer 2020
                {
                    if(!objs.finished[i]) {
                        // spinners are completely ignored (transparent)
                        if(!isSpinner) {
                            blockNextNotes = (this->iCurMusicPos <= objs.clickTimes[i]);

                            // sliders are "finished" after their startcircle
                            if(isSlider && currentSliderPointer->startFinished) {
                                // sliders with finished startcircles do not block
                                blockNextNotes = false;
                            }
                        }
                    }
                }
            } else {
                objs.blocked[i] = false;
            }

            // click events (this also handles hitsounds!)
            const bool isCurrentHitObjectASliderAndHasItsStartCircleFinishedBeforeClickEvents =
                (currentSliderPointer != nullptr && currentSliderPointer->startFinished);
            const bool isCurrentHitObjectFinishedBeforeClickEvents = objs.finished[i];
            if(!this->clicks.empty()) {
                if(isCircle) this->clickCircle(i);
                if(isSlider) this->clickSlider(i);
            }
            const bool isCurrentHitObjectFinishedAfterClickEvents = objs.finished[i];
            const bool isCurrentHitObjectASliderAndHasItsStartCircleFinishedAfterClickEvents =
                (currentSliderPointer != nullptr && currentSliderPointer->startFinished);

            // note blocking / notelock (2.1)
            if(!isCurrentHitObjectASliderAndHasItsStartCircleFinishedBeforeClickEvents &&
               isCurrentHitObjectASliderAndHasItsStartCircleFinishedAfterClickEvents) {
                // in here if a slider had its startcircle clicked successfully in this update iteration

                if(notelockType == 2)  // osu!stable
                {
                    // edge case: frame perfect double tapping on overlapping sliders would incorrectly eat the second
                    // input, because the startFinished 2b edge case check handling happens before
                    // clickSlider(i); so, we check if the currentSliderPointer got its
                    // startFinished within this clickSlider(i); and unlock
                    // blockNextNotes if that is the case note that we still only unlock within duration + tolerance2B
                    // (same as in (1))
                    if((i + 1) < this->hitobjects.size()) {
                        if((objs.clickTimes[i + 1] <= (objs.endTimes[i] + tolerance2B))) blockNextNotes = false;
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
                        if(objs.finished[m]) break;

                        // spinners are completely ignored (transparent)
                        if(objs.types[m] == HitObjectType::SPINNER) continue;

                        // NOTE: 2b exception. only force miss if objects are not overlapping.
                        if(objs.clickTimes[i] > objs.endTimes[m]) {
                            this->missObject(m, this->iCurMusicPos);
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
                        if(objs.finished[m]) break;

                        // spinners are completely ignored (transparent)
                        if(objs.types[m] == HitObjectType::SPINNER) continue;

                        if(this->iCurMusicPos > objs.clickTimes[m]) {
                            // NOTE: 2b exception. only force miss if objects are not overlapping.
                            if(objs.clickTimes[i] > objs.endTimes[m]) {
                                this->missObject(m, this->iCurMusicPos);
                            }
                        }
                    }
                }
            }

            // main hitobject update (after click events, so a press on the same frame as an object end still hits)
            this->updateObject(i, this->iCurMusicPos, frameTimeSecs);

            if(objs.finished[i]) {
                if(isCircle) this->iCurrentNumCircles++;
                if(isSlider) this->iCurrentNumSliders++;
                if(isSpinner) this->iCurrentNumSpinners++;

                this->iCurrentHitObjectIndex = i;
            }

            // notes per second
            const i32 npsHalfGateSizeMS = (i32)(500.0f * this->getSpeedMultiplier());
            if(objs.clickTimes[i] > this->iCurMusicPos - npsHalfGateSizeMS &&
               objs.clickTimes[i] < this->iCurMusicPos + npsHalfGateSizeMS)
                this->iNPS++;

            // note density
            if(HitObjectDrawing::isVisibleAt(objs, i, this->iCurMusicPos, this->lookTiming)) this->iND++;
        }

        while(this->numFinishedFirst < this->hitobjects.size() && objs.finished[this->numFinishedFirst])
            this->numFinishedFirst++;
        while(this->numFinishedFirstByEndTime < this->hitobjectsSortedByEndTime.size() &&
              objs.finished[this->hitobjectsSortedByEndTime[this->numFinishedFirstByEndTime]])
            this->numFinishedFirstByEndTime++;

        // miss hiterrorbar slots
        // this gets the closest previous unfinished hitobject, as well as all following hitobjects which are in 50
        // range and could be clicked
        if(cv::hiterrorbar_misaims.getBool()) {
            this->misaimObjects.clear();
            i32 lastUnfinishedHitObject = -1;
            const i32 hitWindow50 = (i32)this->getHitWindow50();
            for(uSz i = this->numFinishedFirst; i < objs.size(); i++) {
                if(!objs.finished[i]) {
                    if(this->iCurMusicPos >= objs.clickTimes[i])
                        lastUnfinishedHitObject = (i32)i;
                    else if(std::abs(objs.clickTimes[i] - this->iCurMusicPos) < hitWindow50)
                        this->misaimObjects.push_back((u32)i);
                    else
                        break;
                }
            }
            if(lastUnfinishedHitObject >= 0 &&
               std::abs(objs.clickTimes[lastUnfinishedHitObject] - this->iCurMusicPos) < hitWindow50)
                this->misaimObjects.insert(this->misaimObjects.begin(), (u32)lastUnfinishedHitObject);

            // now, go through the remaining clicks, and go through the unfinished hitobjects.
            // handle misaim clicks sequentially (setting the misaim flag on the hitobjects to only allow 1 entry in the
            // hiterrorbar for misses per object) clicks don't have to be consumed here, as they are deleted below
            // anyway
            for(const auto &click : this->clicks) {
                for(const u32 misaimObject : this->misaimObjects) {
                    if(objs.misAimed[misaimObject])  // only 1 slot per object!
                        continue;

                    objs.misAimed[misaimObject] = true;
                    const i32 delta = click.musicPosMS - objs.clickTimes[misaimObject];
                    if(!this->score.simulating) ui->getHUD()->addHitError(delta, false, true);

                    break;  // the current click has been dealt with (and the hitobject has been misaimed)
                }
            }
        }

        // all remaining clicks which have not been consumed by any hitobjects can safely be deleted
        if(!this->clicks.empty()) {
            bool extra_click = !this->bIsInSkippableSection && !this->bInBreak;
            extra_click &= this->iCurrentHitObjectIndex > 0;

            // clicks during a spinner spin it.
            // this uses the click time, since the frame time can be outside the spinner while the click was inside.
            const auto isSpinnerClick = [&](const Click &click) {
                for(size_t i = this->iCurrentHitObjectIndex; i < objs.size() && objs.clickTimes[i] <= click.musicPosMS;
                    i++) {
                    if(objs.types[i] == HitObjectType::SPINNER && click.musicPosMS <= objs.endTimes[i]) return true;
                }
                return false;
            };
            extra_click &= !Mc::ranges::all_of(this->clicks, isSpinnerClick);
            // nightmare mod: extra clicks = sliderbreak
            if(extra_click && mods.has(ModFlags::StrictClicks)) {
                this->addSliderBreak();
                this->addHitResult(-1, LiveHitResult::HIT_MISS_SLIDERBREAK, 0, false, true, true, true, true,
                                   false);  // only decrease health
            } else if(extra_click && this->listener) {
                this->listener->onExtraClick();
            }

            this->clicks.clear();
        }
    }

    // empty section detection
    {
        const i32 legacyOffset = (this->iPreviousHitObjectTime < this->hitobjects.clickTimes[0] ? 0 : 1000);  // Mc
        const i32 nextHitObjectDelta = this->iNextHitObjectTime - this->iCurMusicPos;
        // (before the first one there's no previous one, so a storyboard's lead-in before 0 counts too)
        const bool beforeFirst = this->iCurMusicPos < this->hitobjects.clickTimes[0];
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
    if(this->currentHitObject >= 0) {
        const i32 current = this->currentHitObject;
        this->bIsSpinnerActive = objs.types[current] == HitObjectType::SPINNER &&
                                 this->iCurMusicPos > objs.clickTimes[current] &&
                                 this->iCurMusicPos < objs.endTimes[current];
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
    HitObjects &objs = this->hitobjects;
    const bool autopilot = mods.has(ModFlags::Autopilot);
    const i32 curMusicPos = this->iCurMusicPos;

    // general
    i32 prevTime = 0;
    i32 nextTime = this->hitobjects.clickTimes[0];
    vec2 prevPos = this->vAutoCursorPos;
    vec2 curPos = this->vAutoCursorPos;
    vec2 nextPos = this->vAutoCursorPos;
    bool haveCurPos = false;

    // dance
    int nextPosIndex = 0;

    if(this->hitobjects.clickTimes[0] < cv::early_note_time.getInt())
        prevTime = (int)std::roundf((float)-cv::early_note_time.getInt() * this->getSpeedMultiplier());

    if(!autopilot) {
        const uSz firstLive = this->getFirstLiveObject(curMusicPos);
        if(firstLive > 0) {
            const u32 o = (u32)firstLive - 1;
            prevTime = objs.endTimes[o];
            prevPos = this->getAutoCursorPos(o, curMusicPos);
        }

        bool autoDanceOverride = false;
        for(int i = (int)firstLive; i < this->hitobjects.size(); i++) {
            const u32 o = (u32)i;

            // get previous object
            if(objs.clickTimes[o] <= curMusicPos) {
                prevTime = objs.endTimes[o];
                prevPos = this->getAutoCursorPos(o, curMusicPos);
                if((objs.endTimes[o] - objs.clickTimes[o]) > 0 &&
                   curMusicPos - objs.clickTimes[o] <= (objs.endTimes[o] - objs.clickTimes[o])) {
                    if(cv::auto_cursordance.getBool()) {
                        if(objs.types[o] == HitObjectType::SLIDER) {
                            const std::vector<SliderClick> &clicks = objs.sliderAt(o).clicks;

                            // start
                            prevTime = objs.clickTimes[o];
                            prevPos = this->osuCoords2Pixels(objs.rawPosAt(o, prevTime));

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
                                    prevPos = this->osuCoords2Pixels(objs.rawPosAt(o, prevTime));
                                }

                                // get next click
                                if(click.timeMS > curMusicPos && click.timeMS < smallestNext) {
                                    smallestNext = click.timeMS;
                                    nextTime = click.timeMS;
                                    nextPos = this->osuCoords2Pixels(objs.rawPosAt(o, nextTime));
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
                                if(endTime == 0) endTime = objs.clickTimes[o];

                                prevTime = endTime;
                                prevPos = this->osuCoords2Pixels(objs.rawPosAt(o, prevTime));
                                nextTime = objs.endTimes[o];
                                nextPos = this->osuCoords2Pixels(objs.rawPosAt(o, nextTime));
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
            if(objs.clickTimes[o] > curMusicPos) {
                nextPosIndex = i;
                if(!autoDanceOverride) {
                    nextPos = this->getAutoCursorPos(o, curMusicPos);
                    nextTime = objs.clickTimes[o];
                }
                break;
            }
        }
    } else {
        if(this->numFinishedFirst > 0) {
            const u32 o = (u32)this->numFinishedFirst - 1;
            prevTime = objs.endTimes[o] + this->hitobjects.autopilotDeltas[this->numFinishedFirst - 1];
            prevPos = this->getAutoCursorPos(o, curMusicPos);
        }

        for(int i = (int)this->numFinishedFirst; i < this->hitobjects.size(); i++) {
            const u32 o = (u32)i;
            i32 &autopilotDelta = this->hitobjects.autopilotDeltas[i];

            // get previous object
            if(objs.finished[o] ||
               (curMusicPos > objs.endTimes[o] + (i32)(this->getHitWindow50() * mods.autopilot_lenience))) {
                prevTime = objs.endTimes[o] + autopilotDelta;
                prevPos = this->getAutoCursorPos(o, curMusicPos);
            } else if(!objs.finished[o])  // get next object
            {
                nextPosIndex = i;
                nextPos = this->getAutoCursorPos(o, curMusicPos);
                nextTime = objs.clickTimes[o];

                // wait for the user to click
                if(curMusicPos >= nextTime + (objs.endTimes[o] - objs.clickTimes[o])) {
                    haveCurPos = true;
                    curPos = nextPos;

                    autopilotDelta = curMusicPos - (nextTime + (objs.endTimes[o] - objs.clickTimes[o]));
                } else if((objs.endTimes[o] - objs.clickTimes[o]) > 0 &&
                          curMusicPos >= nextTime)  // handle objects with duration
                {
                    haveCurPos = true;
                    curPos = nextPos;
                    autopilotDelta = 0;
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

    this->hitobjects.stack(0, this->hitobjects.size(), this->getAR(), this->getMapVersion(),
                           this->getMapStackLeniency(), this->fRawHitcircleDiameter,
                           this->getMods().has(ModFlags::HardRock));
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
            int lastTime = (int)(this->hitobjects.clickTimes[0] - (i32)this->getApproachTime());
            fail = false;

            const int breakCount = this->breaks.size();
            int breakNumber = 0;

            int comboTooLowCount = 0;

            for(int i = 0; i < this->hitobjects.size(); i++) {
                const HitObjects &objs = this->hitobjects;
                const i32 clickTime = objs.clickTimes[i];
                const i32 duration = objs.endTimes[i] - clickTime;
                const bool isSlider = objs.types[i] == HitObjectType::SLIDER;
                const bool isSpinner = objs.types[i] == HitObjectType::SPINNER;

                const int localLastTime = lastTime;

                int breakTime = 0;
                if(breakCount > 0 && breakNumber < breakCount) {
                    const DBType::BREAK &e = this->breaks[breakNumber];
                    if(e.startTime >= localLastTime && e.endTime <= clickTime) {
                        // consider break start equal to object end time for version 8+ since drain stops during this
                        // time
                        breakTime = (version < 8) ? (e.endTime - e.startTime) : (e.endTime - localLastTime);
                        breakNumber++;
                    }
                }

                testPlayer.decreaseHealth(testDrop * (clickTime - lastTime - breakTime));

                lastTime = (int)(objs.endTimes[i]);

                if(testPlayer.health < lowestHp) lowestHp = testPlayer.health;

                if(testPlayer.health > lowestHpEver) {
                    const f64 longObjectDrop = testDrop * (f64)duration;
                    const f64 maxLongObjectDrop = std::max(0.0, longObjectDrop - testPlayer.health);

                    testPlayer.decreaseHealth(longObjectDrop);

                    // nested hitobjects
                    if(isSlider) {
                        // startcircle
                        testPlayer.increaseHealth(
                            LiveScore::getHealthIncrease(LiveHitResult::HIT_SLIDER30, HP, testPlayer.hpMultiplierNormal,
                                                         testPlayer.hpMultiplierComboEnd, 1.0));  // slider30

                        // ticks + repeats + repeat ticks
                        const std::vector<SliderClick> &clicks = objs.sliderAt(i).clicks;
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
                    } else if(isSpinner) {
                        const i32 halfSpinsNeeded = GameRules::getSpinnerHalfSpinsRequired(this, duration);
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
                        if((i == this->hitobjects.size() - 1) || this->hitobjects.endsCombo[i]) {
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
