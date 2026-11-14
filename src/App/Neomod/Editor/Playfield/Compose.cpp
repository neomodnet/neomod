// Copyright (c) 2026, WH, All rights reserved.
#include "Compose.h"

#include "BeatSnap.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmap.h"
#include "EditorState.h"
#include "Environment.h"
#include "GameRules.h"
#include "Hashing.h"
#include "HitObjects.h"
#include "Skin.h"
#include "SliderCurves.h"
#include "SliderPath.h"
#include "TimeDisplay.h"

#include "fmt/format.h"
#include "fmt/ranges.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <tuple>
#include <numeric>
#include <utility>
#include <variant>

using namespace neomod;
using Action = Document::Action;
using HO = BeatmapFile::HitObject;

namespace {
constexpr vec2 FIELD{GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT};
constexpr u8 COLOUR_SKIP_BITS{0b111 << HO::TYPE_COLOUR_SKIP_SHIFT};
// how long before its start a spinner can be clicked
constexpr i32 SPINNER_SHOWN_BEFORE_MS{400};
// how far (in circle radii) a placed or dragged head stacks onto another, with distance snap and without
constexpr f32 STACK_REACH{0.6f};
constexpr f32 STACK_REACH_CLOSE{0.15f};
// how far the cursor goes (osu!px) before a drag stacked onto the object before lets go of it
constexpr f32 STACK_RELEASE{8.f};
// what the readout shows the spacing for at most
constexpr i32 SPACING_SHOWN_MS{3000};

bool inField(vec2 p) { return p.x >= 0.f && p.y >= 0.f && p.x <= FIELD.x && p.y <= FIELD.y; }

bool startsCombo(const HO &object) { return object.type & HO::TYPE_NEW_COMBO; }

// `by` ms later (spinner ends too)
void shift(HO &object, i32 by) {
    object.time += by;
    if(object.kind == HO::Kind::SPINNER) object.endTime += by;
}

// among the positions it's given, the one nearest to p within `within`
struct Nearest {
    vec2 p;
    f32 within;
    std::optional<vec2> found{};
    void operator()(vec2 at) {
        if(const f32 d = vec::distance(at, this->p); d < this->within) {
            this->found = at;
            this->within = d;
        }
    }
};

// how far the map draws the object of this line from where its points are (its stack moved it)
vec2 stackOffsetOf(const MapObjects &objects, Document::Id id) {
    const HitObject *object = objects.find(id);
    return object ? object->getRawPosAt(object->getClickTime()) - object->getOriginalRawPosAt(object->getClickTime())
                  : vec2{0.f};
}

// when the object edges are: its start, the slider repeats, its end (if it lasts)
std::vector<i32> edgeTimes(const HitObject &object) {
    std::vector<i32> times{object.getClickTime()};
    if(object.isSlider()) {
        for(const auto &click : static_cast<const Slider &>(object).getClicks()) {
            if(click.type == 0) times.push_back(click.timeMS);
        }
    }
    if(object.getDuration() > 0) times.push_back(object.getEndTime());
    return times;
}

// whether pos is within `radius` from the slider body (along its first span, as finely as a few points per radius)
bool onBody(const Slider &slider, vec2 pos, f32 radius) {
    const f32 span = (f32)slider.getDuration() / (f32)std::max(slider.getRepeat(), 1);
    const i32 samples = std::clamp((i32)(slider.getPixelLength() / std::max(radius / 2.f, 1.f)), 2, 1000);
    for(i32 i = 0; i <= samples; i++) {
        const i32 at = slider.getClickTime() + (i32)(span * (f32)i / (f32)samples);
        if(vec::distance(slider.getRawPosAt(at), pos) <= radius) return true;
    }
    return false;
}
}  // namespace

Compose::Compose(EditorState &state) : state(state) {}

Compose::~Compose() = default;

void Compose::beginDrag(Drag &drag, Action action) {
    assert(!this->dragging() && !this->sliderPlacement && !this->spinner && !this->previewing &&
           "a drag pressed while another goes on");
    drag.gesture = this->state.newGesture();
    drag.action = action;
    drag.revision = this->state.document->getRevision();
}

template <typename F>
bool Compose::dragStep(Drag &drag, F &&fn) {
    const Document &document = *this->state.document;
    if(document.getRevision() != drag.revision) return false;
    this->state.drag(drag.action, std::forward<F>(fn), drag.gesture);
    // moved in place or in time, the selection sounds are as they were; the rules may write new combos
    if(drag.action == Action::MOVE && this->cachedSummary.revision == drag.revision) {
        this->cachedSummary.revision = document.getRevision();
    }
    drag.revision = document.getRevision();
    return true;
}

const Compose::Drag *Compose::dragging() const {
    if(this->moving) return &*this->moving;
    if(this->timeMoving) return &*this->timeMoving;
    if(this->endDrag) return &*this->endDrag;
    if(this->breakDrag) return &*this->breakDrag;
    if(this->pointDrag) return &*this->pointDrag;
    return nullptr;
}

template <typename F>
void Compose::mapSelection(Document::Change &c, const F &map, bool reshape) const {
    for(const Document::Id id : this->selection) {
        auto object = this->state.document->getObject(id);
        if(!object || object->kind == HO::Kind::SPINNER || object->kind == HO::Kind::NONE) continue;
        const vec2 head = map(vec2{object->x, object->y});
        object->x = head.x;
        object->y = head.y;
        for(vec2 &point : object->curvePoints) point = map(point);
        if(reshape && object->kind == HO::Kind::SLIDER) {
            const std::vector<vec2> points = SliderPath::pointsOf(*object);
            if(auto snapped = this->reshaped(*object, points, (SLIDERCURVETYPE)object->curveType)) {
                object = std::move(snapped);
            } else if(const f64 tick = this->tickLengthAt(object->time); tick > 0.0) {
                object->length = SliderPath::snappedLength(tick, tick);
            }
        }
        c.setObject(id, *object);
    }
}

i32 Compose::selectCombos(i32 timeMS, std::span<const i32> numbers) {
    const MapObjects &objects = *this->state.objects;
    const uSz first = objects.firstFrom(timeMS);
    std::vector<Document::Id> picked;
    uSz from = first;
    for(const i32 number : numbers) {
        for(uSz i = from; i < objects.objects.size(); i++) {
            const HitObject &object = *objects.objects[i];
            if((object.isSpinner() ? 1 : object.getComboNumber()) != number) continue;
            picked.push_back(objects.ids[i]);
            from = i + 1;
            break;
        }
    }
    this->select(std::move(picked));
    return first < objects.objects.size() ? objects.objects[first]->getClickTime() : timeMS;
}

void Compose::setTool(Tool tool) {
    if(tool == this->tool) return;
    this->endPlacing();
    if(this->tool == Tool::SELECT) this->clearSelection();
    this->tool = tool;
}

void Compose::reset() {
    this->selection.clear();
    this->selectionChanged();
    this->brush = {};
    this->endGesture();
    this->spinner.reset();
    this->sliderPlacement.reset();
    this->previewing.reset();
    this->nudgeGesture = 0;
}

bool Compose::isSelected(Document::Id id) const {
    return std::binary_search(this->selection.begin(), this->selection.end(), id);
}

void Compose::select(std::vector<Document::Id> ids) {
    Mc::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if(ids == this->selection) return;
    this->selection = std::move(ids);
    this->selectionChanged();
    if(!this->selection.empty() && this->tool != Tool::SELECT) {
        if(this->sliderPlacement) this->finishSlider();
        if(this->spinner) this->finishSpinner();
        this->tool = Tool::SELECT;
    }
}

void Compose::select(std::vector<Document::Id> ids, std::span<const Document::Id> kept) {
    const Hash::flat::set<Document::Id> in{ids.begin(), ids.end()};
    for(const Document::Id id : kept) {
        if(!in.contains(id)) ids.push_back(id);
    }
    this->select(std::move(ids));
}

void Compose::toggle(Document::Id id) {
    std::vector<Document::Id> ids = this->selection;
    if(const auto it = Mc::ranges::find(ids, id); it != ids.end()) {
        ids.erase(it);
    } else {
        ids.push_back(id);
    }
    this->select(std::move(ids));
}

void Compose::selectAll() {
    if(this->state.objects) this->select(this->state.objects->ids);
}

void Compose::clearSelection() {
    this->brush = {};
    if(this->selection.empty()) return;
    this->selection.clear();
    this->selectionChanged();
}

void Compose::selectTouched(std::vector<Document::Id> ids) {
    std::erase_if(ids, [&](Document::Id id) { return !this->state.objects->find(id); });
    if(!ids.empty()) this->select(std::move(ids));
}

void Compose::onObjectsRebuilt() {
    const MapObjects &objects = *this->state.objects;
    const uSz before = this->selection.size();
    std::erase_if(this->selection, [&](Document::Id id) { return !objects.find(id); });
    if(this->selection.size() != before) this->selectionChanged();
}

void Compose::selectionChanged() {
    this->selectionVersion++;
    this->picked.reset();
}

std::optional<Compose::Part> Compose::getPicked() const {
    if(!this->picked || !this->state.objects || this->selection.size() != 1) return std::nullopt;
    const HitObject *object = this->state.objects->find(this->selection.front());
    if(!object || !object->isSlider()) return std::nullopt;
    // an edge the slider doesn't have any more is none
    if(this->picked->edge && *this->picked->edge > std::max(static_cast<const Slider &>(*object).getRepeat(), 1)) {
        return std::nullopt;
    }
    return this->picked;
}

void Compose::pick(Part part) {
    if(!this->state.objects || this->selection.size() != 1) return;
    const HitObject *object = this->state.objects->find(this->selection.front());
    if(object && object->isSlider()) this->picked = part;
}

std::optional<Compose::Part> Compose::partAt(vec2 pos) const {
    if(!this->state.objects || this->selection.size() != 1) return std::nullopt;
    const HitObject *object = this->state.objects->find(this->selection.front());
    if(!object || !object->isSlider()) return std::nullopt;
    const f32 radius = this->state.objects->view.rawHitcircleDiameter / 2.f;
    const i32 time = this->state.clock.getTime();
    const std::vector<i32> times = edgeTimes(*object);
    std::optional<uSz> nearest;
    for(uSz i = 0; i < times.size(); i++) {
        if(vec::distance(object->getRawPosAt(times[i]), pos) > radius) continue;
        if(!nearest || std::abs(times[i] - time) < std::abs(times[*nearest] - time)) nearest = i;
    }
    if(nearest) return Part{.edge = (i32)*nearest};
    if(onBody(static_cast<const Slider &>(*object), pos, radius)) return Part{.edge = std::nullopt};
    return std::nullopt;
}

