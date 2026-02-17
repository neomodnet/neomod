#pragma once
// Copyright (c) 2015, PG, All rights reserved.

#include "AbstractBeatmapInterface.h"
#include "DatabaseBeatmap.h"
#include "AsyncPPCalculator.h"
#include "LegacyReplay.h"
#include "PlaybackInterpolator.h"
#include "score.h"
#include "LivePPCalc.h"

#include <memory>

class RenderTarget;
class Sound;
class Shader;
class ConVar;
struct Skin;
class Resource;
class HitObject;
class DatabaseBeatmap;
class SimulatedBeatmapInterface;
struct LiveReplayFrame;
struct ScoreFrame;

struct Click {
    u64 timestamp;  // Timing::getTicksNS() when the event occurred
    vec2 pos{0.f};
    i32 music_pos;  // current music position when the click happened
};

class BeatmapInterface final : public AbstractBeatmapInterface {
    NOCOPY_NOMOVE(BeatmapInterface)
   public:
    // for handling transition from unloaded database to loaded database
    static inline constinit MD5Hash loading_reselect_map{};

    BeatmapInterface();
    ~BeatmapInterface() override;

    void draw();
    void drawDebug();
    void drawBackground();

    void update();
    void update2();  // Used to be Playfield::update()

    // this should make all the necessary internal updates to hitobjects when legacy osu mods or static mods change
    // live (but also on start)
    void onModUpdate(bool rebuildSliderVertexBuffers = true, bool recomputeDrainRate = true);

    // HACK: Updates buffering state and pauses/unpauses the music!
    bool isBuffering();

    // Returns true if we're loading or waiting on other players
    bool isLoading();

    // Returns true if the local player is loading
    [[nodiscard]] bool isActuallyLoading() const;

    // returns true if we are actually just in the pause menu doing nothing (but NOT in "continue" screen)
    [[nodiscard]] bool isActuallyPausedAndNotSpectating() const;

    [[nodiscard]] vec2 pixels2OsuCoords(vec2 pixelCoords) const override;  // only used for positional audio atm
    [[nodiscard]] vec2 osuCoords2Pixels(
        vec2 coords) const override;  // hitobjects should use this one (includes lots of special behaviour)
    [[nodiscard]] vec2 osuCoords2RawPixels(vec2 coords)
        const override;  // raw transform from osu!pixels to absolute screen pixels (without any mods whatsoever)
    [[nodiscard]] vec2 osuCoords2LegacyPixels(vec2 coords)
        const override;  // only applies vanilla osu mods and static mods to the coordinates (used for generating
                         // the static slider mesh) centered at (0, 0, 0)

    // cursor
    [[nodiscard]] vec2 getMousePos() const;
    [[nodiscard]] vec2 getCursorPos() const override;
    [[nodiscard]] vec2 getFirstPersonCursorDelta() const;

    // playfield
    [[nodiscard]] inline vec2 getPlayfieldSize() const { return this->vPlayfieldSize; }
    [[nodiscard]] inline vec2 getPlayfieldCenter() const { return this->vPlayfieldCenter; }
    [[nodiscard]] inline f32 getPlayfieldRotation() const { return this->fPlayfieldRotation; }

    // hitobjects
    [[nodiscard]] inline f32 getHitcircleXMultiplier() const {
        return this->fXMultiplier;
    }  // multiply osu!pixels with this to get screen pixels
    [[nodiscard]] inline f32 getNumberScale() const { return this->fNumberScale; }
    [[nodiscard]] inline f32 getHitcircleOverlapScale() const { return this->fHitcircleOverlapScale; }
    [[nodiscard]] inline bool isInMafhamRenderChunk() const { return this->bInMafhamRenderChunk; }

