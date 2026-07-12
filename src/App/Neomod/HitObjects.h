#pragma once
#include "types.h"
#include "noinclude.h"

#include "AnimationHandler.h"
#include "Vectors.h"
#include "Color.h"
#include "DatabaseBeatmapTypes.h"
#include "SliderCurves.h"

#include <vector>
#include <memory>

class ConVar;
class ModFPoSu;
class SkinImage;
class VertexArrayObject;
class Image;
class AbstractBeatmapInterface;
class BeatmapInterface;

struct Click;
struct Skin;

enum class LiveHitResult : uint8_t;

enum class HitObjectType : uint8_t {
    CIRCLE,
    SLIDER,
    SPINNER,
};

namespace neomod::HitSoundUtils {
struct Set_Slider_Hit;
}

class HitObject {
   public:
    using Set_Slider_Hit = neomod::HitSoundUtils::Set_Slider_Hit;
    using DBHitSample = neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS;
    using SliderCurve = neomod::SliderCurve;

    // TEMP constructor helpers (DatabaseBeatmap::loadGameplay)
    void setIsEndOfCombo(bool end) { m_endOfCombo = end; }
    void setComboStartTime(i32 tms) { m_comboStartMS = tms; }
    void setComboNumber(i32 comboNumber) { m_comboNumber = comboNumber; }

   public:
    static void drawHitResult(BeatmapInterface *pf, vec2 rawPos, LiveHitResult result, f32 animPercentInv,
                              f32 hitDeltaRangePercent);
    static void drawHitResult(const Skin *skin, f32 hitcircleDiameter, f32 rawHitcircleDiameter, vec2 rawPos,
                              LiveHitResult result, f32 animPercentInv, f32 hitDeltaRangePercent);