std::optional<vec2> Compose::getPickedEdgePos() const {
    const auto part = this->getPicked();
    if(!part || !part->edge) return std::nullopt;
    const HitObject &object = *this->state.objects->find(this->selection.front());
    const std::vector<i32> times = edgeTimes(object);
    if((uSz)*part->edge >= times.size()) return std::nullopt;
    return object.getRawPosAt(times[(uSz)*part->edge]);
}

std::optional<SliderPath::Edge> Compose::pickedSounds() const {
    const auto part = this->getPicked();
    const SelectedSlider *selected = part ? this->selectedSlider() : nullptr;
    if(!selected) return std::nullopt;
    const HO &slider = selected->slider;
    if(!part->edge) {
        return SliderPath::Edge{
            .sounds = (u8)slider.hitSounds,
            .sets = {.normalSet = slider.sample.normalSet, .additionSet = slider.sample.additionSet}};
    }
    const auto edges = SliderPath::edgesOf(slider);
    return (uSz)*part->edge < edges.size() ? std::optional{edges[(uSz)*part->edge]} : std::nullopt;
}

template <typename F>
bool Compose::changePicked(Action action, F &&change) {
    const auto part = this->getPicked();
    const SelectedSlider *selected = part ? this->selectedSlider() : nullptr;
    if(!selected) return false;
    const Document::Id id = selected->id;
    HO slider = selected->slider;
    // every edge as it plays now, the changed part changed
    std::vector<SliderPath::Edge> edges = SliderPath::edgesOf(slider);
    if(part->edge) {
        if((uSz)*part->edge >= edges.size()) return false;
        SliderPath::Edge &edge = edges[(uSz)*part->edge];
        i32 sounds = edge.sounds;
        change(sounds, edge.sets);
        edge.sounds = (u8)sounds;
    } else {
        HO::EdgeSet sets{.normalSet = slider.sample.normalSet, .additionSet = slider.sample.additionSet};
        change(slider.hitSounds, sets);
        if(sets != HO::EdgeSet{.normalSet = slider.sample.normalSet, .additionSet = slider.sample.additionSet}) {
            slider.sample.normalSet = sets.normalSet;
            slider.sample.additionSet = sets.additionSet;
            if(slider.sample.parts < 2) slider.sample.parts = 5;
        }
    }
    SliderPath::setEdges(slider, edges);
    this->state.edit(action, [&](Document::Change &c) { c.setObject(id, slider); });
    return true;
}

const HitObject *Compose::getEarliestSelected() const {
    if(!this->state.objects || this->selection.empty()) return nullptr;
    const auto earliest = this->summary().earliest;
    return earliest ? this->state.objects->objects[*earliest].get() : nullptr;
}

void Compose::seekToSelection() {
    if(const HitObject *earliest = this->getEarliestSelected()) this->state.clock.seek(earliest->getClickTime());
}

bool Compose::isShown(const HitObject &object) const {
    const i32 time = this->state.clock.getTime();
    if(object.isSpinner()) {
        return time >= object.getClickTime() - SPINNER_SHOWN_BEFORE_MS && time <= object.getEndTime();
    }
    const i32 fadeOutMS = (i32)(GameRules::getFadeOutTime() * 1000.f);
    const i32 after =
        object.isCircle() && !this->state.showsHitAnimations() ? GameRules::EDITOR_TRAIL_TIME_MS : fadeOutMS;
    const i32 before = (i32)this->state.objects->view.approachTimeMS + GameRules::getFadeInTime();
    return time >= object.getClickTime() - before && time <= object.getEndTime() + after;
}

i32 Compose::tailTime(const HitObject &object) {
    if(object.isSlider()) {
        for(const auto &click : static_cast<const Slider &>(object).getClicks()) {
            if(click.type == 0) return click.timeMS;
        }
    }
    return object.getEndTime();
}

std::optional<std::pair<vec2, vec2>> Compose::selectionBounds() const {
    std::optional<std::pair<vec2, vec2>> bounds;
    const MapObjects &objects = *this->state.objects;
    for(const Document::Id id : this->selection) {
        const HitObject *object = objects.find(id);
        if(!object || object->isSpinner()) continue;
        for(const i32 at : {object->getClickTime(), tailTime(*object)}) {
            const vec2 p = object->getOriginalRawPosAt(at);
            bounds = bounds ? std::pair{vec::min(bounds->first, p), vec::max(bounds->second, p)} : std::pair{p, p};
        }
    }
    return bounds;
}

std::pair<vec2, vec2> Compose::fieldRange() const {
    const auto bounds = this->selectionBounds();
    if(!bounds) return {vec2{0.f}, vec2{0.f}};
    return {vec::min(-bounds->first, vec2{0.f}), vec::max(FIELD - bounds->second, vec2{0.f})};
}

std::pair<uSz, uSz> Compose::shownRange() const {
    const MapObjects &objects = *this->state.objects;
    const i32 time = this->state.clock.getTime();
    // the longest any object shows before its start and after its end
    const i32 before = std::max((i32)objects.view.approachTimeMS + GameRules::getFadeInTime(), SPINNER_SHOWN_BEFORE_MS);
    const i32 after = std::max(GameRules::EDITOR_TRAIL_TIME_MS, (i32)(GameRules::getFadeOutTime() * 1000.f));
    return {objects.firstFrom(time - after - objects.longestObjectMS), objects.firstFrom(time + before + 1)};
}

std::optional<Compose::Hit> Compose::objectAt(vec2 pos) const {
    if(!this->state.objects) return std::nullopt;
    const MapObjects &objects = *this->state.objects;
    const f32 radius = objects.view.rawHitcircleDiameter / 2.f;
    const auto reaches = [&](const HitObject &object, f32 within) {
        if(vec::distance(object.getRawPosAt(object.getClickTime()), pos) <= within) return true;
        return object.isSlider() && vec::distance(object.getRawPosAt(tailTime(object)), pos) <= within;
    };
    const auto hit = [&](uSz i) {
        return Hit{.object = objects.objects[i].get(), .id = objects.ids[i], .node = std::nullopt};
    };

    // a selected one first, the earliest: its ring shows however far it is from the time (a spinner has none)
    std::optional<uSz> earliest;
    for(const Document::Id id : this->selection) {
        const auto it = objects.indexes.find(id);
        if(it == objects.indexes.end() || (earliest && *earliest < it->second)) continue;
        const HitObject &object = *objects.objects[it->second];
        if((!object.isSpinner() || this->isShown(object)) && reaches(object, radius)) earliest = it->second;
    }
    if(earliest) return hit(*earliest);

    // then the one nearest in time to the playhead (the earlier of two as near), slider bodies only after that
    const i32 time = this->state.clock.getTime();
    const auto [first, last] = this->shownRange();
    const auto nearest = [&](auto &&isHit) -> std::optional<Hit> {
        std::optional<uSz> best;
        for(uSz i = first; i < last; i++) {
            const HitObject &object = *objects.objects[i];
            if(!this->isShown(object) || !isHit(object)) continue;
            if(!best ||
               std::abs(object.getClickTime() - time) < std::abs(objects.objects[*best]->getClickTime() - time)) {
                best = i;
            }
        }
        return best ? std::optional{hit(*best)} : std::nullopt;
    };
    if(auto found = nearest([&](const HitObject &object) { return reaches(object, radius * 0.8f); })) return found;
    return nearest([&](const HitObject &object) {
        return object.isSlider() && onBody(static_cast<const Slider &>(object), pos, radius);
    });
}

std::optional<Compose::Hit> Compose::objectAtTime(f64 timeMS, f64 withinMS) const {
    if(!this->state.objects) return std::nullopt;
    const MapObjects &objects = *this->state.objects;
    // the object circle nearest to the time (the earlier of two as near): which one, and how far
    struct Circle {
        i32 node;
        f64 distance;
    };
    const auto nearestCircle = [&](const HitObject &object) {
        const std::vector<i32> times = edgeTimes(object);
        Circle nearest{.node = 0, .distance = std::abs(times[0] - timeMS)};
        for(uSz i = 1; i < times.size(); i++) {
            if(const f64 d = std::abs(times[i] - timeMS); d < nearest.distance) {
                nearest = {.node = (i32)i, .distance = d};
            }
        }
        return nearest;
    };
    // from the first object that can reach the time
    const uSz first = objects.firstFrom((i32)std::floor(timeMS - withinMS) - objects.longestObjectMS);
    std::optional<uSz> best;
    Circle bestCircle{};
    bool bestSelected = false;
    for(uSz i = first; i < objects.objects.size(); i++) {
        const HitObject &object = *objects.objects[i];
        if(object.getClickTime() > timeMS + withinMS) break;
        const Circle circle = nearestCircle(object);
        if(circle.distance > withinMS) continue;
        // a selected one wins over the others
        const bool selected = this->isSelected(objects.ids[i]);
        if(!best || (selected && !bestSelected) ||
           (selected == bestSelected && circle.distance < bestCircle.distance)) {
            best = i;
            bestCircle = circle;
            bestSelected = selected;
        }
    }
    if(best) return Hit{.object = objects.objects[*best].get(), .id = objects.ids[*best], .node = bestCircle.node};
    // what spans it, the latest starting
    std::optional<uSz> spanning;
    for(uSz i = first; i < objects.objects.size(); i++) {
        const HitObject &object = *objects.objects[i];
        if(object.getClickTime() > timeMS) break;
        if(timeMS < object.getEndTime()) spanning = i;
    }
    if(!spanning) return std::nullopt;
    return Hit{.object = objects.objects[*spanning].get(), .id = objects.ids[*spanning], .node = std::nullopt};
}

std::vector<Document::Id> Compose::objectsIn(const McRect &area) const {
    std::vector<Document::Id> ids;
    if(!this->state.objects) return ids;
    const MapObjects &objects = *this->state.objects;
    const auto [first, last] = this->shownRange();
    for(uSz i = first; i < last; i++) {
        const HitObject &object = *objects.objects[i];
        if(this->isShown(object) && area.contains(object.getRawPosAt(object.getClickTime()))) {
            ids.push_back(objects.ids[i]);
        }
    }
    return ids;
}

std::vector<Document::Id> Compose::objectsBetween(f64 fromMS, f64 toMS) const {
    std::vector<Document::Id> ids;
    if(!this->state.objects) return ids;
    const f64 from = std::min(fromMS, toMS);
    const f64 to = std::max(fromMS, toMS);
    const MapObjects &objects = *this->state.objects;
    for(uSz i = objects.firstFrom((i32)std::floor(from) - objects.longestObjectMS); i < objects.objects.size(); i++) {
        const HitObject &object = *objects.objects[i];
        if(object.getClickTime() > to) break;
        if(object.getClickTime() >= from || (object.getEndTime() >= from && object.getEndTime() <= to)) {
            ids.push_back(objects.ids[i]);
        }
    }
    return ids;
}

