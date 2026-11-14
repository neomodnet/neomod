// Copyright (c) 2026, WH, All rights reserved.
#include "TimingPanel.h"

#include "CBaseUINumberField.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "ConVar.h"
#include "ContainerRanges.h"
#include "EditorState.h"
#include "Engine.h"
#include "Environment.h"
#include "Form.h"
#include "Hashing.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "KeyboardEvent.h"
#include "Metronome.h"
#include "NotificationOverlay.h"
#include "Parsing.h"
#include "Retiming.h"
#include "SString.h"
#include "TimeDisplay.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UIStyle.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <span>
#include <utility>

using Step = Document::Action;
using Change = Document::Change;
using Point = neomod::BeatmapFile::TimingPoint;

namespace {
constexpr Color UNINHERITED_COLOR{argb(255, 220, 60, 60)};
constexpr Color INHERITED_COLOR{argb(255, 140, 220, 40)};

// the tempo of an uninherited point, and the slider velocity of an inherited one as taken by the game
f64 bpmOf(const Point &point) { return point.beatLength > 0.0 ? 60000.0 / point.beatLength : 0.0; }
f64 velocityOf(const Point &point) {
    return point.beatLength < 0.0 ? 100.0 / std::clamp(-point.beatLength, 10.0, 1000.0) : 1.0;
}
// what's shown has this many decimals: `scale` is ten to their number
f64 rounded(f64 value, f64 scale) { return std::round(value * scale) / scale; }

void notify(std::string text) { ui->getNotificationOverlay()->addNotification(std::move(text)); }

// the timing points on the system clipboard lines (none unless every line that says something is one)
std::vector<Point> clipboardPoints() {
    std::vector<Point> points;
    for(std::string_view line : SString::split_newlines(env->getClipBoardText())) {
        line = SString::trimmed(line);
        if(line.empty()) continue;
        // a whole point: its eight fields, all numbers (other lines of a map start with two numbers too)
        const auto fields = SString::split(line, ',');
        Point point;
        if(fields.size() != 8 ||
           !Mc::ranges::all_of(fields,
                               [](std::string_view field) {
                                   f64 number{0.0};
                                   return Parsing::parse(field, &number);
                               }) ||
           !neomod::BeatmapFile::parse(line, point)) {
            return {};
        }
        points.push_back(point);
    }
    return points;
}

// taps further apart than this aren't one run
constexpr i32 TAP_PAUSE_MS{2500};
constexpr i32 MAX_LAMPS{8};
}  // namespace

// a lamp for each beat in the bar the editor time is in, the current beat lit
class TimingPanel::Lamps final : public CBaseUIElement {
    NOCOPY_NOMOVE(Lamps)
   public:
    explicit Lamps(const EditorState &state)
        : CBaseUIElement(0, 0, 0, 0, std::string{"editor_timing_lamps"}), state(state) {}
    ~Lamps() override = default;

    void draw() override {
        if(!this->isVisible() || !this->state.document) return;
        const auto beat = Metronome::beatAt(this->state.document->getTimingPoints(), this->state.clock.getTime());
        if(!beat) return;
        const UIStyle &style = uiStyle();
        const McRect &rect = this->getRect();
        const i32 count = std::min(beat->meter, MAX_LAMPS);
        const float gap = style.px(style.metrics.gap);
        const float width = (rect.getWidth() - gap * (float)(count - 1)) / (float)count;
        for(i32 i = 0; i < count; i++) {
            style.drawControl({rect.getX() + (float)i * (width + gap), rect.getY(), width, rect.getHeight()},
                              UIStyle::ControlState::NORMAL, i == beat->inBar);
        }
    }
    [[nodiscard]] vec2 getNaturalSize() override { return {0.f, uiStyle().px(uiStyle().metrics.controlHeight)}; }

   private:
    const EditorState &state;
};