    // score
    [[nodiscard]] inline int getNumHitObjects() const { return this->hitobjects.size(); }
    [[nodiscard]] inline f32 getAimStars() const { return this->fAimStars; }
    [[nodiscard]] inline f32 getAimSliderFactor() const { return this->fAimSliderFactor; }
    [[nodiscard]] inline f32 getSpeedStars() const { return this->fSpeedStars; }
    [[nodiscard]] inline f32 getSpeedNotes() const { return this->fSpeedNotes; }
    [[nodiscard]] f32 getSpeedMultiplier() const override;
    [[nodiscard]] f32 getPitchMultiplier() const override;

    const AsyncPPC::pp_res &getWholeMapPPInfo();

    // hud
    [[nodiscard]] inline bool isSpinnerActive() const { return this->bIsSpinnerActive; }

    // callbacks called by the Osu class (osu!standard)
    void skipEmptySection();
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
    void pausePreviewMusic(bool toggle = true);
    bool isPreviewMusicPlaying();
    void stop(bool quit = true);
    void fail(bool force_death = false);
    void cancelFailing();
    void resetScore();

    // music/sound
    inline void reloadMusicNow() { this->loadMusic(true, false); }
    void loadMusic(bool reload = false, bool async = false);
    void unloadMusic();
    static void onMusicLoadingFinished(Resource *rs, void *this_);

    f32 getIdealVolume();
    void setMusicSpeed(f32 speed);
    void setMusicPitch(f32 pitch);
    void seekMS(u32 ms);
    [[nodiscard]] inline DatabaseBeatmap::TIMING_INFO getCurrentTimingInfo() const { return this->cur_timing_info; }
    [[nodiscard]] inline i32 getDefaultSampleSet() const { return this->default_sample_set; }

    [[nodiscard]] inline Sound *getMusic() const { return this->music; }
    [[nodiscard]] u32 getTime() const;
    [[nodiscard]] u32 getStartTimePlayable() const;
    [[nodiscard]] u32 getLength() const override;
    [[nodiscard]] u32 getLengthPlayable() const override;
    [[nodiscard]] f32 getPercentFinished() const;
    [[nodiscard]] f32 getPercentFinishedPlayable() const;

    // live statistics
    [[nodiscard]] int getMostCommonBPM() const;
    [[nodiscard]] inline int getNPS() const { return this->iNPS; }
    [[nodiscard]] inline int getND() const { return this->iND; }

    // set to false when using protected cvars
    bool is_submittable = true;

    // replay recording
    void write_frame();
    std::vector<LegacyReplay::Frame> live_replay;
    f64 last_event_time = 0.0;
    i32 last_event_ms = 0;
    u8 current_keys = 0;
    u8 last_keys = 0;

    // replay replaying (prerecorded)
    // current_keys, last_keys also reused
    std::vector<LegacyReplay::Frame> spectated_replay;
    vec2 interpolatedMousePos{0.f};
    bool is_watching = false;
    i32 current_frame_idx = 0;
    std::unique_ptr<SimulatedBeatmapInterface> sim{nullptr};

    // getting spectated (live)
    void broadcast_spectator_frames();
    std::vector<LiveReplayFrame> frame_batch;
    f64 last_spectator_broadcast = 0;
    u16 spectator_sequence = 0;

    // spectating (live)
    std::vector<ScoreFrame> score_frames;
    bool is_buffering = false;
    i32 last_frame_ms = 0;
    bool spectate_pause = false;  // the player we're spectating has paused

    // multiplayer
    bool all_players_loaded = false;
    bool all_players_skipped = false;
    bool player_loaded = false;

    // used by HitObject children and ModSelector
    [[nodiscard]] const Skin *getSkin() const;  // maybe use this for beatmap skins, maybe
    [[nodiscard]] Skin *getSkinMutable();

    [[nodiscard]] inline i32 getCurMusicPos() const { return this->iCurMusicPos; }
    [[nodiscard]] inline i32 getCurMusicPosWithOffsets() const { return this->iCurMusicPosWithOffsets; }