i32 Compose::snappedNow() const {
    const i32 time = this->state.clock.getTime();
    return this->state.document->getBeatSnap().snap(time, this->state.divisor, time);
}

f64 Compose::velocityAt(i32 timeMS) const {
    const auto &c = this->state.document->getPrimitives();
    if(c.timingpoints.empty()) return 0.0;
    const f32 beatLength = c.timingpoints.getTimingInfo(timeMS).beatLength;
    return beatLength > 0.f ? 100.0 * c.sliderMultiplier / beatLength : 0.0;
}

f64 Compose::tickLengthAt(i32 timeMS) const {
    const Document &document = *this->state.document;
    const auto &c = document.getPrimitives();
    if(c.timingpoints.empty()) return 0.0;
    return 100.0 * document.getSliderMultiplier() /
           (c.timingpoints.getBeatLengthMultiplier(timeMS) * this->state.divisor);
}

const HitObject *Compose::endingBefore(i32 timeMS) const {
    const MapObjects &objects = *this->state.objects;
    const auto ending = Mc::ranges::lower_bound(objects.byEndTime, timeMS, {},
                                                [](const MapObjects::Shown &s) { return s.object->getEndTime(); });
    if(ending == objects.byEndTime.begin()) return nullptr;
    const HitObject *last = std::prev(ending)->object;
    return last->isSpinner() ? nullptr : last;
}

std::optional<Snapping::Reference> Compose::referenceBefore(i32 timeMS) const {
    const HitObject *last = this->endingBefore(timeMS);
    if(!last || !(this->velocityAt(timeMS) > 0.0)) return std::nullopt;
    return Snapping::Reference{.end = last->getOriginalRawPosAt(last->getEndTime()), .endTime = last->getEndTime()};
}

template <typename F>
void Compose::forStackTargets(bool unselected, std::optional<vec2> except, F &&fn) const {
    const MapObjects &objects = *this->state.objects;
    const auto [first, last] = this->shownRange();
    for(uSz i = first; i < last; i++) {
        const HitObject &object = *objects.objects[i];
        if(object.isSpinner() || (unselected && this->isSelected(objects.ids[i])) || !this->isShown(object)) continue;
        if(const vec2 head = object.getOriginalRawPosAt(object.getClickTime()); head != except) fn(head);
        if(const vec2 end = object.getOriginalRawPosAt(object.getEndTime()); object.isSlider() && end != except) {
            fn(end);
        }
    }
}

std::optional<f32> Compose::spacing(vec2 from, i32 fromMS, vec2 to, i32 toMS, i32 velocityAtMS) const {
    const i32 elapsed = toMS - fromMS;
    const f64 velocity = this->velocityAt(velocityAtMS);
    if(elapsed <= 0 || elapsed > SPACING_SHOWN_MS || !(velocity > 0.0)) return std::nullopt;
    return (f32)(vec::distance(from, to) / (velocity * elapsed));
}

std::optional<Document::Id> Compose::objectStartingAt(i32 timeMS) const {
    const MapObjects &objects = *this->state.objects;
    const uSz i = objects.firstFrom(timeMS);
    if(i == objects.objects.size() || objects.objects[i]->getClickTime() != timeMS) return std::nullopt;
    return objects.ids[i];
}

const HitObject *Compose::objectBefore(i32 timeMS) const {
    const MapObjects &objects = *this->state.objects;
    const uSz i = objects.firstFrom(timeMS);
    return i > 0 ? objects.objects[i - 1].get() : nullptr;
}

std::optional<Compose::Placement> Compose::placement(vec2 cursor, const Snapping::Settings &snap) const {
    if(!this->state.objects) return std::nullopt;
    return this->headAt(cursor, snap, this->snappedNow(), false);
}

std::optional<Compose::Placement> Compose::headAt(vec2 cursor, const Snapping::Settings &snap, i32 timeMS,
                                                  bool unselected) const {
    if(!inField(cursor)) return std::nullopt;
    const f32 radius = this->state.objects->view.rawHitcircleDiameter / 2.f;
    Placement at{.pos = cursor, .time = timeMS, .guide = {}};
    Nearest onto{.p = cursor, .within = radius * (snap.distance ? STACK_REACH : STACK_REACH_CLOSE)};
    this->forStackTargets(unselected, std::nullopt, onto);
    if(onto.found) {
        at.pos = *onto.found;
    } else {
        const auto reference = snap.distance ? this->referenceBefore(at.time) : std::nullopt;
        if(reference) {
            if(const auto spaced =
                   Snapping::distance(*reference, cursor, at.time, this->velocityAt(at.time), snap.spacing)) {
                at.pos = *spaced;
            }
        }
        if(const auto angled = reference && snap.angle ? Snapping::angle(reference->end, at.pos) : std::nullopt) {
            at.pos = *angled;
            at.guide = Guide{reference->end, reference->end + (*angled - reference->end) * 2.f};
        } else if(snap.grid) {
            at.pos = Snapping::grid(at.pos, snap.gridSize);
        }
    }
    at.pos = {std::round(at.pos.x), std::round(at.pos.y)};
    if(!inField(at.pos)) return std::nullopt;
    return at;
}

std::optional<Compose::Preview> Compose::previewCircle(vec2 cursor, const Snapping::Settings &snap) const {
    // a slider under a tick, which the map doesn't have yet, shows its head
    std::optional<Placement> at;
    if(const auto &slider = this->sliderPlacement) {
        if(!slider->placed) at = Placement{.pos = slider->points.front(), .time = slider->start, .guide = {}};
    } else if(this->tool == Tool::CIRCLE || this->tool == Tool::SLIDER) {
        at = this->placement(cursor, snap);
    }
    if(!at) return std::nullopt;

    // the combo it would join or start, from the object before its time (the game starts one after a spinner or a
    // break, one that ended after that object started)
    const HitObject *before = this->objectBefore(at->time);
    if(!before) return Preview{.at = *at, .number = 1, .colorCounter = 1, .colorOffset = 0};
    const auto breaks = this->state.document->getBreaks();
    const bool newCombo = this->brush.newCombo || before->isSpinner() || Mc::ranges::any_of(breaks, [&](const auto &b) {
                              return b.end >= before->getClickTime() && b.end < at->time;
                          });
    return Preview{.at = *at,
                   .number = newCombo ? 1 : before->getComboNumber() + 1,
                   .colorCounter = before->getColorCounter() + (newCombo ? 1 : 0),
                   .colorOffset = before->getColorOffset()};
}

std::optional<Compose::Spot> Compose::spot(vec2 cursor, const Snapping::Settings &snap) const {
    if(!this->state.objects) return std::nullopt;
    Spot spot{};
    if(const HitObject *earliest = this->getEarliestSelected()) {
        spot.pos = earliest->getOriginalRawPosAt(earliest->getClickTime());
        spot.time = earliest->getClickTime();
        spot.selected = true;
    } else if(const auto at = this->tool == Tool::CIRCLE || (this->tool == Tool::SLIDER && !this->sliderPlacement)
                                  ? this->placement(cursor, snap)
                                  : std::nullopt) {
        spot.pos = at->pos;
        spot.time = at->time;
    } else if(this->tool == Tool::SELECT && inField(cursor)) {
        // no object is there to space
        spot.pos = {std::round(cursor.x), std::round(cursor.y)};
        spot.time = this->snappedNow();
        return spot;
    } else {
        return std::nullopt;
    }

    // the last object ending before the time, the first starting after it
    const MapObjects &objects = *this->state.objects;
    if(const HitObject *previous = this->endingBefore(spot.time)) {
        spot.previous = this->spacing(previous->getOriginalRawPosAt(previous->getEndTime()), previous->getEndTime(),
                                      spot.pos, spot.time, spot.time);
    }
    if(const uSz starting = objects.firstFrom(spot.time + 1);
       starting != objects.objects.size() && !objects.objects[starting]->isSpinner()) {
        const HitObject &next = *objects.objects[starting];
        spot.next = this->spacing(spot.pos, spot.time, next.getOriginalRawPosAt(next.getClickTime()),
                                  next.getClickTime(), next.getClickTime());
    }
    return spot;
}

std::optional<Compose::SnapRings> Compose::snapRings(const Snapping::Settings &snap) const {
    if(!snap.distance || !this->state.objects) return std::nullopt;
    i32 time;
    if(this->moving) {
        time = this->moving->time;
    } else if(this->tool == Tool::CIRCLE || (this->tool == Tool::SLIDER && !this->sliderPlacement)) {
        time = this->snappedNow();
    } else {
        return std::nullopt;
    }
    const auto reference = this->referenceBefore(time);
    if(!reference) return std::nullopt;
    const f64 perMS = snap.spacing * this->velocityAt(time);
    return SnapRings{
        .center = reference->end,
        .radius = (f32)(perMS * (time - reference->endTime)),
        .tick = (f32)(perMS * this->state.document->getBeatSnap().beatLengthAt(time) / this->state.divisor)};
}

std::optional<f32> Compose::currentSpacing() const {
    if(!this->state.objects) return std::nullopt;
    const MapObjects &objects = *this->state.objects;
    const HitObject *to = this->getEarliestSelected();
    if(!to) {
        const uSz starting = objects.firstFrom(this->state.clock.getTime());
        if(starting == objects.objects.size()) return std::nullopt;
        to = objects.objects[starting].get();
    }
    const HitObject *from = to->isSpinner() ? nullptr : this->endingBefore(to->getClickTime());
    if(!from) return std::nullopt;
    return this->spacing(from->getOriginalRawPosAt(from->getEndTime()), from->getEndTime(),
                         to->getOriginalRawPosAt(to->getClickTime()), to->getClickTime(), to->getClickTime());
}

void Compose::placeCircle(vec2 cursor, const Snapping::Settings &snap) {
    const auto at = this->placement(cursor, snap);
    if(!at) return;

    const HO circle = this->brushed(at->pos, at->time, this->brush.newCombo);
    // an object at exactly that time is replaced
    const std::optional<Document::Id> replaced = this->objectStartingAt(at->time);
    this->state.edit(Action::ADD_CIRCLE, [&](Document::Change &c) {
        if(replaced) c.remove(*replaced);
        c.addObject(circle);
    });
    this->brush.newCombo = false;
}

