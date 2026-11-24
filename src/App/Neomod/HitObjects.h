#pragma once
#include "types.h"
#include "noinclude.h"

#include "AnimationHandler.h"
#include "Vectors.h"
#include "Color.h"
#include "DatabaseBeatmapTypes.h"
#include "SliderCurves.h"
#include "SliderRenderer.h"

#include <optional>
#include <span>
#include <utility>
#include <vector>
#include <memory>

class ConVar;
class ModFPoSu;
class SkinImage;
class Image;
class GameplayJudge;
class PlayfieldView;
class RenderTarget;

struct Click;
struct Skin;

enum class LiveHitResult : uint8_t;
enum class ModFlags : u64;

namespace neomod {

namespace HitSoundUtils {
struct Set_Slider_Hit;
}
namespace Primitives {
struct PRIMITIVE_CONTAINER;
}

enum class HitObjectType : uint8_t {
    CIRCLE,
    SLIDER,
    SPINNER,
};

class HitObjects;

class HitObject {
   public:
    // a sound a perfect play makes: what judgement plays when the object, or a part of it, is hit at its time
    struct SoundCue {
        enum class Kind : u8 {
            HIT,    // a circle, a slider's head, repeats and end, a spinner's end
            TICK,   // a slider tick
            SLIDE,  // a slider's slide sound, held until endTimeMS
        };
        i32 timeMS;
        i32 endTimeMS;
        Kind kind;
        DatabaseBeatmapTypes::HITSAMPLE_BITS samples;
        vec2 rawPos;  // where it happens (osu!px), which its pan follows
    };

    // pos is on the screen
    static void drawHitResult(const PlayfieldView &view, vec2 pos, LiveHitResult result, f32 animPercentInv,
                              f32 hitDeltaRangePercent);

   protected:  // only constructable through subclasses
    // the one at index among objects, whose fields there it reads and writes.
    // judge: what judges it (NULL: never updated), view: what it's drawn on (NULL: never drawn)
    HitObject(HitObjects &objects, u32 index, DatabaseBeatmapTypes::HITSAMPLE_BITS samples, GameplayJudge *judge,
              const PlayfieldView *view);

   public:
    HitObject() = delete;
    virtual ~HitObject() = default;

    HitObject(const HitObject &) = delete;
    HitObject &operator=(const HitObject &) = delete;
    HitObject(HitObject &&) noexcept = delete;
    HitObject &operator=(HitObject &&) noexcept = delete;

    virtual void animate(f64 curPosMS);
    virtual void draw() {}
    virtual void draw2();
    virtual void update(i32 curPosMS, f64 frameTimeMS) = 0;

    // shows the object at timeMS as a perfect play would have left it, without judging it: every part hit at its
    // time, hit animations timeMS - hit time into fadeOutMS of music time, no hit results; spinners aren't spun.
    // with a trail (trailMS > 0, osu!stable's editor without hit animations), circles and slider heads stay white after
    // their hit and fade out over trailMS instead, and slider repeats and ends don't animate
    virtual void pose(i32 timeMS, i32 fadeOutMS, i32 trailMS) = 0;
    // the sounds a perfect play makes for it: pose()'s counterpart for sound
    virtual void addSoundCues(std::vector<SoundCue> &out) const = 0;

    virtual void updateStackPosition(f32 /*stackOffset*/, bool /*hardRock*/) {}  // unused by spinners
    virtual void miss(i32 /*curPos*/) {}                                         // only used by notelock
    [[nodiscard]] virtual bool isClickableFrom(i32 /*music_pos*/, vec2 /*cursor_pos*/) const { return false; }

    // where it is among the objects
    [[nodiscard]] u32 getIndex() const { return m_index; }

    [[nodiscard]] HitObjectType getType() const;
    [[nodiscard]] bool isCircle() const { return this->getType() == HitObjectType::CIRCLE; }
    [[nodiscard]] bool isSlider() const { return this->getType() == HitObjectType::SLIDER; }
    [[nodiscard]] bool isSpinner() const { return this->getType() == HitObjectType::SPINNER; }

    // Gameplay logic
    [[nodiscard]] i32 getEndTime() const;
    [[nodiscard]] i32 getClickTime() const;
    [[nodiscard]] i32 getDuration() const { return this->getEndTime() - this->getClickTime(); }

    [[nodiscard]] bool isEndOfCombo() const;

    // Visual
    [[nodiscard]] i32 getComboStartTime() const;
    [[nodiscard]] i32 getComboNumber() const;

    // the line in its file it was read from (1-based, 0: not from a file)
    [[nodiscard]] u32 getLine() const;

