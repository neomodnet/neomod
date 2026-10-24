#pragma once
// Copyright (c) 2015, PG, All rights reserved.

#include "AnimationHandler.h"
#include "GameplayJudge.h"
#include "AsyncPPCalculator.h"
#include "LegacyReplay.h"
#include "score.h"
#include "LivePPCalc.h"
#include "Vectors.h"
#include "DatabaseBeatmapTypes.h"
#include "PlayfieldView.h"

#include "Storyboard.h"

#include <limits>
#include <memory>

class RenderTarget;
class MusicTrack;
class Shader;
class ConVar;
struct Skin;
class Resource;
namespace neomod {
class HitObject;
}  // namespace neomod

class DatabaseBeatmap;
class SpectatorScreen;
struct LiveReplayFrame;
struct ScoreFrame;

// this class handles the beatmap and general state of a play:
// music, intro, pausing, skipping, failing, drawing, recording and spectating
// the gameplay itself is handled by GameplayJudge, which this class forwards input to
class BeatmapInterface final : public GameplayJudge::Listener, public PlayfieldView {
    NOCOPY_NOMOVE(BeatmapInterface)
   public:
    using DBTimingInfo = neomod::DatabaseBeatmapTypes::TIMING_INFO;
    using DBBreak = neomod::DatabaseBeatmapTypes::BREAK;

    // plays the selected map's music on `music`
    explicit BeatmapInterface(MusicTrack &music);
    ~BeatmapInterface() override;

    void draw();
    void drawDebug();
    void drawBackground();

    void update();
    void update2();  // Used to be Playfield::update()

    [[nodiscard]] bool clickableHitobjectAt(vec2 cursor_pos) const;

    // this should make all the necessary internal updates to hitobjects when legacy osu mods or static mods change
    // live (but also on start)
    void onModUpdate(bool rebuildSliderVertexBuffers = true, bool recomputeDrainRate = true);

    void loadBeatmapSkin();
    void loadStoryboard();

    // puts the selected map's settings in effect for the play (see CvarEditor::CONTEXT), again after they changed
    void applyMapSettings();

    [[nodiscard]] inline Storyboard *getStoryboard() const { return this->storyboard.get(); }
    [[nodiscard]] Storyboard::View getStoryboardView() const;

    // Returns true if we're loading or waiting on other players
    bool isLoading();

    // Returns true if the local player is loading
    [[nodiscard]] bool isActuallyLoading() const;

    // returns true if we are actually just in the pause menu doing nothing (but NOT in "continue" screen)
    [[nodiscard]] bool isActuallyPausedAndNotSpectating() const;

    [[nodiscard]] vec2 legacyPixels2RawPixels(
        vec2 coords) const;  // only used for bounds calculations atm (just scales, nothing else)
    [[nodiscard]] vec2 pixels2OsuCoords(vec2 pixelCoords) const;  // only used for positional audio atm
    [[nodiscard]] vec2 osuCoords2Pixels(
        vec2 coords) const override;  // hitobjects should use this one (includes lots of special behaviour)
    [[nodiscard]] vec2 osuCoords2LegacyPixels(vec2 coords)
        const override;  // only applies vanilla osu mods and static mods to the coordinates (used for generating
                         // the static slider mesh) centered at (0, 0, 0)
    [[nodiscard]] f32 osuAngle2PixelAngle(f32 degrees) const override;

    // this sounds redundant to osu pixels, but the difference is, replay coords don't include playfield flip/rotation.
    // and that's only a thing because i wanted mirror mod to be usable on vanilla servers 2+ years ago
    [[nodiscard]] vec2 pixels2ReplayCoords(vec2 pixelCoords) const;
    [[nodiscard]] vec2 replayCoords2Pixels(vec2 coords) const;

    // cursor
    [[nodiscard]] vec2 getMousePos() const;
    [[nodiscard]] vec2 getCursorPos() const;

    [[nodiscard]] const Replay::Mods &getMods() const;
    [[nodiscard]] vec2 getFirstPersonCursorDelta() const override;