void Compose::startSlider(vec2 cursor, const Snapping::Settings &snap) {
    if(!this->state.objects || this->sliderPlacement) return;
    const auto at = this->placement(cursor, snap);
    if(!at) return;
    SliderPlacement placement;
    this->beginDrag(placement, Action::ADD_SLIDER);
    placement.start = at->time;
    placement.points = {at->pos};
    placement.replaced = this->objectStartingAt(at->time);
    this->sliderPlacement = std::move(placement);
}

void Compose::aimSlider(std::optional<vec2> cursor, const Snapping::Settings &snap) {
    if(!this->sliderPlacement) return;
    SliderPlacement &placement = *this->sliderPlacement;
    // the grid and whole osu!px; none on the last point kept, where a click makes a red anchor
    placement.aim.reset();
    if(cursor) {
        const vec2 at = vec::round(snap.grid ? Snapping::grid(*cursor, snap.gridSize) : *cursor);
        if(at != placement.points.back()) placement.aim = at;
    }
    this->restateSlider();
}

void Compose::addSliderPoint() {
    if(!this->sliderPlacement) return;
    SliderPlacement &placement = *this->sliderPlacement;
    std::vector<vec2> &points = placement.points;
    if(placement.aim) {
        points.push_back(*std::exchange(placement.aim, std::nullopt));
    } else if(points.size() >= 2 && points.back() != points[points.size() - 2]) {
        points.push_back(points.back());
    }
    this->restateSlider();
}

void Compose::restateSlider() {
    SliderPlacement &placement = *this->sliderPlacement;
    std::vector<vec2> points = placement.points;
    if(placement.aim) points.push_back(*placement.aim);

    std::optional<HO> slider;
    const auto type = SliderPath::curveTypeFor(points);
    const f64 length =
        SliderPath::snappedLength(SliderPath::curveLength(type, points), this->tickLengthAt(placement.start));
    if(length > 0.0) {
        slider.emplace();
        slider->kind = HO::Kind::SLIDER;
        SliderPath::setPoints(*slider, points);
        slider->time = placement.start;
        slider->type = HO::TYPE_SLIDER | (this->brush.newCombo ? HO::TYPE_NEW_COMBO : 0);
        slider->hitSounds = this->brush.hitSounds;
        slider->curveType = (char)type;
        slider->slides = 1;
        slider->length = length;
        // sample sets on the edges and the sample written together, none without sets
        slider->sample.parts = 0;
        if(this->brush.normalSet != 0 || this->brush.additionSet != 0) {
            slider->edgeSounds.assign(2, this->brush.hitSounds);
            slider->edgeSets.assign(2, {.normalSet = this->brush.normalSet, .additionSet = this->brush.additionSet});
            slider->sample = {
                .normalSet = this->brush.normalSet, .additionSet = this->brush.additionSet, .filename = {}, .parts = 5};
        }
    }
    if(slider == placement.placed) return;
    placement.placed = slider;
    const bool going = this->dragStep(placement, [&](Document::Change &c) {
        if(!slider) return;
        if(placement.replaced) c.remove(*placement.replaced);
        placement.line = c.addObject(*slider);
    });
    if(!going) this->sliderPlacement.reset();
}

void Compose::finishSlider() {
    if(!this->sliderPlacement) return;
    if(this->sliderPlacement->placed) this->brush.newCombo = false;
    this->sliderPlacement.reset();
}

void Compose::cancelSlider() {
    if(!this->sliderPlacement) return;
    this->dragStep(*this->sliderPlacement, [](Document::Change &) {});
    this->sliderPlacement.reset();
}

void Compose::takeBackSliderPoint() {
    if(!this->sliderPlacement) return;
    if(this->sliderPlacement->points.size() < 2) {
        this->cancelSlider();
        return;
    }
    this->sliderPlacement->points.pop_back();
    this->restateSlider();
}

std::optional<Compose::ControlPoints> Compose::controlPoints() const {
    if(!this->state.objects) return std::nullopt;
    if(const auto &placement = this->sliderPlacement) {
        return ControlPoints{
            .points = placement->points,
            .aim = placement->aim,
            .offset = placement->placed ? stackOffsetOf(*this->state.objects, placement->line) : vec2{0.f}};
    }
    const SelectedSlider *selected = this->selectedSlider();
    if(!selected) return std::nullopt;
    return ControlPoints{.points = selected->points, .aim = std::nullopt, .offset = selected->offset};
}

const Compose::SelectedSlider *Compose::selectedSlider() const {
    if(!this->state.objects || this->selection.size() != 1) return nullptr;
    Summary &s = this->cachedSummary;
    if(s.selectionVersion != this->selectionVersion) s = {.selectionVersion = this->selectionVersion};
    if(const u32 revision = this->state.document->getRevision(); s.sliderRevision != revision) {
        s.sliderRevision = revision;
        s.slider.reset();
        const Document::Id id = this->selection.front();
        if(auto slider = this->state.document->getObject(id); slider && slider->kind == HO::Kind::SLIDER) {
            std::vector<vec2> points = SliderPath::pointsOf(*slider);
            s.slider = SelectedSlider{.id = id,
                                      .slider = std::move(*slider),
                                      .points = std::move(points),
                                      .offset = stackOffsetOf(*this->state.objects, id)};
        }
    }
    return s.slider ? &*s.slider : nullptr;
}

std::optional<HO> Compose::reshaped(const HO &slider, std::span<const vec2> points, SLIDERCURVETYPE type) const {
    const f64 length =
        SliderPath::snappedLength(SliderPath::curveLength(type, points), this->tickLengthAt(slider.time));
    if(!(length > 0.0)) return std::nullopt;
    HO out = slider;
    SliderPath::setPoints(out, points);
    out.curveType = (char)type;
    out.length = length;
    return out;
}

std::optional<uSz> Compose::controlPointAt(vec2 pos, f32 within) const {
    const SelectedSlider *selected = this->selectedSlider();
    if(!selected) return std::nullopt;
    const std::vector<vec2> &points = selected->points;
    std::optional<uSz> nearest;
    f32 nearestDistance = within;
    for(uSz i = 0; i < points.size(); i++) {
        if(const f32 d = vec::distance(points[i] + selected->offset, pos); d < nearestDistance) {
            nearest = i;
            nearestDistance = d;
        }
    }
    return nearest;
}

void Compose::startPointDrag(uSz index) {
    this->pointDrag.reset();
    const SelectedSlider *selected = this->selectedSlider();
    if(!selected) return;
    PointDrag drag;
    this->beginDrag(drag, Action::EDIT_SLIDER);
    drag.id = selected->id;
    drag.slider = selected->slider;
    drag.index = index;
    drag.offset = selected->offset;
    this->pointDrag = std::move(drag);
}

void Compose::dragPoint(vec2 cursor, const Snapping::Settings &snap) {
    if(!this->pointDrag) return;
    PointDrag &drag = *this->pointDrag;
    std::vector<vec2> points = SliderPath::pointsOf(drag.slider);
    const vec2 at = cursor - drag.offset;
    vec2 to;
    if(drag.index == 0 && !drag.added) {
        const auto head = this->headAt(at, snap, drag.slider.time, true);
        if(!head) return;
        to = head->pos;
    } else {
        to = vec::round(snap.grid ? Snapping::grid(at, snap.gridSize) : at);
    }
    if(drag.added) {
        points.insert(points.begin() + (sSz)drag.index, to);
    } else {
        if(drag.index + 1 < points.size() && points[drag.index + 1] == points[drag.index]) points[drag.index + 1] = to;
        points[drag.index] = to;
    }
    // back where it started, the slider is as it was: its length doesn't snap again
    const auto slider =
        points == SliderPath::pointsOf(drag.slider)
            ? std::optional{drag.slider}
            : this->reshaped(drag.slider, points,
                             SliderPath::typeAfterEdit((SLIDERCURVETYPE)drag.slider.curveType, points, !drag.added));
    if(!slider || slider == drag.applied) return;
    drag.applied = slider;
    const bool going = this->dragStep(drag, [&](Document::Change &c) { c.setObject(drag.id, *slider); });
    if(!going) this->pointDrag.reset();
}

void Compose::addControlPoint(vec2 cursor, const Snapping::Settings &snap) {
    this->pointDrag.reset();
    const SelectedSlider *selected = this->selectedSlider();
    if(!selected) return;
    const vec2 at =
        vec::round(snap.grid ? Snapping::grid(cursor - selected->offset, snap.gridSize) : cursor - selected->offset);
    PointDrag drag;
    this->beginDrag(drag, Action::EDIT_SLIDER);
    drag.id = selected->id;
    drag.slider = selected->slider;
    drag.index = SliderPath::insertionIndex(selected->points, at);
    drag.added = true;
    drag.offset = selected->offset;
    this->pointDrag = std::move(drag);
    this->dragPoint(cursor, snap);
}

void Compose::makeRedAnchor(uSz index) {
    const SelectedSlider *selected = this->selectedSlider();
    if(!selected) return;
    std::vector<vec2> points = selected->points;
    // an inner point that isn't one already
    if(index == 0 || index + 1 >= points.size() || points[index - 1] == points[index] ||
       points[index + 1] == points[index]) {
        return;
    }
    points.insert(points.begin() + (sSz)index, points[index]);
    const auto type = SliderPath::typeAfterEdit((SLIDERCURVETYPE)selected->slider.curveType, points, false);
    if(const auto slider = this->reshaped(selected->slider, points, type)) {
        this->state.edit(Action::EDIT_SLIDER,
                         [&, id = selected->id](Document::Change &c) { c.setObject(id, *slider); });
    }
}

void Compose::removeControlPoint(uSz index) {
    const SelectedSlider *selected = this->selectedSlider();
    if(!selected) return;
    std::vector<vec2> points = selected->points;
    if(index >= points.size()) return;
    const bool anchor = (index > 0 && points[index - 1] == points[index]) ||
                        (index + 1 < points.size() && points[index + 1] == points[index]);
    if(!anchor && points.size() <= 2) return;
    points.erase(points.begin() + (sSz)index);
    const auto type = SliderPath::typeAfterEdit((SLIDERCURVETYPE)selected->slider.curveType, points, false);
    if(const auto slider = this->reshaped(selected->slider, points, type)) {
        this->state.edit(Action::EDIT_SLIDER,
                         [&, id = selected->id](Document::Change &c) { c.setObject(id, *slider); });
    }
}

std::optional<SLIDERCURVETYPE> Compose::getCurveType() const {
    const SelectedSlider *selected = this->selectedSlider();
    return selected ? std::optional{(SLIDERCURVETYPE)selected->slider.curveType} : std::nullopt;
}

bool Compose::takesCurveType(SLIDERCURVETYPE type) const {
    const SelectedSlider *selected = this->selectedSlider();
    return selected && (type != SLIDERCURVETYPE::PASSTHROUGH || SliderPath::makesCircle(selected->points));
}