    void addHitResult(LiveHitResult result, i32 delta, bool isEndOfCombo, vec2 posRaw, f32 targetDeltaPct = 0.0f,
                      f32 targetAngle = 0.0f, bool ignoreOnHitErrorBar = false, bool ignoreCombo = false,
                      bool ignoreHealth = false, bool addObjectDurationToSkinAnimationTimeStartOffset = true);
    // shows a hit result at posRaw without judging anything
    void addHitResultAnim(LiveHitResult result, i32 delta, vec2 posRaw,
                          bool addObjectDurationToSkinAnimationTimeStartOffset);

    void setForceDrawApproachCircle(bool firstNote) { m_overrideHDApproachCircle = firstNote; }
    // draws a slider border or a spinner in this colour instead of its own (none: its own)
    void setTint(std::optional<Color> tint) { m_tint = tint; }

    [[nodiscard]] virtual vec2 getRawPosAt(i32 posMS) const = 0;          // with stack calculation modifications
    [[nodiscard]] virtual vec2 getOriginalRawPosAt(i32 posMS) const = 0;  // without stack calculations
    [[nodiscard]] virtual vec2 getAutoCursorPos(i32 curPosMS) const = 0;

    [[nodiscard]] i32 getStack() const;
    [[nodiscard]] i32 getColorCounter() const;
    [[nodiscard]] i32 getColorOffset() const;
    [[nodiscard]] inline f32 getApproachScale() const { return m_approachScale; }
    [[nodiscard]] inline i32 getDelta() const { return m_deltaMS; }
    [[nodiscard]] inline i32 getApproachTime() const { return m_approachTimeMS; }

    [[nodiscard]] bool isVisible() const;
    [[nodiscard]] bool isVisibleAt(f64 curPosMS) const;
    [[nodiscard]] bool isFinished() const;

    virtual void onClickEvent(std::vector<Click> & /*clicks*/) { ; }
    virtual void onReset(i32 curPosMS);

    // only for sliders
    virtual void rebuildVertexBuffer() { ; }

   protected:
    // the fades, approach, Hidden, the hittable dim and visibility at curPosMS.
    // animate() passes view mods and judge timing.
    void updateLook(f64 curPosMS, ModFlags mods, f32 approachTimeMS, f32 speedAdjustedAnimationSpeed);

    // its fields in the arrays
    [[nodiscard]] i32 clickTimeMS() const;
    [[nodiscard]] i32 durationMS() const { return this->getDuration(); }
    [[nodiscard]] i32 comboStartMS() const;
    [[nodiscard]] i32 comboNumber() const;
    [[nodiscard]] i32 colorCounter() const;
    [[nodiscard]] i32 colorOffset() const;
    [[nodiscard]] bool endOfCombo() const;
    [[nodiscard]] i32 stack() const;
    [[nodiscard]] u8 &finished() const;
    [[nodiscard]] u8 &blocked() const;
    [[nodiscard]] u8 &misAim() const;
    [[nodiscard]] u8 &visible() const;
    [[nodiscard]] i32 &autopilotDelta() const;

   private:
    static f32 lerp3f(f32 a, f32 b, f32 c, f32 percent);

    [[nodiscard]] ModFlags getLookMods() const;
    [[nodiscard]] f32 getLookAnimationSpeed() const;
    [[nodiscard]] i32 getLookApproachTime(i32 fadeInTimeMS, f32 approachTimeMS) const {
        return m_useFadeInTimeAsApproachTime ? fadeInTimeMS : (i32)approachTimeMS;
    }
    [[nodiscard]] bool isVisibleAt(f64 curPosMS, ModFlags mods, i32 approachTimeMS) const;

    struct HITRESULTANIM {
        vec2 rawPos{0.f};
        i32 deltaMS{0};
        f32 deltaRangePercent{0.f};  // deltaMS within the 50 window, -1 (early) to 1 (late)
        f32 timeSecs{-9999.0f};
        LiveHitResult result{0 /* LiveHitResult::HIT_NULL*/};
        bool addObjectDurationToSkinAnimationTimeStartOffset{false};
    };

    void drawHitResultAnim(const HITRESULTANIM &hitresultanim);

    HITRESULTANIM m_hitresultanim1;
    HITRESULTANIM m_hitresultanim2;

   protected:
    HitObjects &m_objects;
    u32 m_index;
    GameplayJudge *m_judge;       // what judges it (NULL: never updated)
    const PlayfieldView *m_view;  // what it's drawn on (NULL: never drawn, no draw-only animations)

