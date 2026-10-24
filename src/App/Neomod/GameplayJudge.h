#pragma once
#include "noinclude.h"
#include "types.h"

#include "BeatmapPrimitives.h"
#include "Color.h"
#include "DatabaseBeatmapTypes.h"
#include "LegacyReplay.h"
#include "Vectors.h"

#include <memory>
#include <span>
#include <vector>

class LiveScore;
class PlayfieldView;
class DatabaseBeatmap;
using BeatmapDifficulty = DatabaseBeatmap;
struct PlayedMap;
enum class LiveHitResult : uint8_t;
namespace Replay {
struct Mods;
}
namespace neomod {
class HitObject;
namespace HitSoundUtils {
struct Set_Slider_Hit;
}
}  // namespace neomod

struct Click {
    u64 timestampNS;      // Timing::getTicksNS() when the event occurred
    vec2 cursorPos{0.f};  // osu!px
    i32 musicPosMS;
};

// judges a play from its input frames into a score
// BeatmapInterface steps this and listens to it for the sounds and effects
class GameplayJudge final {
    NOCOPY_NOMOVE(GameplayJudge)
   public:
    using DBBreak = neomod::DatabaseBeatmapTypes::BREAK;

    class Listener {
       public:
        Listener() = default;
        virtual ~Listener() = default;
        Listener(const Listener &) = default;
        Listener &operator=(const Listener &) = default;
        Listener(Listener &&) = default;
        Listener &operator=(Listener &&) = default;