void Compose::setCurveType(SLIDERCURVETYPE type) {
    const SelectedSlider *selected = this->selectedSlider();
    if(!selected || (SLIDERCURVETYPE)selected->slider.curveType == type || !this->takesCurveType(type)) return;
    if(const auto slider = this->reshaped(selected->slider, selected->points, type)) {
        this->state.edit(Action::CURVE_TYPE, [&, id = selected->id](Document::Change &c) { c.setObject(id, *slider); });
    }
}

void Compose::startSpinner() {
    if(!this->state.objects || this->spinner) return;
    const i32 start = this->snappedNow();
    SpinnerPlacement placement;
    this->beginDrag(placement, Action::ADD_SPINNER);
    placement.start = start;
    placement.beat = std::max((i32)this->state.document->getBeatSnap().beatLengthAt(start), 1);
    placement.replaced = this->objectStartingAt(start);
    this->spinner = placement;
    this->updateSpinner(start + placement.beat);
}

void Compose::updateSpinner(i32 end) {
    SpinnerPlacement &placement = *this->spinner;
    placement.end = end;
    HO spinner;
    spinner.kind = HO::Kind::SPINNER;
    spinner.x = FIELD.x / 2.f;
    spinner.y = FIELD.y / 2.f;
    spinner.time = placement.start;
    spinner.type = HO::TYPE_SPINNER | HO::TYPE_NEW_COMBO;
    spinner.hitSounds = this->brush.hitSounds;
    spinner.endTime = end;
    spinner.sample = this->brushedSample();
    const bool going = this->dragStep(placement, [&](Document::Change &c) {
        if(placement.replaced) c.remove(*placement.replaced);
        c.addObject(spinner);
    });
    if(!going) this->spinner.reset();
}

void Compose::finishSpinner() {
    if(!this->spinner) return;
    const i32 end = std::max(this->snappedNow(), this->spinner->start);
    if(end <= this->spinner->start) {
        this->cancelSpinner();
        return;
    }
    if(end != this->spinner->end) this->updateSpinner(end);
    this->spinner.reset();
}

void Compose::cancelSpinner() {
    if(!this->spinner) return;
    // a drag that ends where it started leaves no step
    this->dragStep(*this->spinner, [](Document::Change &) {});
    this->spinner.reset();
}

void Compose::update() {
    if(!this->spinner || !this->state.document) return;
    // its end follows the playhead, a beat after it, once that's after its start
    if(const i32 end = this->snappedNow() + this->spinner->beat;
       end > this->spinner->start && end != this->spinner->end) {
        this->updateSpinner(end);
    }
}

void Compose::rightClick(std::optional<Hit> hit) {
    if(this->sliderPlacement) {
        this->finishSlider();
        return;
    }
    if(this->spinner) {
        this->finishSpinner();
        return;
    }
    if(hit && this->isSelected(hit->id)) {
        this->deleteSelection();
        return;
    }
    if(hit && (!this->state.clock.isPlaying() || this->tool == Tool::SELECT)) {
        this->state.edit(Action::DELETE_OBJECTS, [&](Document::Change &c) { c.remove(hit->id); });
        return;
    }
    if(hit || this->tool == Tool::CIRCLE || this->tool == Tool::SLIDER) this->brush.newCombo = !this->brush.newCombo;
}

bool Compose::removeBreakAt(f64 timeMS) {
    if(!this->state.document) return false;
    const auto breaks = this->state.document->getBreaks();
    const auto it = Mc::ranges::find_if(breaks, [&](const auto &b) { return timeMS >= b.start && timeMS <= b.end; });
    if(it == breaks.end()) return false;
    // one the rules give its gap would come back with the next edit there
    if(!this->state.document->isRequired(*it)) {
        this->state.edit(Action::REMOVE_BREAK, [id = it->id](Document::Change &c) { c.remove(id); });
    }
    return true;
}

bool Compose::startBreakDrag(f64 timeMS, f64 withinMS) {
    this->breakDrag.reset();
    if(!this->state.document) return false;
    BreakDrag nearest;
    f64 distance = withinMS;
    for(const Document::Break &b : this->state.document->getBreaks()) {
        for(const bool end : {false, true}) {
            if(const f64 off = std::abs(timeMS - (f64)(end ? b.end : b.start)); off <= distance) {
                distance = off;
                nearest.id = b.id;
                nearest.end = end;
            }
        }
    }
    if(nearest.id == 0) return false;
    this->beginDrag(nearest, Action::MOVE_BREAK);
    this->breakDrag = nearest;
    return true;
}

void Compose::dragBreak(f64 timeMS, bool snap) {
    if(!this->breakDrag) return;
    BreakDrag &drag = *this->breakDrag;
    const i64 time = snap ? this->state.document->getBeatSnap().snap(timeMS, this->state.divisor, timeMS) : (i64)timeMS;
    if(drag.applied == time) return;
    drag.applied = time;
    const bool going = this->dragStep(drag, [&](Document::Change &c) { c.moveBreakEdge(drag.id, drag.end, time); });
    if(!going) this->breakDrag.reset();
}

bool Compose::insertBreak() {
    if(!this->state.document) return false;
    Document::Id added{0};
    this->state.edit(Action::INSERT_BREAK,
                     [&](Document::Change &c) { added = c.insertBreak(this->state.clock.getTime()); });
    return added != 0;
}

void Compose::resetBreaks() {
    if(!this->state.document) return;
    this->state.edit(Action::RESET_BREAKS, [](Document::Change &c) { c.resetBreaks(); });
}

bool Compose::canCancel() const {
    return this->sliderPlacement || this->spinner || this->dragging() || this->getPicked() ||
           !this->selection.empty() || this->tool != Tool::SELECT;
}

void Compose::cancel() {
    if(this->sliderPlacement) {
        this->cancelSlider();
    } else if(this->spinner) {
        this->cancelSpinner();
    } else if(const Drag *drag = this->dragging()) {
        // a drag that ends where it started leaves no step
        Drag back = *drag;
        this->dragStep(back, [](Document::Change &) {});
        this->endGesture();
    } else if(this->getPicked()) {
        this->picked.reset();
    } else if(!this->selection.empty()) {
        this->clearSelection();
    } else {
        this->setTool(Tool::SELECT);
    }
}

void Compose::deleteSelection(Action action) {
    if(this->selection.empty()) return;
    if(this->state.edit(action, [&](Document::Change &c) { c.remove(this->selection); })) {
        this->clearSelection();
    }
}

std::pair<i32, std::vector<i32>> Compose::timestamp() const {
    std::optional<i32> start;
    std::vector<i32> numbers;
    if(const MapObjects *objects = this->state.objects.get()) {
        for(uSz i = 0; i < objects->objects.size(); i++) {
            if(!this->isSelected(objects->ids[i])) continue;
            const HitObject &object = *objects->objects[i];
            if(!start) start = object.getClickTime();
            // a spinner, which has no number, counts as 1
            numbers.push_back(object.isSpinner() ? 1 : object.getComboNumber());
        }
    }
    return {start.value_or(this->state.clock.getTime()), std::move(numbers)};
}

void Compose::copy() {
    if(!this->state.objects) return;
    const MapObjects &objects = *this->state.objects;
    this->copied.clear();
    for(uSz i = 0; i < objects.objects.size(); i++) {
        if(!this->isSelected(objects.ids[i])) continue;
        if(const auto record = this->state.document->getObject(objects.ids[i])) {
            this->copied.push_back(BeatmapFile::format(*record));
        }
    }
    const auto [start, numbers] = this->timestamp();
    const std::string time = TimeDisplay::format(start);
    env->setClipBoardText(numbers.empty() ? fmt::format("{} - ", time)
                                          : fmt::format("{} ({}) - ", time, fmt::join(numbers, ",")));
}

void Compose::cut() {
    if(this->selection.empty()) return;
    this->copy();
    this->deleteSelection(Action::CUT);
}

Compose::Placed Compose::paste() {
    if(!this->state.objects) return Placed::NOTHING;
    return this->place(Action::PASTE, this->copied, this->snappedNow());
}

Compose::Placed Compose::clone() {
    if(!this->state.objects || this->selection.empty()) return Placed::NOTHING;
    const MapObjects &objects = *this->state.objects;
    std::vector<std::string> lines;
    i32 latest = std::numeric_limits<i32>::min();
    for(uSz i = 0; i < objects.objects.size(); i++) {
        if(!this->isSelected(objects.ids[i])) continue;
        if(const auto record = this->state.document->getObject(objects.ids[i])) {
            lines.push_back(BeatmapFile::format(*record));
        }
        latest = std::max(latest, objects.objects[i]->getEndTime());
    }
    // a beat after (the beat there), snapped
    const BeatSnap &snap = this->state.document->getBeatSnap();
    const f64 after = latest + snap.beatLengthAt(latest);
    return this->place(Action::CLONE, lines, snap.snap(after, this->state.divisor, after));
}

Compose::Placed Compose::place(Action action, std::span<const std::string> lines, i32 timeMS) {
    std::vector<HO> records;
    for(const std::string &line : lines) {
        if(HO record; BeatmapFile::parse(line, record)) records.push_back(std::move(record));
    }
    if(records.empty()) return Placed::NOTHING;

    const i32 by = timeMS - Mc::ranges::min_element(records, {}, &HO::time)->time;
    for(HO &record : records) shift(record, by);
    const auto replaced = this->replacedBy(records);
    if(!replaced) return Placed::PAST_THE_END;
    std::vector<Document::Id> added;
    if(!this->state.edit(action, [&](Document::Change &c) {
           c.remove(*replaced);
           added = c.addObjects(records);
       })) {
        return Placed::NOTHING;
    }
    this->select(std::move(added));
    return Placed::DONE;
}

std::optional<std::vector<Document::Id>> Compose::replacedBy(std::span<const HO> records) {
    const i32 length = (i32)this->state.clock.getLength();
    std::vector<i32> times;
    for(const HO &record : records) {
        if(length > 0 && record.time >= length) return std::nullopt;
        times.push_back(record.time);
    }
    Mc::ranges::sort(times);
    times.erase(std::unique(times.begin(), times.end()), times.end());
    const MapObjects &objects = *this->state.objects;
    std::vector<Document::Id> replaced;
    if(!this->previewing) {
        for(const i32 time : times) {
            for(uSz i = objects.firstFrom(time);
                i < objects.objects.size() && objects.objects[i]->getClickTime() == time; i++) {
                replaced.push_back(objects.ids[i]);
            }
        }
        return replaced;
    }
    // later updates find the objects the earlier ones replaced, which their take-back brings back
    auto &starts = this->previewing->starts;
    if(!starts) {
        starts.emplace();
        starts->reserve(objects.objects.size());
        for(uSz i = 0; i < objects.objects.size(); i++)
            starts->emplace_back(objects.objects[i]->getClickTime(), objects.ids[i]);
    }
    for(const i32 time : times) {
        for(auto it = Mc::ranges::lower_bound(*starts, time, {}, &std::pair<i32, Document::Id>::first);
            it != starts->end() && it->first == time; ++it) {
            replaced.push_back(it->second);
        }
    }
    return replaced;
}

