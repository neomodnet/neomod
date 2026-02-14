#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "UIScreen.h"
#include "MD5Hash.h"
#include "types.h"
#include "UString.h"

#include <memory>
#include <cassert>

class UIAvatar;
class ScoreboardSlot;
class McFont;
class ConVar;
class Image;
class BeatmapInterface;
class Shader;
class VertexArrayObject;

class CBaseUIContainer;
namespace LegacyReplay {
enum KeyFlags : uint8_t;
}
using GameplayKeys = LegacyReplay::KeyFlags;

enum class WinCondition : uint8_t;

struct SCORE_ENTRY {
    UString name;
    i32 entry_id = 0;
    i32 player_id = 0;

    f32 accuracy;
    f64 pp{0.f};
    u64 score;
    int currentCombo;
    int maxCombo;
    int misses;
    bool dead;
    bool highlight;
};

class HUD final : public UIScreen {
    NOCOPY_NOMOVE(HUD)
   public:
    HUD();
    ~HUD() override;

    void draw() override;
    void drawDummy();
    void drawRuntimeInfo();

    void drawCursor(vec2 pos, float alphaMultiplier = 1.0f, bool secondTrail = false, bool updateAndDrawTrail = true);
    void drawCursorTrail(
        vec2 pos, float alphaMultiplier = 1.0f,
        bool secondTrail = false);  // NOTE: only use if drawCursor() with updateAndDrawTrail = false (FPoSu)
    void drawCursorRipples();
    void drawFps();
    void drawHitErrorBar(BeatmapInterface *pf);
    void drawPlayfieldBorder(vec2 playfieldCenter, vec2 playfieldSize, float hitcircleDiameter);
    void drawPlayfieldBorder(vec2 playfieldCenter, vec2 playfieldSize, float hitcircleDiameter, float borderSize);
    void drawLoadingSmall(const UString &text);

    struct SkinDigitDrawOpts { // NOLINT
        u64 number;
        float scale{1.f};
        bool combo; // true == skin combo digits, false == skin score digits
        bool drawLeadingZeroes{false};
    };
    static void drawNumberWithSkinDigits(const SkinDigitDrawOpts &opts);
    static void drawComboSimple(int combo, float scale = 1.0f);          // used by RankingScreen
    static void drawAccuracySimple(float accuracy, float scale = 1.0f);  // used by RankingScreen
    static void drawWarningArrow(vec2 pos, bool flipVertically, bool originLeft = true);

    [[nodiscard]] bool shouldDrawScoreboard() const;

    [[nodiscard]] inline WinCondition getScoringMetric() const { return this->scoring_metric; }
    void updateScoringMetric();

    void resetScoreboard();
    void updateScoreboard(bool animate);
    void drawFancyScoreboard();

    void drawScorebarBg(float alpha, float breakAnim);
    void drawSectionPass(float alpha);
    void drawSectionFail(float alpha);

    void animateCombo();
    void addHitError(i32 delta, bool miss = false, bool misaim = false);
    void addTarget(float delta, float angle);
    void animateInputOverlay(GameplayKeys key_flag, bool down);

    void addCursorRipple(vec2 pos);
    void animateCursorExpand();
    void animateCursorShrink();
    void animateKiBulge();
    void animateKiExplode();

    void resetHitErrorBar();

    McRect getSkipClickRect();

    void drawSkip();

    // ILLEGAL:
    [[nodiscard]] inline float getScoreBarBreakAnim() const { return this->fScoreBarBreakAnim; }

    std::vector<std::unique_ptr<ScoreboardSlot>> slots;
    ScoreboardSlot *player_slot{nullptr};  // pointer to an entry inside "slots"

    MD5Hash beatmap_md5;

    static float getCursorScaleFactor();

   private:
    const std::vector<SCORE_ENTRY> &getCurrentScores();
    std::vector<SCORE_ENTRY> scores_cache;

    WinCondition scoring_metric{};

    struct CursorTrailElement {
        vec2 pos{0.f};
        float time;
        float alpha;
        float scale;
    };

    // ring buffer
    struct CursorTrail {
       private:
        std::vector<CursorTrailElement> buffer;
        size_t head{0};  // index of oldest element
        size_t tail{0};  // index where next element will be written
        size_t count{0};

       public:
        CursorTrail();

        [[nodiscard]] size_t size() const { return count; }
        [[nodiscard]] bool empty() const { return count == 0; }
        [[nodiscard]] size_t capacity() const { return buffer.size(); }

        void push_back(const CursorTrailElement &elem) {
            if(buffer.empty()) return;

            buffer[tail] = elem;
            tail = (tail + 1) % buffer.size();

            if(count < buffer.size()) {
                count++;
            } else {
                head = (head + 1) % buffer.size();
            }
        }

        void pop_front() {
            if(count > 0) {
                head = (head + 1) % buffer.size();
                count--;
            }
        }