    // playfield
    [[nodiscard]] inline f32 getPlayfieldScaleFactor() const override { return this->fScaleFactor; }
    [[nodiscard]] inline vec2 getPlayfieldSize() const override { return this->vPlayfieldSize; }
    [[nodiscard]] inline vec2 getPlayfieldCenter() const override { return this->vPlayfieldCenter; }
    [[nodiscard]] vec2 getScreenSize() const override;

    // hitobjects
    [[nodiscard]] inline f32 getHitcircleXMultiplier() const {
        return this->fXMultiplier;
    }  // multiply osu!pixels with this to get screen pixels
    [[nodiscard]] inline f32 getHitcircleDiameter() const override { return this->fHitcircleDiameter; }
    [[nodiscard]] inline f32 getRawHitcircleDiameter() const override { return this->fRawHitcircleDiameter; }
    [[nodiscard]] inline f32 getSliderFollowCircleDiameter() const override {
        return this->fSliderFollowCircleDiameter;
    }
    [[nodiscard]] inline f32 getNumberScale() const override { return this->fNumberScale; }
    [[nodiscard]] inline f32 getHitcircleOverlapScale() const override { return this->fHitcircleOverlapScale; }
    [[nodiscard]] inline bool isInMafhamRenderChunk() const override { return this->bInMafhamRenderChunk; }
    [[nodiscard]] bool slidersRenderDynamically() const override;

    // null without a selected beatmap
    // XXX: this means we have nullptr checks splattered everywhere, which i'm not totally happy with
    [[nodiscard]] inline GameplayJudge *getJudge() const { return this->judge.get(); }

    // score
    [[nodiscard]] inline f32 getAimStars() const { return this->fAimStars; }
    [[nodiscard]] inline f32 getAimSliderFactor() const { return this->fAimSliderFactor; }
    [[nodiscard]] inline f32 getSpeedStars() const { return this->fSpeedStars; }
    [[nodiscard]] inline f32 getSpeedNotes() const { return this->fSpeedNotes; }
    [[nodiscard]] f32 getSpeedMultiplier() const;
    [[nodiscard]] f32 getPitchMultiplier() const;

    const AsyncPPC::pp_res &getWholeMapPPInfo() const;

    // the judgements shown on the scrubbing timeline: all of them when watching a replay, the ones so far otherwise
    [[nodiscard]] const std::vector<LiveScore::TimedHitResult> &getTimelineHitresults() const;

    // hud
    [[nodiscard]] bool isSpinnerActive() const;

    // callbacks called by the Osu class (osu!standard)
    void skipEmptySection();
    void scheduleSkip() { this->bSkipScheduled = true; }  // skips once the current section turns out to be skippable
    void onKey(GameplayKeys key_flag, bool down, u64 timestamp);

    // loads the music of the currently selected diff and starts playing from the previewTime (e.g. clicking on a beatmap)
    void selectBeatmap();
    void selectBeatmap(DatabaseBeatmap *map);

    // stops + unloads the currently loaded music and deletes all hitobjects
    void deselectBeatmap();

    bool play();
    bool watch(const FinishedScore &score, u32 start_ms);
    bool spectate();

    bool start();
    void restart(bool quick = false);
    void pause(bool quitIfWaiting = true);
    void stop(bool quit = true);
    void fail(bool force_death = false);
    void cancelFailing();
    void resetScore();

    // the speed (and the pitch that goes with it) gameplay plays the music at, or 1 outside of it
    void setMusicSpeed(f32 speed);
    void seekMS(u32 ms);
    [[nodiscard]] inline DBTimingInfo getCurrentTimingInfo() const { return this->cur_timing_info; }

    [[nodiscard]] u32 getTime() const;
    [[nodiscard]] u32 getStartTimePlayable() const;
    [[nodiscard]] u32 getLength() const;
    [[nodiscard]] u32 getLengthPlayable() const;
    [[nodiscard]] f32 getPercentFinished() const;
    [[nodiscard]] f32 getPercentFinishedPlayable() const;

    // live statistics
    [[nodiscard]] int getMostCommonBPM() const;

    // set to false when using protected cvars
    bool is_submittable = true;

