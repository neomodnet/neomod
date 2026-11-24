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

// a hit result shown over an object
struct HitResultAnim {
    vec2 rawPos{0.f};
    i32 deltaMS{0};
    f32 deltaRangePercent{0.f};  // deltaMS within the 50 window, -1 (early) to 1 (late)
    f32 timeSecs{-9999.0f};
    LiveHitResult result{0 /* LiveHitResult::HIT_NULL*/};
    bool addObjectDurationToSkinAnimationTimeStartOffset{false};
};

// what an object looks like at the time it was last animated or posed
struct HitObjectLook {
    // currently a maximum of 2 simultaneous results are supported (for drawing, per hitobject)
    HitResultAnim result1;
    HitResultAnim result2;

    i32 deltaMS{0};  // this must be signed
    i32 approachTimeMS{0};
    i32 fadeInTimeMS{0};  // extra time added before the approachTime to let the object smoothly become visible

    f32 alpha{0.f};
    f32 alphaWithoutHidden{0.f};
    f32 alphaForApproachCircle{0.f};
    f32 approachScale{0.f};
    f32 hittableDimRGBColorMultiplierPct{1.f};
    // a posed trail (see HitObjectDrawing::pose()): the circle (or slider head) alpha, 0 without one, and the
    // approach circle scale
    f32 trailAlpha{0.f};
    f32 trailApproachScale{1.f};
    std::optional<Color> tint;  // a slider border or a spinner in this colour instead of its own (none: its own)

    bool overrideHDApproachCircle{false};
    bool useFadeInTimeAsApproachTime{false};
};

struct CircleState {
    AnimFloat hitAnimation;
    f32 shakeAnimation{0.f};
    bool waiting{false};
};

struct SliderClick {
    i32 timeMS;
    i32 type;  // 0: a repeat, 1: a tick
    i32 tickIndex;
    bool finished;
    bool successful;
    bool sliderend;
};

struct SliderTick {
    f32 percent;
    bool finished;
};

struct SliderHitAnim {
    AnimFloat percent;
    // HEAD_TAIL is to emulate how pre-2015 osu! sliders were rendered,
    // when the tail animation happens, the head also gets an additional hit animation, but without drawing the numbers again
    // (TODO: shelved the idea for now)
    enum : u8 { HEAD = (1 << 0), TAIL = (1 << 1), HEAD_TAIL = HEAD | TAIL | (1 << 2) } type;
    [[nodiscard]] bool isAnimating() const { return (percent > 0.f && percent != 1.f); };
};

struct SliderState {
    SliderState(SLIDERCURVETYPE stype, std::span<const vec2> points, f32 pixelLength);

    std::vector<SliderHitAnim> clickAnimations;
    std::vector<DatabaseBeatmapTypes::HITSAMPLE_BITS> edgeSamples;
    std::vector<HitSoundUtils::Set_Slider_Hit> lastSliderSampleSets;

    std::vector<SliderTick> ticks;  // ticks (drawing)

    // TEMP: auto cursordance
    std::vector<SliderClick> clicks;  // repeats (type 0) + ticks (type 1), in time order

    SliderRenderer::Mesh mesh;

    SliderCurve curve;

    vec2 stackOffset{0.f};
    vec2 curPoint{0.f};
    vec2 curPointRaw{0.f};

    i32 strictTrackingModLastClickHeldTime{0};

    f32 sliderTimeMS{0.f};
    f32 sliderTimeMSWithoutRepeats{0.f};

    f32 slidePct{0.f};  // 0.0f - 1.0f - 0.0f - 1.0f - etc.
    f32 sliderSnakePercent{0.f};
    f32 reverseArrowAlpha{0.f};
    f32 bodyAlpha{0.f};

    AnimFloat endSliderBodyFadeAnimation;

    AnimFloat followCircleTickAnimationScale;
    f32 followCircleAnimationScale{0.f};
    f32 followCircleAnimationAlpha{0.f};

    i32 repeat{0};
    i32 ignoredKeys{0};
    i32 reverseArrowPos{0};
    i32 curRepeat{0};
    i32 curRepeatCounterForHitSounds{0};
    i32 slideStartTimeMS{-1};
    u32 numCtrlPoints{0};

    LiveHitResult startResult{0 /* LiveHitResult::HIT_NULL*/};
    LiveHitResult endResult{0 /* LiveHitResult::HIT_NULL*/};

    bool startFinished{false};
    bool endFinished{false};
    bool cursorLeft{true};
    bool cursorInside{false};
    bool tracking{false};  // inside and held: the follow circle shows
    bool heldTillEnd{false};
    bool heldTillEndForLenienceHack{false};
    bool heldTillEndForLenienceHackCheck{false};
    bool inReverse{false};
    bool hideNumberAfterFirstRepeatHit{false};

