// Copyright (c) 2026, WH, All rights reserved.
#include "Timeline.h"

#include "BeatSnap.h"
#include "CBaseUIDispatch.h"
#include "ContainerRanges.h"
#include "EditorState.h"
#include "Engine.h"
#include "Font.h"
#include "Graphics.h"
#include "HitObjects.h"
#include "Keyboard.h"
#include "MapObjects.h"
#include "Mouse.h"
#include "Osu.h"
#include "PlayfieldView.h"
#include "Skin.h"
#include "UIStyle.h"
#include "UniString.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace neomod;
using TimingPoint = BeatmapFile::TimingPoint;

namespace {
// at zoom 1
constexpr f64 RANGE_MS{6000.0};
// a tick closer than this to the next one (px) isn't drawn
constexpr f32 MIN_TICK_SPACING{3.f};

constexpr Color PREVIEW_COLOR{argb(150, 255, 247, 17)};
constexpr Color UNINHERITED_COLOR{argb(150, 180, 20, 20)};
constexpr Color INHERITED_COLOR{argb(150, 156, 255, 0)};
constexpr Color BOOKMARK_COLOR{argb(240, 58, 110, 170)};
constexpr Color BREAK_COLOR{argb(150, 100, 100, 100)};
constexpr Color SPINNER_COLOR{argb(255, 128, 128, 128)};
constexpr Color RANGE_COLOR{argb(50, 255, 255, 255)};
constexpr Color LABEL_COLOR{argb(255, 255, 236, 150)};
constexpr f32 BREAK_EDGE_REACH{6.f};  // units from a break edge within which a press grabs it
// the zoom under which sound labels would run into each other
constexpr f32 LABEL_MIN_ZOOM{0.6f};
// how much of a sample file name a label shows
constexpr uSz LABEL_FILE_CHARACTERS{8};
// what's left free between two labels (units)
constexpr f32 LABEL_GAP{4.f};
// the part of the selected slider picked: an edge ring, the body span
constexpr Color PICKED_COLOR{argb(255, 255, 50, 50)};
// how far the cursor goes before a press on the selection moves it (units)
constexpr f32 MOVE_THRESHOLD{4.f};
// objects are hit within this fraction of the circle radius from the middle line (a circle within as much from its
// middle): less than they're drawn, so that a press between them in a dense stretch, or above or below them, starts a
// range
constexpr f32 HIT_REACH{0.5f};

// slider "body" thickness in the timeline (default skin circles don't fill their images: at 1 the span ends show around
// them)
constexpr f32 SLIDER_SPAN_THICKNESS{0.9f};
constexpr f32 SLIDER_SPAN_UNSELECTED_ALPHA{0.67f};

// the tick colour by where it falls in its beat
Color tickColor(i64 indexInBeat, i32 divisor) {
    if(indexInBeat == 0) return argb(220, 255, 255, 255);
    if((indexInBeat * 2) % divisor == 0) return argb(225, 255, 0, 0);
    if((indexInBeat * 3) % divisor == 0) return argb(225, 200, 0, 200);
    if((indexInBeat * 4) % divisor == 0) return argb(225, 50, 128, 255);
    if((indexInBeat * 6) % divisor == 0) return argb(225, 144, 64, 144);
    if((indexInBeat * 8) % divisor == 0) return argb(225, 255, 255, 0);
    return argb(225, 160, 160, 160);
}

i64 floorDiv(i64 a, i64 b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }
}  // namespace

Timeline::Timeline(EditorState &state) : CBaseUIElement(0, 0, 0, 0, "editor_timeline"), state(state) {
    this->setHandleRightMouse(true);
}

Timeline::~Timeline() = default;

f32 Timeline::xAt(f64 timeMS) const {
    const f64 range = RANGE_MS / this->state.zoom;
    return this->getPos().x + this->getSize().x / 2.f +
           (f32)((timeMS - this->state.clock.getTime()) * this->getSize().x / range);
}

f64 Timeline::timeAt(f32 x) const {
    const f64 range = RANGE_MS / this->state.zoom;
    return this->state.clock.getTime() + (x - this->getPos().x - this->getSize().x / 2.f) * range / this->getSize().x;
}

f32 Timeline::circleDiameter() const { return this->getSize().y * 0.85f; }