    [[nodiscard]] f32 getRawAR() const override;
    [[nodiscard]] f32 getAR() const override;
    [[nodiscard]] f32 getCS() const override;
    [[nodiscard]] f32 getHP() const override;
    [[nodiscard]] f32 getRawOD() const override;
    [[nodiscard]] f32 getOD() const override;
    [[nodiscard]] f32 getApproachTime() const override;
    [[nodiscard]] f32 getRawApproachTime() const override;

    // health
    [[nodiscard]] inline f64 getHealth() const { return this->fHealth; }
    [[nodiscard]] inline bool hasFailed() const { return this->bFailed; }

    // generic state
    [[nodiscard]] u8 getKeys() const override { return this->current_keys; }
    [[nodiscard]] inline bool isPlaying() const override { return this->bIsPlaying; }
    [[nodiscard]] inline bool isPaused() const override { return this->bIsPaused; }
    [[nodiscard]] inline bool isRestartScheduled() const { return this->bIsRestartScheduled; }
    [[nodiscard]] inline bool isContinueScheduled() const override { return this->bContinueScheduled; }
    [[nodiscard]] inline bool isInSkippableSection() const { return this->bIsInSkippableSection; }
    [[nodiscard]] inline bool isInBreak() const { return this->bInBreak; }
    [[nodiscard]] inline bool shouldFlashWarningArrows() const { return this->bShouldFlashWarningArrows; }
    [[nodiscard]] inline f32 shouldFlashSectionPass() const { return this->fShouldFlashSectionPass; }
    [[nodiscard]] inline f32 shouldFlashSectionFail() const { return this->fShouldFlashSectionFail; }
    [[nodiscard]] bool isWaiting() const override { return this->bIsWaiting; }

    [[nodiscard]] inline const std::vector<DatabaseBeatmap::BREAK> &getBreaks() const { return this->breaks; }
    [[nodiscard]] u32 getBreakDurationTotal() const override;
    [[nodiscard]] DatabaseBeatmap::BREAK getBreakForTimeRange(i64 startMS, i64 positionMS, i64 endMS) const;

    // HitObject and other helper functions
    LiveScore::HIT addHitResult(HitObject *hitObject, LiveScore::HIT hit, i32 delta, bool isEndOfCombo = false,
                                bool ignoreOnHitErrorBar = false, bool hitErrorBarOnly = false,
                                bool ignoreCombo = false, bool ignoreScore = false, bool ignoreHealth = false) override;
    void addSliderBreak() override;
    void addScorePoints(int points, bool isSpinner = false) override;
    void addHealth(f64 percent, bool isFromHitResult);

    static bool sortHitObjectByStartTimeComp(HitObject const *a, HitObject const *b);
    static bool sortHitObjectByEndTimeComp(HitObject const *a, HitObject const *b);

    [[nodiscard]] inline f32 live_pp() const { return this->ppv2_calc.get_pp(); }
    [[nodiscard]] inline f32 live_stars() const { return this->ppv2_calc.get_stars(); }

    // ILLEGAL:
    [[nodiscard]] inline f32 getBreakBackgroundFadeAnim() const { return this->fBreakBackgroundFade; }

    int iCurrentHitObjectIndex;
    int iCurrentNumCircles;
    int iCurrentNumSliders;
    int iCurrentNumSpinners;

    // beatmap state
    bool bIsPlaying;
    bool bIsPaused;
    bool bIsWaiting;
    bool bIsRestartScheduled;
    bool bIsRestartScheduledQuick;
    bool bWasSeekFrame;
    bool bTempSeekNF{false};

   protected:
    // internal
    bool canDraw();

    void actualRestart();

    void handlePreviewPlay();
    void unloadObjects();

    void resetHitObjects(i32 curPos = 0);

    void playMissSound();

    [[nodiscard]] i32 getInterpedMusicPos() const;

    bool bIsInSkippableSection;
    bool bShouldFlashWarningArrows;
    f32 fShouldFlashSectionPass;
    f32 fShouldFlashSectionFail;
    bool bContinueScheduled;
    u32 iContinueMusicPos;
    f64 fWaitTime{0.f};
    f64 fPrevUnpauseTime{0.f};