    // the point on the curve at t (0 to 1), with its stack
    [[nodiscard]] vec2 pointAt(f32 t) const { return this->curve.pointAt(t) - this->stackOffset; }
    // what the repeat-th repeat (counted from 1) plays
    [[nodiscard]] const DatabaseBeatmapTypes::HITSAMPLE_BITS &repeatSamples(i32 repeat) const;
};

struct SpinnerState {
    f32 percent{0.f};

    f32 drawRot{0.f};
    f32 rotations{0.f};
    i32 halfSpinsNeeded{-1};
    f32 deltaOverflowMS{0.f};
    f32 deltaAngleOverflow{0.f};

    // in rad per real ms
    f32 velocity{0.f};
    f32 theoreticalVelocity{0.f};
    i32 stillSteps{0};

    f32 RPM{0.f};

    f32 lastMouseAngle{0.f};
    f32 ratio{0.f};

    // music positions driving the skin element animations (-1 == not happened yet)
    i32 completedTimeMS{-1};  // spinner cleared ("CLEAR!")
    i32 firstSpinTimeMS{-1};  // first half rotation (fades out "SPIN!")
    i32 bonusTimeMS{-1};      // latest bonus spin (bonus counter, glow flash)
    i32 bonusSpins{0};
    i32 lastDrawMusicPosMS{0};
    bool hitSuccess{false};  // non-miss result ("spinner-osu")
};

// an object read from HitObjects (the editor and the tests use it)
class HitObject {
   public:
    HitObject(const HitObjects &objects, u32 index) : objects(&objects), index(index) {}

    // where it is among the objects
    [[nodiscard]] u32 getIndex() const { return this->index; }

    [[nodiscard]] HitObjectType getType() const;
    [[nodiscard]] bool isCircle() const { return this->getType() == HitObjectType::CIRCLE; }
    [[nodiscard]] bool isSlider() const { return this->getType() == HitObjectType::SLIDER; }
    [[nodiscard]] bool isSpinner() const { return this->getType() == HitObjectType::SPINNER; }

    [[nodiscard]] i32 getEndTime() const;
    [[nodiscard]] i32 getClickTime() const;
    [[nodiscard]] i32 getDuration() const { return this->getEndTime() - this->getClickTime(); }
    [[nodiscard]] bool isEndOfCombo() const;
    [[nodiscard]] i32 getComboStartTime() const;
    [[nodiscard]] i32 getComboNumber() const;
    // the line in its file it was read from (1-based, 0: not from a file)
    [[nodiscard]] u32 getLine() const;
    [[nodiscard]] i32 getStack() const;
    [[nodiscard]] i32 getColorCounter() const;
    [[nodiscard]] i32 getColorOffset() const;

    [[nodiscard]] vec2 getRawPosAt(i32 posMS) const;          // with stack calculation modifications
    [[nodiscard]] vec2 getOriginalRawPosAt(i32 posMS) const;  // without stack calculations

    // only for sliders
    [[nodiscard]] i32 getRepeat() const;
    [[nodiscard]] f32 getPixelLength() const;
    [[nodiscard]] const std::vector<SliderClick> &getClicks() const;

    bool operator==(const HitObject &) const = default;

   private:
    const HitObjects *objects;
    u32 index;
};

// a hit circle drawn anywhere (rawPos in osu!px)
namespace Circle {

// main (rawPos in osu!px)
void drawApproachCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                        f32 colorRGBMultiplier, f32 approachScale, f32 alpha, bool overrideHDApproachCircle = false);
void drawCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, i32 colorOffset,
                f32 colorRGBMultiplier, f32 approachScale, f32 alpha, f32 numberAlpha, bool drawNumber = true,
                bool overrideHDApproachCircle = false);
void drawCircle(const Skin *skin, vec2 pos, f32 hitcircleDiameter, Color color, f32 alpha = 1.0f);
void drawSliderStartCircle(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter = 0,
                           i32 colorOffset = 0, f32 colorRGBMultiplier = 1.0f, f32 approachScale = 1.0f,
                           f32 alpha = 1.0f, f32 numberAlpha = 1.0f, bool drawNumber = true,
                           bool overrideHDApproachCircle = false);
void drawSliderEndCircle(const PlayfieldView &view, vec2 rawPos, i32 number = 0, i32 colorCounter = 0,
                         i32 colorOffset = 0, f32 colorRGBMultiplier = 1.0f, f32 approachScale = 1.0f, f32 alpha = 1.0f,
                         f32 numberAlpha = 1.0f, bool drawNumber = true, bool overrideHDApproachCircle = false);
