#pragma once
#include "AnimationHandler.h"
#include "BeatmapInterface.h"
#include <vector>

class ConVar;
class ModFPoSu;
class SkinImage;
class SliderCurve;
class VertexArrayObject;
class Image;

enum class HitObjectType : uint8_t {
    CIRCLE,
    SLIDER,
    SPINNER,
};

namespace PpyHitObjectType {
enum {
    CIRCLE = (1 << 0),
    SLIDER = (1 << 1),
    NEW_COMBO = (1 << 2),
    SPINNER = (1 << 3),
    // 4, 5, 6: 3-bit integer specifying how many combo colors to skip (if NEW_COMBO is set)
    MANIA_HOLD_NOTE = (1 << 7),
};
}

class HitObject {
   public:
    // TEMP constructor helpers (DatabaseBeatmap::loadGameplay)
    void setIsEndOfCombo(bool end) { m_endOfCombo = end; }
    void setComboNumber(i32 comboNumber) { m_comboNumber = comboNumber; }

   public:
    static void drawHitResult(BeatmapInterface *pf, vec2 rawPos, LiveScore::HIT result, float animPercentInv,
                              float hitDeltaRangePercent);
    static void drawHitResult(const Skin *skin, float hitcircleDiameter, float rawHitcircleDiameter, vec2 rawPos,
                              LiveScore::HIT result, float animPercentInv, float hitDeltaRangePercent);

   protected:  // only constructable through subclasses
    HitObject(i32 timeMS, HitSamples samples, int comboNumber, bool isEndOfCombo, int colorCounter, int colorOffset,
              AbstractBeatmapInterface *beatmap);

   public:
    HitObject() = delete;
    virtual ~HitObject() = default;

    HitObject(const HitObject &) = delete;
    HitObject &operator=(const HitObject &) = delete;
    HitObject(HitObject &&) noexcept = default;
    HitObject &operator=(HitObject &&) noexcept = default;

    virtual void draw() {}
    virtual void draw2();
    virtual void update(i32 curPosMS, f64 frameTimeMS);

    virtual void updateStackPosition(float /*stackOffset*/) {}  // unused by spinners
    virtual void miss(i32 /*curPos*/) {}                        // only used by notelock

    // [[nodiscard]] virtual constexpr forceinline int getCombo() const {
    //     return 1;
    // }  // how much combo this hitobject is "worth"

    [[nodiscard]] forceinline HitObjectType getType() const { return m_type; }

    // Gameplay logic
    [[nodiscard]] forceinline i32 getEndTime() const { return m_clickTimeMS + m_durationMS; }
    [[nodiscard]] forceinline i32 getClickTime() const { return m_clickTimeMS; }
    [[nodiscard]] forceinline i32 getDuration() const { return m_durationMS; }

    [[nodiscard]] forceinline bool isEndOfCombo() const { return m_endOfCombo; }

    // Visual
    [[nodiscard]] forceinline i32 getComboNumber() const { return m_comboNumber; }

    void addHitResult(LiveScore::HIT result, i32 delta, bool isEndOfCombo, vec2 posRaw, float targetDeltaPct = 0.0f,
                      float targetAngle = 0.0f, bool ignoreOnHitErrorBar = false, bool ignoreCombo = false,
                      bool ignoreHealth = false, bool addObjectDurationToSkinAnimationTimeStartOffset = true);
    void misAimed() { m_misAim = true; }

    void setStack(int stack) { m_stackNum = stack; }
    void setForceDrawApproachCircle(bool firstNote) { m_overrideHDApproachCircle = firstNote; }
    void setAutopilotDelta(i32 deltaMS) { m_autopilotDeltaMS = deltaMS; }
    void setBlocked(bool blocked) { m_blocked = blocked; }

    [[nodiscard]] virtual vec2 getRawPosAt(i32 posMS) const = 0;          // with stack calculation modifications
    [[nodiscard]] virtual vec2 getOriginalRawPosAt(i32 posMS) const = 0;  // without stack calculations
    [[nodiscard]] virtual vec2 getAutoCursorPos(i32 curPosMS) const = 0;

    [[nodiscard]] inline int getStack() const { return m_stackNum; }
    [[nodiscard]] inline int getColorCounter() const { return m_colorCounter; }
    [[nodiscard]] inline int getColorOffset() const { return m_colorOffset; }
    [[nodiscard]] inline float getApproachScale() const { return m_approachScale; }
    [[nodiscard]] inline i32 getDelta() const { return m_deltaMS; }
    [[nodiscard]] inline i32 getApproachTime() const { return m_approachTimeMS; }
    [[nodiscard]] inline i32 getAutopilotDelta() const { return m_autopilotDeltaMS; }