    [[nodiscard]] bool animatesHits() const;

    i32 m_deltaMS{0};  // this must be signed
    i32 m_approachTimeMS{0};
    i32 m_fadeInTimeMS{0};  // extra time added before the approachTime to let the object smoothly become visible

    DatabaseBeatmapTypes::HITSAMPLE_BITS m_hitSamples;

    f32 m_alpha{0.f};
    f32 m_alphaWithoutHidden{0.f};
    f32 m_alphaForApproachCircle{0.f};
    f32 m_approachScale{0.f};
    f32 m_hittableDimRGBColorMultiplierPct{1.f};
    // a posed trail (see pose()): the circle's (or slider head's) alpha, 0 without one, and its approach circle's scale
    f32 m_trailAlpha{0.f};
    f32 m_trailApproachScale{1.f};
    std::optional<Color> m_tint;

    bool m_overrideHDApproachCircle{false};
    bool m_useFadeInTimeAsApproachTime{false};
};

class Circle final : public HitObject {
   public:
    // main (rawPos in osu!px)
    static void drawApproachCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter,
                                   i32 colorOffset, f32 colorRGBMultiplier, f32 approachScale, f32 alpha,
                                   bool overrideHDApproachCircle = false);
    static void drawCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                           f32 colorRGBMultiplier, f32 approachScale, f32 alpha, f32 numberAlpha,
                           bool drawNumber = true, bool overrideHDApproachCircle = false);
    static void drawCircle(const Skin *skin, vec2 pos, f32 hitcircleDiameter, Color color, f32 alpha = 1.0f);
    static void drawSliderStartCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter = 0,
                                      i32 colorOffset = 0, f32 colorRGBMultiplier = 1.0f, f32 approachScale = 1.0f,
                                      f32 alpha = 1.0f, f32 numberAlpha = 1.0f, bool drawNumber = true,
                                      bool overrideHDApproachCircle = false);
    static void drawSliderEndCircle(const PlayfieldView &view, vec2 rawPos, i32 number = 0, i32 colorCounter = 0,
                                    i32 colorOffset = 0, f32 colorRGBMultiplier = 1.0f, f32 approachScale = 1.0f,
                                    f32 alpha = 1.0f, f32 numberAlpha = 1.0f, bool drawNumber = true,
                                    bool overrideHDApproachCircle = false);
    // a circle in `color` (the combo colour, or white for a trail): the skin's hit circle, or for a slider's head its
    // slider start circle where the skin has one
    static void drawCircleIn(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, Color color,
                             f32 colorRGBMultiplier, f32 alpha, f32 numberAlpha, bool drawNumber, bool sliderHead);

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

    Circle(HitObjects &objects, u32 index, vec2 pos, DatabaseBeatmapTypes::HITSAMPLE_BITS samples, GameplayJudge *judge,
           const PlayfieldView *view);

    Circle(const Circle &) = delete;
    Circle &operator=(const Circle &) = delete;
    Circle(Circle &&) noexcept = delete;
    Circle &operator=(Circle &&) noexcept = delete;

    void draw() override;
    void draw2() override;
    void update(i32 curPosMS, f64 frameTimeMS) override;
    void pose(i32 timeMS, i32 fadeOutMS, i32 trailMS) override;
    void addSoundCues(std::vector<SoundCue> &out) const override;

    void updateStackPosition(f32 stackOffset, bool hardRock) override;
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

class Slider final : public HitObject, public SliderRenderer::BodySource {
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

    Slider(HitObjects &objects, u32 index, neomod::SLIDERCURVETYPE stype, i32 repeat, f32 pixelLength,
           std::span<const vec2> points, std::span<const f32> ticks, f32 sliderTimeMS, f32 sliderTimeMSWithoutRepeats,
           DatabaseBeatmapTypes::HITSAMPLE_BITS hoverSamples,
           std::vector<DatabaseBeatmapTypes::HITSAMPLE_BITS> edgeSamples, GameplayJudge *judge,
           const PlayfieldView *view);

    Slider(const Slider &) = delete;
    Slider &operator=(const Slider &) = delete;
    Slider(Slider &&) noexcept = delete;
    Slider &operator=(Slider &&) noexcept = delete;

    void animate(f64 curPosMS) override;
    void draw() override;
    inline void draw2() override { draw2(true, false); }
    void draw2(bool drawApproachCircle, bool drawOnlyApproachCircle);
    void update(i32 curPosMS, f64 frameTimeSecs) override;
    void pose(i32 timeMS, i32 fadeOutMS, i32 trailMS) override;
    void addSoundCues(std::vector<SoundCue> &out) const override;

