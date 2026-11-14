#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Color.h"
#include "Document.h"
#include "Rect.h"
#include "Snapping.h"
#include "Vectors.h"
#include "noinclude.h"
#include "types.h"

#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

struct EditorState;
namespace neomod {
class HitObject;
enum class SLIDERCURVETYPE : char;
}  // namespace neomod
namespace SliderPath {
struct Edge;
}

// the compose tools on the shown map: the tool, the selection, what the next object placed gets, a slider or spinner
// being placed, and the edits the playfield, the timeline, the panels, keys and menus make, each an undoable step.
// positions are osu!px, times are map milliseconds
class Compose {
    NOCOPY_NOMOVE(Compose)
   public:
    enum class Tool : u8 { SELECT, CIRCLE, SLIDER, SPINNER };

    // what the next object placed gets (the sidebar toggles, sample sets and own sample while nothing is selected)
    struct Brush {
        bool newCombo{false};
        u8 hitSounds{0};  // whistle, finish and clap
        i32 normalSet{0};
        i32 additionSet{0};
        // for a circle or a spinner (see setSampleIndex())
        i32 index{0};
        i32 volume{0};
        std::string file;
    };

    struct Hit {
        neomod::HitObject *object;
        Document::Id id;
        // on the timeline, which circle it has: 0 its start, then the slider repeats, last its end (none: between them)
        std::optional<i32> node;
    };

    explicit Compose(EditorState &state);
    ~Compose();

    [[nodiscard]] Tool getTool() const { return this->tool; }
    // finishes what is being placed or dragged where it is; leaving Select clears the selection
    void setTool(Tool tool);
    [[nodiscard]] const Brush &getBrush() const { return this->brush; }
    // another map is shown: the selection, the brush and what was going on go. the tool and what was copied stay
    void reset();

    // the lines for the selected objects
    [[nodiscard]] const std::vector<Document::Id> &getSelection() const { return this->selection; }
    // goes up whenever the selection changes
    [[nodiscard]] u32 getSelectionVersion() const { return this->selectionVersion; }
    [[nodiscard]] bool isSelected(Document::Id id) const;
    // these become the selection (a part picked goes with the old one); anything selected switches to Select
    void select(std::vector<Document::Id> ids);
    // the same with `kept` staying selected (a box or a range that adds to what was selected when it started)
    void select(std::vector<Document::Id> ids, std::span<const Document::Id> kept);
    void toggle(Document::Id id);
    void selectAll();
    // ...and resets what the next object gets
    void clearSelection();
    // the objects on these lines (from a step undone or done again, or from the session) become the selection, if there
    // are any
    void selectTouched(std::vector<Document::Id> ids);
    // the objects were made again: lines that are no objects any more drop out of the selection
    void onObjectsRebuilt();
    [[nodiscard]] const neomod::HitObject *getEarliestSelected() const;
    // to the start of the earliest selected object (a double-click), not snapped
    void seekToSelection();
    // selects by combo numbers from the first object starting at timeMS or later: each number the next object that
    // has it, across combos (a spinner has 1; a number without one is skipped). returns the start of that first object
    // (timeMS when there's none)
    i32 selectCombos(i32 timeMS, std::span<const i32> numbers);

    // a part of the selected slider (when it's the only selection): one of its edges (0 its head, then its repeats,
    // last its tail) or its body. while one is picked, the hitsounds and sample sets shown and set belong to that part:
    // an edge has them in the slider edge lists, the body in the slider itself (the slide). a slider without edge lists
    // gets them written out first, each edge as it plays now, so that a change to one part leaves the others
    struct Part {
        std::optional<i32> edge;  // none: the body
        bool operator==(const Part &) const = default;
    };
    // none: the sounds shown and set belong to the whole selection
    [[nodiscard]] std::optional<Part> getPicked() const;
    void pick(Part part);
    // the part of the selected slider at pos: the edge there nearest in time to the playhead, else its body (none:
    // neither is there)
    [[nodiscard]] std::optional<Part> partAt(vec2 pos) const;
    // where the picked edge is drawn (osu!px; none: no edge is picked)
    [[nodiscard]] std::optional<vec2> getPickedEdgePos() const;