    [[nodiscard]] inline bool isVisible() const { return m_visible; }
    [[nodiscard]] inline bool isFinished() const { return m_finished; }
    [[nodiscard]] inline bool isBlocked() const { return m_blocked; }
    [[nodiscard]] inline bool hasMisAimed() const { return m_misAim; }

    virtual void onClickEvent(std::vector<Click> & /*clicks*/) { ; }
    virtual void onReset(i32 curPosMS);

   private:
    static float lerp3f(float a, float b, float c, float percent);

    struct HITRESULTANIM {
        vec2 rawPos{0.f};
        i32 deltaMS{0};
        float timeSecs{-9999.0f};
        LiveScore::HIT result{LiveScore::HIT::HIT_NULL};
        bool addObjectDurationToSkinAnimationTimeStartOffset{false};
    };

    void drawHitResultAnim(const HITRESULTANIM &hitresultanim);

    HITRESULTANIM m_hitresultanim1;
    HITRESULTANIM m_hitresultanim2;

   protected:
    AbstractBeatmapInterface *m_pi;
    BeatmapInterface *m_pf;  // NULL when simulating

    i32 m_clickTimeMS;
    i32 m_durationMS{0};

    i32 m_comboNumber;

    i32 m_deltaMS{0};  // this must be signed
    i32 m_approachTimeMS{0};
    i32 m_fadeInTimeMS{0};  // extra time added before the approachTime to let the object smoothly become visible
    i32 m_autopilotDeltaMS{0};

    HitSamples m_hitSamples;
    int m_colorCounter;
    int m_colorOffset;

    int m_stackNum{0};

    float m_alpha{0.f};
    float m_alphaWithoutHidden{0.f};
    float m_alphaForApproachCircle{0.f};
    float m_approachScale{0.f};
    float m_hittableDimRGBColorMultiplierPct{1.f};

    bool m_blocked{false};
    bool m_overrideHDApproachCircle{false};
    bool m_misAim{false};
    bool m_useFadeInTimeAsApproachTime{false};
    bool m_visible{false};
    bool m_finished{false};

    bool m_endOfCombo;
    HitObjectType m_type{HitObjectType::CIRCLE};
};

class Circle final : public HitObject {
   public:
    // main
    static void drawApproachCircle(BeatmapInterface *pf, vec2 rawPos, int number, int colorCounter, int colorOffset,
                                   float colorRGBMultiplier, float approachScale, float alpha,
                                   bool overrideHDApproachCircle = false);
    static void drawCircle(BeatmapInterface *pf, vec2 rawPos, int number, int colorCounter, int colorOffset,
                           float colorRGBMultiplier, float approachScale, float alpha, float numberAlpha,
                           bool drawNumber = true, bool overrideHDApproachCircle = false);
    static void drawCircle(const Skin *skin, vec2 pos, float hitcircleDiameter, float numberScale, float overlapScale,
                           int number, int colorCounter, int colorOffset, float colorRGBMultiplier, float approachScale,
                           float alpha, float numberAlpha, bool drawNumber = true,
                           bool overrideHDApproachCircle = false);
    static void drawCircle(const Skin *skin, vec2 pos, float hitcircleDiameter, Color color, float alpha = 1.0f);
    static void drawSliderStartCircle(BeatmapInterface *pf, vec2 rawPos, int number, int colorCounter, int colorOffset,
                                      float colorRGBMultiplier, float approachScale, float alpha, float numberAlpha,
                                      bool drawNumber = true, bool overrideHDApproachCircle = false);
    static void drawSliderStartCircle(const Skin *skin, vec2 pos, float hitcircleDiameter, float numberScale,
                                      float hitcircleOverlapScale, int number, int colorCounter = 0,
                                      int colorOffset = 0, float colorRGBMultiplier = 1.0f, float approachScale = 1.0f,
                                      float alpha = 1.0f, float numberAlpha = 1.0f, bool drawNumber = true,
                                      bool overrideHDApproachCircle = false);
    static void drawSliderEndCircle(BeatmapInterface *pf, vec2 rawPos, int number, int colorCounter, int colorOffset,
                                    float colorRGBMultiplier, float approachScale, float alpha, float numberAlpha,
                                    bool drawNumber = true, bool overrideHDApproachCircle = false);
    static void drawSliderEndCircle(const Skin *skin, vec2 pos, float hitcircleDiameter, float numberScale,
                                    float overlapScale, int number = 0, int colorCounter = 0, int colorOffset = 0,
                                    float colorRGBMultiplier = 1.0f, float approachScale = 1.0f, float alpha = 1.0f,
                                    float numberAlpha = 1.0f, bool drawNumber = true,
                                    bool overrideHDApproachCircle = false);