        virtual void playHitSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 delta,
                                  i32 timeMS);
        virtual void playSliderTickSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 timeMS);
        virtual std::vector<neomod::HitSoundUtils::Set_Slider_Hit> updateSliderSlideSounds(
            bool sliding, neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos,
            const std::vector<neomod::HitSoundUtils::Set_Slider_Hit> &started);
        virtual void stopSliderSounds(const std::vector<neomod::HitSoundUtils::Set_Slider_Hit> &started);
        // ratio: of the spinning speed needed
        virtual void playSpinnerSpinSound(f32 ratio, neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples);
        virtual void stopSpinnerSpinSound();
        virtual void playSpinnerBonusSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples);
        virtual void addTargetHit(f32 delta, f32 angle);  // target mod hits by distance and angle from the center

        virtual void onComboBreak();  // called right before the combo is reset
        virtual void onHealthGain(f64 healthBefore);
        virtual bool onFail(bool byMods);  // byMods true when fail is caused by Perfect/SuddenDeath
    };

    GameplayJudge(DatabaseBeatmap *map, LiveScore &score, const PlayfieldView *view = nullptr,
                  Listener *listener = nullptr);
    ~GameplayJudge();

    // attempt to read the map and make its objects
    neomod::Primitives::LoadError load();
    void unload();

    // play from the start again, or seek to curPos with everything before it done
    void reset(i32 curPos = 0);

    void step(const LegacyReplay::Frame &frame, f64 frameTimeSecs, u8 clickKeys);
    void simulate_to(i32 musicPos);
    std::vector<LegacyReplay::Frame> replay;

    void animate(i32 curPos);  // TODO: this shouldn't be in this class

    // updates all objects at curPos, even those outside of the PVS
    void updateAll(i32 curPos, f64 frameTimeSecs);

    // whether the last step judged something (or changed the judgement of future steps)
    [[nodiscard]] inline bool needsReplayFrame() const { return this->bNeedsReplayFrame; }
    void markReplayFrame() { this->bNeedsReplayFrame = true; }

    void fail(bool byMods = false);
    void markFailed() { this->bFailed = true; }
    void cancelFailing();
    [[nodiscard]] inline bool hasFailed() const { return this->bFailed; }
    [[nodiscard]] inline f64 getHealth() const { return this->fHealth; }
    void setHealth(f64 health) { this->fHealth = health; }
    bool bTempSeekNF{false};  // prevents health drain after seeking

    [[nodiscard]] inline const BeatmapDifficulty *getBeatmap() const { return this->beatmap; }
    [[nodiscard]] inline BeatmapDifficulty *getBeatmapMutable() const { return this->beatmap; }
    [[nodiscard]] inline const PlayedMap *getMapData() const { return this->mapData.get(); }

    [[nodiscard]] f32 getMapAR() const;
    [[nodiscard]] f32 getMapCS() const;
    [[nodiscard]] f32 getMapOD() const;
    [[nodiscard]] f32 getMapHP() const;
    [[nodiscard]] f32 getMapStackLeniency() const;
    [[nodiscard]] i32 getMapVersion() const;
    [[nodiscard]] const neomod::Primitives::TimingPoints &getMapTimingPoints() const;
    [[nodiscard]] inline const std::vector<Color> &getComboColors() const { return this->comboColors; }
    [[nodiscard]] inline u8 getDefaultSampleSet() const { return this->defaultSampleSet; }

    [[nodiscard]] const Replay::Mods &getMods() const;
    [[nodiscard]] inline LiveScore &getScore() const { return this->score; }

    // the current difficulty attributes as set by mods and time
    [[nodiscard]] f32 getRawAR() const;
    [[nodiscard]] f32 getAR() const;
    [[nodiscard]] f32 getCS() const;
    [[nodiscard]] f32 getHP() const;
    [[nodiscard]] f32 getRawOD() const;
    [[nodiscard]] f32 getOD() const;
    [[nodiscard]] f32 getApproachTime() const;
    [[nodiscard]] f32 getRawApproachTime() const;
    [[nodiscard]] f32 getSpeedMultiplier() const;
    [[nodiscard]] f32 getPitchMultiplier() const;
    [[nodiscard]] f64 getHitWindow300() const;
    [[nodiscard]] f32 getRawHitWindow300() const;
    [[nodiscard]] f64 getHitWindow100() const;
    [[nodiscard]] f64 getHitWindow50() const;
    [[nodiscard]] f32 getApproachRateForSpeedMultiplier() const;
    [[nodiscard]] f32 getRawARForSpeedMultiplier() const;
    [[nodiscard]] f32 getConstantApproachRateForSpeedMultiplier() const;
    [[nodiscard]] f32 getOverallDifficultyForSpeedMultiplier() const;
    [[nodiscard]] f32 getRawODForSpeedMultiplier() const;
    [[nodiscard]] f32 getConstantOverallDifficultyForSpeedMultiplier() const;
    [[nodiscard]] u32 getScoreV1DifficultyMultiplier() const;
    [[nodiscard]] LiveHitResult getHitResult(i32 delta) const;

    [[nodiscard]] u32 getLength() const;
    [[nodiscard]] u32 getLengthPlayable() const;
    [[nodiscard]] u32 getBreakDurationTotal() const;
    [[nodiscard]] DBBreak getBreakForTimeRange(i32 startMS, i32 positionMS, i32 endMS) const;
    [[nodiscard]] inline const std::vector<DBBreak> &getBreaks() const { return this->breaks; }

    // Potentially Visible Set gate time size, for optimizing draw() and update() when iterating over all hitobjects
    [[nodiscard]] i32 getPVS() const;

    // the objects, in the order they are played, and in the order of their end times
    [[nodiscard]] inline std::span<const std::unique_ptr<neomod::HitObject>> getHitObjects() const {
        return this->hitobjects;
    }
    [[nodiscard]] inline std::span<neomod::HitObject *const> getHitObjectsSortedByEndTime() const {
        return this->hitobjectsSortedByEndTime;
    }

    // the unfinished objects the last clicks could have been meant for (see cv::hiterrorbar_misaims)
    [[nodiscard]] inline std::span<neomod::HitObject *const> getMisaimObjects() const { return this->misaimObjects; }

    [[nodiscard]] inline i32 getCurMusicPos() const { return this->iCurMusicPos; }
    [[nodiscard]] inline const neomod::HitObject *getCurrentHitObject() const { return this->currentHitObject; }
    [[nodiscard]] inline i32 getCurrentHitObjectIndex() const { return this->iCurrentHitObjectIndex; }
    [[nodiscard]] inline i32 getNextHitObjectTime() const { return this->iNextHitObjectTime; }
    [[nodiscard]] inline i32 getPreviousHitObjectTime() const { return this->iPreviousHitObjectTime; }
    [[nodiscard]] inline i32 getPreviousFollowPointObjectIndex() const { return this->iPreviousFollowPointObjectIndex; }
    [[nodiscard]] inline bool isInBreak() const { return this->bInBreak; }
    [[nodiscard]] bool isPassing() const;  // section pass/fail state for storyboards
    [[nodiscard]] inline bool isInSkippableSection() const { return this->bIsInSkippableSection; }
    [[nodiscard]] inline bool isSpinnerActive() const { return this->bIsSpinnerActive; }
    [[nodiscard]] inline int getNPS() const { return this->iNPS; }
    [[nodiscard]] inline int getND() const { return this->iND; }
    [[nodiscard]] inline int getNumCirclesFinished() const { return this->iCurrentNumCircles; }
    [[nodiscard]] inline int getNumSlidersFinished() const { return this->iCurrentNumSliders; }
    [[nodiscard]] inline int getNumSpinnersFinished() const { return this->iCurrentNumSpinners; }

    [[nodiscard]] vec2 getCursorPos() const;
    [[nodiscard]] inline vec2 getAutoCursorPos() const { return this->vAutoCursorPos; }
    [[nodiscard]] inline u8 getKeys() const { return this->current_keys; }
    [[nodiscard]] bool isClickHeld() const;

    [[nodiscard]] vec2 osuCoords2Pixels(vec2 coords) const;
    [[nodiscard]] inline f32 getPlayfieldRotation() const { return this->fPlayfieldRotation; }

    // for the hitobjects to report what they judged
    LiveHitResult addHitResult(neomod::HitObject *hitObject, LiveHitResult hit, i32 delta, bool isEndOfCombo = false,
                               bool ignoreOnHitErrorBar = false, bool hitErrorBarOnly = false, bool ignoreCombo = false,
                               bool ignoreScore = false, bool ignoreHealth = false);
    void addSliderBreak();
    void addScorePoints(int points, bool isSpinner = false);

    // and what they play and show besides that (see Listener)
    void playHitSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 delta, i32 timeMS = -1);
    void playSliderTickSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos, i32 timeMS);
    std::vector<neomod::HitSoundUtils::Set_Slider_Hit> updateSliderSlideSounds(
        bool sliding, neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples, vec2 rawPos,
        const std::vector<neomod::HitSoundUtils::Set_Slider_Hit> &started);
    void stopSliderSounds(const std::vector<neomod::HitSoundUtils::Set_Slider_Hit> &started);
    void playSpinnerSpinSound(f32 ratio, neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples);
    void stopSpinnerSpinSound();
    void playSpinnerBonusSound(neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS samples);
    void addTargetHit(f32 delta, f32 angle);

    // for HitObject::update to avoid recalculating for each object every frame
    [[nodiscard]] forceinline f32 getCachedApproachTimeForUpdate() const { return this->fCachedApproachTimeForUpdate; }

    void updateHitobjectMetrics();
    void calculateStacks();
    void computeDrainRate();

    f64 fHpMultiplierComboEnd = 1.0;
    f64 fHpMultiplierNormal = 1.0;
    i32 iMaxPossibleCombo = 0;
    u32 iScoreV2ComboPortionMaximum = 0;
    u32 nb_hitobjects = 0;
    f32 fHitcircleDiameter = 0.f;
    f32 fRawHitcircleDiameter = 0.f;
    f32 fSliderFollowCircleDiameter = 0.f;
    u8 lastPressedKey = 0;
    bool holding_slider = false;

   private:
    void update(f64 frameTimeSecs);
    void updateAutoCursorPos();
    void addHealth(f64 percent, bool isFromHitResult);
    void resetScore();

    BeatmapDifficulty *beatmap;
    LiveScore &score;
    const PlayfieldView *view;
    Listener *listener;
    std::unique_ptr<PlayedMap> mapData;
    std::vector<Color> comboColors;
    u8 defaultSampleSet{1};

    // input
    i32 iCurMusicPos = 0;
    u8 current_keys = 0;
    vec2 vCursorPos{0.f};
    std::vector<Click> clicks;
    i32 current_frame_idx = 0;  // in replay
    bool bNeedsReplayFrame = false;

    // health
    bool bFailed = false;
    f64 fHealth = 1.0;
    f64 fDrainRate = 0.0;

    // breaks and sections
    std::vector<DBBreak> breaks;
    bool bInBreak = false;
    bool bLastComboPerfect = true;
    bool bBreakPassed = true;
    // breaks long enough to count toward passing at the end
    i32 iNumLongBreaksPassed = 0;
    i32 iNumLongBreaks = 0;
    bool bIsInSkippableSection = false;
    bool bIsSpinnerActive = false;
    neomod::HitObject *currentHitObject = nullptr;
    i32 iNextHitObjectTime = 0;
    i32 iPreviousHitObjectTime = 0;
    i32 iPreviousFollowPointObjectIndex = 0;  // TODO: this shouldn't be in this class

    // hitobjects
    std::vector<std::unique_ptr<neomod::HitObject>> hitobjects;
    std::vector<neomod::HitObject *> hitobjectsSortedByEndTime;  // non-owning view of hitobjects data
    std::vector<neomod::HitObject *> misaimObjects;

    // statistics
    i32 iNPS = 0;
    i32 iND = 0;
    i32 iCurrentHitObjectIndex = 0;
    i32 iCurrentNumCircles = 0;
    i32 iCurrentNumSliders = 0;
    i32 iCurrentNumSpinners = 0;

    // playfield
    f32 fPlayfieldRotation = 0.f;  // wobble
    f32 fCachedApproachTimeForUpdate = 0.f;

    // auto
    vec2 vAutoCursorPos{0.f};
    int iAutoCursorDanceIndex = 0;
};