   protected:  // only constructable through subclasses
    HitObject(i32 timeMS, DBHitSample samples, i32 comboNumber, bool isEndOfCombo, i32 colorCounter, i32 colorOffset,
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

    virtual void updateStackPosition(f32 /*stackOffset*/) {}  // unused by spinners
    virtual void miss(i32 /*curPos*/) {}                      // only used by notelock
    [[nodiscard]] virtual bool isClickableFrom(i32 /*music_pos*/, vec2 /*cursor_pos*/) const { return false; }

    // [[nodiscard]] virtual constexpr forceinline i32 getCombo() const {
    //     return 1;
    // }  // how much combo this hitobject is "worth"

    [[nodiscard]] forceinline HitObjectType getType() const { return m_type; }
    [[nodiscard]] forceinline bool isCircle() const { return m_type == HitObjectType::CIRCLE; }
    [[nodiscard]] forceinline bool isSlider() const { return m_type == HitObjectType::SLIDER; }
    [[nodiscard]] forceinline bool isSpinner() const { return m_type == HitObjectType::SPINNER; }

    // Gameplay logic
    [[nodiscard]] forceinline i32 getEndTime() const { return m_clickTimeMS + m_durationMS; }
    [[nodiscard]] forceinline i32 getClickTime() const { return m_clickTimeMS; }
    [[nodiscard]] forceinline i32 getDuration() const { return m_durationMS; }

    [[nodiscard]] forceinline bool isEndOfCombo() const { return m_endOfCombo; }

    // Visual
    [[nodiscard]] forceinline i32 getComboStartTime() const { return m_comboStartMS; }
    [[nodiscard]] forceinline i32 getComboNumber() const { return m_comboNumber; }

    void addHitResult(LiveHitResult result, i32 delta, bool isEndOfCombo, vec2 posRaw, f32 targetDeltaPct = 0.0f,
                      f32 targetAngle = 0.0f, bool ignoreOnHitErrorBar = false, bool ignoreCombo = false,
                      bool ignoreHealth = false, bool addObjectDurationToSkinAnimationTimeStartOffset = true);
    void misAimed() { m_misAim = true; }

    void setStack(i32 stack) { m_stackNum = stack; }
    void setForceDrawApproachCircle(bool firstNote) { m_overrideHDApproachCircle = firstNote; }
    void setAutopilotDelta(i32 deltaMS) { m_autopilotDeltaMS = deltaMS; }
    void setBlocked(bool blocked) { m_blocked = blocked; }

    [[nodiscard]] virtual vec2 getRawPosAt(i32 posMS) const = 0;          // with stack calculation modifications
    [[nodiscard]] virtual vec2 getOriginalRawPosAt(i32 posMS) const = 0;  // without stack calculations
    [[nodiscard]] virtual vec2 getAutoCursorPos(i32 curPosMS) const = 0;

    [[nodiscard]] inline i32 getStack() const { return m_stackNum; }
    [[nodiscard]] inline i32 getColorCounter() const { return m_colorCounter; }
    [[nodiscard]] inline i32 getColorOffset() const { return m_colorOffset; }
    [[nodiscard]] inline f32 getApproachScale() const { return m_approachScale; }
    [[nodiscard]] inline i32 getDelta() const { return m_deltaMS; }
    [[nodiscard]] inline i32 getApproachTime() const { return m_approachTimeMS; }
    [[nodiscard]] inline i32 getAutopilotDelta() const { return m_autopilotDeltaMS; }

    [[nodiscard]] inline bool isVisible() const { return m_visible; }
    [[nodiscard]] inline bool isFinished() const { return m_finished; }
    [[nodiscard]] inline bool isBlocked() const { return m_blocked; }
    [[nodiscard]] inline bool hasMisAimed() const { return m_misAim; }

    virtual void onClickEvent(std::vector<Click> & /*clicks*/) { ; }
    virtual void onReset(i32 curPosMS);

    // only for sliders
    virtual void rebuildVertexBuffer(bool /*useRawCoords*/ = false) { ; }

   private:
    static f32 lerp3f(f32 a, f32 b, f32 c, f32 percent);

    struct HITRESULTANIM {
        vec2 rawPos{0.f};
        i32 deltaMS{0};
        f32 timeSecs{-9999.0f};
        LiveHitResult result{0 /* LiveHitResult::HIT_NULL*/};
        bool addObjectDurationToSkinAnimationTimeStartOffset{false};
    };

    void drawHitResultAnim(const HITRESULTANIM &hitresultanim);

    HITRESULTANIM m_hitresultanim1;
    HITRESULTANIM m_hitresultanim2;

   protected:
    AbstractBeatmapInterface *m_pi;
    BeatmapInterface *m_pf;  // NULL when simulating

    i32 m_comboStartMS{0};  // for freeze time mod
    i32 m_clickTimeMS;
    i32 m_durationMS{0};

    i32 m_comboNumber;

    i32 m_deltaMS{0};  // this must be signed
    i32 m_approachTimeMS{0};
    i32 m_fadeInTimeMS{0};  // extra time added before the approachTime to let the object smoothly become visible
    i32 m_autopilotDeltaMS{0};

    DBHitSample m_hitSamples;
    i32 m_colorCounter;
    i32 m_colorOffset;

    i32 m_stackNum{0};

    f32 m_alpha{0.f};
    f32 m_alphaWithoutHidden{0.f};
    f32 m_alphaForApproachCircle{0.f};
    f32 m_approachScale{0.f};
    f32 m_hittableDimRGBColorMultiplierPct{1.f};

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
    static void drawApproachCircle(BeatmapInterface *pf, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                                   f32 colorRGBMultiplier, f32 approachScale, f32 alpha,
                                   bool overrideHDApproachCircle = false);
    static void drawCircle(BeatmapInterface *pf, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                           f32 colorRGBMultiplier, f32 approachScale, f32 alpha, f32 numberAlpha,
                           bool drawNumber = true, bool overrideHDApproachCircle = false);
    static void drawCircle(const Skin *skin, vec2 pos, f32 hitcircleDiameter, f32 numberScale, f32 overlapScale,
                           i32 number, i32 colorCounter, i32 colorOffset, f32 colorRGBMultiplier, f32 approachScale,
                           f32 alpha, f32 numberAlpha, bool drawNumber = true, bool overrideHDApproachCircle = false);
    static void drawCircle(const Skin *skin, vec2 pos, f32 hitcircleDiameter, Color color, f32 alpha = 1.0f);
    static void drawSliderStartCircle(BeatmapInterface *pf, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                                      f32 colorRGBMultiplier, f32 approachScale, f32 alpha, f32 numberAlpha,
                                      bool drawNumber = true, bool overrideHDApproachCircle = false);
    static void drawSliderStartCircle(const Skin *skin, vec2 pos, f32 hitcircleDiameter, f32 numberScale,
                                      f32 hitcircleOverlapScale, i32 number, i32 colorCounter = 0, i32 colorOffset = 0,
                                      f32 colorRGBMultiplier = 1.0f, f32 approachScale = 1.0f, f32 alpha = 1.0f,
                                      f32 numberAlpha = 1.0f, bool drawNumber = true,
                                      bool overrideHDApproachCircle = false);
    static void drawSliderEndCircle(BeatmapInterface *pf, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                                    f32 colorRGBMultiplier, f32 approachScale, f32 alpha, f32 numberAlpha,
                                    bool drawNumber = true, bool overrideHDApproachCircle = false);
    static void drawSliderEndCircle(const Skin *skin, vec2 pos, f32 hitcircleDiameter, f32 numberScale,
                                    f32 overlapScale, i32 number = 0, i32 colorCounter = 0, i32 colorOffset = 0,
                                    f32 colorRGBMultiplier = 1.0f, f32 approachScale = 1.0f, f32 alpha = 1.0f,
                                    f32 numberAlpha = 1.0f, bool drawNumber = true,
                                    bool overrideHDApproachCircle = false);

    // split helper functions
    static void drawApproachCircle(const Skin *skin, vec2 pos, Color comboColor, f32 hitcircleDiameter,
                                   f32 approachScale, f32 alpha, bool modHD, bool overrideHDApproachCircle);
    static void drawHitCircleOverlay(const SkinImage &hitCircleOverlayImage, vec2 pos, f32 circleOverlayImageScale,
                                     f32 alpha, f32 colorRGBMultiplier);
    static void drawHitCircle(Image *hitCircleImage, vec2 pos, Color comboColor, f32 circleImageScale, f32 alpha);
    static void drawHitCircleNumber(const Skin *skin, f32 numberScale, f32 overlapScale, vec2 pos, i32 number,
                                    f32 numberAlpha, f32 colorRGBMultiplier);

   public:
    Circle() = delete;
    ~Circle() override;

    Circle(vec2 pos, i32 timeMS, DBHitSample samples, i32 comboNumber, bool isEndOfCombo, i32 colorCounter,
           i32 colorOffset, AbstractBeatmapInterface *beatmap);

    Circle(const Circle &) = delete;
    Circle &operator=(const Circle &) = delete;
    Circle(Circle &&) noexcept = default;
    Circle &operator=(Circle &&) noexcept = default;

    void draw() override;
    void draw2() override;
    void update(i32 curPosMS, f64 frameTimeMS) override;

    void updateStackPosition(f32 stackOffset) override;
    void miss(i32 curPosMS) override;
    [[nodiscard]] bool isClickableFrom(i32 music_pos, vec2 cursor_pos) const override;

    [[nodiscard]] vec2 getRawPosAt(i32 /*pos*/) const override { return m_rawPos; }
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 /*pos*/) const override { return m_originalRawPos; }
    [[nodiscard]] vec2 getAutoCursorPos(i32 curPosMS) const override;

