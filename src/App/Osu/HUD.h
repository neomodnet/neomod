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
    i32 currentCombo;
    i32 maxCombo;
    i32 misses;
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
    static void drawRuntimeInfo();

    void drawCursor(vec2 pos, f32 alphaMultiplier = 1.0f, bool secondTrail = false, bool updateAndDrawTrail = true);
    void drawCursorTrail(
        vec2 pos, f32 alphaMultiplier = 1.0f,
        bool secondTrail = false);  // NOTE: only use if drawCursor() with updateAndDrawTrail = false (FPoSu)
    void drawCursorRipples();
    void drawFps();
    void drawHitErrorBar(BeatmapInterface *pf);
    void drawPlayfieldBorder(vec2 playfieldCenter, vec2 playfieldSize, f32 hitcircleDiameter);
    void drawPlayfieldBorder(vec2 playfieldCenter, vec2 playfieldSize, f32 hitcircleDiameter, f32 borderSize);
    void drawLoadingSmall(const UString &text);

    struct SkinDigitDrawOpts {  // NOLINT
        u64 number;
        f32 scale{1.f};
        bool combo;        // true == skin combo digits, false == skin score digits
        u32 minDigits{0};  // left pad with N zeroes
    };
    static void drawNumberWithSkinDigits(const SkinDigitDrawOpts &opts);
    static void drawComboSimple(i32 combo, f32 scale = 1.0f);        // used by RankingScreen
    static void drawAccuracySimple(f32 accuracy, f32 scale = 1.0f);  // used by RankingScreen
    static void drawWarningArrow(vec2 pos, bool flipVertically, bool originLeft = true);

    [[nodiscard]] bool shouldDrawScoreboard() const;

    [[nodiscard]] inline WinCondition getScoringMetric() const { return this->scoring_metric; }
    void updateScoringMetric();

    void resetScoreboard();
    void updateScoreboard(bool animate);
    void drawFancyScoreboard();

    void drawScorebarBg(f32 alpha, f32 breakAnim);
    void drawSectionPass(f32 alpha);
    void drawSectionFail(f32 alpha);

    void animateCombo();
    void addHitError(i32 delta, bool miss = false, bool misaim = false);
    void addTarget(f32 delta, f32 angle);
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
    [[nodiscard]] inline f32 getScoreBarBreakAnim() const { return this->fScoreBarBreakAnim; }

    std::vector<std::unique_ptr<ScoreboardSlot>> slots;
    ScoreboardSlot *player_slot{nullptr};  // pointer to an entry inside "slots"

    MD5Hash beatmap_md5;

    static f32 getCursorScaleFactor();

   private:
    const std::vector<SCORE_ENTRY> &getCurrentScores();
    std::vector<SCORE_ENTRY> scores_cache;

    WinCondition scoring_metric{};

    struct CursorTrailElement {
        vec2 pos{0.f};
        f32 time;
        f32 alpha;
        f32 scale;
    };

    // ring buffer
    struct CursorTrail {
       private:
        std::vector<CursorTrailElement> buffer;
        uSz head{0};  // index of oldest element
        uSz tail{0};  // index where next element will be written
        uSz count{0};

       public:
        CursorTrail();

        [[nodiscard]] uSz size() const;
        [[nodiscard]] bool empty() const;
        [[nodiscard]] uSz capacity() const;

        void push_back(const CursorTrailElement &elem);
        void pop_front();
        CursorTrailElement &front();
        [[nodiscard]] const CursorTrailElement &front() const;

        CursorTrailElement &back();
        [[nodiscard]] const CursorTrailElement &back() const;

        CursorTrailElement &next();
        // index 0 = oldest (front), index size()-1 = newest (back)
        CursorTrailElement &operator[](uSz i);
        const CursorTrailElement &operator[](uSz i) const;

        void clear();
    };

    struct CursorRippleElement {
        vec2 pos{0.f};
        f32 time;
    };

    struct HITERROR {
        f32 time;
        i32 delta;
        bool miss;
        bool misaim;
    };

    struct TARGET {
        f32 time;
        f32 delta;
        f32 angle;
    };

    struct BREAK {
        f32 startPercent;
        f32 endPercent;
    };

    struct HUDStats {
        i32 misses, sliderbreaks;
        i32 maxPossibleCombo;
        f32 liveStars, totalStars;
        i32 bpm;

        f32 ar, cs, od, hp;
        i32 nps;
        i32 nd;
        i32 ur;
        f32 pp, ppfc;

        f32 hitWindow300;
        i32 hitdeltaMin, hitdeltaMax;
    };

    void onCursorTrailMaxChange();
    void addCursorTrailPosition(CursorTrail &trail, vec2 pos) const;
    void drawCursorTrailInt(Shader *trailShader, CursorTrail &trail, vec2 pos, f32 alphaMultiplier = 1.0f,
                            bool emptyTrailFrame = false);
    void drawCursorTrailRaw(f32 alpha, vec2 pos);
    void drawAccuracy(f32 accuracy);
    void drawCombo(i32 combo);
    static void drawScore(u64 score);
    void drawHPBar(double health, f32 alpha, f32 breakAnim);
    static void drawWarningArrows(f32 hitcircleDiameter = 0.0f);
    void drawHitErrorBar(f32 hitWindow300, f32 hitWindow100, f32 hitWindow50, f32 hitWindowMiss, i32 ur);
    void drawHitErrorBarInt(f32 hitWindow300, f32 hitWindow100, f32 hitWindow50, f32 hitWindowMiss);
    static void drawHitErrorBarInt2(vec2 center, i32 ur);
    void drawProgressBar(f32 percent, bool waiting);
    static void drawStatistics(const HUDStats &stats);
    void drawTargetHeatmap(f32 hitcircleDiameter);
    static void drawScrubbingTimeline(u32 beatmapTime, u32 beatmapLengthPlayable, u32 beatmapStartTimePlayable,
                                      f32 beatmapPercentFinishedPlayable, const std::vector<BREAK> &breaks);
    void drawInputOverlay(i32 numK1, i32 numK2, i32 numM1, i32 numM2);

    static bool shouldDrawRuntimeInfo();

    static f32 getCursorTrailScaleFactor();

    static f32 getScoreScale();

    McFont *tempFont;

    // shit code
    const f64 fScoreboardCacheRefreshTime{0.250f};  // only update every 250ms instead of every frame
    f64 fScoreboardLastUpdateTime{0.f};

    f32 fAccuracyXOffset;
    f32 fAccuracyYOffset;
    f32 fScoreHeight;

    f32 fComboAnim1;
    f32 fComboAnim2;

    // fps counter
    f32 fCurFps;
    f32 fCurFpsSmooth;
    f32 fFpsUpdate;

    // hit error bar
    std::vector<HITERROR> hiterrors;

    // inputoverlay / key overlay
    f32 fInputoverlayK1AnimScale;
    f32 fInputoverlayK2AnimScale;
    f32 fInputoverlayM1AnimScale;
    f32 fInputoverlayM2AnimScale;

    f32 fInputoverlayK1AnimColor;
    f32 fInputoverlayK2AnimColor;
    f32 fInputoverlayM1AnimColor;
    f32 fInputoverlayM2AnimColor;

    // cursor & trail & ripples
    f32 fCursorExpandAnim;
    CursorTrail cursorTrail;
    CursorTrail cursorTrail2;
    CursorTrail cursorTrailSpectator1;
    CursorTrail cursorTrailSpectator2;
    Shader *cursorTrailShader;
    std::unique_ptr<VertexArrayObject> cursorTrailVAO;
    std::vector<CursorRippleElement> cursorRipples;

    // target heatmap
    std::vector<TARGET> targets;

    // health
    double fHealth;
    f32 fScoreBarBreakAnim;
    f32 fKiScaleAnim;
};