void Timeline::draw() {
    if(!this->isVisible()) return;
    uiStyle().drawPanel(this->getRect());
    if(!this->state.document) return;

    const f64 range = RANGE_MS / this->state.zoom;
    const f64 time = this->state.clock.getTime();
    const f64 left = time - range / 2.0;
    const f64 right = time + range / 2.0;
    const vec2 pos = this->getPos();
    const vec2 size = this->getSize();

    g->pushClipRect(this->getRect());

    for(const auto &b : this->state.document->getPrimitives().breaks) {
        if(b.endTime < left || b.startTime > right) continue;
        const f32 x0 = this->xAt((f64)b.startTime);
        g->setColor(BREAK_COLOR);
        g->fillRectf(x0, pos.y + size.y * 0.25f, this->xAt((f64)b.endTime) - x0, size.y * 0.5f);
    }

    if(this->gesture == Gesture::RANGE) {
        const f32 x0 = this->xAt(this->pressedTime);
        const f32 x1 = std::clamp(mouse->getPos().x, pos.x, pos.x + size.x);
        g->setColor(RANGE_COLOR);
        g->fillRectf(std::min(x0, x1), pos.y, std::abs(x1 - x0), size.y);
    }

    this->drawObjects(left, right);
    this->drawGrid(left, right);

    // full-height lines on top: the preview point, timing points, bookmarks
    const auto line = [&](f64 at, Color color) {
        if(at < left || at > right) return;
        g->setColor(color);
        g->fillRectf(std::round(this->xAt(at)), pos.y, 1.f, size.y);
    };
    if(const i32 preview = this->state.document->getPreviewTime(); preview >= 0) line(preview, PREVIEW_COLOR);
    const auto points = this->state.document->getTimingPoints();
    for(auto it = Mc::ranges::lower_bound(points, left, {}, &TimingPoint::time); it != points.end(); ++it) {
        if(it->time > right) break;
        line(it->time, it->uninherited ? UNINHERITED_COLOR : INHERITED_COLOR);
    }
    const auto bookmarks = this->state.document->getBookmarks();
    for(auto it = Mc::ranges::lower_bound(bookmarks, left); it != bookmarks.end() && *it <= right; ++it) {
        line(*it, BOOKMARK_COLOR);
    }

    // the base line and the time, between two lines in the middle
    g->setColor(argb(255, 255, 255, 255));
    g->fillRectf(pos.x, pos.y + size.y - 1.f, size.x, 1.f);
    const f32 middle = std::round(pos.x + size.x / 2.f);
    g->fillRectf(middle - 1.f, pos.y, 1.f, size.y);
    g->fillRectf(middle + 1.f, pos.y, 1.f, size.y);
    if(this->state.showsSoundLabels()) this->drawSoundLabels(left, right);

    g->popClipRect();
}

void Timeline::drawGrid(f64 left, f64 right) const {
    const auto points = this->state.document->getTimingPoints();
    const i32 divisor = this->state.divisor;
    const f32 bottom = this->getPos().y + this->getSize().y;
    const f32 beatHeight = this->getSize().y * 0.18f;
    const f64 pxPerMS = this->getSize().x / (RANGE_MS / this->state.zoom);

    // every uninherited section shown, from its point to the next point (the first section before its point too): from
    // the one in force at the left edge
    const auto first = Mc::ranges::find_if(points, &TimingPoint::uninherited);
    auto shown = Mc::ranges::upper_bound(points, left, {}, &TimingPoint::time);
    while(shown != points.begin() && !std::prev(shown)->uninherited) --shown;
    shown = shown != points.begin() ? std::prev(shown) : first;
    for(auto point = shown; point != points.end() && (point == first || point->time <= right); ++point) {
        // a file can say any beat length
        if(!point->uninherited || !(point->beatLength > 0.0) || !std::isfinite(point->beatLength)) continue;

        const auto next = std::find_if(point + 1, points.end(), [](const TimingPoint &p) { return p.uninherited; });
        const f64 end = next != points.end() ? next->time : std::numeric_limits<f64>::infinity();
        const f64 from = std::max(point == first ? left : point->time, left);
        const f64 to = std::min(end, right);
        if(from > to) continue;

        // closer ticks than can be told apart: beats only, or only bar lines (a step a bar), or none
        const i32 meter = point->meter > 0 ? point->meter : 4;
        const f64 beatPx = point->beatLength * pxPerMS;
        if(beatPx * meter < MIN_TICK_SPACING) continue;
        const i32 shownDivisor = beatPx / divisor >= MIN_TICK_SPACING ? divisor : 1;
        const i64 stride = beatPx < MIN_TICK_SPACING ? meter : 1;

        const f64 step = point->beatLength / shownDivisor;
        const i64 start = (i64)std::ceil((from - point->time) / step);
        for(i64 k = floorDiv(start + stride - 1, stride) * stride;; k += stride) {
            const f64 at = point->time + (f64)k * step;
            if(at > to || at >= end) break;
            // the index within the beat for the shown divisor, then the one within the beat for the chosen divisor
            const i64 inBeat = ((k % shownDivisor) + shownDivisor) % shownDivisor;
            const bool bar = inBeat == 0 && ((floorDiv(k, shownDivisor) % meter) + meter) % meter == 0;
            const f32 height = inBeat != 0 ? beatHeight * 7.f / 8.f : bar ? beatHeight * 2.f : beatHeight;
            g->setColor(tickColor(inBeat * (divisor / shownDivisor), divisor));
            g->fillRectf(std::round(this->xAt(at)), bottom - height, 1.f, height);
        }
    }
}