HO Compose::brushed(vec2 pos, i32 time, bool newCombo) const {
    HO circle;
    circle.kind = HO::Kind::CIRCLE;
    circle.x = pos.x;
    circle.y = pos.y;
    circle.time = time;
    circle.type = HO::TYPE_CIRCLE | (newCombo ? HO::TYPE_NEW_COMBO : 0);
    circle.hitSounds = this->brush.hitSounds;
    circle.sample = this->brushedSample();
    return circle;
}

neomod::BeatmapFile::HitSample Compose::brushedSample() const {
    return {.normalSet = this->brush.normalSet,
            .additionSet = this->brush.additionSet,
            .index = this->brush.index,
            .volume = this->brush.volume,
            .filename = this->brush.file,
            .parts = 5};
}

void Compose::startMove() {
    this->moving.reset();
    if(!this->state.objects || this->selection.empty()) return;

    // where the heads and tails are from the earliest one, which leads
    const MapObjects &objects = *this->state.objects;
    Move move;
    for(uSz i = 0; i < objects.objects.size(); i++) {
        const HitObject &object = *objects.objects[i];
        if(object.isSpinner() || !this->isSelected(objects.ids[i])) continue;
        if(move.points.empty()) {
            move.head = object.getOriginalRawPosAt(object.getClickTime());
            move.time = object.getClickTime();
        }
        move.points.push_back(object.getOriginalRawPosAt(object.getClickTime()) - move.head);
        if(object.isSlider()) move.points.push_back(object.getOriginalRawPosAt(tailTime(object)) - move.head);
    }
    if(move.points.empty()) return;  // spinners only
    this->beginDrag(move, Action::MOVE);
    std::tie(move.min, move.max) = this->fieldRange();
    this->moving = std::move(move);
}

void Compose::move(vec2 delta, const Snapping::Settings &snap) {
    if(!this->moving) return;
    Move &move = *this->moving;
    // asked every drag frame
    if(const auto now = std::tuple{delta, snap, this->state.clock.getTime()}; move.made != now) {
        move.made = now;
    } else {
        return;
    }
    const f32 radius = this->state.objects->view.rawHitcircleDiameter / 2.f;
    const vec2 cursor = move.head + delta;
    const auto reference = this->referenceBefore(move.time);
    move.guide.reset();

    // stacking: without distance snap onto the object before, held until the cursor has gone 8 osu!px from where it
    // stacked; any selected head or tail onto one on another object (not on the object before, with distance snap);
    // with distance snap onto the object before
    std::optional<vec2> to;
    if(reference && !snap.distance) {
        if(move.lockedAt && vec::distance(delta, *move.lockedAt) > STACK_RELEASE) move.lockedAt.reset();
        if(!move.lockedAt && vec::distance(cursor, reference->end) < radius * STACK_REACH_CLOSE) move.lockedAt = delta;
        if(move.lockedAt) to = reference->end;
    } else {
        move.lockedAt.reset();
    }
    const std::optional<vec2> previousEnd = reference && snap.distance ? std::optional{reference->end} : std::nullopt;
    std::vector<vec2> targets;
    this->forStackTargets(true, previousEnd, [&](vec2 at) { targets.push_back(at); });
    for(uSz i = 0; !to && i < move.points.size(); i++) {
        Nearest onto{.p = cursor + move.points[i], .within = radius * STACK_REACH_CLOSE};
        for(const vec2 target : targets) onto(target);
        if(onto.found) to = *onto.found - move.points[i];
    }
    if(!to && previousEnd && vec::distance(cursor, *previousEnd) < radius * STACK_REACH) to = previousEnd;
    if(!to) {
        vec2 p = cursor;
        if(previousEnd) {
            if(const auto spaced =
                   Snapping::distance(*reference, cursor, move.time, this->velocityAt(move.time), snap.spacing)) {
                p = *spaced;
            }
        }
        if(const auto angled = previousEnd && snap.angle ? Snapping::angle(*previousEnd, p) : std::nullopt) {
            p = *angled;
            move.guide = Guide{*previousEnd, *previousEnd + (*angled - *previousEnd) * 2.f};
        } else if(snap.grid) {
            p = Snapping::grid(p, snap.gridSize);
        }
        to = p;
    }

    const vec2 by =
        vec::clamp(vec2{std::round(to->x - move.head.x), std::round(to->y - move.head.y)}, move.min, move.max);
    if(by == move.applied) return;
    move.applied = by;
    const bool going = this->dragStep(move, [&](Document::Change &c) {
        if(by != vec2{0.f}) this->mapSelection(c, [by](vec2 p) { return p + by; });
    });
    if(!going) this->moving.reset();
}

void Compose::nudge(vec2 by, const Snapping::Settings &snap) {
    const HitObject *earliest = nullptr;
    const MapObjects *objects = this->state.objects.get();
    for(uSz i = 0; objects && !earliest && i < objects->objects.size(); i++) {
        if(!objects->objects[i]->isSpinner() && this->isSelected(objects->ids[i])) earliest = objects->objects[i].get();
    }
    if(!earliest) return;
    // with the grid, the head of the earliest one to the next grid line that way (and onto the grid the other way)
    if(snap.grid) {
        const vec2 head = earliest->getOriginalRawPosAt(earliest->getClickTime());
        const vec2 to = Snapping::grid(head + by * ((f32)(snap.gridSize / 2) + 0.1f), snap.gridSize);
        by = {std::round(to.x - head.x), std::round(to.y - head.y)};
    }
    const auto [min, max] = this->fieldRange();
    if(by == vec2{0.f} || by != vec::clamp(by, min, max)) return;
    if(this->nudgeGesture == 0) this->nudgeGesture = this->state.newGesture();
    this->state.edit(
        Action::MOVE, [&](Document::Change &c) { this->mapSelection(c, [by](vec2 p) { return p + by; }); },
        this->nudgeGesture);
}

void Compose::endNudge() { this->nudgeGesture = 0; }

void Compose::startTimeMove(Document::Id grabbed) {
    this->timeMoving.reset();
    if(!this->state.objects || !this->isSelected(grabbed)) return;
    const MapObjects &objects = *this->state.objects;
    i32 first = std::numeric_limits<i32>::max();
    i32 last = std::numeric_limits<i32>::min();
    for(const Document::Id id : this->selection) {
        if(const HitObject *object = objects.find(id)) {
            first = std::min(first, object->getClickTime());
            last = std::max(last, object->getClickTime());
        }
    }
    // starts stay within the song
    const i32 length = (i32)this->state.clock.getLength();
    TimeMove move;
    this->beginDrag(move, Action::MOVE);
    move.grabbedTime = objects.find(grabbed)->getClickTime();
    move.min = std::min(-first, 0);
    move.max = length > 0 ? std::max(length - 1 - last, 0) : std::numeric_limits<i32>::max() / 2;
    this->timeMoving = move;
}

void Compose::moveInTime(i32 timeMS) {
    if(!this->timeMoving) return;
    TimeMove &move = *this->timeMoving;
    const i32 by = std::clamp(timeMS - move.grabbedTime, move.min, move.max);
    if(by == move.applied) return;
    move.applied = by;
    Document &document = *this->state.document;
    const bool going = this->dragStep(move, [&](Document::Change &c) {
        if(by == 0) return;
        for(const Document::Id id : this->selection) {
            auto object = document.getObject(id);
            if(!object || object->kind == HO::Kind::NONE) continue;
            shift(*object, by);
            c.setObject(id, *object);
        }
    });
    if(!going) this->timeMoving.reset();
}

bool Compose::startEndDrag(const Hit &hit) {
    this->endDrag.reset();
    const HitObject &object = *hit.object;
    if(object.isCircle() || object.getDuration() <= 0 || !hit.node) return false;
    const i32 spans = object.isSlider() ? std::max(static_cast<const Slider &>(object).getRepeat(), 1) : 1;
    if(*hit.node != spans || (this->isSelected(hit.id) && this->selection.size() > 1)) return false;
    const auto record = this->state.document->getObject(hit.id);
    if(!record) return false;
    this->select({hit.id});
    // its edges may change
    this->picked.reset();
    EndDrag drag;
    this->beginDrag(drag, record->kind == HO::Kind::SLIDER ? Action::REPEATS : Action::SPINNER_END);
    drag.id = hit.id;
    drag.object = *record;
    drag.span = (f64)object.getDuration() / spans;
    this->endDrag = std::move(drag);
    return true;
}

void Compose::dragEnd(f64 timeMS, bool snap) {
    if(!this->endDrag) return;
    EndDrag &drag = *this->endDrag;
    HO object = drag.object;
    if(object.kind == HO::Kind::SLIDER) {
        // the span end nearest to the time, the earlier of two as near; no more than the game plays
        const f64 spans = std::ceil((timeMS - object.time) / drag.span - 0.5);
        SliderPath::setSpans(object, (i32)std::clamp(spans, 1.0, (f64)DatabaseBeatmap::limits().sliderMaxRepeats));
    } else {
        const i32 end =
            snap ? this->state.document->getBeatSnap().snap(timeMS, this->state.divisor, timeMS) : (i32)timeMS;
        if(end <= object.time) return;
        object.endTime = end;
    }
    if(object == drag.applied) return;
    drag.applied = object;
    const bool going = this->dragStep(drag, [&](Document::Change &c) {
        if(object != drag.object) c.setObject(drag.id, object);
    });
    if(!going) this->endDrag.reset();
}

void Compose::endGesture() {
    this->moving.reset();
    this->timeMoving.reset();
    this->endDrag.reset();
    this->breakDrag.reset();
    this->pointDrag.reset();
}

void Compose::endPlacing() {
    if(this->isPlacingSlider()) this->finishSlider();
    if(this->isPlacingSpinner()) this->finishSpinner();
    this->endGesture();
}