    f32 fHitcircleDiameter{0.f};  // screen px

    // replay recording
    void write_frame(const LegacyReplay::Frame &frame, bool force);
    f64 last_event_time = 0.0;
    i32 last_event_ms = 0;
    u8 current_keys = 0;
    u8 raw_gameplay_keys{0};  // physical key state before keylock filtering
    u8 last_keys = 0;

    // replay replaying (prerecorded)
    FinishedScore replay_data;
    std::vector<LiveScore::TimedHitResult> replay_hitresults;  // every judgement of replay_data
    vec2 interpolatedMousePos{0.f};
    bool is_watching = false;
    i32 current_frame_idx = 0;

    // getting spectated (live)
    void broadcast_spectator_frames();
    std::vector<LiveReplayFrame> frame_batch;
    f64 last_spectator_broadcast = 0;
    u16 spectator_sequence = 0;

    // spectating (live)
    friend class SpectatorScreen;
    [[nodiscard]] i32 getSpectatingLeeway() const;
    std::vector<ScoreFrame> score_frames;
    bool is_buffering = false;
    bool spectate_pause = false;  // the player we're spectating has paused

    // multiplayer
    bool all_players_loaded = false;
    bool all_players_skipped = false;
    bool player_loaded = false;

    // used by HitObject children and ModSelector
    [[nodiscard]] const Skin *getSkin() const override;  // maybe use this for beatmap skins, maybe
    [[nodiscard]] Skin *getSkinMutable();

    // NOTE: these may not be current outside of gameplay!
    [[nodiscard]] inline i32 getCurMusicPos() const override { return this->iCurMusicPos; }
    [[nodiscard]] inline i32 getCurMusicPosWithOffsets() const override { return this->iCurMusicPosWithOffsets; }
    [[nodiscard]] f32 getBaseAnimationSpeed() const override;
    [[nodiscard]] f32 getSpeedAdjustedAnimationSpeed() const override;
    [[nodiscard]] ModFlags getModFlags() const override;
    [[nodiscard]] Color getComboColor(i32 colorCounter, i32 colorOffset) const override;

    [[nodiscard]] f32 getRawAR() const;
    [[nodiscard]] f32 getAR() const;
    [[nodiscard]] f32 getCS() const;
    [[nodiscard]] f32 getHP() const;
    [[nodiscard]] f32 getRawOD() const;
    [[nodiscard]] f32 getOD() const;
    [[nodiscard]] f32 getApproachTime() const override;
    [[nodiscard]] f64 getHitWindow300() const;
    [[nodiscard]] f64 getHitWindow100() const;
    [[nodiscard]] f64 getHitWindow50() const;
    [[nodiscard]] f32 getApproachRateForSpeedMultiplier() const;
    [[nodiscard]] f32 getRawARForSpeedMultiplier() const;
    [[nodiscard]] f32 getOverallDifficultyForSpeedMultiplier() const;
    [[nodiscard]] f32 getRawODForSpeedMultiplier() const;
    [[nodiscard]] i32 getCurrentHitObjectIndex() const;
    [[nodiscard]] inline const BeatmapDifficulty *getBeatmap() const { return this->beatmap; }
    [[nodiscard]] inline BeatmapDifficulty *getBeatmapMutable() const { return this->beatmap; }

    // health
    [[nodiscard]] f64 getHealth() const;
    [[nodiscard]] bool hasFailed() const override;