TimingPanel::TimingPanel(EditorState &state, CBaseUIPopupLayer &popups, std::function<void(i32)> showSamples)
    : CBaseUIBox(Axis::ROW, "editor_timing"), state(state) {
    const UIStyle &style = uiStyle();
    this->setPadding(style.metrics.padding);
    this->setGap(2.f * style.metrics.padding);

    this->list = new CBaseUIList(*this,
                                 {{_("Time"), 110.f},
                                  {_("Tempo"), 0.f},
                                  {_("Meter"), 70.f},
                                  {_("Samples"), 90.f},
                                  {_("Volume"), 84.f},
                                  {_("Kiai"), 64.f}},
                                 "editor_timing_points");
    this->list->setSelectionCallback([this] { this->selectionChanged(); });
    this->list->setActivateCallback([this](uSz row) {
        const auto points = this->state.document->getTimingPoints();
        if(row < points.size()) this->state.clock.seek((i32)points[row].time);
    });
    auto *addUninherited = new CBaseUIStyledButton(_("Add Timing Point"), "editor_timing_add");
    addUninherited->setClickCallback([this] { this->addPoint(true); });
    auto *addInherited = new CBaseUIStyledButton(_("Add Inherited Point"), "editor_timing_add_inherited");
    addInherited->setClickCallback([this] { this->addPoint(false); });
    this->removeButton = new CBaseUIStyledButton(_("Delete"), "editor_timing_delete");
    this->removeButton->setClickCallback([this] { this->removePoints(false); });
    auto *buttons = new CBaseUIBox(Axis::ROW);
    buttons->setGap(style.metrics.gap);
    buttons->add(addUninherited, BoxItem::fit())
        ->add(addInherited, BoxItem::fit())
        ->add(this->removeButton, BoxItem::fit());
    auto *points = new CBaseUIBox(Axis::COLUMN);
    points->setGap(style.metrics.gap);
    points->add(this->list, BoxItem::flex())->add(buttons, BoxItem::fit());

    this->form = new Form(state, popups, "editor_timing_fields", _("Selected Points"));
    const Form::Enabled any = [this](const Document &) { return !this->summarize().rows.empty(); };
    const Form::Enabled uninherited = [this](const Document &) { return this->summarize().uninherited; };
    const Form::Enabled inherited = [this](const Document &) { return this->summarize().inherited; };
    // the time of the first selected point: a change moves every selected point by as much
    const std::function<void(Change &, f64)> moveBy = [this](Change &c, f64 by) {
        this->edit(c, Which::ALL, [by](Point &point) { point.time += by; });
    };
    CBaseUINumberField *time = this->form->makeNumber(
        "editor_timing_time", Step::TIMING, [this](const Document &) { return this->summarize().time; },
        [this, moveBy](Change &c, f64 value) { moveBy(c, value - this->summarize().time); }, any);
    time->setRange(-100000.0, 36000000.0)->setStep(1.0, 1.0)->setSuffix(_(" ms"));
    // repeated clicks are one step, see retime()
    this->earlier = new CBaseUIStyledButton("-", "editor_timing_time_less");
    this->earlier->setClickCallback([this] { this->nudgeTime(-1.0); });
    this->later = new CBaseUIStyledButton("+", "editor_timing_time_more");
    this->later->setClickCallback([this] { this->nudgeTime(1.0); });
    auto *timeRow = new CBaseUIBox(Axis::ROW);
    timeRow->setGap(style.metrics.gap);
    timeRow->add(time, BoxItem::flex())->add(this->earlier, BoxItem::fit())->add(this->later, BoxItem::fit());
    this->form->addRow(_("Time"), timeRow, BoxItem::flex());
    this->form->addButton(
        "editor_timing_time_now", _("Use Current Time"), Step::TIMING,
        [this, moveBy](Change &c) { moveBy(c, this->state.clock.getTime() - this->summarize().time); }, any);

    CBaseUINumberField *bpm = this->form->makeNumber(
        "editor_timing_bpm", Step::TIMING, [this](const Document &) { return this->summarize().bpm; },
        [this](Change &c, f64 value) {
            this->edit(c, Which::UNINHERITED,
                       [value](Point &point) { point.beatLength = Retiming::beatLengthOf(value); });
        },
        uninherited);
    bpm->setRange(1.0, 10000.0)->setStep(1.0, 0.1)->setDecimals(3);
    // repeated clicks are one step, see retime()
    this->slower = new CBaseUIStyledButton("-", "editor_timing_bpm_less");
    this->slower->setClickCallback([this] { this->nudgeTempo(-1.0); });
    this->faster = new CBaseUIStyledButton("+", "editor_timing_bpm_more");
    this->faster->setClickCallback([this] { this->nudgeTempo(1.0); });
    auto *tempo = new CBaseUIBox(Axis::ROW);
    tempo->setGap(style.metrics.gap);
    tempo->add(bpm, BoxItem::flex())->add(this->slower, BoxItem::fit())->add(this->faster, BoxItem::fit());
    this->form->addRow(_("BPM"), tempo, BoxItem::flex());

    this->velocity = this->form->addNumber(
        "editor_timing_velocity", _("Slider Velocity"), Step::TIMING,
        [this](const Document &) { return this->summarize().velocity; },
        [this](Change &c, f64 value) {
            this->edit(c, Which::INHERITED,
                       [value](Point &point) { point.beatLength = Retiming::velocityBeatLength(value); });
        },
        inherited);
    this->velocity->setRange(0.1, 10.0)->setStep(0.1, 0.01)->setDecimals(2)->setSuffix("x");
    this->form
        ->addNumber(
            "editor_timing_meter", _("Meter"), Step::TIMING,
            [this](const Document &) { return this->summarize().meter; },
            [this](Change &c, f64 value) {
                this->edit(c, Which::UNINHERITED, [value](Point &point) { point.meter = (i32)value; });
            },
            uninherited)
        ->setRange(1.0, 16.0)
        ->setStep(1.0, 1.0)
        ->setSuffix("/4");
    // as numbered by the file; a point without one of the three shows none
    this->form->addChoice(
        "editor_timing_sample_set", _("Sample Set"), {_("Normal"), _("Soft"), _("Drum")}, Step::TIMING,
        [this](const Document &) { return this->summarize().sampleSet; },
        [this](Change &c, int index) {
            this->edit(c, Which::ALL, [index](Point &point) { point.sampleSet = index + 1; });
        },
        any);
    CBaseUINumberField *sampleIndex = this->form->makeNumber(
        "editor_timing_sample_index", Step::TIMING, [this](const Document &) { return this->summarize().sampleIndex; },
        [this](Change &c, f64 value) {
            this->edit(c, Which::ALL, [value](Point &point) { point.sampleIndex = (i32)value; });
        },
        any);
    // the skin samples, none from the map
    sampleIndex->setRange(0.0, 100.0)->setStep(1.0, 1.0)->setNamedValue(0.0, _("Default"));
    // the files the map has for the index on the selected points (the first, without one)
    auto *sampleFiles = new CBaseUIStyledButton(_("Files..."), "editor_timing_sample_files");
    sampleFiles->setClickCallback([this, showSamples = std::move(showSamples)] {
        const f64 index = this->summarize().sampleIndex;
        if(showSamples) showSamples(index >= 1.0 ? (i32)index : 1);
    });
    auto *samples = new CBaseUIBox(Axis::ROW);
    samples->setGap(style.metrics.gap);
    samples->add(sampleIndex, BoxItem::flex())->add(sampleFiles, BoxItem::fit());
    this->form->addRow(_("Sample Index"), samples, BoxItem::flex());
    this->form
        ->addNumber(
            "editor_timing_volume", _("Volume"), Step::TIMING,
            [this](const Document &) { return this->summarize().volume; },
            [this](Change &c, f64 value) {
                this->edit(c, Which::ALL, [value](Point &point) { point.volume = (i32)value; });
            },
            any)
        ->setRange(5.0, 100.0)
        ->setStep(5.0, 1.0)
        ->setSuffix("%");
    const auto effect = [&](const char *name, std::string label, i32 bit, bool Summary::*has, Which which,
                            const Form::Enabled &enabled) {
        this->form->addToggle(
            name, std::move(label), Step::TIMING, [this, has](const Document &) { return this->summarize().*has; },
            [this, bit, which](Change &c, bool on) {
                this->edit(c, which, [bit, on](Point &point) {
                    point.effects = on ? point.effects | bit : point.effects & ~bit;
                });
            },
            enabled);
    };
    effect("editor_timing_kiai", _("Kiai"), Point::EFFECT_KIAI, &Summary::kiai, Which::ALL, any);
    effect("editor_timing_omit_bar_line", _("Omit First Bar Line"), Point::EFFECT_OMIT_FIRST_BARLINE,
           &Summary::omitBarLine, Which::UNINHERITED, uninherited);

    auto *tapButton = new CBaseUIStyledButton(_("Tap"), "editor_timing_tap");
    tapButton->setClickCallback([this] { this->tap(); });
    auto *tapRow = new CBaseUIBox(Axis::ROW);
    tapRow->setGap(style.metrics.gap);
    tapRow->add(tapButton, BoxItem::fit())->add(new Lamps(state), BoxItem::flex());
    this->tapped = new CBaseUIStyledLabel({}, "editor_timing_tapped");
    this->tapped->setDim(true);
    auto *side = new CBaseUIBox(Axis::COLUMN);
    side->setGap(2.f * style.metrics.padding);
    auto *tapping = new CBaseUIBox(Axis::COLUMN);
    tapping->setGap(style.metrics.gap);
    tapping->add(tapRow, BoxItem::fit())->add(this->tapped, BoxItem::fit());
    side->add(this->form, BoxItem::fit())->add(tapping, BoxItem::fit());

    this->add(points, BoxItem::flex())->add(side, BoxItem::fixed(380.f));
}