    // split helper functions
    static void drawApproachCircle(const Skin *skin, vec2 pos, Color comboColor, float hitcircleDiameter,
                                   float approachScale, float alpha, bool modHD, bool overrideHDApproachCircle);
    static void drawHitCircleOverlay(const SkinImage &hitCircleOverlayImage, vec2 pos, float circleOverlayImageScale,
                                     float alpha, float colorRGBMultiplier);
    static void drawHitCircle(Image *hitCircleImage, vec2 pos, Color comboColor, float circleImageScale, float alpha);
    static void drawHitCircleNumber(const Skin *skin, float numberScale, float overlapScale, vec2 pos, int number,
                                    float numberAlpha, float colorRGBMultiplier);

   public:
    Circle() = delete;
    ~Circle() override;

    Circle(int x, int y, i32 timeMS, HitSamples samples, int comboNumber, bool isEndOfCombo, int colorCounter,
           int colorOffset, AbstractBeatmapInterface *beatmap);

    Circle(const Circle &) = delete;
    Circle &operator=(const Circle &) = delete;
    Circle(Circle &&) noexcept = default;
    Circle &operator=(Circle &&) noexcept = default;

    void draw() override;
    void draw2() override;
    void update(i32 curPosMS, f64 frameTimeMS) override;

    void updateStackPosition(float stackOffset) override;
    void miss(i32 curPosMS) override;

    [[nodiscard]] vec2 getRawPosAt(i32 /*pos*/) const override { return m_rawPos; }
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 /*pos*/) const override { return m_originalRawPos; }
    [[nodiscard]] vec2 getAutoCursorPos(i32 curPosMS) const override;

    void onClickEvent(std::vector<Click> &clicks) override;
    void onReset(i32 curPosMS) override;

   private:
    // necessary due to the static draw functions
    static int rainbowNumber;
    static int rainbowColorCounter;

    void onHit(LiveScore::HIT result, i32 hitDeltaMS, float targetDeltaPct = 0.0f, float targetAngle = 0.0f);

    bool m_waiting{false};

    vec2 m_rawPos;
    vec2 m_originalRawPos;  // for live mod changing

    AnimFloat m_hitAnimation;
    float m_shakeAnimation{0.f};
};

class Slider final : public HitObject {
   public:
    struct SLIDERCLICK {
        i32 timeMS;
        int type;
        int tickIndex;
        bool finished;
        bool successful;
        bool sliderend;
    };

   public:
    using SLIDERCURVETYPE = char;
    Slider() = delete;
    ~Slider() override;

    Slider(SLIDERCURVETYPE stype, int repeat, float pixelLength, std::vector<vec2> points,
           const std::vector<float> &ticks, float sliderTimeMS, float sliderTimeMSWithoutRepeats, i32 timeMS,
           HitSamples hoverSamples, std::vector<HitSamples> edgeSamples, int comboNumber, bool isEndOfCombo,
           int colorCounter, int colorOffset, AbstractBeatmapInterface *beatmap);

    Slider(const Slider &) = delete;
    Slider &operator=(const Slider &) = delete;
    Slider(Slider &&) noexcept = default;
    Slider &operator=(Slider &&) noexcept = default;

    void draw() override;
    inline void draw2() override { draw2(true, false); }
    void draw2(bool drawApproachCircle, bool drawOnlyApproachCircle);
    void update(i32 curPosMS, f64 frameTimeSecs) override;

    void updateStackPosition(float stackOffset) override;
    void miss(i32 curPosMS) override;
    // [[nodiscard]] constexpr forceinline int getCombo() const override {
    //     return 2 + std::max((iRepeat - 1), 0) + (std::max((iRepeat - 1), 0) + 1) * ticks.size();
    // }