void Timeline::drawObjects(f64 left, f64 right) const {
    const Skin *skin = osu->getSkin();
    if(!this->state.objects || !skin) return;
    const MapObjects &objects = *this->state.objects;

    const vec2 pos = this->getPos();
    const vec2 size = this->getSize();
    const f32 y = pos.y + size.y * 0.5f;
    const f32 diameter = this->circleDiameter();
    const Compose &compose = this->state.compose;
    const f64 margin = diameter / (size.x / (RANGE_MS / this->state.zoom));
    const f32 fade = uiStyle().px(40.f);
    const auto edgeAlpha = [&](f32 x) { return std::clamp(std::min(x - pos.x, pos.x + size.x - x) / fade, 0.f, 1.f); };
    const auto color = [&](const HitObject &obj) {
        return obj.isSpinner() ? SPINNER_COLOR
                               : skin->getComboColorForCounter(obj.getColorCounter(), obj.getColorOffset(),
                                                               objects.view.comboColors);
    };

    // the part of the selected slider picked
    const auto picked = compose.getPicked();
    const Document::Id pickedId = picked ? compose.getSelection().front() : Document::Id{0};

    // what overlaps the time shown
    const auto first =
        objects.objects.begin() + (sSz)objects.firstFrom((i32)std::ceil(left - margin) - objects.longestObjectMS);
    const auto last = objects.objects.begin() + (sSz)objects.firstFrom((i32)std::floor(right + margin) + 1);

    // the spans of sliders and spinners under every circle, round around the first and the last (opaque when selected,
    // red for a picked body)
    for(auto it = first; it != last; ++it) {
        const HitObject &obj = **it;
        if(obj.getDuration() <= 0 || obj.getEndTime() < left - margin) continue;
        const f32 thickness = diameter * SLIDER_SPAN_THICKNESS;
        const f32 x0 = this->xAt(obj.getClickTime()) - thickness / 2.f;
        const f32 x1 = this->xAt(obj.getEndTime()) + thickness / 2.f;
        const Document::Id id = objects.ids[it - objects.objects.begin()];
        const bool selected = compose.isSelected(id);
        g->setColor(picked && !picked->edge && id == pickedId
                        ? PICKED_COLOR
                        : Color(color(obj)).setA(selected ? 1.f : SLIDER_SPAN_UNSELECTED_ALPHA));
        g->fillRectf(Graphics::FillRectOptions{
            .x = x0, .y = y - thickness / 2.f, .width = x1 - x0, .height = thickness, .cornerRadius = thickness / 2.f});
    }

    // circles: the latest first, so that earlier ones end up on top
    const f32 numberScale = PlayfieldView::numberScale(skin, diameter, diameter);
    const f32 overlapScale = PlayfieldView::hitcircleOverlapScale(diameter, diameter);
    const BasicSkinImage &ring = skin->i_hitcircleselect;
    const f32 ringScale = diameter / (128.f * ring.scale());
    const auto circle = [&](f64 at, Color c, i32 number, bool reverse, std::optional<Color> ringColor) {
        const f32 x = this->xAt(at);
        const f32 alpha = edgeAlpha(x);
        if(alpha <= 0.f) return;
        Circle::drawCircle(skin, vec2{x, y}, diameter, c, alpha);
        if(number > 0) Circle::drawHitCircleNumber(skin, numberScale, overlapScale, vec2{x, y}, number, alpha, 1.f);
        if(ringColor) {
            g->setColor(Color(*ringColor).setA(alpha));
            g->pushTransform();
            {
                g->scale(ringScale, ringScale);
                g->translate(x, y);
                g->drawImage(ring);
            }
            g->popTransform();
        }
        if(reverse) {
            const BasicSkinImage &arrow = skin->i_reversearrow;
            const f32 arrowScale = diameter / (128.f * arrow.scale());
            g->setColor(argb(alpha, 1.f, 1.f, 1.f));
            g->pushTransform();
            {
                g->scale(arrowScale, arrowScale);
                g->translate(x, y);
                g->drawImage(arrow);
            }
            g->popTransform();
        }
    };
    for(auto it = last; it != first;) {
        const HitObject &obj = **--it;
        if(obj.getEndTime() < left - margin) continue;
        const Color c = color(obj);
        const Document::Id id = objects.ids[it - objects.objects.begin()];
        // white around the start and end of a selected object, red around the picked edge
        const bool selected = compose.isSelected(id);
        const std::optional<i32> pickedEdge = picked && id == pickedId ? picked->edge : std::nullopt;
        const auto ring = [&](i32 edge, bool end) {
            return pickedEdge == edge ? std::optional{PICKED_COLOR}
                   : selected && end  ? std::optional{argb(255, 255, 255, 255)}
                                      : std::nullopt;
        };
        const i32 spans = obj.isSlider() ? std::max(static_cast<const Slider &>(obj).getRepeat(), 1) : 1;
        if(obj.getDuration() > 0) circle(obj.getEndTime(), c, 0, false, ring(spans, true));
        if(obj.isSlider()) {
            i32 repeat = 0;
            for(const auto &click : static_cast<const Slider &>(obj).getClicks()) {
                if(click.type == 0) circle(click.timeMS, c, 0, true, ring(++repeat, false));
            }
        }
        circle(obj.getClickTime(), c, obj.isSpinner() ? 0 : obj.getComboNumber(), false, ring(0, true));
    }
}