// a circle in `color` (the combo colour, or white for a trail): the skin's hit circle, or for a slider's head its
// slider start circle where the skin has one
void drawCircleIn(const PlayfieldView &view, vec2 rawPos, i32 number, i32 colorCounter, Color color,
                  f32 colorRGBMultiplier, f32 alpha, f32 numberAlpha, bool drawNumber, bool sliderHead);

// split helper functions
void drawApproachCircle(const Skin *skin, vec2 pos, Color comboColor, f32 hitcircleDiameter, f32 approachScale,
                        f32 alpha, bool modHD, bool overrideHDApproachCircle);
void drawHitCircleOverlay(const SkinImage &hitCircleOverlayImage, vec2 pos, f32 circleOverlayImageScale, f32 alpha,
                          f32 colorRGBMultiplier);
void drawHitCircle(Image *hitCircleImage, vec2 pos, Color comboColor, f32 circleImageScale, f32 alpha);
void drawHitCircleNumber(const Skin *skin, f32 numberScale, f32 overlapScale, vec2 pos, i32 number, f32 numberAlpha,
                         f32 colorRGBMultiplier);
}  // namespace Circle

// map objects as plain data, sorted by start time: one array per field, indexed by their place in play order.
// whatever plays or shows them owns them: the judge, the editor MapObjects
class HitObjects {
    NOCOPY_NOMOVE(HitObjects)
   public:
    HitObjects();
    ~HitObjects();

    // makes them from the map primitives (with timing points if it has sliders), in the order they're played,
    // their combo ends and combo start times set
    void build(const Primitives::PRIMITIVE_CONTAINER &primitives);
    // makes the one at `index` again from the primitives, as build() makes it but for the combo start time (which
    // follows from the objects before it)
    void remake(const Primitives::PRIMITIVE_CONTAINER &primitives, uSz index);
    void clear();

    [[nodiscard]] uSz size() const { return this->types.size(); }
    [[nodiscard]] bool empty() const { return this->types.empty(); }
    [[nodiscard]] HitObject operator[](uSz index) const { return {*this, (u32)index}; }

    // gives the ones in [from, to) (the first one starting a combo) their combo start times, each combo starting
    // after an object that ends one
    void startCombos(uSz from, uSz to);
    // stacks the ones in [from, to) as osu! does: moved up and left by a twentieth of the circle diameter per stack
    // level, down and left with Hard Rock (which flips the playfield)
    void stack(uSz from, uSz to, f32 AR, i32 beatmapVersion, f32 stackLeniency, f32 rawHitcircleDiameter,
               bool hardRock);
    // where the stacking run after the one at `start` starts (see Primitives::nextStackingRun())
    [[nodiscard]] uSz nextStackingRun(uSz start, f32 AR, f32 stackLeniency) const;

    // where one is at posMS, with its stack and without
    [[nodiscard]] vec2 rawPosAt(uSz index, i32 posMS) const;
    [[nodiscard]] vec2 originalRawPosAt(uSz index, i32 posMS) const;
    // the sounds a perfect play makes for one: what HitObjectDrawing::pose() shows, for sound
    void addSoundCues(uSz index, std::vector<SoundCue> &out) const;

    [[nodiscard]] CircleState &circleAt(uSz index) { return this->circles[this->typeIndices[index]]; }
    [[nodiscard]] const CircleState &circleAt(uSz index) const { return this->circles[this->typeIndices[index]]; }
    [[nodiscard]] SliderState &sliderAt(uSz index) { return this->sliders[this->typeIndices[index]]; }
    [[nodiscard]] const SliderState &sliderAt(uSz index) const { return this->sliders[this->typeIndices[index]]; }
    [[nodiscard]] SpinnerState &spinnerAt(uSz index) { return this->spinners[this->typeIndices[index]]; }
    [[nodiscard]] const SpinnerState &spinnerAt(uSz index) const { return this->spinners[this->typeIndices[index]]; }

    std::vector<HitObjectType> types;
    std::vector<u32> typeIndices;  // into circles, sliders or spinners by type
    std::vector<i32> clickTimes;
    std::vector<i32> endTimes;
    std::vector<i32> comboStartTimes;  // for freeze frame
    std::vector<i32> comboNumbers;
    std::vector<i32> colorCounters;
    std::vector<i32> colorOffsets;
    std::vector<u8> endsCombo;
    std::vector<u32> lines;  // in the file they were read from (1-based, 0: not from a file)
    std::vector<i32> stacks;
    std::vector<DatabaseBeatmapTypes::HITSAMPLE_BITS> samples;  // hover samples for sliders
    std::vector<vec2> positions;                                // where they start (osu!px), stacked
    std::vector<vec2> originalPositions;                        // and without the stacks