    void onClickEvent(std::vector<Click> &clicks) override;
    void onReset(i32 curPosMS) override;

   private:
    // necessary due to the static draw functions
    static i32 rainbowNumber;
    static i32 rainbowColorCounter;

    void onHit(LiveHitResult result, i32 hitDeltaMS, f32 targetDeltaPct = 0.0f, f32 targetAngle = 0.0f);

    bool m_waiting{false};

    vec2 m_rawPos;
    vec2 m_originalRawPos;  // for live mod changing

    AnimFloat m_hitAnimation;
    f32 m_shakeAnimation{0.f};
};

class Slider final : public HitObject {
   public:
    struct SLIDERCLICK {
        i32 timeMS;
        i32 type;
        i32 tickIndex;
        bool finished;
        bool successful;
        bool sliderend;
    };

   public:
    Slider() = delete;
    ~Slider() override;

    Slider(neomod::SLIDERCURVETYPE stype, i32 repeat, f32 pixelLength, std::vector<vec2> points,
           const std::vector<f32> &ticks, f32 sliderTimeMS, f32 sliderTimeMSWithoutRepeats, i32 timeMS,
           DBHitSample hoverSamples, std::vector<DBHitSample> edgeSamples, i32 comboNumber, bool isEndOfCombo,
           i32 colorCounter, i32 colorOffset, AbstractBeatmapInterface *beatmap);

    Slider(const Slider &) = delete;
    Slider &operator=(const Slider &) = delete;
    Slider(Slider &&) noexcept = default;
    Slider &operator=(Slider &&) noexcept = default;

    void draw() override;
    inline void draw2() override { draw2(true, false); }
    void draw2(bool drawApproachCircle, bool drawOnlyApproachCircle);
    void update(i32 curPosMS, f64 frameTimeSecs) override;

    void updateStackPosition(f32 stackOffset) override;
    void miss(i32 curPosMS) override;
    [[nodiscard]] bool isClickableFrom(i32 music_pos, vec2 cursor_pos) const override;
    // [[nodiscard]] constexpr forceinline i32 getCombo() const override {
    //     return 2 + std::max((iRepeat - 1), 0) + (std::max((iRepeat - 1), 0) + 1) * ticks.size();
    // }

