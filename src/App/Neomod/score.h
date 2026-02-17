#pragma once
#include "MD5Hash.h"
#include "Replay.h"

#include <vector>
#include <unordered_set>

class ConVar;
class DatabaseBeatmap;
class AbstractBeatmapInterface;
class HitObject;

namespace LegacyReplay {
struct Frame;
enum KeyFlags : uint8_t;
}  // namespace LegacyReplay
using GameplayKeys = LegacyReplay::KeyFlags;

enum class ScoreGrade : uint8_t {
    XH,
    SH,
    X,
    S,
    A,
    B,
    C,
    D,
    F,
    N  // means "no grade"
};

struct FinishedScore {
    [[nodiscard]] inline bool operator==(const FinishedScore &c) const {
        return unixTimestamp == c.unixTimestamp &&      //
               score == c.score &&                      //
               mods == c.mods &&                        //
               beatmap_hash == c.beatmap_hash &&        //
               bancho_score_id == c.bancho_score_id &&  //
               player_id == c.player_id &&              //
               num300s == c.num300s &&                  //
               num100s == c.num100s &&                  //
               num50s == c.num50s &&                    //
               numMisses == c.numMisses &&              //
               comboMax == c.comboMax &&                //
               playerName == c.playerName;              //
    }

    // Absolute hit deltas of every hitobject (0-254). 255 == miss
    // This is exclusive to PPV3-converted scores
    // NOTE: unfinished feature
    // std::vector<u8> hitdeltas;

#ifndef BUILD_TOOLS_ONLY

    std::vector<LegacyReplay::Frame> replay;  // not always loaded

#endif

    const DatabaseBeatmap *map{nullptr};  // NOTE: do NOT assume this is set

    MD5Hash beatmap_hash;
    Replay::Mods mods;

    u64 score = 0;
    u64 spinner_bonus = 0;
    u64 unixTimestamp = 0;
    u64 play_time_ms = 0;

    std::string playerName;

    std::string client;
    std::string server;

    // Only present in scores parsed from osu!.db, aka "peppy" replays
    // So it will always be 0 in mcosu/neomod scores, or in online scores
    u64 peppy_replay_tms = 0;

    i64 bancho_score_id = 0;

    i32 player_id = 0;

    int num300s = 0;
    int num100s = 0;
    int num50s = 0;
    int numGekis = 0;
    int numKatus = 0;
    int numMisses = 0;

    int comboMax = 0;

    u32 ppv2_version = 0;
    float ppv2_score = 0.f;
    float ppv2_total_stars = 0.f;
    float ppv2_aim_stars = 0.f;
    float ppv2_speed_stars = 0.f;

    int numSliderBreaks = 0;
    float unstableRate = 0.f;
    float hitErrorAvgMin = 0.f;
    float hitErrorAvgMax = 0.f;
    int maxPossibleCombo = -1;
    int numHitObjects = -1;
    int numCircles = -1;

    ScoreGrade grade = ScoreGrade::N;
    bool perfect : 1 = false;
    bool passed : 1 = false;
    bool ragequit : 1 = false;

    // Online scores are not saved to db
    bool is_online_score : 1 = false;
    bool is_online_replay_available : 1 = false;

    // NOTE: unfinished feature
    // float ppv3_score = 0.f;
    // std::string ppv3_algorithm;

    [[nodiscard]] std::string dbgstr() const;

    [[nodiscard]] inline bool is_peppy_imported() const { return this->server == "ppy.sh"; }
    [[nodiscard]] inline bool is_mcosu_imported() const { return this->client.starts_with("mcosu"); }

    f64 get_or_calc_pp();
    [[nodiscard]] f64 get_pp() const;
    [[nodiscard]] ScoreGrade calculate_grade() const;

    [[nodiscard]] bool has_possible_replay() const {
        if(this->is_online_score) {
            return this->is_online_replay_available;
        } else {
            // Assume we always have a replay, as long as it's not a McOsu-imported score
            return !this->is_mcosu_imported();
        }
    }
};

namespace std {
template <>
struct hash<FinishedScore> {
    using is_avalanching = void;
    u64 operator()(const FinishedScore &s) const noexcept {
        u64 h = hash<MD5Hash>{}(s.beatmap_hash);

        auto combine = [&h](u64 v) { h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2); };

        combine(s.unixTimestamp);
        combine(static_cast<u64>(s.mods.flags));
        combine(s.score);
        combine(static_cast<u64>(s.bancho_score_id));
        combine(hash<string_view>{}(s.playerName));

        return h;
    }
};
}  // namespace std

class LiveScore {
   public:
    enum class HIT : uint8_t {
        // score
        HIT_NULL,
        HIT_MISS,
        HIT_50,
        HIT_100,
        HIT_300,

        // only used for health + SS/PF mods
        HIT_MISS_SLIDERBREAK,
        HIT_MU,
        HIT_100K,
        HIT_300K,
        HIT_300G,
        HIT_SLIDER10,  // tick
        HIT_SLIDER30,  // repeat
        HIT_SPINNERSPIN,
        HIT_SPINNERBONUS
    };

    static float calculateAccuracy(int num300s, int num100s, int num50s, int numMisses);

   public:
    LiveScore(bool simulating = true);

    void reset();  // only Beatmap may call this function!

    // only Beatmap/SimulatedBeatmapInterface may call this function!
    void addHitResult(AbstractBeatmapInterface *beatmap, HitObject *hitObject, LiveScore::HIT hit, i32 delta,
                      bool ignoreOnHitErrorBar, bool hitErrorBarOnly, bool ignoreCombo, bool ignoreScore);