    // when the end circle on an object is: the slider tail (where its curve first ends, also when it repeats), else its
    // end
    [[nodiscard]] static i32 tailTime(const neomod::HitObject &object);
    // what a press at pos is on: a selected object first, then the one nearest in time to the playhead, slider bodies
    // only when that finds nothing
    [[nodiscard]] std::optional<Hit> objectAt(vec2 pos) const;
    // on the timeline: the circle (a start, a slider repeat, an end) nearest to timeMS within withinMS (selected
    // objects first), else what spans it
    [[nodiscard]] std::optional<Hit> objectAtTime(f64 timeMS, f64 withinMS) const;
    // the shown objects whose head is inside
    [[nodiscard]] std::vector<Document::Id> objectsIn(const McRect &area) const;
    // the objects starting or ending between two times
    [[nodiscard]] std::vector<Document::Id> objectsBetween(f64 fromMS, f64 toMS) const;

    // a line angle snap shows, from the object it spaces from (osu!px)
    using Guide = std::pair<vec2, vec2>;
    // where a circle placed with the cursor at `cursor` goes, at the snapped playhead: onto the head or the end of a
    // shown object within reach (0.6 times the circle radius with distance snap, 0.15 without), else spaced from the
    // object before, at 45 degrees, on the grid. whole osu!px, none outside the field
    struct Placement {
        vec2 pos;
        i32 time;
        std::optional<Guide> guide;
    };
    [[nodiscard]] std::optional<Placement> placement(vec2 cursor, const Snapping::Settings &snap) const;
    // the circle a click with the cursor at `cursor` would place: where, and the combo number and colour it would get
    struct Preview {
        Placement at;
        i32 number{0};
        i32 colorCounter{0};
        i32 colorOffset{0};
    };
    [[nodiscard]] std::optional<Preview> previewCircle(vec2 cursor, const Snapping::Settings &snap) const;
    // what the playfield shows for a position: the one on the earliest selected object, else where a click with the
    // cursor at `cursor` would place, and the spacing to the objects before and after it as counted by distance snap
    // (none further than 3 s or a spinner); with the Select tool and nothing selected, the cursor position without
    // spacing
    struct Spot {
        vec2 pos;
        i32 time;
        bool selected;
        std::optional<f32> previous;
        std::optional<f32> next;
    };
    [[nodiscard]] std::optional<Spot> spot(vec2 cursor, const Snapping::Settings &snap) const;
    // what distance snap spaces from while a head is placed or the selection dragged: the end of the object before, how
    // far from it the head goes at its time and how far a divisor tick is (osu!px). none when nothing is spaced
    struct SnapRings {
        vec2 center;
        f32 radius;
        f32 tick;
    };
    [[nodiscard]] std::optional<SnapRings> snapRings(const Snapping::Settings &snap) const;
    // the spacing the map has here, as counted by distance snap: from the object before the earliest selected one to
    // it, without a selection to the first object starting at the time or later (none: no such pair)
    [[nodiscard]] std::optional<f32> currentSpacing() const;