std::string Timeline::soundLabel(const neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS &samples,
                                 std::span<const std::string> sampleFiles) {
    using neomod::DatabaseBeatmapTypes::HitSoundType;
    static constexpr std::string_view SETS{"NSD"};
    std::string label;
    if(samples.filename >= 1 && samples.filename <= sampleFiles.size()) {
        const std::string &file = sampleFiles[samples.filename - 1u];
        const std::string_view stem = std::string_view{file}.substr(0, file.rfind('.'));
        uSz cut = 0;
        for(uSz shown = 0; shown < LABEL_FILE_CHARACTERS && cut < stem.size(); shown++) {
            cut = UniString::next(stem, cut);
        }
        label = stem.substr(0, cut);
        if(cut < stem.size()) label += "..";
    } else {
        if(samples.normalSet >= 1 && samples.normalSet <= 3) label += SETS[samples.normalSet - 1u];
        if(samples.index > 0) label += fmt::format("{}", samples.index);
    }
    const u8 additions = samples.hitSounds & (HitSoundType::WHISTLE | HitSoundType::FINISH | HitSoundType::CLAP);
    if(additions && !label.empty()) label += ':';
    if(additions & HitSoundType::WHISTLE) label += 'W';
    if(additions & HitSoundType::FINISH) label += 'F';
    if(additions & HitSoundType::CLAP) label += 'C';
    if(samples.volume > 0) label += fmt::format("{}{}%", label.empty() ? "" : " ", samples.volume);
    return label;
}

