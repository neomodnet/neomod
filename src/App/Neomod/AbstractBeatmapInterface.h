#pragma once
#include "Replay.h"
#include "score.h"
#include "Vectors.h"

class HitObject;
class DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;

// either simulated or actual
class AbstractBeatmapInterface {
    NOCOPY_NOMOVE(AbstractBeatmapInterface)
   public:
    AbstractBeatmapInterface() = default;
    virtual ~AbstractBeatmapInterface() = default;

    virtual LiveScore::HIT addHitResult(HitObject *hitObject, LiveScore::HIT hit, i32 delta, bool isEndOfCombo = false,
                                        bool ignoreOnHitErrorBar = false, bool hitErrorBarOnly = false,
                                        bool ignoreCombo = false, bool ignoreScore = false,
                                        bool ignoreHealth = false) = 0;

    [[nodiscard]] virtual u32 getBreakDurationTotal() const = 0;
    [[nodiscard]] virtual u8 getKeys() const = 0;
    [[nodiscard]] virtual u32 getLength() const = 0;
    [[nodiscard]] virtual u32 getLengthPlayable() const = 0;
    [[nodiscard]] virtual bool isContinueScheduled() const = 0;
    [[nodiscard]] virtual bool isPaused() const = 0;
    [[nodiscard]] virtual bool isPlaying() const = 0;
    [[nodiscard]] virtual bool isWaiting() const = 0;

    [[nodiscard]] virtual f32 getSpeedMultiplier() const = 0;
    [[nodiscard]] virtual f32 getPitchMultiplier() const = 0;
    [[nodiscard]] virtual f32 getRawAR() const = 0;
    [[nodiscard]] virtual f32 getRawOD() const = 0;
    [[nodiscard]] virtual f32 getAR() const = 0;
    [[nodiscard]] virtual f32 getCS() const = 0;
    [[nodiscard]] virtual f32 getHP() const = 0;
    [[nodiscard]] virtual f32 getOD() const = 0;
    [[nodiscard]] virtual f32 getApproachTime() const = 0;
    [[nodiscard]] virtual f32 getRawApproachTime() const = 0;

    [[nodiscard]] virtual const Replay::Mods &getMods() const;  // overridden by SimulatedBeatmapInterface
    [[nodiscard]] virtual LegacyFlags getModsLegacy() const;    // overridden by SimulatedBeatmapInterface
    [[nodiscard]] virtual vec2 getCursorPos() const = 0;

    virtual void addScorePoints(int points, bool isSpinner = false) = 0;
    virtual void addSliderBreak() = 0;

    [[nodiscard]] virtual vec2 pixels2OsuCoords(vec2 pixelCoords) const = 0;
    [[nodiscard]] virtual vec2 osuCoords2Pixels(vec2 coords) const = 0;
    [[nodiscard]] virtual vec2 osuCoords2RawPixels(vec2 coords) const = 0;
    [[nodiscard]] virtual vec2 osuCoords2LegacyPixels(vec2 coords) const = 0;

    f64 fHpMultiplierComboEnd = 1.0;
    f64 fHpMultiplierNormal = 1.0;
    i32 iMaxPossibleCombo = 0;
    u32 iScoreV2ComboPortionMaximum = 0;

    // Cache for update loop
    f32 fCachedApproachTimeForUpdate = 0.f;
    f32 fSpeedAdjustedAnimationSpeedFactor = 1.f;
    f32 fBaseAnimationSpeedFactor = 1.f;

    // It is assumed these values are set correctly
    u32 nb_hitobjects = 0;
    f32 fHitcircleDiameter = 0.f;
    f32 fRawHitcircleDiameter = 0.f;
    f32 fSliderFollowCircleDiameter = 0.f;
    u8 lastPressedKey = 0;
    bool holding_slider = false;

    // Generic behavior below, do not override
    [[nodiscard]] inline BeatmapDifficulty *getBeatmap() const { return this->beatmap; }

    [[nodiscard]] bool isClickHeld() const;
    [[nodiscard]] LiveScore::HIT getHitResult(i32 delta) const;

    // Potentially Visible Set gate time size, for optimizing draw() and update() when iterating over all hitobjects
    [[nodiscard]] i32 getPVS() const;

    [[nodiscard]] f32 getHitWindow300() const;
    [[nodiscard]] f32 getRawHitWindow300() const;
    [[nodiscard]] f32 getHitWindow100() const;
    [[nodiscard]] f32 getHitWindow50() const;
    [[nodiscard]] f32 getApproachRateForSpeedMultiplier() const;
    [[nodiscard]] f32 getRawARForSpeedMultiplier() const;
    [[nodiscard]] f32 getConstantApproachRateForSpeedMultiplier() const;
    [[nodiscard]] f32 getOverallDifficultyForSpeedMultiplier() const;
    [[nodiscard]] f32 getRawODForSpeedMultiplier() const;
    [[nodiscard]] f32 getConstantOverallDifficultyForSpeedMultiplier() const;
    [[nodiscard]] u32 getScoreV1DifficultyMultiplier() const;

    // for HitObject::update to avoid recalculating for each object every frame
    [[nodiscard]] forceinline f32 getCachedApproachTimeForUpdate() const { return this->fCachedApproachTimeForUpdate; }
    [[nodiscard]] forceinline f32 getSpeedAdjustedAnimationSpeed() const {
        return this->fSpeedAdjustedAnimationSpeedFactor;
    }
    [[nodiscard]] forceinline f32 getBaseAnimationSpeed() const { return this->fBaseAnimationSpeedFactor; }

   protected:
    BeatmapDifficulty *beatmap{nullptr};
};