    // play state
    std::vector<u8> finished;
    std::vector<u8> blocked;   // by notelock
    std::vector<u8> misAimed;  // has its slot in the hit error bar (see cv::hiterrorbar_misaims)
    std::vector<u8> visible;
    std::vector<i32> autopilotDeltas;

    std::vector<HitObjectLook> looks;

    // what only one type has
    std::vector<CircleState> circles;
    std::vector<SliderState> sliders;
    std::vector<SpinnerState> spinners;

   private:
    void resize(uSz size);
    // remade: in place of one made before
    void make(const Primitives::PRIMITIVE_CONTAINER &primitives, uSz index, bool remade);
};

// where the slider is along its curve at curPosMS, and its repeats and reverse arrows: the slider state that both
// judging and drawing follow
void updateSlide(SliderState &slider, i32 clickTimeMS, f64 curPosMS, bool visible);
// the part of its time a spinner has left at curPosMS (1 until it starts)
[[nodiscard]] f32 spinnerTimeLeftPercent(const HitObjects &objects, uSz index, i32 curPosMS);

inline HitObjectType HitObject::getType() const { return this->objects->types[this->index]; }
inline i32 HitObject::getEndTime() const { return this->objects->endTimes[this->index]; }
inline i32 HitObject::getClickTime() const { return this->objects->clickTimes[this->index]; }
inline bool HitObject::isEndOfCombo() const { return this->objects->endsCombo[this->index]; }
inline i32 HitObject::getComboStartTime() const { return this->objects->comboStartTimes[this->index]; }
inline i32 HitObject::getComboNumber() const { return this->objects->comboNumbers[this->index]; }
inline u32 HitObject::getLine() const { return this->objects->lines[this->index]; }
inline i32 HitObject::getStack() const { return this->objects->stacks[this->index]; }
inline i32 HitObject::getColorCounter() const { return this->objects->colorCounters[this->index]; }
inline i32 HitObject::getColorOffset() const { return this->objects->colorOffsets[this->index]; }
inline vec2 HitObject::getRawPosAt(i32 posMS) const { return this->objects->rawPosAt(this->index, posMS); }
inline vec2 HitObject::getOriginalRawPosAt(i32 posMS) const {
    return this->objects->originalRawPosAt(this->index, posMS);
}
inline i32 HitObject::getRepeat() const { return this->objects->sliderAt(this->index).repeat; }
inline f32 HitObject::getPixelLength() const { return this->objects->sliderAt(this->index).curve.getPixelLength(); }
inline const std::vector<SliderClick> &HitObject::getClicks() const {
    return this->objects->sliderAt(this->index).clicks;
}

// what play, the editor and the tests draw the objects with
namespace HitObjectDrawing {

// pos is on the screen
void drawHitResult(const PlayfieldView &view, vec2 pos, LiveHitResult result, f32 animPercentInv,
                   f32 hitDeltaRangePercent);

// sets how the object looks at curPosMS while judge plays it, drawn on view (NULL: not drawn, by the judge mods and
// timing)
void animate(HitObjects &objects, uSz index, f64 curPosMS, const GameplayJudge &judge, const PlayfieldView *view);
// whether it shows at curPosMS
[[nodiscard]] bool isVisibleAt(const HitObjects &objects, uSz index, f64 curPosMS, const GameplayJudge &judge,
                               const PlayfieldView *view);

// shows the object at timeMS on view as a perfect play would have left it, without judging it: every part hit at its
// time, hit animations timeMS - hit time into fadeOutMS of music time, no hit results; spinners aren't spun.
// with a trail (trailMS > 0, osu!stable's editor without hit animations), circles and slider heads stay white after
// their hit and fade out over trailMS instead, and slider repeats and ends don't animate
void pose(HitObjects &objects, uSz index, const PlayfieldView &view, i32 timeMS, i32 fadeOutMS, i32 trailMS);

// the follow points between the objects at the view time; the ones before firstIndex are gone
void drawFollowPoints(const PlayfieldView &view, const HitObjects &objects, uSz firstIndex);

// makes the slider body mesh for view (nothing for other objects), before it's first drawn
void rebuildMesh(const PlayfieldView &view, HitObjects &objects, uSz index);

// draws the objects at these indices (the ones to show, latest-ending first) the way play does: spinners first (unless
// not spinnersFirst), then the others with their slider bodies rendered together into sliderBodies, so that earlier
// objects end up on top. the caller runs their draw2(), earliest-ending first
void draw(const PlayfieldView &view, HitObjects &objects, std::span<const u32> indices, RenderTarget *sliderBodies,
          bool spinnersFirst = true);
// what goes over all of them: approach circles, follow circles, slider balls and hit results
void draw2(const PlayfieldView &view, HitObjects &objects, uSz index);

}  // namespace HitObjectDrawing
}  // namespace neomod