    [[nodiscard]] vec2 getRawPosAt(i32 posMS) const override;
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 posMS) const override;
    [[nodiscard]] inline vec2 getAutoCursorPos(i32 /*curPos*/) const override { return m_curPoint; }

    void onClickEvent(std::vector<Click> &clicks) override;
    void onReset(i32 curPosMS) override;

    void rebuildVertexBuffer(bool useRawCoords = false);

    [[nodiscard]] inline bool isStartCircleFinished() const { return m_startFinished; }
    [[nodiscard]] inline int getRepeat() const { return m_repeat; }
    [[nodiscard]] inline std::vector<vec2> getRawPoints() const { return m_points; }
    [[nodiscard]] inline float getPixelLength() const { return m_pixelLength; }
    [[nodiscard]] inline const std::vector<SLIDERCLICK> &getClicks() const { return m_clicks; }

   private:
    void drawStartCircle(float alpha);
    void drawEndCircle(float alpha, float sliderSnake);
    void drawBody(float alpha, float from, float to);

    void updateAnimations(i32 curPosMS);

    void onHit(LiveScore::HIT result, i32 hitDeltaMS, bool isEndCircle, float targetDelta = 0.0f,
               float targetAngle = 0.0f, bool isEndResultFromStrictTrackingMod = false);
    void onRepeatHit(const SLIDERCLICK &click);
    void onTickHit(const SLIDERCLICK &click);
    void onSliderBreak();

    [[nodiscard]] float getT(i32 posMS, bool raw) const;

    bool isClickHeldSlider();  // special logic to disallow hold tapping
    struct SLIDERTICK {
        float percent;
        bool finished;
    };

    struct HitAnim {
        AnimFloat percent;
        // HEAD_TAIL is to emulate how pre-2015 osu! sliders were rendered,
        // when the tail animation happens, the head also gets an additional hit animation, but without drawing the numbers again
        // (TODO: shelved the idea for now)
        enum : u8 { HEAD = (1 << 0), TAIL = (1 << 1), HEAD_TAIL = HEAD | TAIL | (1 << 2) } type;
        [[nodiscard]] bool isAnimating() const { return (percent > 0.f && percent != 1.f); };
    };
    HitAnim &addHitAnim(u8 typeFlags, float duration);


    std::vector<HitAnim> m_clickAnimations;
    std::vector<vec2> m_points;
    std::vector<HitSamples> m_edgeSamples;
    std::vector<HitSamples::Set_Slider_Hit> m_lastSliderSampleSets{};

    std::vector<SLIDERTICK> m_ticks;  // ticks (drawing)

    // TEMP: auto cursordance
    std::vector<SLIDERCLICK> m_clicks;  // repeats (type 0) + ticks (type 1)

    std::unique_ptr<SliderCurve> m_curve;
    std::unique_ptr<VertexArrayObject> m_vao{nullptr};

    vec2 m_curPoint{0.f};
    vec2 m_curPointRaw{0.f};

    i32 m_strictTrackingModLastClickHeldTime{0};

    float m_pixelLength;

    float m_sliderTimeMS;
    float m_sliderTimeMSWithoutRepeats;

    float m_slidePct{0.f};  // 0.0f - 1.0f - 0.0f - 1.0f - etc.
    float m_sliderSnakePercent{0.f};
    float m_reverseArrowAlpha{0.f};
    float m_bodyAlpha{0.f};

    AnimFloat m_endSliderBodyFadeAnimation;

    AnimFloat m_followCircleTickAnimationScale;
    float m_followCircleAnimationScale{0.f};
    float m_followCircleAnimationAlpha{0.f};

    int m_repeat;
    int m_ignoredKeys{0};
    int m_reverseArrowPos{0};
    int m_curRepeat{0};
    int m_curRepeatCounterForHitSounds{0};

    SLIDERCURVETYPE m_curveType;

    LiveScore::HIT m_startResult{LiveScore::HIT::HIT_NULL};
    LiveScore::HIT m_endResult{LiveScore::HIT::HIT_NULL};

    bool m_startFinished{false};
    bool m_endFinished{false};
    bool m_cursorLeft{true};
    bool m_cursorInside{false};
    bool m_heldTillEnd{false};
    bool m_heldTillEndForLenienceHack{false};
    bool m_heldTillEndForLenienceHackCheck{false};
    bool m_inReverse{false};
    bool m_hideNumberAfterFirstRepeatHit{false};
};

class Spinner final : public HitObject {
   public:
    Spinner() = delete;
    Spinner(int x, int y, i32 timeMS, HitSamples samples, bool isEndOfCombo, i32 endTimeMS,
            AbstractBeatmapInterface *beatmap);
    ~Spinner() override;

    Spinner(const Spinner &) = delete;
    Spinner &operator=(const Spinner &) = delete;
    Spinner(Spinner &&) noexcept = default;
    Spinner &operator=(Spinner &&) noexcept = default;

    void draw() override;
    void update(i32 curPosMS, f64 frameTimeSecs) override;

    [[nodiscard]] vec2 getRawPosAt(i32 /*pos*/) const override { return m_rawPos; }
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 /*pos*/) const override { return m_originalRawPos; }
    [[nodiscard]] vec2 getAutoCursorPos(i32 curPosMS) const override;

    void onReset(i32 curPosMS) override;

   private:
    void onHit();
    void rotate(float rad);

    vec2 m_rawPos;
    vec2 m_originalRawPos;

    std::unique_ptr<float[]> m_storedDeltaAngles;

    // bool bClickedOnce;
    float m_percent{0.f};

    float m_drawRot{0.f};
    float m_rotations{0.f};
    float m_rotationsNeeded{-1.f};
    float m_deltaOverflowMS{0.f};
    float m_sumDeltaAngle{0.f};

    int m_maxStoredDeltaAngles;
    int m_deltaAngleIndex{0};
    float m_deltaAngleOverflow{0.f};

    float m_RPM{0.f};

    float m_lastMouseAngle{0.f};
    float m_ratio{0.f};
};