        CursorTrailElement &front() { return buffer[head]; }
        [[nodiscard]] const CursorTrailElement &front() const { return buffer[head]; }

        CursorTrailElement &back() { return buffer[(tail + buffer.size() - 1) % buffer.size()]; }
        [[nodiscard]] const CursorTrailElement &back() const {
            return buffer[(tail + buffer.size() - 1) % buffer.size()];
        }

        CursorTrailElement &next() {
            assert(!buffer.empty());

            auto &ret = buffer[tail];
            tail = (tail + 1) % buffer.size();

            if(count < buffer.size()) {
                count++;
            } else {
                head = (head + 1) % buffer.size();
            }
            return ret;
        }

        // index 0 = oldest (front), index size()-1 = newest (back)
        CursorTrailElement &operator[](size_t i) { return buffer[(head + i) % buffer.size()]; }
        const CursorTrailElement &operator[](size_t i) const { return buffer[(head + i) % buffer.size()]; }

        void clear() { head = tail = count = 0; }
    };

    struct CursorRippleElement {
        vec2 pos{0.f};
        float time;
    };

    struct HITERROR {
        float time;
        i32 delta;
        bool miss;
        bool misaim;
    };

    struct TARGET {
        float time;
        float delta;
        float angle;
    };

    struct BREAK {
        float startPercent;
        float endPercent;
    };

    struct HUDStats {
        int misses, sliderbreaks;
        int maxPossibleCombo;
        float liveStars, totalStars;
        int bpm;

        float ar, cs, od, hp;
        int nps;
        int nd;
        int ur;
        float pp, ppfc;

        float hitWindow300;
        int hitdeltaMin, hitdeltaMax;
    };

    void onCursorTrailMaxChange();
    void addCursorTrailPosition(CursorTrail &trail, vec2 pos) const;
    void drawCursorTrailInt(Shader *trailShader, CursorTrail &trail, vec2 pos, float alphaMultiplier = 1.0f,
                            bool emptyTrailFrame = false);
    void drawCursorTrailRaw(float alpha, vec2 pos);
    void drawAccuracy(float accuracy);
    void drawCombo(int combo);
    static void drawScore(u64 score);
    void drawHPBar(double health, float alpha, float breakAnim);
    static void drawWarningArrows(float hitcircleDiameter = 0.0f);
    void drawHitErrorBar(float hitWindow300, float hitWindow100, float hitWindow50, float hitWindowMiss, int ur);
    void drawHitErrorBarInt(float hitWindow300, float hitWindow100, float hitWindow50, float hitWindowMiss);
    static void drawHitErrorBarInt2(vec2 center, int ur);
    void drawProgressBar(float percent, bool waiting);
    static void drawStatistics(const HUDStats &stats);
    void drawTargetHeatmap(float hitcircleDiameter);
    static void drawScrubbingTimeline(u32 beatmapTime, u32 beatmapLengthPlayable, u32 beatmapStartTimePlayable,
                               f32 beatmapPercentFinishedPlayable, const std::vector<BREAK> &breaks);
    void drawInputOverlay(int numK1, int numK2, int numM1, int numM2);

    [[nodiscard]] bool shouldDrawRuntimeInfo() const;

    static float getCursorTrailScaleFactor();

    static float getScoreScale();

    McFont *tempFont;

    // shit code
    const f64 fScoreboardCacheRefreshTime{0.250f};  // only update every 250ms instead of every frame
    f64 fScoreboardLastUpdateTime{0.f};

    float fAccuracyXOffset;
    float fAccuracyYOffset;
    float fScoreHeight;

    float fComboAnim1;
    float fComboAnim2;

    // fps counter
    float fCurFps;
    float fCurFpsSmooth;
    float fFpsUpdate;

    // hit error bar
    std::vector<HITERROR> hiterrors;

    // inputoverlay / key overlay
    float fInputoverlayK1AnimScale;
    float fInputoverlayK2AnimScale;
    float fInputoverlayM1AnimScale;
    float fInputoverlayM2AnimScale;

    float fInputoverlayK1AnimColor;
    float fInputoverlayK2AnimColor;
    float fInputoverlayM1AnimColor;
    float fInputoverlayM2AnimColor;

    // cursor & trail & ripples
    float fCursorExpandAnim;
    CursorTrail cursorTrail;
    CursorTrail cursorTrail2;
    CursorTrail cursorTrailSpectator1;
    CursorTrail cursorTrailSpectator2;
    Shader *cursorTrailShader;
    std::unique_ptr<VertexArrayObject> cursorTrailVAO;
    std::vector<CursorRippleElement> cursorRipples;

    // target heatmap
    std::vector<TARGET> targets;

    std::vector<UIAvatar *> avatars;

    // health
    double fHealth;
    float fScoreBarBreakAnim;
    float fKiScaleAnim;
};