    void updateStackPosition(f32 stackOffset, bool hardRock) override;
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

    void rebuildVertexBuffer() override;

    // the live body, or the one fading out after the end
    [[nodiscard]] std::optional<SliderRenderer::Body> getBody() const override;

    [[nodiscard]] inline bool isStartCircleFinished() const { return m_startFinished; }
    [[nodiscard]] inline i32 getRepeat() const { return m_repeat; }
    [[nodiscard]] inline f32 getPixelLength() const { return m_curve.getPixelLength(); }
    [[nodiscard]] inline const std::vector<SLIDERCLICK> &getClicks() const { return m_clicks; }

   private:
    void drawStartCircle(f32 alpha);
    void drawEndCircle(f32 alpha, f32 sliderSnake);

    [[nodiscard]] SliderRenderer::Body makeBody(f32 alpha, f32 from, f32 to) const;
    // the drawn part of the curve while snaking in or shrinking
    [[nodiscard]] std::pair<f32, f32> getSnakeRange() const;

    void updateSlideLook(f64 curPosMS, ModFlags mods);
    void updateSlide(f64 curPosMS, bool visible);
    // the follow circle's fades at curPosMS
    void updateAnimations(f64 curPosMS, f32 speedAdjustedAnimationSpeed);
    // whatever changes the cursor's position or the keys that count calls this
    void updateTracking();

    void onHit(LiveHitResult result, i32 hitDeltaMS, bool isEndCircle, f32 targetDelta = 0.0f, f32 targetAngle = 0.0f,
               bool isEndResultFromStrictTrackingMod = false);
    void onRepeatHit(const SLIDERCLICK &click);
    void onTickHit(const SLIDERCLICK &click);
    void onSliderBreak();
    // what the repeat-th repeat (counted from 1) plays
    [[nodiscard]] const DatabaseBeatmapTypes::HITSAMPLE_BITS &getRepeatSamples(i32 repeat) const;

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
    std::vector<DatabaseBeatmapTypes::HITSAMPLE_BITS> m_edgeSamples;
    std::vector<HitSoundUtils::Set_Slider_Hit> m_lastSliderSampleSets;

    std::vector<SLIDERTICK> m_ticks;  // ticks (drawing)

    // TEMP: auto cursordance
    std::vector<SLIDERCLICK> m_clicks;  // repeats (type 0) + ticks (type 1)

    SliderRenderer::Mesh m_mesh;

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
    i32 m_slideStartTimeMS{-1};
    u32 m_numCtrlPoints{0};

    LiveHitResult m_startResult{0 /* LiveHitResult::HIT_NULL*/};
    LiveHitResult m_endResult{0 /* LiveHitResult::HIT_NULL*/};

    bool m_startFinished{false};
    bool m_endFinished{false};
    bool m_cursorLeft{true};
    bool m_cursorInside{false};
    bool m_tracking{false};  // inside and held: the follow circle shows
    bool m_heldTillEnd{false};
    bool m_heldTillEndForLenienceHack{false};
    bool m_heldTillEndForLenienceHackCheck{false};
    bool m_inReverse{false};
    bool m_hideNumberAfterFirstRepeatHit{false};
};

class Spinner final : public HitObject {
   public:
    Spinner() = delete;
    Spinner(HitObjects &objects, u32 index, vec2 pos, DatabaseBeatmapTypes::HITSAMPLE_BITS samples,
            GameplayJudge *judge, const PlayfieldView *view);
    ~Spinner() override;

    Spinner(const Spinner &) = delete;
    Spinner &operator=(const Spinner &) = delete;
    Spinner(Spinner &&) noexcept = delete;
    Spinner &operator=(Spinner &&) noexcept = delete;

    void draw() override;
    void update(i32 curPosMS, f64 frameTimeSecs) override;
    void pose(i32 timeMS, i32 fadeOutMS, i32 trailMS) override;
    void addSoundCues(std::vector<SoundCue> &out) const override;