    // generic state
    [[nodiscard]] u8 getKeys() const { return this->current_keys; }
    [[nodiscard]] bool isClickHeld() const { return this->current_keys & ~LegacyReplay::Smoke; }
    [[nodiscard]] inline bool isPlaying() const { return this->bIsPlaying; }
    [[nodiscard]] inline bool isPaused() const { return this->bIsPaused; }
    [[nodiscard]] inline bool isRestartScheduled() const { return this->bIsRestartScheduled; }
    [[nodiscard]] inline bool isContinueScheduled() const { return this->bContinueScheduled; }
    // whether a continue is waiting for its click and the cursor is where it would go ahead
    [[nodiscard]] bool isCursorOnContinuePoint() const;
    [[nodiscard]] inline bool isInSkippableSection() const { return this->bIsInSkippableSection; }
    [[nodiscard]] inline bool isSkipScheduled() const { return this->bSkipScheduled; }
    [[nodiscard]] inline bool isSkipButtonHeld() const { return this->bClickedSkipButton; }
    [[nodiscard]] inline bool isInBreak() const { return this->bInBreak; }
    [[nodiscard]] inline bool shouldFlashWarningArrows() const { return this->bShouldFlashWarningArrows; }
    [[nodiscard]] inline f32 shouldFlashSectionPass() const { return this->fShouldFlashSectionPass; }
    [[nodiscard]] inline f32 shouldFlashSectionFail() const { return this->fShouldFlashSectionFail; }
    [[nodiscard]] bool isWaiting() const { return this->bIsWaiting; }

