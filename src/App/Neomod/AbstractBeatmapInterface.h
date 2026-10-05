#pragma once
#include "noinclude.h"

#include "Vectors.h"

#include <vector>

namespace Replay {
struct Mods;
}
enum class LegacyFlags : u32;
enum class LiveHitResult : uint8_t;
namespace neomod {
class HitObject;
namespace DatabaseBeatmapTypes {
struct HITSAMPLE_BITS;
}
namespace HitSoundUtils {
struct Set_Slider_Hit;
}
}  // namespace neomod

class DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;

// a click as the play hands it to its objects to judge (HitObject::onClickEvent)
struct Click {
    u64 timestampNS;      // Timing::getTicksNS() when the event occurred
    vec2 cursorPos{0.f};  // cursor position when the click happened
    i32 musicPosMS;       // current music position when the click happened
};

// the play that judges hitobjects: gameplay, or a simulated play (watched replays, spectating, seeks) that only keeps
// the score
class AbstractBeatmapInterface {
    NOCOPY_NOMOVE(AbstractBeatmapInterface)
   public:
    AbstractBeatmapInterface() = default;
    virtual ~AbstractBeatmapInterface() = default;

    virtual LiveHitResult addHitResult(neomod::HitObject *hitObject, LiveHitResult hit, i32 delta,
                                       bool isEndOfCombo = false, bool ignoreOnHitErrorBar = false,
                                       bool hitErrorBarOnly = false, bool ignoreCombo = false, bool ignoreScore = false,
                                       bool ignoreHealth = false) = 0;

    [[nodiscard]] virtual u32 getBreakDurationTotal() const = 0;
    [[nodiscard]] virtual u8 getKeys() const = 0;
    [[nodiscard]] virtual u32 getLength() const = 0;
    [[nodiscard]] virtual u32 getLengthPlayable() const = 0;
    [[nodiscard]] virtual bool isContinueScheduled() const = 0;
    [[nodiscard]] virtual bool isPaused() const = 0;
    [[nodiscard]] virtual bool isPlaying() const = 0;
    [[nodiscard]] virtual bool isWaiting() const = 0;
    // stopped at a fail (gameplay's fail animation); a simulated play keeps judging after it fails
    [[nodiscard]] virtual bool hasFailed() const { return false; }

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

    [[nodiscard]] virtual const Replay::Mods &getMods() const = 0;
    [[nodiscard]] virtual LegacyFlags getModsLegacy() const = 0;
    [[nodiscard]] virtual vec2 getCursorPos() const = 0;

    virtual void addScorePoints(int points, bool isSpinner = false) = 0;
    virtual void addSliderBreak() = 0;

    // what judging plays and shows besides the results (rawPos: where on the playfield, for panning); a simulated play
    // has none of it
    virtual void playHitSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 delta,
                              i32 timeMS = -1);
    virtual void playSliderTickSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 timeMS);
    // a slider's slide sounds while it's slid, stopped otherwise; returns the ones it started (see HitSoundUtils::play)
    virtual std::vector<neomod::HitSoundUtils::Set_Slider_Hit> updateSliderSlideSounds(
        bool sliding, neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos,
        const std::vector<neomod::HitSoundUtils::Set_Slider_Hit> &started);
    // the started ones, or every slider sound if there are none
    virtual void stopSliderSounds(const std::vector<neomod::HitSoundUtils::Set_Slider_Hit> &started);
    virtual void playSpinnerSpinSound(f32 ratio);  // ratio: of the spinning speed needed
    virtual void stopSpinnerSpinSound();
    virtual void playSpinnerBonusSound();
    virtual void addTargetHit(f32 delta, f32 angle);  // the Target mod's hits, by distance and angle from the center

    // osu!px to the space the cursor is in
    [[nodiscard]] virtual vec2 osuCoords2Pixels(vec2 coords) const = 0;

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
    [[nodiscard]] inline const BeatmapDifficulty *getBeatmap() const { return this->beatmap; }
    [[nodiscard]] inline BeatmapDifficulty *getBeatmapMutable() const { return this->beatmap; }

    [[nodiscard]] bool isClickHeld() const;
    [[nodiscard]] LiveHitResult getHitResult(i32 delta) const;

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