    void placeCircle(vec2 cursor, const Snapping::Settings &snap);
    // placing a slider: its head goes where a circle would, at the snapped playhead; then the cursor (none off the
    // playfield) is its next point, which addSliderPoint() keeps (the last point again becomes a red anchor). until it
    // reaches a tick it isn't in the map; finishing keeps it as it is. a seek while it's placed gives it the span count
    // whose end is nearest to the playhead (see update())
    void startSlider(vec2 cursor, const Snapping::Settings &snap);
    void aimSlider(std::optional<vec2> cursor, const Snapping::Settings &snap);
    void addSliderPoint();
    void finishSlider();
    void cancelSlider();
    // its last point kept goes, the head last (which cancels it)
    void takeBackSliderPoint();
    [[nodiscard]] bool isPlacingSlider() const { return this->sliderPlacement.has_value(); }
    // the control points shown: those on the slider being placed (the cursor last, `aim`), else those on the selection
    // if it's one slider; where they are drawn is `offset` (its stack offset) away. the points are kept until the next
    // change
    struct ControlPoints {
        std::span<const vec2> points;
        std::optional<vec2> aim;
        vec2 offset;
    };
    [[nodiscard]] std::optional<ControlPoints> controlPoints() const;
    // which control point on the selected slider is at pos, within `within` osu!px: the nearest, the first in a red
    // pair
    [[nodiscard]] std::optional<uSz> controlPointAt(vec2 pos, f32 within) const;
    // a drag of one of them: startPointDrag() at the press, then the cursor. the head goes where a dragged head snaps
    // (and stays in the field), the others onto the grid; a red pair moves together. the slider keeps its curve type
    // (a perfect circle while its points make one) and its length snaps again; where that would be under a tick the
    // point stays where it last was
    void startPointDrag(uSz index);
    void dragPoint(vec2 cursor, const Snapping::Settings &snap);
    // a point on the nearest control polygon edge, dragged from there on the same press
    void addControlPoint(vec2 cursor, const Snapping::Settings &snap);
    // an inner point becomes a red anchor
    void makeRedAnchor(uSz index);
    // a point goes (in a red pair: the anchor goes), never below two
    void removeControlPoint(uSz index);
    // the curve type on the selected slider (none: the selection isn't one slider)
    [[nodiscard]] std::optional<neomod::SLIDERCURVETYPE> getCurveType() const;
    // whether it can take this one (a perfect circle needs three points that make one)
    [[nodiscard]] bool takesCurveType(neomod::SLIDERCURVETYPE type) const;
    // its curve becomes of this type: the points stay, the length snaps again (not under a tick)
    void setCurveType(neomod::SLIDERCURVETYPE type);
    // from the snapped time to a beat later; until it's finished its end follows the playhead
    void startSpinner();
    // at the snapped playhead; one no later than its start is taken back
    void finishSpinner();
    void cancelSpinner();
    [[nodiscard]] bool isPlacingSpinner() const { return this->spinner.has_value(); }
    // finishes a slider or spinner being placed, deletes, or toggles the new combo for the next object
    void rightClick(std::optional<Hit> hit);
    // a right-click on the timeline inside a break: removes it unless the rules give its gap one; whether there was one
    bool removeBreakAt(f64 timeMS);
    // a drag on a break edge on the timeline: the edge within withinMS from timeMS becomes the one dragged (false: none
    // is), which dragBreak() takes to the cursor time (snapped to the divisor), as far as its gap allows
    bool startBreakDrag(f64 timeMS, f64 withinMS);
    void dragBreak(f64 timeMS, bool snap);
    // the break a long gap gets by itself, in the gap between objects the editor time is in; false if that gap has one
    // or is too short for it
    bool insertBreak();
    // every break as the map would have it without any moved, added or taken out by hand
    void resetBreaks();

    // the selection as named by an osu! timestamp: the start of its first object and the combo number for each (the
    // editor time and none without a selection)
    [[nodiscard]] std::pair<i32, std::vector<i32>> timestamp() const;

    // how copies of objects were put in (paste, clone)
    enum class Placed : u8 { DONE, NOTHING, PAST_THE_END };
    // the selected objects become what paste() puts in (none selected: nothing), and the system clipboard gets the osu!
    // timestamp for them, "mm:ss:mmm (1,2,3) - " (the editor time without a selection)
    void copy();
    // copy(), then the selection goes (one step)
    void cut();
    [[nodiscard]] bool canPaste() const { return !this->copied.empty(); }
    // the copied objects, the earliest at the snapped time and the others as far after it as they were; objects at
    // exactly their times are replaced, nothing goes in if one would start after the song ends
    Placed paste();
    // copies of the selection a beat after its latest end, snapped (the same rules); the clipboard stays as it was
    Placed clone();
    // what Escape takes back first: a slider or spinner being placed, a drag, a part picked, the selection, a tool
    // other than Select
    [[nodiscard]] bool canCancel() const;
    void cancel();
    void deleteSelection(Document::Action action = Document::Action::DELETE_OBJECTS);