void Timeline::drawSoundLabels(f64 left, f64 right) const {
    using namespace neomod::DatabaseBeatmapTypes;
    if(!this->state.objects || this->state.zoom < LABEL_MIN_ZOOM) return;
    const MapObjects &objects = *this->state.objects;
    const auto &read = this->state.document->getPrimitives().objects;
    const std::span<const std::string> sampleFiles = this->state.document->getPrimitives().sampleFiles;
    McFont *font = uiStyle().font();
    const f32 baseline = std::round(this->getPos().y + font->getHeight() + uiStyle().px(1.f));
    const f32 gap = uiStyle().px(LABEL_GAP);
    // a label is drawn once the one after it is known: it's as wide as the nearer neighbour leaves room for, cut off at
    // its end
    struct Waiting {
        f32 x;
        f32 room;  // to the label before it
        std::string text;
    };
    std::optional<Waiting> waiting;
    const auto draw = [&](f32 next) {
        std::string &text = waiting->text;
        const f32 room = std::min(waiting->room, std::abs(next - waiting->x)) - gap;
        while(font->getStringWidth(text) > room && UniString::prev(text, text.size()) > 0) {
            text.resize(UniString::prev(text, text.size()));
        }
        const f32 x = std::round(waiting->x - font->getStringWidth(text) / 2.f);
        for(const bool shadow : {true, false}) {
            g->setColor(shadow ? argb(220, 0, 0, 0) : LABEL_COLOR);
            g->pushTransform();
            {
                g->translate(x + (shadow ? 1.f : 0.f), baseline + (shadow ? 1.f : 0.f));
                g->drawString(font, text);
            }
            g->popTransform();
        }
    };
    const auto label = [&](f64 at, const HITSAMPLE_BITS &samples) {
        if(at < left || at > right) return;
        std::string text = soundLabel(samples, sampleFiles);
        if(text.empty()) return;
        const f32 x = this->xAt(at);
        f32 room = std::numeric_limits<f32>::infinity();
        if(waiting) {
            room = std::abs(x - waiting->x);
            draw(x);
        }
        waiting = Waiting{.x = x, .room = room, .text = std::move(text)};
    };

    const uSz first = objects.firstFrom((i32)std::ceil(left) - objects.longestObjectMS);
    const uSz last = objects.firstFrom((i32)std::floor(right) + 1);
    for(uSz i = first; i < last; i++) {
        const HitObject &obj = *objects.objects[i];
        if(const auto *circle = std::get_if<HITCIRCLE>(&read[i])) {
            label(obj.getClickTime(), circle->samples);
        } else if(const auto *slider = std::get_if<SLIDER>(&read[i])) {
            // the sounds on each end itself, or the slider sounds for all of them
            const i32 spans = std::max(slider->repeat, 1);
            const f64 span = (f64)obj.getDuration() / spans;
            for(i32 edge = 0; edge <= spans; edge++) {
                label(obj.getClickTime() + span * edge,
                      (uSz)edge < slider->edgeSamples.size() ? slider->edgeSamples[(uSz)edge] : slider->hoverSamples);
            }
        } else {
            label(obj.getEndTime(), std::get<SPINNER>(read[i]).samples);
        }
    }
    if(waiting) draw(std::numeric_limits<f32>::infinity());
}

void Timeline::zoom(i32 steps) {
    this->state.zoom = std::clamp(std::round((this->state.zoom + 0.1f * (f32)steps) * 10.f) / 10.f, 0.1f, 8.f);
}

bool Timeline::onWheel(WheelDelta vertical, WheelDelta /*horizontal*/) {
    // Alt and the wheel over the timeline zoom it, elsewhere they change the volume
    if(!keyboard->isAltDown()) return false;
    if(vertical.ticks != 0) this->zoom(vertical.ticks);
    return true;
}

void Timeline::onMouseDownInside(bool left, bool right) {
    if(!this->state.objects) return;
    Compose &compose = this->state.compose;
    const f64 time = this->timeAt(mouse->getPos().x);
    const f64 pxPerMS = this->getSize().x / (RANGE_MS / this->state.zoom);
    const f32 reach = this->circleDiameter() / 2.f * HIT_REACH;
    const f32 dy = std::abs(mouse->getPos().y - (this->getPos().y + this->getSize().y * 0.5f));
    const auto objectAt = [&]() -> std::optional<Compose::Hit> {
        if(dy >= reach) return std::nullopt;
        return compose.objectAtTime(time, std::sqrt(reach * reach - dy * dy) / pxPerMS);
    };
    if(right) {
        // a click in a break belongs to the break, over no object
        if(const auto hit = objectAt(); hit || !compose.removeBreakAt(time)) compose.rightClick(hit);
        return;
    }
    if(!left) return;

    // the timeline selects, whatever the tool: a spinner being placed is finished first
    compose.setTool(Compose::Tool::SELECT);
    const auto hit = objectAt();
    // Shift: everything from the object last clicked to this one (Ctrl: on top of the selection, and on from this one)
    if(const neomod::HitObject *from = this->state.objects->find(this->pivot); from && hit && keyboard->isShiftDown()) {
        std::vector<Document::Id> range =
            compose.objectsBetween(std::min(from->getClickTime(), hit->object->getClickTime()),
                                   std::max(from->getEndTime(), hit->object->getEndTime()));
        if(keyboard->isControlDown()) {
            compose.select(std::move(range), std::vector{compose.getSelection()});
            this->pivot = hit->id;
        } else {
            compose.select(std::move(range));
        }
        return;
    }
    if(hit) this->pivot = hit->id;
    // a break edge, grabbed within its bar
    if(!hit && dy < this->getSize().y * 0.25f &&
       compose.startBreakDrag(time, uiStyle().px(BREAK_EDGE_REACH) / pxPerMS)) {
        this->gesture = Gesture::BREAK;
        return;
    }
    if(keyboard->isControlDown()) {
        if(hit) {
            compose.toggle(hit->id);
        } else {
            // a range that adds to the selection
            this->kept = compose.getSelection();
            this->gesture = Gesture::RANGE;
            this->pressedTime = time;
        }
        return;
    }
    if(!hit) {
        compose.clearSelection();
        this->kept.clear();
        this->gesture = Gesture::RANGE;
        this->pressedTime = time;
        return;
    }
    // a click without a drag on the selected slider picks the circle pressed, else its body
    this->picking = hit->object->isSlider() && compose.getSelection() == std::vector<Document::Id>{hit->id};
    this->pressedNode = hit->node;
    this->pressedX = mouse->getPos().x;
    this->moving = false;
    if(compose.startEndDrag(*hit)) {
        this->gesture = Gesture::END;
        return;
    }
    if(!compose.isSelected(hit->id)) compose.select({hit->id});
    if(CBaseUIDispatch::getClicks(hit->id) == 2) compose.seekToSelection();
    compose.startTimeMove(hit->id);
    this->gesture = Gesture::MOVE;
    this->pressedTime = time - hit->object->getClickTime();
}