TimingPanel::~TimingPanel() = default;

uSz TimingPanel::getRowCount() const {
    return this->state.document ? this->state.document->getTimingPoints().size() : 0;
}

u64 TimingPanel::getRowKey(uSz row) const { return this->state.document->getTimingPointIds()[row]; }

std::string TimingPanel::getCellText(uSz row, uSz column) const {
    const Point &point = this->state.document->getTimingPoints()[row];
    switch(column) {
        case 0:
            return TimeDisplay::format((i32)point.time);
        case 1:
            if(!point.uninherited) return fmt::format("x{:.2f}", velocityOf(point));
            return point.beatLength > 0.0 ? tformat("{} BPM", rounded(bpmOf(point), 1000.0)) : std::string{"-"};
        case 2:
            return point.uninherited ? fmt::format("{}/4", point.meter) : std::string{};
        case 3: {
            static constexpr std::string_view SETS{"-NSD"};
            const char set = point.sampleSet >= 1 && point.sampleSet <= 3 ? SETS[(uSz)point.sampleSet] : SETS[0];
            return point.sampleIndex > 0 ? fmt::format("{}:{}", set, point.sampleIndex) : std::string{set};
        }
        case 4:
            return fmt::format("{}%", point.volume);
        default:
            return point.effects & Point::EFFECT_KIAI ? _("Kiai") : "";
    }
}