    // a drag on the selection in the playfield: startMove() at the press, then the cursor delta since it, which takes
    // the head on the earliest selected object where it snaps (onto the object before or another one near it, else
    // spaced from the object before, at 45 degrees, on the grid), the others along
    void startMove();
    void move(vec2 delta, const Snapping::Settings &snap);
    // the angle snap line while a drag shows one
    [[nodiscard]] std::optional<Guide> getMoveGuide() const {
        return this->moving ? this->moving->guide : std::nullopt;
    }
    // by a whole osu!px (with grid snap: the earliest selected head to the next grid line that way), one step until
    // endNudge()
    void nudge(vec2 by, const Snapping::Settings &snap);
    void endNudge();
    // a drag on the timeline from a selected object: its start goes to timeMS, the other starts along
    void startTimeMove(Document::Id grabbed);
    void moveInTime(i32 timeMS);
    // a drag on a slider or spinner end on the timeline, when the hit is on one (and its object isn't part of a larger
    // selection, which a drag moves): the object becomes the selection, and then the cursor time gives a slider the
    // span count whose end is nearest to it (at least one; see SliderPath::setSpans()), a spinner its end (snapped to
    // the divisor unless `snap` is off, after its start). false: the hit isn't on such an end
    bool startEndDrag(const Hit &hit);
    void dragEnd(f64 timeMS, bool snap);
    // ends a drag (also over once another step came after its own)
    void endGesture();
    // finishes a slider or spinner being placed where it is and ends a drag: for whatever takes the editor elsewhere
    void endPlacing();
    // J/K: each selected object onto the divisor tick before (-1) or after (1) its start
    void nudgeInTime(i32 direction);
    // Ctrl+G: the selection backwards in time, one step: each object starts where the selection span mirrors its end
    // (sliders keep their length, so their durations follow the velocity there), new combos stay where they were in
    // time (spinners keep theirs), and every slider path goes the other way (see SliderPath::reversed()), its repeats
    // and edge sounds as they were
    void reverse();
    // Ctrl+H, Ctrl+J: the selection mirrored across the playfield middle line, one step (spinners stay)
    void flip(bool vertically);
    // Ctrl+>, Ctrl+<: the selection turned by 90 degrees around the playfield center, clockwise (1) or anticlockwise
    // (-1), one step (spinners stay); what it carries off the field stays there
    void rotate(i32 direction);
    // the sounds on the selected objects (all: on every object) back to none, one step: whistle, finish and clap off,
    // both sample sets auto, no custom index, volume or file, and no edge lists on sliders; new combos stay
    void resetSamples(bool all);
    // the colour skip on every object gone, one step
    void resetColourSkips();
    // every slider length is the length of its whole curve, snapped down to the divisor ticks (as reshaped() does: one
    // that was shorter than its curve can get longer, one under a tick stays as it is), one step
    void recalculateSliderLengths();

    // the step a popup makes, shown while its values change: startPreview() as the popup opens, then each preview
    // function makes the step again from the map as it was at the start, and endPreview() keeps it or takes it back
    void startPreview(Document::Action action);
    void endPreview(bool keep);
    [[nodiscard]] bool isPreviewing() const { return this->previewing.has_value(); }
    // the selected heads and control points through `map` (spinners stay); with `reshape` each slider length snaps
    // again to the divisor ticks, at least one. whether the step changes anything
    bool previewMap(const std::function<vec2(vec2)> &map, bool reshape);
    // the middle of the bounds around the selected heads and slider tails (none: nothing but spinners is selected)
    [[nodiscard]] std::optional<vec2> selectionCenter() const;
    // the largest factor (at least 1) by which the selection scales around `center` on these axes with its heads and
    // slider ends in the field (5 when they're all at the center)
    [[nodiscard]] f32 maxScale(vec2 center, bool x, bool y) const;
    // objects at their own times, replacing the objects that started at exactly those times as the preview started (and
    // `replacing`); nothing when one starts after the song ends. they become the selection; whether any went in
    bool previewPlace(std::span<const neomod::BeatmapFile::HitObject> records,
                      std::optional<Document::Id> replacing = std::nullopt);
    // circles around the playfield center from the snapped playhead, a divisor tick apart: `corners` of them (the first
    // at `offsetDegrees` + 360 / corners, clockwise) `repeats` times round, a side `spacing` times as long as the tick
    // distance there, each with what the next object gets (its new combo on the first). none when a corner is off the
    // field
    [[nodiscard]] std::vector<neomod::BeatmapFile::HitObject> polygon(i32 corners, i32 repeats, f64 offsetDegrees,
                                                                      f64 spacing) const;
    // the selected slider (the only selection) and its line (none: the selection isn't one slider)
    [[nodiscard]] std::optional<std::pair<Document::Id, neomod::BeatmapFile::HitObject>> getSelectedSlider() const;
    // circles along `slider` from its head (one pass, its repeats aside), a tick of `divisor` apart in time: `spacing`
    // times as far apart as it moves in a tick, or with `count` above 0, that many spread evenly to its end. the first
    // has its new combo and its head sounds
    [[nodiscard]] std::vector<neomod::BeatmapFile::HitObject> stream(const neomod::BeatmapFile::HitObject &slider,
                                                                     i32 divisor, f64 spacing, i32 count) const;