    [[nodiscard]] vec2 getRawPosAt(i32 posMS) const override;
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 posMS) const override;
    [[nodiscard]] inline vec2 getAutoCursorPos(i32 /*curPos*/) const override { return m_curPoint; }

    void onClickEvent(std::vector<Click> &clicks) override;
    void onReset(i32 curPosMS) override;

    void rebuildVertexBuffer(bool useRawCoords = false) override;

    [[nodiscard]] inline bool isStartCircleFinished() const { return m_startFinished; }
    [[nodiscard]] inline i32 getRepeat() const { return m_repeat; }
    [[nodiscard]] inline const std::vector<vec2> &getRawPoints() const { return m_ctrlPoints; }
    [[nodiscard]] inline f32 getPixelLength() const { return m_curve.getPixelLength(); }
    [[nodiscard]] inline const std::vector<SLIDERCLICK> &getClicks() const { return m_clicks; }

   private:
    void drawStartCircle(f32 alpha);
    void drawEndCircle(f32 alpha, f32 sliderSnake);
    void drawBody(f32 alpha, f32 from, f32 to);

    void updateAnimations(i32 curPosMS);

    void onHit(LiveHitResult result, i32 hitDeltaMS, bool isEndCircle, f32 targetDelta = 0.0f, f32 targetAngle = 0.0f,
               bool isEndResultFromStrictTrackingMod = false);
    void onRepeatHit(const SLIDERCLICK &click);
    void onTickHit(const SLIDERCLICK &click);
    void onSliderBreak();

    [[nodiscard]] vec2 curvePointAt(f32 t) const { return m_curve.pointAt(t) - m_stackOffset; }
    [[nodiscard]] f32 getT(i32 posMS, bool raw) const;

    [[nodiscard]] bool isClickHeldSlider() const;  // special logic to disallow hold tapping
    struct SLIDERTICK {
        f32 percent;
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
    HitAnim &addHitAnim(u8 typeFlags, f32 duration);

    std::vector<HitAnim> m_clickAnimations;
    std::vector<vec2> m_ctrlPoints;
    std::vector<DBHitSample> m_edgeSamples;
    std::vector<Set_Slider_Hit> m_lastSliderSampleSets;

    std::vector<SLIDERTICK> m_ticks;  // ticks (drawing)

    // TEMP: auto cursordance
    std::vector<SLIDERCLICK> m_clicks;  // repeats (type 0) + ticks (type 1)

    std::unique_ptr<VertexArrayObject> m_vao{nullptr};

    SliderCurve m_curve;

    vec2 m_stackOffset{0.f};
    vec2 m_curPoint{0.f};
    vec2 m_curPointRaw{0.f};

    i32 m_strictTrackingModLastClickHeldTime{0};

    f32 m_sliderTimeMS;
    f32 m_sliderTimeMSWithoutRepeats;

    f32 m_slidePct{0.f};  // 0.0f - 1.0f - 0.0f - 1.0f - etc.
    f32 m_sliderSnakePercent{0.f};
    f32 m_reverseArrowAlpha{0.f};
    f32 m_bodyAlpha{0.f};

    AnimFloat m_endSliderBodyFadeAnimation;

    AnimFloat m_followCircleTickAnimationScale;
    f32 m_followCircleAnimationScale{0.f};
    f32 m_followCircleAnimationAlpha{0.f};

    i32 m_repeat;
    i32 m_ignoredKeys{0};
    i32 m_reverseArrowPos{0};
    i32 m_curRepeat{0};
    i32 m_curRepeatCounterForHitSounds{0};

    LiveHitResult m_startResult{0 /* LiveHitResult::HIT_NULL*/};
    LiveHitResult m_endResult{0 /* LiveHitResult::HIT_NULL*/};

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
    Spinner(vec2 pos, i32 timeMS, DBHitSample samples, bool isEndOfCombo, i32 endTimeMS,
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
    void rotate(f32 rad);

    vec2 m_rawPos;
    vec2 m_originalRawPos;

    std::unique_ptr<f32[]> m_storedDeltaAngles;

    // bool bClickedOnce;
    f32 m_percent{0.f};

    f32 m_drawRot{0.f};
    f32 m_rotations{0.f};
    f32 m_rotationsNeeded{-1.f};
    f32 m_deltaOverflowMS{0.f};
    f32 m_sumDeltaAngle{0.f};

    i32 m_maxStoredDeltaAngles;
    i32 m_deltaAngleIndex{0};
    f32 m_deltaAngleOverflow{0.f};

    f32 m_RPM{0.f};

    f32 m_lastMouseAngle{0.f};
    f32 m_ratio{0.f};
};