CBaseUIList::RowLook TimingPanel::getRowLook(uSz row) const {
    return {.bar = this->state.document->getTimingPoints()[row].uninherited ? UNINHERITED_COLOR : INHERITED_COLOR,
            .marked = row == this->inForce || row == this->inForceUninherited};
}

const TimingPanel::Summary &TimingPanel::summarize() {
    const Document *document = this->state.document.get();
    if(document && this->summarized == document->getRevision()) return this->summary;

    Summary s;
    if(document) {
        const auto points = document->getTimingPoints();
        const auto ids = document->getTimingPointIds();
        const auto &selection = this->list->getSelection();
        // a value they all have: the one on the first until another one differs
        const auto fold = [](f64 &into, f64 value, bool first) {
            if(first) {
                into = value;
            } else if(into != value) {
                into = NONE;
            }
        };
        std::vector<u64> there;
        for(uSz row = 0; row < points.size() && there.size() < selection.size(); row++) {
            if(!selection.contains(ids[row])) continue;
            const Point &point = points[row];
            const bool first = s.rows.empty();
            there.push_back(ids[row]);
            s.rows.push_back(row);

            if(first) s.time = point.time;
            const int set = point.sampleSet >= 1 && point.sampleSet <= 3 ? point.sampleSet - 1 : -1;
            s.sampleSet = first || s.sampleSet == set ? set : -1;
            fold(s.sampleIndex, point.sampleIndex, first);
            fold(s.volume, point.volume, first);
            s.kiai = (first || s.kiai) && (point.effects & Point::EFFECT_KIAI);
            if(point.uninherited) {
                const bool omits = point.effects & Point::EFFECT_OMIT_FIRST_BARLINE;
                s.omitBarLine = (!s.uninherited || s.omitBarLine) && omits;
                fold(s.bpm, rounded(bpmOf(point), 1000.0), !s.uninherited);
                fold(s.meter, point.meter, !s.uninherited);
                s.uninherited = true;
            } else {
                fold(s.velocity, rounded(velocityOf(point), 100.0), !s.inherited);
                s.inherited = true;
            }
        }
        // points that are gone
        if(there.size() != selection.size()) this->list->setSelection(there);
        this->summarized = document->getRevision();
    } else {
        this->summarized.reset();
    }
    this->summary = std::move(s);
    return this->summary;
}