    [[nodiscard]] vec2 getRawPosAt(i32 /*pos*/) const override { return m_rawPos; }
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 /*pos*/) const override { return m_originalRawPos; }
    [[nodiscard]] vec2 getAutoCursorPos(i32 curPosMS) const override;

    void onReset(i32 curPosMS) override;

   private:
    void onHit();
    void rotate(f32 rad, i32 curPosMS);
    // the part of its time left at curPosMS (1 until it starts)
    [[nodiscard]] f32 getTimeLeftPercent(i32 curPosMS) const;

    vec2 m_rawPos;
    vec2 m_originalRawPos;

    // bool bClickedOnce;
    f32 m_percent{0.f};

    f32 m_drawRot{0.f};
    f32 m_rotations{0.f};
    i32 m_halfSpinsNeeded{-1};
    f32 m_deltaOverflowMS{0.f};
    f32 m_deltaAngleOverflow{0.f};

    // in rad per real ms
    f32 m_velocity{0.f};
    f32 m_theoreticalVelocity{0.f};
    i32 m_stillSteps{0};

    f32 m_RPM{0.f};

    f32 m_lastMouseAngle{0.f};
    f32 m_ratio{0.f};

    // music positions driving the skin element animations (-1 == not happened yet)
    i32 m_completedTimeMS{-1};  // spinner cleared ("CLEAR!")
    i32 m_firstSpinTimeMS{-1};  // first half rotation (fades out "SPIN!")
    i32 m_bonusTimeMS{-1};      // latest bonus spin (bonus counter, glow flash)
    i32 m_bonusSpins{0};
    i32 m_lastDrawMusicPosMS{0};
    bool m_hitSuccess{false};  // non-miss result ("spinner-osu")
};

// map objects as plain data, sorted by start time: one array per field, indexed by their place in play order.
// whatever plays or shows them owns them: the judge, the editor MapObjects
class HitObjects {
    NOCOPY_NOMOVE(HitObjects)
   public:
    HitObjects();
    ~HitObjects();

    // makes them from the map primitives (with timing points if it has sliders), in the order they're played,
    // their combo ends and combo start times set; judged by judge and drawn on view (see the HitObject constructor)
    void build(const Primitives::PRIMITIVE_CONTAINER &primitives, GameplayJudge *judge, const PlayfieldView *view);
    // makes the one at `index` again from the primitives, as build() makes it but for the combo start time (which
    // follows from the objects before it)
    void remake(const Primitives::PRIMITIVE_CONTAINER &primitives, uSz index, GameplayJudge *judge,
                const PlayfieldView *view);
    void clear();

    [[nodiscard]] uSz size() const { return this->types.size(); }
    [[nodiscard]] bool empty() const { return this->types.empty(); }
    [[nodiscard]] HitObject &operator[](uSz index) const { return *this->objects[index]; }

    // gives the ones in [from, to) (the first one starting a combo) their combo start times, each combo starting
    // after an object that ends one
    void startCombos(uSz from, uSz to);
    // stacks the ones in [from, to) as osu! does: moved up and left by a twentieth of the circle diameter per stack
    // level, down and left with Hard Rock (which flips the playfield)
    void stack(uSz from, uSz to, f32 AR, i32 beatmapVersion, f32 stackLeniency, f32 rawHitcircleDiameter,
               bool hardRock);
    // where the stacking run after the one at `start` starts (see Primitives::nextStackingRun())
    [[nodiscard]] uSz nextStackingRun(uSz start, f32 AR, f32 stackLeniency) const;

    std::vector<HitObjectType> types;
    std::vector<i32> clickTimes;
    std::vector<i32> endTimes;
    std::vector<i32> comboStartTimes;  // for freeze frame
    std::vector<i32> comboNumbers;
    std::vector<i32> colorCounters;
    std::vector<i32> colorOffsets;
    std::vector<u8> endsCombo;
    std::vector<u32> lines;  // in the file they were read from (1-based, 0: not from a file)
    std::vector<i32> stacks;

    // play state
    std::vector<u8> finished;
    std::vector<u8> blocked;   // by notelock
    std::vector<u8> misAimed;  // has its slot in the hit error bar (see cv::hiterrorbar_misaims)
    std::vector<u8> visible;
    std::vector<i32> autopilotDeltas;

    std::vector<std::unique_ptr<HitObject>> objects;

   private:
    void resize(uSz size);
    void make(const Primitives::PRIMITIVE_CONTAINER &primitives, uSz index, GameplayJudge *judge,
              const PlayfieldView *view);
};

namespace HitObjectDrawing {

// the follow points between the objects at the view time; the ones before firstIndex are gone
void drawFollowPoints(const PlayfieldView &view, const HitObjects &objects, uSz firstIndex);

// draws the objects at these indices (the ones to show, latest-ending first) the way play does: spinners first, then
// the others with their slider bodies rendered together into sliderBodies, so that earlier objects end up on top.
// their draw2() is the caller's, earliest-ending first
void draw(const HitObjects &objects, std::span<const u32> indices, RenderTarget *sliderBodies);

}  // namespace HitObjectDrawing
}  // namespace neomod