    // sound
    mutable std::unique_ptr<GameplayInterpolator> musicInterp;

    f32 fMusicFrequencyBackup;
    i32 iCurMusicPos;
    i32 iCurMusicPosWithOffsets;
    u64 iLastMusicPosUpdateTime{0};
    f32 fAfterMusicIsFinishedVirtualAudioTimeStart;
    bool bIsFirstMissSound;
    bool bIsWaitingForPreview{false};
    DatabaseBeatmap::TIMING_INFO cur_timing_info{};
    i32 default_sample_set{1};

    // health
    bool bFailed;
    f32 fFailAnim;
    f64 fHealth;
    f32 fHealth2;

    // drain
    f64 fDrainRate;

    // breaks
    std::vector<DatabaseBeatmap::BREAK> breaks;
    f32 fBreakBackgroundFade;
    bool bInBreak;
    HitObject *currentHitObject;
    i32 iNextHitObjectTime;
    i32 iPreviousHitObjectTime;
    i32 iPreviousSectionPassFailTime;

    // player input
    bool bClickedContinue;
    int iAllowAnyNextKeyUntilHitObjectIndex;
    std::vector<Click> clicks;
    std::vector<Click> all_clicks;

    // hitobjects
    std::vector<std::unique_ptr<HitObject>> hitobjects;
    // these are non-owning views of "hitobjects" in different arrangements
    std::vector<HitObject *> hitobjectsSortedByEndTime;  // for hitObject->draw/draw2()
    std::vector<HitObject *> nonSpinnerObjectsToDraw;    // for drawHitObjects, temp buffer
    std::vector<HitObject *> misaimObjects;

    // statistics
    int iNPS;
    int iND;

    // custom
    int iPreviousFollowPointObjectIndex;  // TODO: this shouldn't be in this class

   private:
    static inline vec2 mapNormalizedCoordsOntoUnitCircle(const vec2 &in) {
        return vec2(in.x * std::sqrt(1.0f - in.y * in.y / 2.0f), in.y * std::sqrt(1.0f - in.x * in.x / 2.0f));
    }

    static f32 quadLerp3f(f32 left, f32 center, f32 right, f32 percent) {
        if(percent >= 0.5f) {
            percent = (percent - 0.5f) / 0.5f;
            percent *= percent;
            return std::lerp(center, right, percent);
        } else {
            percent = percent / 0.5f;
            percent = 1.0f - (1.0f - percent) * (1.0f - percent);
            return std::lerp(left, center, percent);
        }
    }

    FinishedScore saveAndSubmitScore(bool quit);

    void drawFollowPoints();
    void drawHitObjects();
    void drawSmoke();

    enum class FLType : uint8_t { NORMAL_FL, ACTUAL_FL };
    void drawFlashlight(FLType type);

    void drawContinue();

    void updateAutoCursorPos();
    void updatePlayfieldMetrics();
    void updateHitobjectMetrics();
    void updateSliderVertexBuffers();

    void calculateStacks();
    void computeDrainRate();

    // beatmap
    bool bIsSpinnerActive;
    vec2 vContinueCursorPoint{0.f};
    Sound *music;

    // playfield
    f32 fPlayfieldRotation;
    f32 fScaleFactor;
    vec2 vPlayfieldCenter{0.f};
    vec2 vPlayfieldOffset{0.f};
    vec2 vPlayfieldSize{0.f};

    // hitobject scaling
    f32 fXMultiplier;
    f32 fNumberScale;
    f32 fHitcircleOverlapScale;

    // auto
    vec2 vAutoCursorPos{0.f};
    int iAutoCursorDanceIndex;

    // live and precomputed pp/stars
    void resetLiveStarsTasks();
    void invalidateWholeMapPPInfo();

    AsyncPPC::pp_res full_ppinfo;
    AsyncPPC::pp_calc_request full_calc_req_params;

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