template <typename F>
void TimingPanel::edit(Change &c, Which which, F &&edit) {
    const Document &document = *this->state.document;
    const auto points = document.getTimingPoints();
    const auto ids = document.getTimingPointIds();
    std::vector<Retiming::Edit> edits;
    for(const uSz row : this->summarize().rows) {
        if(which != Which::ALL && (which == Which::UNINHERITED) != points[row].uninherited) continue;
        edit(edits.emplace_back(ids[row], points[row]).point);
    }
    Retiming::apply(c, document, edits, this->state.carriesObjects());
}

template <typename F>
void TimingPanel::retime(std::span<const Document::Id> targets, Which which, u64 &gesture, F &&edit) {
    const Document &document = *this->state.document;
    const auto points = document.getTimingPoints();
    const auto ids = document.getTimingPointIds();
    const Hash::flat::set<Document::Id> wanted{targets.begin(), targets.end()};
    std::vector<Retiming::Edit> edits;
    for(uSz row = 0; row < points.size(); row++) {
        if(which != Which::ALL && (which == Which::UNINHERITED) != points[row].uninherited) continue;
        if(wanted.contains(ids[row])) edit(edits.emplace_back(ids[row], points[row]).point);
    }
    if(edits.empty()) return;
    if(gesture == 0) gesture = this->state.newGesture();
    // what the run did so far is taken back first: Retiming::apply() carries from where things were before it
    this->state.drag(
        Step::TIMING, [&](Change &c) { Retiming::apply(c, document, edits, this->state.carriesObjects()); }, gesture);
}

void TimingPanel::nudgeTempo(f64 by) {
    by *= keyboard->isShiftDown() ? 1.0 : keyboard->isControlDown() ? 0.01 : 0.1;
    const auto ids = this->state.document->getTimingPointIds();
    std::vector<Document::Id> targets;
    for(const uSz row : this->summarize().rows) targets.push_back(ids[row]);
    this->retime(targets, Which::UNINHERITED, this->tempoGesture, [by](Point &point) {
        point.beatLength = Retiming::beatLengthOf(std::clamp(rounded(bpmOf(point) + by, 1000.0), 1.0, 10000.0));
    });
}

void TimingPanel::nudgeTime(f64 by) {
    if(keyboard->isShiftDown()) by *= 10.0;
    const auto ids = this->state.document->getTimingPointIds();
    std::vector<Document::Id> targets;
    for(const uSz row : this->summarize().rows) targets.push_back(ids[row]);
    this->retime(targets, Which::ALL, this->timeGesture, [by](Point &point) { point.time += by; });
}

bool TimingPanel::isTapping() const {
    const i32 time = this->state.clock.getTime();
    return !this->taps.empty() && time >= this->taps.back() && time - this->taps.back() <= TAP_PAUSE_MS;
}

void TimingPanel::tap() {
    Clock &clock = this->state.clock;
    // a tap that starts the music starts a run: the time stood still since the last one
    if(!clock.isPlaying() || !this->isTapping()) {
        this->taps.clear();
        this->tapGesture = 0;
        this->tapTargets.clear();
        const auto points = this->state.document->getTimingPoints();
        const auto ids = this->state.document->getTimingPointIds();
        for(const uSz row : this->summarize().rows) {
            if(points[row].uninherited) this->tapTargets.push_back(ids[row]);
        }
        if(this->tapTargets.empty()) {
            if(const auto row = this->rowInForce(true)) this->tapTargets.push_back(ids[*row]);
        }
    }
    if(!clock.isPlaying()) clock.play();
    this->taps.push_back(clock.getTime());
    if(const auto bpm = Retiming::tappedBpm(this->taps)) {
        this->retime(this->tapTargets, Which::UNINHERITED, this->tapGesture,
                     [&](Point &point) { point.beatLength = Retiming::beatLengthOf(*bpm); });
    }
}