void Timeline::onCapturedMouseMove() {
    if(!flags::has<MouseButtonFlags::MF_LEFT>(CBaseUIDispatch::getCaptorButtons())) return;
    // the map went while the button was held
    if(!this->state.objects) {
        this->onMouseCancel();
        return;
    }
    if(this->gesture == Gesture::NONE) return;
    Compose &compose = this->state.compose;
    const f32 x = mouse->getPos().x;
    // past the edges a drag scrolls while paused, by half the distance past them every 60th of a second
    const f32 left = this->getPos().x;
    const f32 right = left + this->getSize().x;
    if((x < left || x > right) && !this->state.clock.isPlaying()) {
        const f64 edge = this->timeAt(std::clamp(x, left, right));
        const f64 by = (this->timeAt(x) - edge) / 2.0 * engine->getFrameTime() * 60.0;
        this->state.clock.seek(this->state.clock.getTime() + (i32)by);
    }
    switch(this->gesture) {
        case Gesture::MOVE: {
            if(!this->moving && std::abs(x - this->pressedX) < uiStyle().px(MOVE_THRESHOLD)) return;
            this->moving = true;
            // the start of the grabbed object, snapped to the divisor (Shift: not)
            const f64 target = this->timeAt(x) - this->pressedTime;
            compose.moveInTime(keyboard->isShiftDown()
                                   ? (i32)target
                                   : this->state.document->getBeatSnap().snap(target, this->state.divisor, target));
            return;
        }
        case Gesture::END:
            if(!this->moving && std::abs(x - this->pressedX) < uiStyle().px(MOVE_THRESHOLD)) return;
            this->moving = true;
            // the spinner end snapped, Shift: not
            compose.dragEnd(this->timeAt(x), !keyboard->isShiftDown());
            return;
        case Gesture::BREAK:
            // snapped, Shift: not
            compose.dragBreak(this->timeAt(x), !keyboard->isShiftDown());
            return;
        case Gesture::RANGE:
            compose.select(compose.objectsBetween(this->pressedTime, this->timeAt(std::clamp(x, left, right))),
                           this->kept);
            return;
        case Gesture::NONE:
            return;
    }
}

void Timeline::onMouseUpInside(bool left, bool /*right*/) {
    if(!left) return;
    Compose &compose = this->state.compose;
    if(this->picking && !this->moving && (this->gesture == Gesture::MOVE || this->gesture == Gesture::END)) {
        compose.pick({.edge = this->pressedNode});
    }
    this->gesture = Gesture::NONE;
    this->picking = false;
    compose.endGesture();
}

void Timeline::onMouseUpOutside(bool left, bool right) { this->onMouseUpInside(left, right); }

void Timeline::onMouseCancel() {
    this->gesture = Gesture::NONE;
    this->picking = false;
    this->state.compose.endGesture();
}