    void toggleNewCombo();
    // whistle, finish or clap (HitSoundType bits)
    void toggleHitSound(u8 hitSound);
    void setSampleSet(bool additions, i32 set);
    // a sample on a hit itself in place of the one from its timing point: the custom sample index (0: from the timing
    // point), the volume in percent (0: from the timing point) and the file in the map folder that plays as its normal
    // sound (empty: none). circles and spinners have one, sliders take theirs from timing points: these set it on the
    // selected circles and spinners (on the next one placed without a selection), one step. false, nothing changed, for
    // a selection without either
    bool setSampleIndex(i32 index);
    bool setSampleVolume(i32 volume);
    bool setSampleFile(std::string_view file);
    void setColourSkip(i32 skip);

    // what the sidebar shows: the selection state, or the state for the next object without one
    [[nodiscard]] bool hasNewCombo() const;
    [[nodiscard]] bool hasHitSound(u8 hitSound) const;
    // none: it differs between the selected objects
    [[nodiscard]] std::optional<i32> getSampleSet(bool additions) const;
    // none: it differs between the selected circles and spinners, or there are none of them
    [[nodiscard]] std::optional<i32> getSampleIndex() const;
    [[nodiscard]] std::optional<i32> getSampleVolume() const;
    // none: they differ (without any of them it's empty). good until the map or the selection changes next
    [[nodiscard]] std::optional<std::string_view> getSampleFile() const;
    // the colours one selected new combo (not a spinner) can skip ahead to, the first one its own without a skip
    struct ColourSkips {
        std::array<Color, 8> colours;
        uSz count;
        i32 current;
    };
    [[nodiscard]] std::optional<ColourSkips> getColourSkips() const;

    // once a frame after the clock: a spinner being placed follows the playhead, a slider being placed a seek
    void update();