void Compose::nudgeInTime(i32 direction) {
    if(!this->state.objects || this->selection.empty()) return;
    const BeatSnap &snap = this->state.document->getBeatSnap();
    const i32 divisor = this->state.divisor;
    // each onto the tick a tick away from it (a tick is rarely a whole number of milliseconds)
    const auto nudged = [&](i32 time) { return snap.step(time, direction, divisor, false); };
    const i32 length = (i32)this->state.clock.getLength();
    const MapObjects &objects = *this->state.objects;
    // all or none: none may leave the song
    for(const Document::Id id : this->selection) {
        if(const HitObject *object = objects.find(id)) {
            const i32 to = nudged(object->getClickTime());
            if(to < 0 || (length > 0 && to >= length)) return;
        }
    }
    Document &document = *this->state.document;
    this->state.edit(Action::NUDGE, [&](Document::Change &c) {
        for(const Document::Id id : this->selection) {
            auto object = document.getObject(id);
            if(!object || object->kind == HO::Kind::NONE) continue;
            shift(*object, nudged(object->time) - object->time);
            c.setObject(id, *object);
        }
    });
}

void Compose::reverse() {
    if(!this->state.objects || this->selection.empty()) return;
    const MapObjects &objects = *this->state.objects;
    struct Selected {
        Document::Id id;
        HO record;
        HO reversed;
    };
    // in time order, as the objects are
    std::vector<Selected> selected;
    std::vector<i32> ends;
    i32 first = std::numeric_limits<i32>::max();
    i32 last = std::numeric_limits<i32>::min();
    for(uSz i = 0; i < objects.objects.size(); i++) {
        if(!this->isSelected(objects.ids[i])) continue;
        auto record = this->state.document->getObject(objects.ids[i]);
        if(!record || record->kind == HO::Kind::NONE) continue;
        const HitObject &object = *objects.objects[i];
        first = std::min(first, object.getClickTime());
        last = std::max(last, object.getEndTime());
        ends.push_back(object.getEndTime());
        selected.push_back({.id = objects.ids[i], .record = *record, .reversed = std::move(*record)});
    }

    // new combos (and their colour skips) by their places in time, spinners aside
    std::vector<u8> combos;
    for(uSz i = 0; i < selected.size(); i++) {
        HO &reversed = selected[i].reversed;
        if(reversed.kind != HO::Kind::SPINNER) {
            combos.push_back((u8)(reversed.type & (HO::TYPE_NEW_COMBO | COLOUR_SKIP_BITS)));
        }
        shift(reversed, first + last - ends[i] - reversed.time);
        if(reversed.kind == HO::Kind::SLIDER) {
            const auto path = SliderPath::reversed((SLIDERCURVETYPE)reversed.curveType, SliderPath::pointsOf(reversed),
                                                   reversed.length);
            SliderPath::setPoints(reversed, path.points);
            reversed.curveType = (char)path.type;
        }
    }
    std::vector<uSz> order(selected.size());
    std::iota(order.begin(), order.end(), uSz{0});
    Mc::ranges::stable_sort(order, {}, [&](uSz i) { return selected[i].reversed.time; });
    uSz slot = 0;
    for(const uSz i : order) {
        HO &reversed = selected[i].reversed;
        if(reversed.kind == HO::Kind::SPINNER) continue;
        reversed.type = (reversed.type & ~(HO::TYPE_NEW_COMBO | COLOUR_SKIP_BITS)) | combos[slot++];
    }

    this->state.edit(Action::REVERSE, [&](Document::Change &c) {
        for(const Selected &s : selected) {
            if(s.reversed != s.record) c.setObject(s.id, s.reversed);
        }
    });
}

void Compose::flip(bool vertically) {
    if(this->selection.empty()) return;
    this->state.edit(Action::FLIP, [&](Document::Change &c) {
        this->mapSelection(c, [&](vec2 p) { return vertically ? vec2{p.x, FIELD.y - p.y} : vec2{FIELD.x - p.x, p.y}; });
    });
}

void Compose::rotate(i32 direction) {
    if(this->selection.empty()) return;
    const vec2 center = FIELD / 2.f;
    this->state.edit(Action::ROTATE, [&](Document::Change &c) {
        this->mapSelection(c, [&](vec2 p) {
            const vec2 d = p - center;
            // y points down: clockwise takes right to down
            return center + (direction > 0 ? vec2{-d.y, d.x} : vec2{d.y, -d.x});
        });
    });
}

void Compose::resetSamples(bool all) {
    if(!this->state.objects) return;
    Document &document = *this->state.document;
    this->state.edit(Action::RESET_SAMPLES, [&](Document::Change &c) {
        for(const Document::Id id : all ? this->state.objects->ids : this->selection) {
            auto object = document.getObject(id);
            if(!object) continue;
            object->hitSounds = 0;
            object->edgeSounds.clear();
            object->edgeSets.clear();
            // a slider without sounds writes none after its length, as osu!stable does
            object->sample = {.filename = {}, .parts = object->kind == HO::Kind::SLIDER ? (u8)0 : (u8)5};
            c.setObject(id, *object);
        }
    });
}

void Compose::resetColourSkips() {
    if(!this->state.objects) return;
    Document &document = *this->state.document;
    this->state.edit(Action::RESET_COLOURS, [&](Document::Change &c) {
        for(const Document::Id id : this->state.objects->ids) {
            auto object = document.getObject(id);
            if(!object || !(object->type & COLOUR_SKIP_BITS)) continue;
            object->type &= ~COLOUR_SKIP_BITS;
            c.setObject(id, *object);
        }
    });
}

void Compose::recalculateSliderLengths() {
    if(!this->state.objects) return;
    Document &document = *this->state.document;
    this->state.edit(Action::SLIDER_LENGTHS, [&](Document::Change &c) {
        for(const Document::Id id : this->state.objects->ids) {
            const auto slider = document.getObject(id);
            if(!slider || slider->kind != HO::Kind::SLIDER) continue;
            const std::vector<vec2> points = SliderPath::pointsOf(*slider);
            if(const auto snapped = this->reshaped(*slider, points, (SLIDERCURVETYPE)slider->curveType)) {
                c.setObject(id, *snapped);
            }
        }
    });
}

void Compose::startPreview(Action action) {
    assert(!this->previewing && "a preview started while another goes on");
    // what's being placed or dragged is done as it is
    this->endPlacing();
    StepPreview preview;
    this->beginDrag(preview, action);
    this->previewing = std::move(preview);
}

void Compose::endPreview(bool keep) {
    if(!this->previewing) return;
    // a drag that ends where it started leaves no step
    if(!keep) this->dragStep(*this->previewing, [](Document::Change &) {});
    this->previewing.reset();
}

bool Compose::previewMap(const std::function<vec2(vec2)> &map, bool reshape) {
    if(!this->previewing) return false;
    this->dragStep(*this->previewing, [&](Document::Change &c) { this->mapSelection(c, map, reshape); });
    return this->state.document->getLastGesture() == this->previewing->gesture;
}

bool Compose::previewPlace(std::span<const HO> records, std::optional<Document::Id> replacing) {
    if(!this->previewing) return false;
    auto replaced = records.empty() ? std::nullopt : this->replacedBy(records);
    if(replaced && replacing && !Mc::ranges::contains(*replaced, *replacing)) replaced->push_back(*replacing);
    std::vector<Document::Id> added;
    // none: the step is taken back
    this->dragStep(*this->previewing, [&](Document::Change &c) {
        if(!replaced) return;
        c.remove(*replaced);
        added = c.addObjects(records);
    });
    const bool placed = !added.empty();
    this->select(std::move(added));
    return placed;
}

std::vector<HO> Compose::polygon(i32 corners, i32 repeats, f64 offsetDegrees, f64 spacing) const {
    const BeatSnap &snap = this->state.document->getBeatSnap();
    i32 time = this->snappedNow();
    const f64 side = spacing * this->tickLengthAt(time);
    const f64 radius = side / (2.0 * std::sin(std::numbers::pi / corners));
    const vec2 center = FIELD / 2.f;
    std::vector<HO> circles;
    for(i32 i = 1; i <= corners * repeats; i++) {
        const f64 angle = 2.0 * std::numbers::pi * i / corners + offsetDegrees * std::numbers::pi / 180.0;
        const vec2 pos = vec::round(center + vec2{(f32)(radius * std::cos(angle)), (f32)(radius * std::sin(angle))});
        if(!inField(pos)) return {};
        circles.push_back(this->brushed(pos, time, i == 1 && this->brush.newCombo));
        time = snap.step(time, 1, this->state.divisor, false);
    }
    return circles;
}

std::optional<std::pair<Document::Id, HO>> Compose::getSelectedSlider() const {
    const SelectedSlider *selected = this->selectedSlider();
    if(!selected) return std::nullopt;
    return std::pair{selected->id, selected->slider};
}

std::vector<HO> Compose::stream(const HO &slider, i32 divisor, f64 spacing, i32 count) const {
    const std::vector<vec2> points = SliderPath::pointsOf(slider);
    const auto type = (SLIDERCURVETYPE)slider.curveType;
    const SliderCurve curve{type, points, (f32)slider.length};
    const f64 length = curve.getPixelLength();
    // a file can say any length
    if(!(length > 0.0) || !std::isfinite(length)) return {};
    // as far as it moves in a tick at this divisor
    const f64 apart = count > 0 ? length / std::max(1, count - 1)
                                : spacing * this->tickLengthAt(slider.time) * this->state.divisor / divisor;
    if(!(apart > 0.0)) return {};
    const f64 n = count > 0 ? count : std::floor((length + 1.0) / apart) + 1.0;
    const BeatSnap &snap = this->state.document->getBeatSnap();
    const SliderPath::Edge head = SliderPath::edgesOf(slider).front();
    const i32 songLength = (i32)this->state.clock.getLength();
    std::vector<HO> circles;
    i32 time = slider.time;
    for(i32 i = 0; i < n; i++) {
        // nothing is placed after the song, however long the slider says it is
        if(songLength > 0 && time >= songLength) return {};
        const vec2 pos = vec::round(curve.pointAt((f32)std::min(1.0, i * apart / length)));
        HO circle;
        circle.kind = HO::Kind::CIRCLE;
        circle.x = pos.x;
        circle.y = pos.y;
        circle.time = time;
        circle.type = HO::TYPE_CIRCLE;
        circle.sample = {.filename = {}, .parts = 5};
        if(i == 0) {
            circle.type |= slider.type & (HO::TYPE_NEW_COMBO | COLOUR_SKIP_BITS);
            circle.hitSounds = head.sounds;
            circle.sample.normalSet = head.sets.normalSet;
            circle.sample.additionSet = head.sets.additionSet;
        }
        circles.push_back(circle);
        time = snap.step(time, 1, divisor, false);
    }
    return circles;
}

std::optional<vec2> Compose::selectionCenter() const {
    const auto bounds = this->selectionBounds();
    if(!bounds) return std::nullopt;
    return (bounds->first + bounds->second) / 2.f;
}