std::optional<uSz> TimingPanel::rowInForce(bool uninherited) const {
    if(!this->state.document) return std::nullopt;
    const auto points = this->state.document->getTimingPoints();
    const f64 time = this->state.clock.getTime();
    for(uSz row = Mc::ranges::upper_bound(points, time, {}, &Point::time) - points.begin(); row > 0; row--) {
        if(!uninherited || points[row - 1].uninherited) return row - 1;
    }
    const auto first =
        Mc::ranges::find_if(points, [&](const Point &point) { return !uninherited || point.uninherited; });
    return first != points.end() ? std::optional{(uSz)(first - points.begin())} : std::nullopt;
}

void TimingPanel::selectionChanged() {
    this->summarized.reset();
    // a run belongs to one selection
    this->tempoGesture = 0;
    this->timeGesture = 0;
    this->taps.clear();
}

void TimingPanel::select(Document::Id id) {
    const u64 key = id;
    this->list->setSelection(std::span{&key, 1});
    this->selectionChanged();
    const auto ids = this->state.document->getTimingPointIds();
    if(const auto at = Mc::ranges::find(ids, id); at != ids.end()) this->list->scrollTo((uSz)(at - ids.begin()));
}

void TimingPanel::tick() {
    // a box ticks what it doesn't show too
    if(!this->isVisible()) return;

    const Summary &selected = this->summarize();
    this->inForce = this->rowInForce(false);
    this->inForceUninherited = this->rowInForce(true);
    this->removeButton->setEnabled(!selected.rows.empty());
    this->slower->setEnabled(selected.uninherited);
    this->faster->setEnabled(selected.uninherited);
    this->earlier->setEnabled(!selected.rows.empty());
    this->later->setEnabled(!selected.rows.empty());
    // how far the step buttons go, which the held keys change
    const bool overTime = this->earlier->isMouseInside() || this->later->isMouseInside();
    if(overTime || this->slower->isMouseInside() || this->faster->isMouseInside()) {
        TooltipOverlay *tooltip = ui->getTooltipOverlay();
        tooltip->begin();
        tooltip->addLine(overTime ? _("1 ms, with Shift 10 ms") : _("0.1 BPM, with Shift 1 BPM, with Ctrl 0.01 BPM"));
        tooltip->end();
    }

    const bool tapping = this->isTapping();
    if(this->state.document && !tapping && (!this->state.metronome || this->state.metronome->getBool())) {
        Metronome::play(this->state.clock, *this->state.document);
    }
    if(tapping) {
        const auto bpm = Retiming::tappedBpm(this->taps);
        std::string said =
            bpm ? tformat("Taps: {} ({} BPM)", this->taps.size(), *bpm) : tformat("Taps: {}", this->taps.size());
        if(this->tapped->getText() != said) this->tapped->setText(std::move(said));
    } else if(const char *said = _("Tap along to set the tempo (T)"); this->tapped->getText() != said) {
        this->tapped->setText(said);
    }
    CBaseUIBox::tick();
}

void TimingPanel::onKeyDown(KeyboardEvent &e) {
    CBaseUIBox::onKeyDown(e);
    if(e.isConsumed() || !this->isVisible() || e != KEY_TAB) return;
    Form *const forms[]{this->form};
    if(Form::visitNext(forms, e.getModifiers() & KEYMOD_SHIFT)) e.consume();
}

void TimingPanel::commit() { this->form->commit(); }

void TimingPanel::clear() {
    this->list->setSelection({});
    this->selectionChanged();
}

bool TimingPanel::hasSelection() { return !this->summarize().rows.empty(); }

void TimingPanel::selectInForce() {
    if(this->hasSelection()) return;
    if(const auto row = this->rowInForce(false)) this->select(this->state.document->getTimingPointIds()[*row]);
}

void TimingPanel::visitVelocity() {
    // the field as it is for the selection now
    this->form->refresh();
    this->velocity->visit();
}