    void playHitSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 delta,
                      i32 timeMS) override;
    void playSliderTickSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 timeMS) override;
    std::vector<neomod::HitSoundUtils::Set_Slider_Hit> updateSliderSlideSounds(
        bool sliding, neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos,
        const std::vector<neomod::HitSoundUtils::Set_Slider_Hit> &started) override;
    void stopSliderSounds(const std::vector<neomod::HitSoundUtils::Set_Slider_Hit> &started) override;
    void playSpinnerSpinSound(f32 ratio, neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples) override;
    void stopSpinnerSpinSound() override;
    void playSpinnerBonusSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples) override;
    void addTargetHit(f32 delta, f32 angle) override;
    void onComboBreak() override;
    void onHealthGain(f64 healthBefore) override;
    bool onFail(bool byMods) override;

    void invalidateWholeMapPPInfo();
    [[nodiscard]] inline f32 live_pp() const { return this->ppv2_calc.get_pp(); }
    [[nodiscard]] inline f32 live_stars() const { return this->ppv2_calc.get_stars(); }

    // ILLEGAL:
    [[nodiscard]] inline f32 getBreakBackgroundFadeAnim() const { return this->fBreakBackgroundFade; }

    // beatmap state
    bool bIsPlaying;
    bool bIsPaused;
    bool bIsWaiting;
    bool bLeadInStarted{false};
    i32 iLeadInMS{0};  // the lead-in's length in music time (0: none)
    bool bIsRestartScheduled;
    bool bIsRestartScheduledQuick;
    bool bWasSeekFrame;
    bool bTempSeekNF{false};

   protected:
    // internal
    bool canDraw();

    void actualRestart();
    // puts the music where the play starts, which waits there for its lead-in (see update2()); quick: a quick restart's
    void seekToStart(bool quick);
    // where a storyboard going on after the last hitobject holds the play, as a music position (none after a fail or
    // in multiplayer rooms)
    [[nodiscard]] std::optional<i32> getStoryboardEnd() const;
    // past the last hitobject with more than 3 s of such a storyboard left: skipping goes to its end
    [[nodiscard]] bool isInOutro() const;

    void unloadObjects();

    void step(f64 frametime);
    void stopObjectSounds();

    bool bShouldFlashWarningArrows;
    f32 fShouldFlashSectionPass;
    f32 fShouldFlashSectionFail;
    bool bContinueScheduled;
    f64 fPrevUnpauseTime{0.f};

    i32 iCurMusicPos;
    i32 iCurMusicPosWithOffsets;
    u64 iLastMusicPosUpdateTime{0};
    bool bIsFirstMissSound;
    DBTimingInfo cur_timing_info{};

    // contains hitobjects, score, health
    std::unique_ptr<GameplayJudge> judge;

    const DatabaseBeatmap *storyboardMap{nullptr};  // to check if we can reused the current storyboard
    Async::CancellableHandle<std::unique_ptr<Storyboard>> storyboardHandle;
    std::unique_ptr<Storyboard> storyboard;

    AnimFloat fFailAnim;
    AnimFloat fBreakBackgroundFade;
    bool bInBreak;
    bool bIsInSkippableSection;
    bool bSkipScheduled{false};
    bool bClickedSkipButton{false};  // (keeps the click on the skip button from seeking)
    i32 iPreviousSectionPassFailTime;
    i32 iLastNightcoreBeatTime{std::numeric_limits<i32>::min()};

    struct KeyEvent {
        Click click;  // when and where (screen px)
        u8 keys;      // the keys held after it
        u8 clickKey;  // the key it clicks with (0: none)
    };
    std::vector<KeyEvent> key_events;
    std::vector<Click> all_clicks;
    bool bClickedContinue;
    u8 lastPressedKey{0};
    int iAllowAnyNextKeyUntilHitObjectIndex;

    std::vector<neomod::HitObject *> objectsToDraw;  // for drawHitObjects, temp buffer

   private:
    void playNightcoreBeats(bool wasSeekFrame = false);

    // yes, you're looking at the third fail method (not counting cancelFailing).
    // but between regular dying, replay watching, spectating, multiplayer and others...
    // the complexity really is hard to eliminate.
    bool failPlay(bool force_death);

    FinishedScore saveAndSubmitScore(bool quit);

    void drawHitObjects();
    void drawSmoke();

    enum class FLType : uint8_t { NORMAL_FL, ACTUAL_FL };
    void drawFlashlight(FLType type);

    void drawContinue();

    void updatePlayfieldMetrics();
    void updateHitobjectMetrics();
    void updateSliderVertexBuffers();

    // beatmap
    BeatmapDifficulty *beatmap{nullptr};
    vec2 vContinueCursorPoint{0.f};
    MusicTrack &music;

    // playfield
    f32 fScaleFactor;
    vec2 vPlayfieldCenter{0.f};
    vec2 vPlayfieldOffset{0.f};
    vec2 vPlayfieldSize{0.f};

    // hitobject scaling (screen px)
    f32 fXMultiplier;
    f32 fRawHitcircleDiameter{0.f};  // osu!px
    f32 fSliderFollowCircleDiameter{0.f};
    f32 fNumberScale;
    f32 fHitcircleOverlapScale;

    // auto
    vec2 vAutoCursorPos{0.f};

    // live and precomputed pp/stars
    void resetLiveStarsTasks();

    mutable AsyncPPC::pp_res full_ppinfo;
    mutable AsyncPPC::pp_calc_request full_calc_req_params;

    // live pp/stars
    friend struct LivePPCalc;
    LivePPCalc ppv2_calc;

    // pp calculation buffer (only needs to be recalculated in onModUpdate(), instead of on every hit)
    f32 fAimStars;
    f32 fAimSliderFactor;
    f32 fSpeedStars;
    f32 fSpeedNotes;

    // dynamic slider vertex buffer and other recalculation checks (for live mod switching)
    f32 fPrevHitCircleDiameter;
    bool bWasHorizontalMirrorEnabled;
    bool bWasVerticalMirrorEnabled;
    bool bWasEZEnabled;
    bool bWasMafhamEnabled;
    f32 fPrevPlayfieldRotationFromConVar;

    // FL
    vec2 flashlight_position{0.f};
    Shader *actual_flashlight_shader{nullptr};
    Shader *flashlight_shader{nullptr};

    // custom
    bool bIsPreLoading;
    int iPreLoadingIndex;
    bool bWasHREnabled;  // dynamic stack recalculation

    RenderTarget *mafhamActiveRenderTarget;
    RenderTarget *mafhamFinishedRenderTarget;
    bool bMafhamRenderScheduled;
    int iMafhamHitObjectRenderIndex;  // scene buffering for rendering entire beatmaps at once with an acceptable
                                      // framerate
    int iMafhamPrevHitObjectIndex;
    int iMafhamActiveRenderHitObjectIndex;
    int iMafhamFinishedRenderHitObjectIndex;
    bool bInMafhamRenderChunk;  // used by Slider to not animate the reverse arrow, and by Circle to not animate
                                // note blocking shaking, while being rendered into the scene buffer

    struct SMOKETRAIL {
        vec2 pos{0.f};
        u64 time;
    };
    std::vector<SMOKETRAIL> smoke_trail;
};