   private:
    // the selected slider (the only selection): its line, record and points
    struct SelectedSlider {
        Document::Id id;
        neomod::BeatmapFile::HitObject slider;
        std::vector<vec2> points;  // see SliderPath::pointsOf()
        vec2 offset;               // its stack offset
    };
    // what follows from a selection (the sidebar state, its earliest object, the slider it is), for the selection it
    // was made for
    struct Summary {
        u32 selectionVersion{0};
        std::optional<u32> revision{};  // the document revision the sounds are from (see dragStep())
        u8 hitSounds{0};                // the ones every selected object has
        std::optional<i32> normalSet{};
        std::optional<i32> additionSet{};
        // the own samples on the selected circles and spinners (none: they differ, or there are none of them; the file
        // is empty then)
        std::optional<i32> index{};
        std::optional<i32> volume{};
        std::optional<std::string> file{};
        std::optional<u32> objectsRevision{};  // the object revision `earliest` and `newCombo` are for
        std::optional<uSz> earliest{};         // into the objects
        bool newCombo{false};                  // the line on every selected object starts one
        u8 colourSkip{0};                      // from the line the earliest one is on
        std::optional<u32> sliderRevision{};   // the document revision `slider` is from
        std::optional<SelectedSlider> slider{};
    };
    struct Drag {
        u64 gesture{0};
        Document::Action action{};
        u32 revision{0};  // the document revision after the last drag update (or its press)
    };
    struct Move : Drag {
        vec2 min{0.f}, max{0.f};  // the delta that keeps heads and tails in the field (or no further out)
        vec2 applied{0.f};
        vec2 head{0.f};                // from the earliest selected object (not a spinner), unstacked, at the press
        i32 time{0};                   // ...its start
        std::vector<vec2> points;      // the heads and tails on the selected objects from it
        std::optional<vec2> lockedAt;  // the delta at which it stacked onto the object before (without distance snap)
        std::optional<Guide> guide;
        // what its last update was made from: the cursor delta, the snapping and the time
        std::optional<std::tuple<vec2, Snapping::Settings, i32>> made;
    };
    struct TimeMove : Drag {
        i32 grabbedTime{0};
        i32 min{0}, max{0};
        i32 applied{0};
    };
    struct EndDrag : Drag {
        Document::Id id{0};
        neomod::BeatmapFile::HitObject object;  // as the press found it
        f64 span{0.0};                          // for a slider (ms)
        std::optional<neomod::BeatmapFile::HitObject> applied;
    };
    struct BreakDrag : Drag {
        Document::Id id{0};
        bool end{false};
        std::optional<i64> applied;
    };
    struct SpinnerPlacement : Drag {
        i32 start{0};
        i32 beat{0};
        i32 end{0};
        std::optional<Document::Id> replaced;  // the object at exactly its start
    };
    struct PointDrag : Drag {
        Document::Id id{0};
        neomod::BeatmapFile::HitObject slider;  // as the press found it
        uSz index{0};
        bool added{false};  // a new point, put in by the drag
        vec2 offset{0.f};   // the slider stack offset
        std::optional<neomod::BeatmapFile::HitObject> applied;
    };
    struct SliderPlacement : Drag {
        i32 start{0};
        std::vector<vec2> points;  // kept, the head first
        std::optional<vec2> aim;   // the cursor position
        std::optional<i32> reach;  // where the playhead was taken since it started, which its spans reach to
        std::optional<Document::Id> replaced;
        std::optional<neomod::BeatmapFile::HitObject> placed;  // what the map has of it (none: it's under a tick)
        Document::Id line{0};                                  // ...on this line
    };

    // a new gesture for a drag pressed now, whose steps are `action`
    void beginDrag(Drag &drag, Document::Action action);
    // a drag update: false once another step came after its last update or its press (its lines may have gone)
    template <typename F>
    bool dragStep(Drag &drag, F &&fn);
    // the playfield or timeline drag going on (none: there's none)
    [[nodiscard]] const Drag *dragging() const;
    void selectionChanged();
    [[nodiscard]] const Summary &summary() const;