bool TimingPanel::addPoint(bool uninherited) {
    Document &document = *this->state.document;
    const auto points = document.getTimingPoints();
    const auto ids = document.getTimingPointIds();
    const f64 time = this->state.clock.getTime();
    const Point *inForce = nullptr;  // the last point at or before the time
    const Point *section = nullptr;  // the last uninherited one
    for(uSz i = 0; i < points.size() && points[i].time <= time; i++) {
        if(points[i].time == time && points[i].uninherited == uninherited) {
            this->select(ids[i]);
            return true;
        }
        inForce = &points[i];
        if(points[i].uninherited) section = &points[i];
    }
    if(!uninherited && !section) {
        notify(_("An inherited point needs a timing point at or before it."));
        return false;
    }

    // what is in force there, so that the point changes nothing before it's edited (before the first point: what that
    // one has; without any: 120 BPM). an uninherited point ends the slider velocity an inherited one before it set.
    // an inherited one at its time keeps it, unless one is there already
    const auto first = Mc::ranges::find(points, true, &Point::uninherited);
    const Point *tempo = section ? section : first != points.end() ? &*first : nullptr;
    const Point *sounds = inForce ? inForce : points.empty() ? nullptr : &points.front();
    Point point;
    point.time = time;
    point.uninherited = uninherited;
    point.beatLength = uninherited            ? (tempo ? tempo->beatLength : 500.0)
                       : inForce->uninherited ? -100.0
                                              : inForce->beatLength;
    point.meter = tempo ? tempo->meter : 4;
    point.sampleSet = sounds ? sounds->sampleSet : 1;
    if(sounds) {
        point.sampleIndex = sounds->sampleIndex;
        point.volume = sounds->volume;
        point.effects = sounds->effects & Point::EFFECT_KIAI;
    }
    std::optional<Point> velocity;
    if(uninherited && inForce && !inForce->uninherited && inForce->beatLength != -100.0 && inForce->time < time) {
        velocity = point;
        velocity->uninherited = false;
        velocity->beatLength = inForce->beatLength;
    }
    Document::Id id{0};
    if(!this->state.edit(Step::ADD_TIMING_POINT, [&](Change &c) {
           id = c.addTimingPoint(point);
           if(velocity) c.addTimingPoint(*velocity);
       })) {
        return false;
    }
    this->select(id);
    return true;
}

void TimingPanel::removePoints(bool inForce, Document::Action action) {
    Document &document = *this->state.document;
    const auto points = document.getTimingPoints();
    const auto ids = document.getTimingPointIds();
    std::vector<uSz> rows;
    if(inForce) {
        if(const auto row = this->rowInForce(false)) rows.push_back(*row);
    } else {
        rows = this->summarize().rows;
    }
    if(rows.empty()) return;

    const auto isUninherited = [&](uSz row) { return points[row].uninherited; };
    if(const sSz going = Mc::ranges::count_if(rows, isUninherited);
       going > 0 && going == Mc::ranges::count(points, true, &Point::uninherited)) {
        notify(_("A map keeps at least one timing point."));
        return;
    }
    std::vector<Document::Id> gone;
    gone.reserve(rows.size());
    for(const uSz row : rows) gone.push_back(ids[row]);
    this->state.edit(action, [&](Change &c) { c.remove(gone); });
    this->clear();
}

void TimingPanel::copyPoints() {
    const auto points = this->state.document->getTimingPoints();
    std::string lines;
    for(const uSz row : this->summarize().rows) lines += neomod::BeatmapFile::format(points[row]) + "\r\n";
    if(!lines.empty()) env->setClipBoardText(std::move(lines));
}

void TimingPanel::cutPoints() {
    this->copyPoints();
    this->removePoints(false, Step::CUT);
}

bool TimingPanel::canPastePoints() { return !clipboardPoints().empty(); }

void TimingPanel::pastePoints() {
    const std::vector<Point> pasted = clipboardPoints();
    if(pasted.empty()) return;
    Document &document = *this->state.document;
    const auto points = document.getTimingPoints();
    const auto ids = document.getTimingPointIds();
    std::vector<Document::Id> replaced;
    for(uSz i = 0; i < points.size(); i++) {
        if(Mc::ranges::any_of(pasted, [&](const Point &p) {
               return p.time == points[i].time && p.uninherited == points[i].uninherited;
           })) {
            replaced.push_back(ids[i]);
        }
    }
    std::vector<u64> added;
    if(!this->state.edit(Step::PASTE, [&](Change &c) {
           c.remove(replaced);
           for(const Point &point : pasted) added.push_back(c.addTimingPoint(point));
       })) {
        return;
    }
    this->list->setSelection(added);
    this->selectionChanged();
}
