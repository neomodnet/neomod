// Copyright (c) 2026, WH, All rights reserved.
#include "Overview.h"

#include "EditorState.h"
#include "Engine.h"
#include "Graphics.h"
#include "Mouse.h"
#include "UIStyle.h"

#include <algorithm>
#include <array>
#include <cmath>

using TimingPoint = neomod::BeatmapFile::TimingPoint;

namespace {
// while playing, a drag seeks this often (s)
constexpr f64 PLAYING_SEEK_INTERVAL{0.125};

constexpr Color KIAI_COLOR{argb(140, 255, 146, 18)};
constexpr Color BREAK_COLOR{argb(140, 255, 255, 255)};
constexpr Color LINE_COLOR{argb(128, 255, 255, 255)};
constexpr Color PREVIEW_COLOR{argb(150, 255, 247, 17)};
constexpr Color BOOKMARK_COLOR{argb(240, 58, 110, 170)};
constexpr Color UNINHERITED_COLOR{argb(150, 180, 20, 20)};
constexpr Color INHERITED_COLOR{argb(100, 156, 255, 0)};
}  // namespace

Overview::Overview(EditorState &state) : CBaseUIElement(0, 0, 0, 0, "editor_overview"), state(state) {}

Overview::~Overview() = default;

f32 Overview::xAt(f64 timeMS) const {
    const u32 length = this->state.clock.getLength();
    return this->getPos().x + (length > 0 ? (f32)(this->getSize().x * timeMS / length) : 0.f);
}

void Overview::draw() {
    if(!this->isVisible()) return;
    uiStyle().drawPanel(this->getRect());
    if(!this->state.document || this->state.clock.getLength() == 0) return;

    const vec2 pos = this->getPos();
    const vec2 size = this->getSize();
    // everything but the time in one draw (a marathon has thousands of timing points)
    if(const Made now{.map = this->state.document->getMD5(),
                      .revision = this->state.document->getRevision(),
                      .length = this->state.clock.getLength(),
                      .pos = pos,
                      .size = size};
       this->made != now) {
        this->made = now;
        this->remake();
    }
    g->setColor(argb(255, 255, 255, 255));
    g->drawVAO(&this->rects);

    // the time
    const f32 x = std::round(this->xAt(this->state.clock.getTime()));
    g->setColor(argb(255, 255, 255, 255));
    g->fillRectf(x - 1.f, pos.y, 3.f, size.y);
}

void Overview::remake() {
    const vec2 pos = this->getPos();
    const vec2 size = this->getSize();
    this->rects.clear();
    const auto rect = [&](f32 x, f32 y, f32 width, f32 height, Color color) {
        this->rects.addVertices(std::array<vec3, 6>{vec3{x, y, 0.f}, vec3{x, y + height, 0.f},
                                                    vec3{x + width, y + height, 0.f}, vec3{x, y, 0.f},
                                                    vec3{x + width, y + height, 0.f}, vec3{x + width, y, 0.f}});
        this->rects.addColors(std::array<Color, 6>{color, color, color, color, color, color});
    };
    // a map can have times before the song and after it: nothing is drawn beside the bar
    const f32 right = pos.x + size.x;
    const auto band = [&](f64 from, f64 to, f32 top, f32 bottom, Color color) {
        const f32 x0 = std::clamp(this->xAt(from), pos.x, right);
        const f32 x1 = std::clamp(this->xAt(to), pos.x, right);
        if(x0 >= right || x1 <= pos.x) return;
        rect(x0, pos.y + size.y * top, std::max(x1 - x0, 1.f), size.y * (bottom - top), color);
    };
    const auto line = [&](f64 at, f32 top, f32 bottom, Color color) {
        const f32 x = std::round(this->xAt(at));
        if(x < pos.x || x >= right) return;
        rect(x, pos.y + size.y * top, 1.f, size.y * (bottom - top), color);
    };

    // kiai from a point that starts it to the next point
    const auto points = this->state.document->getTimingPoints();
    for(uSz i = 0; i < points.size(); i++) {
        if(!(points[i].effects & TimingPoint::EFFECT_KIAI)) continue;
        const f64 end = i + 1 < points.size() ? points[i + 1].time : (f64)this->state.clock.getLength();
        band(points[i].time, end, 0.36f, 0.64f, KIAI_COLOR);
    }
    for(const auto &b : this->state.document->getPrimitives().breaks) {
        band((f64)b.startTime, (f64)b.endTime, 0.4f, 0.6f, BREAK_COLOR);
    }
    rect(pos.x, std::round(pos.y + size.y / 2.f), size.x, 1.f, LINE_COLOR);

    if(const i32 preview = this->state.document->getPreviewTime(); preview >= 0) line(preview, 0.f, 1.f, PREVIEW_COLOR);
    for(const i32 bookmark : this->state.document->getBookmarks()) line(bookmark, 0.5f, 1.f, BOOKMARK_COLOR);
    for(const TimingPoint &point : points) {
        line(point.time, 0.f, 0.5f, point.uninherited ? UNINHERITED_COLOR : INHERITED_COLOR);
    }
}

void Overview::onMouseDownInside(bool left, bool /*right*/) {
    if(left) this->seekToCursor();
}

void Overview::onCapturedMouseMove() {
    // every frame: playing, every 125 ms; paused, when the cursor time changed
    if(this->state.clock.isPlaying() ? engine->getTime() - this->lastSeek < PLAYING_SEEK_INTERVAL
                                     : this->cursorTime() == this->seekedTo) {
        return;
    }
    this->seekToCursor();
}

std::optional<i32> Overview::cursorTime() const {
    const u32 length = this->state.clock.getLength();
    if(length == 0) return std::nullopt;
    const f32 at = std::clamp((mouse->getPos().x - this->getPos().x) / this->getSize().x, 0.f, 1.f);
    return (i32)(length * at);
}

void Overview::seekToCursor() {
    this->seekedTo = this->cursorTime();
    if(!this->seekedTo) return;
    this->state.clock.seek(*this->seekedTo);
    this->lastSeek = engine->getTime();
}
