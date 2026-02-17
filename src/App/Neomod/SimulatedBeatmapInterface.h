#pragma once

#include "BeatmapInterface.h"
#include "Replay.h"

class SimulatedBeatmapInterface final : public AbstractBeatmapInterface {
    NOCOPY_NOMOVE(SimulatedBeatmapInterface)
   public:
    SimulatedBeatmapInterface() = delete;
    SimulatedBeatmapInterface(DatabaseBeatmap *map, const Replay::Mods &mods_);
    ~SimulatedBeatmapInterface() override;

    Replay::Mods mods;
    LiveScore live_score;

    void simulate_to(i32 music_pos);

    bool start();
    void update(f64 frame_time);

    [[nodiscard]] const Replay::Mods &getMods() const override { return this->mods; }
    [[nodiscard]] LegacyFlags getModsLegacy() const override { return this->mods.to_legacy(); }

    [[nodiscard]] vec2 pixels2OsuCoords(vec2 pixelCoords) const override;  // only used for positional audio atm
    [[nodiscard]] vec2 osuCoords2Pixels(
        vec2 coords) const override;  // hitobjects should use this one (includes lots of special behaviour)
    [[nodiscard]] vec2 osuCoords2RawPixels(vec2 coords)
        const override;  // raw transform from osu!pixels to absolute screen pixels (without any mods whatsoever)
    [[nodiscard]] vec2 osuCoords2LegacyPixels(vec2 coords)
        const override;  // only applies vanilla osu mods and static mods to the coordinates (used for generating
                         // the static slider mesh) centered at (0, 0, 0)

    // cursor
    [[nodiscard]] vec2 getCursorPos() const override;
    [[nodiscard]] vec2 getFirstPersonCursorDelta() const;

    // playfield
    [[nodiscard]] inline vec2 getPlayfieldSize() const { return this->vPlayfieldSize; }
    [[nodiscard]] inline vec2 getPlayfieldCenter() const { return this->vPlayfieldCenter; }
    [[nodiscard]] inline float getPlayfieldRotation() const { return this->fPlayfieldRotation; }

    // hitobjects
    [[nodiscard]] inline float getHitcircleXMultiplier() const {
        return this->fXMultiplier;
    }  // multiply osu!pixels with this to get screen pixels

    void fail(bool force_death = false);
    void cancelFailing();
    void resetScore();

    // live statistics
    [[nodiscard]] inline int getNPS() const { return this->iNPS; }
    [[nodiscard]] inline int getND() const { return this->iND; }

    // replay recording
    u8 current_keys = 0;
    u8 last_keys = 0;

    // replay replaying (prerecorded)
    // current_keys, last_keys also reused
    std::vector<LegacyReplay::Frame> spectated_replay;
    vec2 interpolatedMousePos{0.f};
    i32 current_frame_idx = 0;

    // generic state
    [[nodiscard]] u8 getKeys() const override { return this->current_keys; }
    [[nodiscard]] bool isContinueScheduled() const override { return false; }
    [[nodiscard]] bool isPaused() const override { return false; }
    [[nodiscard]] bool isPlaying() const override { return true; }
    [[nodiscard]] f32 getRawAR() const override;
    [[nodiscard]] f32 getAR() const override;
    [[nodiscard]] f32 getCS() const override;
    [[nodiscard]] f32 getHP() const override;
    [[nodiscard]] f32 getRawOD() const override;
    [[nodiscard]] f32 getOD() const override;
    [[nodiscard]] f32 getRawApproachTime() const override;
    [[nodiscard]] f32 getApproachTime() const override;

    [[nodiscard]] u32 getLength() const override;
    [[nodiscard]] u32 getLengthPlayable() const override;
    [[nodiscard]] u32 getBreakDurationTotal() const override;
    [[nodiscard]] DatabaseBeatmap::BREAK getBreakForTimeRange(i32 startMS, i32 positionMS, i32 endMS) const;

    // HitObject and other helper functions
    LiveScore::HIT addHitResult(HitObject *hitObject, LiveScore::HIT hit, i32 delta, bool isEndOfCombo = false,
                                bool ignoreOnHitErrorBar = false, bool hitErrorBarOnly = false,
                                bool ignoreCombo = false, bool ignoreScore = false, bool ignoreHealth = false) override;
    void addSliderBreak() override;
    void addHealth(f64 percent, bool isFromHitResult);

    void addScorePoints(int points, bool isSpinner = false) override;
    [[nodiscard]] bool isWaiting() const override { return false; }

    [[nodiscard]] f32 getSpeedMultiplier() const override { return this->mods.speed; }
    [[nodiscard]] f32 getPitchMultiplier() const override;

   protected:
    // sound
    i32 iCurMusicPos = 0;

    // health
    bool bFailed = false;
    f64 fHealth = 1.0;
    f64 fDrainRate = 0.0;

    // breaks
    std::vector<DatabaseBeatmap::BREAK> breaks;
    bool bInBreak = false;
    HitObject *currentHitObject = nullptr;
    i32 iNextHitObjectTime = 0;
    i32 iPreviousHitObjectTime = 0;

    // player input
    i32 iAllowAnyNextKeyUntilHitObjectIndex = 0;
    std::vector<Click> clicks;

    // hitobjects
    std::vector<std::unique_ptr<HitObject>> hitobjects;
    std::vector<HitObject *> hitobjectsSortedByEndTime;  // non-owning view of hitobjects data

    // statistics
    i32 iNPS;
    i32 iND;
    i32 iCurrentHitObjectIndex;

   private:
    static inline vec2 mapNormalizedCoordsOntoUnitCircle(const vec2 &in) {
        return vec2(in.x * std::sqrt(1.0f - in.y * in.y / 2.0f), in.y * std::sqrt(1.0f - in.x * in.x / 2.0f));
    }

    static float quadLerp3f(float left, float center, float right, float percent) {
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

    void updateAutoCursorPos();
    void updatePlayfieldMetrics();
    void updateHitobjectMetrics();

    void calculateStacks();
    void computeDrainRate();

    // beatmap
    bool bIsSpinnerActive;

    // playfield
    float fPlayfieldRotation;
    vec2 vPlayfieldCenter{0.f};
    vec2 vPlayfieldSize{0.f};

    // hitobject scaling
    float fXMultiplier;

    // auto
    vec2 vAutoCursorPos{0.f};
};