f32 Compose::maxScale(vec2 center, bool x, bool y) const {
    f32 limit = std::numeric_limits<f32>::infinity();
    // how far from the center a coordinate can be scaled before it leaves [0, size]
    const auto room = [&](f32 p, f32 c, f32 size) {
        if(p > c) limit = std::min(limit, (size - c) / (p - c));
        if(p < c) limit = std::min(limit, c / (c - p));
    };
    // the outermost ones leave first
    if(const auto bounds = this->selectionBounds()) {
        for(const vec2 p : {bounds->first, bounds->second}) {
            if(x) room(p.x, center.x, FIELD.x);
            if(y) room(p.y, center.y, FIELD.y);
        }
    }
    return std::isinf(limit) ? 5.f : std::max(limit, 1.f);
}

void Compose::toggleNewCombo() {
    if(this->selection.empty()) {
        this->brush.newCombo = !this->brush.newCombo;
        return;
    }
    const bool on = !this->summary().newCombo;
    Document &document = *this->state.document;
    this->state.edit(Action::NEW_COMBO, [&](Document::Change &c) {
        for(const Document::Id id : this->selection) {
            auto object = document.getObject(id);
            if(!object) continue;
            if(on) {
                object->type |= HO::TYPE_NEW_COMBO;
            } else {
                object->type &= ~(HO::TYPE_NEW_COMBO | COLOUR_SKIP_BITS);
            }
            c.setObject(id, *object);
        }
    });
}

void Compose::toggleHitSound(u8 hitSound) {
    if(this->selection.empty()) {
        this->brush.hitSounds ^= hitSound;
        return;
    }
    if(this->changePicked(Action::HITSOUNDS, [&](i32 &sounds, HO::EdgeSet &) { sounds ^= hitSound; })) return;
    const bool on = !(this->summary().hitSounds & hitSound);
    Document &document = *this->state.document;
    this->state.edit(Action::HITSOUNDS, [&](Document::Change &c) {
        for(const Document::Id id : this->selection) {
            auto object = document.getObject(id);
            if(!object) continue;
            // the slider body and every edge it lists
            const auto apply = [&](auto &sounds) { sounds = on ? (sounds | hitSound) : (sounds & ~hitSound); };
            apply(object->hitSounds);
            for(u8 &sounds : object->edgeSounds) apply(sounds);
            c.setObject(id, *object);
        }
    });
}

void Compose::setSampleSet(bool additions, i32 set) {
    if(this->selection.empty()) {
        (additions ? this->brush.additionSet : this->brush.normalSet) = set;
        return;
    }
    if(this->changePicked(additions ? Action::ADDITION_SET : Action::SAMPLE_SET,
                          [&](i32 &, HO::EdgeSet &sets) { (additions ? sets.additionSet : sets.normalSet) = set; })) {
        return;
    }
    Document &document = *this->state.document;
    this->state.edit(additions ? Action::ADDITION_SET : Action::SAMPLE_SET, [&](Document::Change &c) {
        for(const Document::Id id : this->selection) {
            auto object = document.getObject(id);
            if(!object) continue;
            (additions ? object->sample.additionSet : object->sample.normalSet) = set;
            if(object->sample.parts < 2) object->sample.parts = 5;
            if(object->kind == HO::Kind::SLIDER) {
                // every edge too: a slider without edge sets gets them, with its hitsounds on every edge (other editors
                // keep slider sets only per edge)
                const uSz edges = (uSz)std::max(object->slides, 1) + 1;
                if(object->edgeSets.empty()) {
                    if(object->edgeSounds.empty()) object->edgeSounds.assign(edges, (u8)object->hitSounds);
                    object->edgeSets.assign(
                        edges, {.normalSet = object->sample.normalSet, .additionSet = object->sample.additionSet});
                }
                for(auto &edge : object->edgeSets) (additions ? edge.additionSet : edge.normalSet) = set;
            }
            c.setObject(id, *object);
        }
    });
}

template <typename F>
bool Compose::changeSamples(Action action, F &&change) {
    bool any = false;
    Document &document = *this->state.document;
    this->state.edit(action, [&](Document::Change &c) {
        for(const Document::Id id : this->selection) {
            auto object = document.getObject(id);
            if(!object || object->kind == HO::Kind::SLIDER) continue;
            change(object->sample);
            object->sample.parts = 5;
            c.setObject(id, *object);
            any = true;
        }
    });
    return any;
}

bool Compose::setSampleIndex(i32 index) {
    if(this->selection.empty()) {
        this->brush.index = index;
        return true;
    }
    return this->changeSamples(Action::SAMPLE_INDEX,
                               [&](neomod::BeatmapFile::HitSample &sample) { sample.index = index; });
}

bool Compose::setSampleVolume(i32 volume) {
    if(this->selection.empty()) {
        this->brush.volume = volume;
        return true;
    }
    return this->changeSamples(Action::SAMPLE_VOLUME,
                               [&](neomod::BeatmapFile::HitSample &sample) { sample.volume = volume; });
}

bool Compose::setSampleFile(std::string_view file) {
    if(this->selection.empty()) {
        this->brush.file = file;
        return true;
    }
    return this->changeSamples(Action::SAMPLE_FILE,
                               [&](neomod::BeatmapFile::HitSample &sample) { sample.filename = file; });
}

void Compose::setColourSkip(i32 skip) {
    if(this->selection.size() != 1) return;
    const Document::Id id = this->selection.front();
    auto object = this->state.document->getObject(id);
    if(!object || !startsCombo(*object)) return;
    object->type = (object->type & ~COLOUR_SKIP_BITS) | (u8)((skip & 0b111) << HO::TYPE_COLOUR_SKIP_SHIFT);
    this->state.edit(Action::COLOUR_SKIP, [&](Document::Change &c) { c.setObject(id, *object); });
}

const Compose::Summary &Compose::summary() const {
    Summary &s = this->cachedSummary;
    if(s.selectionVersion != this->selectionVersion) s = {.selectionVersion = this->selectionVersion};

    // made again with the objects, which every change makes again; the game reads the lines in the same order
    const MapObjects *objects = this->state.objects.get();
    if(const u32 revision = objects ? objects->revision : 0; s.objectsRevision != revision) {
        s.objectsRevision = revision;
        s.earliest.reset();
        s.newCombo = true;
        for(uSz i = 0; objects && i < this->selection.size(); i++) {
            const auto it = objects->indexes.find(this->selection[i]);
            if(it == objects->indexes.end()) continue;
            const auto &read = this->state.document->getPrimitives().objects[it->second];
            if(!s.earliest || it->second < *s.earliest) {
                s.earliest = it->second;
                s.colourSkip = std::visit([](const auto &o) { return o.colorSkip; }, read);
            }
            s.newCombo = s.newCombo && std::visit([](const auto &o) { return o.newCombo; }, read);
        }
        s.newCombo = s.newCombo && s.earliest.has_value();
    }

    const Document *document = this->state.document.get();
    const u32 revision = document ? document->getRevision() : 0;
    if(s.revision == revision) return s;
    s.revision = revision;
    s.hitSounds = 0;
    s.normalSet.reset();
    s.additionSet.reset();
    s.index.reset();
    s.volume.reset();
    s.file.reset();
    if(this->selection.empty() || !document) return s;

    bool first = true;
    bool firstOwn = true;
    s.hitSounds = 0xff;
    for(const Document::Id id : this->selection) {
        const auto object = document->getObject(id);
        if(!object) continue;
        s.hitSounds &= (u8)object->hitSounds;
        if(first) {
            s.normalSet = object->sample.normalSet;
            s.additionSet = object->sample.additionSet;
            first = false;
        }
        if(s.normalSet != object->sample.normalSet) s.normalSet.reset();
        if(s.additionSet != object->sample.additionSet) s.additionSet.reset();
        if(object->kind == HO::Kind::SLIDER) continue;
        if(firstOwn) {
            s.index = object->sample.index;
            s.volume = object->sample.volume;
            s.file = std::string{object->sample.filename};
            firstOwn = false;
        }
        if(s.index != object->sample.index) s.index.reset();
        if(s.volume != object->sample.volume) s.volume.reset();
        if(s.file && *s.file != object->sample.filename) s.file.reset();
    }
    if(first) s.hitSounds = 0;
    if(firstOwn) s.file.emplace();
    return s;
}

bool Compose::hasNewCombo() const { return this->selection.empty() ? this->brush.newCombo : this->summary().newCombo; }

bool Compose::hasHitSound(u8 hitSound) const {
    if(const auto picked = this->pickedSounds()) return picked->sounds & hitSound;
    return (this->selection.empty() ? this->brush.hitSounds : this->summary().hitSounds) & hitSound;
}

std::optional<i32> Compose::getSampleSet(bool additions) const {
    if(this->selection.empty()) return additions ? this->brush.additionSet : this->brush.normalSet;
    if(const auto picked = this->pickedSounds()) return additions ? picked->sets.additionSet : picked->sets.normalSet;
    return additions ? this->summary().additionSet : this->summary().normalSet;
}

std::optional<i32> Compose::getSampleIndex() const {
    return this->selection.empty() ? this->brush.index : this->summary().index;
}

std::optional<i32> Compose::getSampleVolume() const {
    return this->selection.empty() ? this->brush.volume : this->summary().volume;
}

std::optional<std::string_view> Compose::getSampleFile() const {
    if(this->selection.empty()) return this->brush.file;
    const Summary &s = this->summary();
    return s.file ? std::optional<std::string_view>{*s.file} : std::nullopt;
}

std::optional<Compose::ColourSkips> Compose::getColourSkips() const {
    if(this->selection.size() != 1 || !this->state.objects) return std::nullopt;
    const MapObjects &objects = *this->state.objects;
    const HitObject *object = objects.find(this->selection.front());
    const Skin *skin = objects.view.skin;
    const Summary &s = this->summary();
    if(!object || object->isSpinner() || !s.newCombo || !skin) return std::nullopt;

    // after another combo, skipping all but one colour would come back to the colour that combo has
    const uSz count = objects.view.comboColors.empty() ? skin->c_combo_colors.size() : objects.view.comboColors.size();
    const bool first = objects.objects.front().get() == object;
    const i32 skips = std::min((i32)count - (first ? 0 : 1), 8);
    if(skips < 2) return std::nullopt;
    const i32 current = s.colourSkip;
    ColourSkips choices{.colours = {}, .count = (uSz)skips, .current = current};
    for(i32 i = 0; i < skips; i++) {
        choices.colours[i] =
            objects.view.getComboColor(object->getColorCounter(), object->getColorOffset() - current + i);
    }
    return choices;
}