    void addHitResultComboEnd(LiveScore::HIT hit);
    void addSliderBreak();  // only Beatmap may call this function!
    void addPoints(int points, bool isSpinner);
    void setComboFull(int comboFull) { this->iComboFull = comboFull; }
    void setComboEndBitmask(int comboEndBitmask) { this->iComboEndBitmask = comboEndBitmask; }
    void setDead(bool dead);

    void addKeyCount(GameplayKeys key_flag);

    void setStarsTomTotal(float starsTomTotal) { this->fStarsTomTotal = starsTomTotal; }
    void setStarsTomAim(float starsTomAim) { this->fStarsTomAim = starsTomAim; }
    void setStarsTomSpeed(float starsTomSpeed) { this->fStarsTomSpeed = starsTomSpeed; }
    void setPPv2(float ppv2) { this->fPPv2 = ppv2; }
    void setIndex(int index) { this->iIndex = index; }

    void setNumEZRetries(int numEZRetries) { this->iNumEZRetries = numEZRetries; }

    [[nodiscard]] inline float getStarsTomTotal() const { return this->fStarsTomTotal; }
    [[nodiscard]] inline float getStarsTomAim() const { return this->fStarsTomAim; }
    [[nodiscard]] inline float getStarsTomSpeed() const { return this->fStarsTomSpeed; }
    [[nodiscard]] inline float getPPv2() const { return this->fPPv2; }
    [[nodiscard]] inline int getIndex() const { return this->iIndex; }

    [[nodiscard]] inline u64 getScore() const {
        return this->mods.has(ModFlags::ScoreV2) ? this->iScoreV2 : this->iScoreV1;
    }
    [[nodiscard]] inline ScoreGrade getGrade() const { return this->grade; }
    [[nodiscard]] inline int getCombo() const { return this->iCombo; }
    [[nodiscard]] inline int getComboMax() const { return this->iComboMax; }
    [[nodiscard]] inline int getComboFull() const { return this->iComboFull; }
    [[nodiscard]] inline int getComboEndBitmask() const { return this->iComboEndBitmask; }
    [[nodiscard]] inline float getAccuracy() const { return this->fAccuracy; }
    [[nodiscard]] inline float getUnstableRate() const { return this->fUnstableRate; }
    [[nodiscard]] inline float getHitErrorAvgMin() const { return this->fHitErrorAvgMin; }
    [[nodiscard]] inline float getHitErrorAvgMax() const { return this->fHitErrorAvgMax; }
    [[nodiscard]] inline float getHitErrorAvgCustomMin() const { return this->fHitErrorAvgCustomMin; }
    [[nodiscard]] inline float getHitErrorAvgCustomMax() const { return this->fHitErrorAvgCustomMax; }
    [[nodiscard]] inline int getNumMisses() const { return this->iNumMisses; }
    [[nodiscard]] inline int getNumSliderBreaks() const { return this->iNumSliderBreaks; }
    [[nodiscard]] inline int getNum50s() const { return this->iNum50s; }
    [[nodiscard]] inline int getNum100s() const { return this->iNum100s; }
    [[nodiscard]] inline int getNum100ks() const { return this->iNum100ks; }
    [[nodiscard]] inline int getNum300s() const { return this->iNum300s; }
    [[nodiscard]] inline int getNum300gs() const { return this->iNum300gs; }

    [[nodiscard]] inline int getNumEZRetries() const { return this->iNumEZRetries; }

    [[nodiscard]] inline bool isDead() const { return this->bDead; }
    [[nodiscard]] inline bool hasDied() const { return this->bDied; }

    [[nodiscard]] inline bool isUnranked() const { return this->bIsUnranked; }
    void setCheated() { this->bIsUnranked = true; }

    static double getHealthIncrease(AbstractBeatmapInterface *beatmap, LiveScore::HIT hit);
    static double getHealthIncrease(LiveScore::HIT hit, double HP = 5.0f, double hpMultiplierNormal = 1.0f,
                                    double hpMultiplierComboEnd = 1.0f, double hpBarMaximumForNormalization = 200.0f);

    [[nodiscard]] int getKeyCount(GameplayKeys key_flag) const;
    [[nodiscard]] LegacyFlags getModsLegacy() const;
    [[nodiscard]] UString getModsStringForRichPresence() const;
    Replay::Mods mods;
    bool simulating;

    [[nodiscard]] f64 getScoreMultiplier() const;
    void onScoreChange();

    std::vector<HIT> hitresults;
    std::vector<int> hitdeltas;

    ScoreGrade grade;

    float fStarsTomTotal;
    float fStarsTomAim;
    float fStarsTomSpeed;
    float fPPv2;
    int iIndex;

    u64 iScoreV1;
    u64 iScoreV2;
    u64 iScoreV2ComboPortion;
    u64 iBonusPoints;
    int iCombo;
    int iComboMax;
    int iComboFull;
    int iComboEndBitmask;
    float fAccuracy;
    float fHitErrorAvgMin;
    float fHitErrorAvgMax;
    float fHitErrorAvgCustomMin;
    float fHitErrorAvgCustomMax;
    float fUnstableRate;

    int iNumMisses;
    int iNumSliderBreaks;
    int iNum50s;
    int iNum100s;
    int iNum100ks;
    int iNum300s;
    int iNum300gs;

    bool bDead;
    bool bDied;

    int iNumK1;
    int iNumK2;
    int iNumM1;
    int iNumM2;

    // custom
    int iNumEZRetries;
    bool bIsUnranked;
};