    // drawn at the editor time, so it can be clicked
    [[nodiscard]] bool isShown(const neomod::HitObject &object) const;
    // the objects isShown() can be true for, from the first index to before the last
    [[nodiscard]] std::pair<uSz, uSz> shownRange() const;
    // the bounds around the selected heads and slider tails, unstacked (none: nothing but spinners is selected)
    [[nodiscard]] std::optional<std::pair<vec2, vec2>> selectionBounds() const;
    // how far the selection heads and tails can move without leaving the field, or going further out than they are
    [[nodiscard]] std::pair<vec2, vec2> fieldRange() const;
    [[nodiscard]] i32 snappedNow() const;
    // osu!px per ms a slider covers at the time (0 without timing points)
    [[nodiscard]] f64 velocityAt(i32 timeMS) const;
    // osu!px a slider starting at the time covers in a divisor tick (0 without timing points)
    [[nodiscard]] f64 tickLengthAt(i32 timeMS) const;
    // where a head goes with the cursor at `cursor` at the time: onto the head or the end of a shown object within
    // reach (only unselected objects with `unselected`), else spaced from the object before, at 45 degrees, on the
    // grid. whole osu!px, none outside the field
    [[nodiscard]] std::optional<Placement> headAt(vec2 cursor, const Snapping::Settings &snap, i32 timeMS,
                                                  bool unselected) const;
    // the sounds and sets on the picked part (none: nothing is picked)
    [[nodiscard]] std::optional<SliderPath::Edge> pickedSounds() const;
    // changes them in one step, as `change` (taking whistle, finish and clap, and the sets) does. the slider edge
    // lists are written out first, each edge as it plays now. false: nothing is picked
    template <typename F>
    bool changePicked(Document::Action action, F &&change);
    // the own samples on the selected circles and spinners through `change`, one step; false: none is selected
    template <typename F>
    bool changeSamples(Document::Action action, F &&change);
    // none: the selection isn't one slider
    [[nodiscard]] const SelectedSlider *selectedSlider() const;
    // `slider` with these points and this curve type, its length snapped again; none under a tick
    [[nodiscard]] std::optional<neomod::BeatmapFile::HitObject> reshaped(const neomod::BeatmapFile::HitObject &slider,
                                                                         std::span<const vec2> points,
                                                                         neomod::SLIDERCURVETYPE type) const;
    // the last object ending before the time, which a position at it is spaced from (none: there's none, or it's a
    // spinner)
    [[nodiscard]] const neomod::HitObject *endingBefore(i32 timeMS) const;
    // what distance snap spaces from at the time (none: no such object, or no timing points)
    [[nodiscard]] std::optional<Snapping::Reference> referenceBefore(i32 timeMS) const;
    // gives `fn` each position things stack onto: the heads and slider ends (unstacked) of the shown objects, or of
    // the unselected ones (`unselected`), except the one at `except`
    template <typename F>
    void forStackTargets(bool unselected, std::optional<vec2> except, F &&fn) const;
    // the spacing from `from` (at its time) to `to` as counted by distance snap, none past 3 s
    [[nodiscard]] std::optional<f32> spacing(vec2 from, i32 fromMS, vec2 to, i32 toMS, i32 velocityAtMS) const;
    // the objects in time order: the line for the first starting exactly at timeMS, the last starting before it
    [[nodiscard]] std::optional<Document::Id> objectStartingAt(i32 timeMS) const;
    [[nodiscard]] const neomod::HitObject *objectBefore(i32 timeMS) const;
    void updateSpinner(i32 end);
    // the slider being placed, as it is now, into the map (out while it's under a tick)
    void restateSlider();
    // the heads and control points on the selected objects through `map` (a function taking a position; spinners stay),
    // in the step `c` makes; with `reshape` each slider length snaps again to the divisor ticks, at least one
    template <typename F>
    void mapSelection(Document::Change &c, const F &map, bool reshape = false) const;
    // objects (lines of them) as one step, the earliest at timeMS and the others at their distances from it; the new
    // lines become the selection
    Placed place(Document::Action action, std::span<const std::string> lines, i32 timeMS);
    // the objects starting at exactly the record times, which putting them in replaces (as the map was when the preview
    // started, while one goes on); none when a record starts after the song ends
    [[nodiscard]] std::optional<std::vector<Document::Id>> replacedBy(
        std::span<const neomod::BeatmapFile::HitObject> records);
    // a circle there with what the next object gets
    [[nodiscard]] neomod::BeatmapFile::HitObject brushed(vec2 pos, i32 time, bool newCombo) const;
    // the sample a circle or a spinner placed next gets (good while the brush stays as it is)
    [[nodiscard]] neomod::BeatmapFile::HitSample brushedSample() const;

    EditorState &state;
    Tool tool{Tool::SELECT};
    Brush brush;
    std::vector<Document::Id> selection;  // sorted
    u32 selectionVersion{0};
    std::optional<Part> picked;
    mutable Summary cachedSummary;
    std::optional<Move> moving;
    std::optional<TimeMove> timeMoving;
    std::optional<EndDrag> endDrag;
    std::optional<BreakDrag> breakDrag;
    std::optional<SpinnerPlacement> spinner;
    std::optional<SliderPlacement> sliderPlacement;
    std::optional<PointDrag> pointDrag;
    struct StepPreview : Drag {
        // the object starts and lines as it started (what previewPlace() replaces), kept from its first placing
        std::optional<std::vector<std::pair<i32, Document::Id>>> starts;
    };
    std::optional<StepPreview> previewing;
    u64 nudgeGesture{0};
    std::vector<std::string> copied;  // the lines copy() took, in time order (kept when another map is shown)
};
