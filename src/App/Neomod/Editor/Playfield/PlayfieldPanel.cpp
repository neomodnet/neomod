// Copyright (c) 2026, WH, All rights reserved.
#include "PlayfieldPanel.h"

#include "BackgroundImageHandler.h"
#include "CBaseUIDispatch.h"
#include "ConVar.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmap.h"
#include "EditorState.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "GameRules.h"
#include "Graphics.h"
#include "HitObjects.h"
#include "Image.h"
#include "Keyboard.h"
#include "Mouse.h"
#include "Osu.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "UIStyle.h"
#include "VertexArrayObject.h"
#include "i18n.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

using namespace neomod;

namespace {
// how long after its end an object can still show (a trail, a slider body fading)
constexpr i32 SHOWN_AFTER_MS{2000};
// the game screen, which the field sits in the middle of
constexpr vec2 AREA{640.f, 480.f};
constexpr vec2 FIELD{GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT};
// how far the cursor goes before a press on the selection moves it (units)
constexpr f32 MOVE_THRESHOLD{4.f};

// the angle snap line is drawn in this many pieces, fading out along it
constexpr i32 GUIDE_PIECES{16};

constexpr Color GRID_COLOR{argb(25, 255, 255, 255)};
constexpr Color GRID_MIDDLE_COLOR{argb(75, 255, 255, 255)};
constexpr Color READOUT_COLOR{argb(255, 255, 255, 255)};
constexpr Color READOUT_SELECTED_COLOR{argb(255, 255, 255, 0)};
constexpr Color SELECTED_SLIDER{argb(255, 49, 151, 255)};
constexpr Color SELECTED_SPINNER{argb(255, 138, 43, 226)};
// the part of the selected slider picked: the body border, an edge ring
constexpr Color PICKED{argb(255, 255, 50, 50)};
constexpr Color BOX_FILL{argb(20, 255, 255, 255)};
constexpr Color RING_COLOR{argb(40, 255, 255, 255)};
constexpr Color RING_NOW_COLOR{argb(150, 255, 255, 255)};
constexpr f32 MIN_RING_GAP{6.f};  // units between distance snap rings, under which only the ring for this time is drawn
constexpr Color BOX_OUTLINE{argb(180, 255, 255, 255)};
// the control points on a slider: squares on a shadow, joined by lines; the second point in a red anchor red
constexpr Color CONTROL_LINE{argb(150, 255, 255, 255)};
constexpr Color CONTROL_POINT{argb(150, 255, 255, 255)};
constexpr Color CONTROL_ANCHOR{argb(255, 255, 40, 40)};
constexpr Color CONTROL_SHADOW{argb(100, 0, 0, 0)};
constexpr Color CONTROL_POINT_HOVERED{argb(255, 255, 255, 255)};
// their half sizes, and how near a press takes one (units)
constexpr f32 CONTROL_POINT_SIZE{3.f};
constexpr f32 CONTROL_SHADOW_SIZE{4.f};
constexpr f32 CONTROL_POINT_REACH{6.f};
}  // namespace

PlayfieldPanel::PlayfieldPanel(EditorState &state) : CBaseUIElement(0, 0, 0, 0, "editor_playfield"), state(state) {
    this->setHandleRightMouse(true);
}

PlayfieldPanel::~PlayfieldPanel() {
    if(this->ringsTarget) resourceManager->destroyResource(this->ringsTarget);
}

void PlayfieldPanel::fit() {
    this->scale = std::max(0.01f, std::min(this->getSize().x / AREA.x, this->getSize().y / AREA.y));
    this->offset = this->getPos() + (this->getSize() - FIELD * this->scale) / 2.f;
}

void PlayfieldPanel::draw() {
    if(!this->isVisible()) return;
    uiStyle().drawPanel(this->getRect());
    const Skin *skin = osu->getSkin();
    if(!this->state.objects || !skin) return;

    MapObjects &objects = *this->state.objects;
    const Compose &compose = this->state.compose;
    const i32 time = this->state.clock.getTime();
    this->fit();
    objects.view.skin = skin;
    objects.view.scale = this->scale;
    objects.view.offset = this->offset;
    objects.view.musicPos = time;
    this->updateRings();

    g->pushClipRect(this->getRect());
    this->drawBackground();

    // what shows at the time (approaching, or fading out after its end), posed, latest-ending first
    const i32 fadeOutMS = (i32)(GameRules::getFadeOutTime() * 1000.0f);
    const i32 trailMS = this->state.showsHitAnimations() ? 0 : GameRules::EDITOR_TRAIL_TIME_MS;
    const i32 lead = (i32)objects.view.approachTimeMS + GameRules::getFadeInTime();
    this->shown.clear();
    const auto picked = compose.getPicked();
    const bool bodyPicked = picked && !picked->edge;
    const auto first = Mc::ranges::lower_bound(objects.byEndTime, time - SHOWN_AFTER_MS, {},
                                               [](const MapObjects::Shown &s) { return s.object->getEndTime(); });
    for(auto it = first; it != objects.byEndTime.end(); ++it) {
        HitObject *obj = it->object;
        if(obj->getEndTime() - objects.longestObjectMS > time + lead) break;
        if(obj->getClickTime() - lead > time) continue;
        if(!it->meshBuilt) {
            obj->rebuildVertexBuffer();
            it->meshBuilt = true;
        }
        obj->pose(time, fadeOutMS, trailMS);
        // selected slider borders and spinners are tinted, and a slider whose body is picked gets a red border
        obj->setTint(!compose.isSelected(it->id) ? std::nullopt
                     : obj->isSpinner()          ? std::optional{SELECTED_SPINNER}
                     : bodyPicked                ? std::optional{PICKED}
                                                 : std::optional{SELECTED_SLIDER});
        this->shown.push_back(obj);
    }
    std::reverse(this->shown.begin(), this->shown.end());

    // follow points fade out soon after the object they lead to
    HitObjects::drawFollowPoints(objects.view, objects.objects, objects.firstFrom(time - SHOWN_AFTER_MS));
    HitObjects::draw(this->shown, osu->getSliderFrameBuffer());
    for(auto it = this->shown.rbegin(); it != this->shown.rend(); ++it) (*it)->draw2();
    if(this->state.showsGrid()) this->drawGrid();
    this->drawMarks();
    this->drawReadout();

    g->popClipRect();
}

f32 PlayfieldPanel::controlPointReach() const { return uiStyle().px(CONTROL_POINT_REACH) / this->scale; }

void PlayfieldPanel::tick() {
    // looked for on disk only when it's another
    if(const DatabaseBeatmap *map = this->state.clock.getMap();
       map &&
       (map->getFolder() != this->backgroundFolder || this->state.document->getBackground() != this->backgroundFile)) {
        this->backgroundFolder = map->getFolder();
        this->backgroundFile = this->state.document->getBackground();
        this->backgroundOnDisk =
            !this->backgroundFile.empty() && Environment::fileExists(this->backgroundFolder + this->backgroundFile);
    }

    // a slider being placed aims at the cursor, while it's over the playfield; with Select, a slider under it shows
    // its control points
    Compose &compose = this->state.compose;
    this->fit();
    const auto cursor = this->isMouseInside() ? std::optional{this->toField(mouse->getPos())} : std::nullopt;
    if(compose.isPlacingSlider()) compose.aimSlider(cursor, this->state.snapSettings());
    compose.hover(this->gesture == Gesture::NONE ? cursor : std::nullopt, this->controlPointReach());
    CBaseUIElement::tick();
}

void PlayfieldPanel::drawGrid() const {
    // a line at every size multiple over the field, its edges included; the first ones from the middle brighter
    const i32 size = this->state.gridSize;
    const vec2 extent = FIELD * this->scale;
    const auto middle = [size](i32 length) { return (length / 2 + size - 1) / size * size; };
    for(i32 x = 0; x <= (i32)FIELD.x; x += size) {
        g->setColor(x == middle((i32)FIELD.x) ? GRID_MIDDLE_COLOR : GRID_COLOR);
        g->fillRectf(std::round(this->offset.x + (f32)x * this->scale), this->offset.y, 1.f, extent.y);
    }
    for(i32 y = 0; y <= (i32)FIELD.y; y += size) {
        g->setColor(y == middle((i32)FIELD.y) ? GRID_MIDDLE_COLOR : GRID_COLOR);
        g->fillRectf(this->offset.x, std::round(this->offset.y + (f32)y * this->scale), extent.x, 1.f);
    }
}

void PlayfieldPanel::drawGuide(vec2 from, vec2 to) const {
    // white, pulsing at its start and fading out along it
    const MapObjects &objects = *this->state.objects;
    from = objects.view.osuCoords2Pixels(from);
    to = objects.view.osuCoords2Pixels(to);
    const f32 pulse = std::abs(std::fmod((f32)(engine->getTime() * 1000.0), 600.f) - 300.f) / 300.f;
    for(i32 i = 0; i < GUIDE_PIECES; i++) {
        const f32 a = (f32)i / GUIDE_PIECES;
        const f32 b = (f32)(i + 1) / GUIDE_PIECES;
        g->setColor(argb(pulse * (1.f - a), 1.f, 1.f, 1.f));
        g->drawLine(from + (to - from) * a, from + (to - from) * b);
    }
}

void PlayfieldPanel::drawReadout() {
    // the position and the spacing to the objects before and after it, yellow for the selection
    const auto spot = this->state.compose.spot(this->toField(mouse->getPos()), this->state.snapSettings());
    if(!spot || (!spot->selected && !this->isMouseInside())) return;

    const UIStyle &style = uiStyle();
    McFont *font = style.font();
    const f32 padding = style.px(style.metrics.padding);
    f32 y = this->getPos().y + padding;
    const auto line = [&](const std::string &text) {
        y += font->getHeight() * 1.5f;
        const f32 x = std::round(this->getPos().x + this->getSize().x - padding - font->getStringWidth(text));
        for(const bool shadow : {true, false}) {
            g->setColor(shadow ? argb(160, 0, 0, 0) : spot->selected ? READOUT_SELECTED_COLOR : READOUT_COLOR);
            g->pushTransform();
            {
                g->translate(x + (shadow ? 1.f : 0.f), std::round(y) + (shadow ? 1.f : 0.f));
                g->drawString(font, text);
            }
            g->popTransform();
        }
    };
    line(fmt::format("x:{} y:{}", std::lround(spot->pos.x), std::lround(spot->pos.y)));
    if(spot->previous) line(tformat("Prev: {:.2f}x", *spot->previous));
    if(spot->next) line(tformat("Next: {:.2f}x", *spot->next));
}

void PlayfieldPanel::drawMarks() {
    const MapObjects &objects = *this->state.objects;
    const Compose &compose = this->state.compose;

    // the selection rings (see updateRings())
    if(this->ringsTarget && !this->drawnRings.empty()) {
        g->setBlendMode(DrawBlendMode::PREMUL_COLOR);
        g->pushTransform();
        {
            // by whole pixels, so that it stays sharp
            g->translate(std::round(this->ringsShift.x * this->scale), std::round(this->ringsShift.y * this->scale));
            this->ringsTarget->draw(0, 0);
        }
        g->popTransform();
        g->setBlendMode(DrawBlendMode::ALPHA);
    }

    // the circle a click would place (or the head on a slider too short to be placed yet)
    if(this->gesture == Gesture::NONE && (this->isMouseInside() || compose.isPlacingSlider())) {
        if(const auto preview = compose.previewCircle(this->toField(mouse->getPos()), this->state.snapSettings())) {
            Circle::drawCircle(objects.view, preview->at.pos, preview->number, preview->colorCounter,
                               preview->colorOffset, 1.f, 1.f, 1.f, 1.f, true);
            if(const auto &guide = preview->at.guide) this->drawGuide(guide->first, guide->second);
        }
    }
    // the distance snap rings around the object heads are spaced from: one per tick out to the far field corner (unless
    // they'd be too close to tell apart), the one for this time brighter
    if((this->gesture == Gesture::NONE && this->isMouseInside()) || (this->gesture == Gesture::MOVE && this->moving)) {
        if(const auto rings = compose.snapRings(this->state.snapSettings())) {
            const vec2 center = objects.view.osuCoords2Pixels(rings->center);
            const vec2 corner{
                rings->center.x < GameRules::OSU_COORD_WIDTH / 2.f ? (f32)GameRules::OSU_COORD_WIDTH : 0.f,
                rings->center.y < GameRules::OSU_COORD_HEIGHT / 2.f ? (f32)GameRules::OSU_COORD_HEIGHT : 0.f};
            const f32 reach = vec::distance(rings->center, corner);
            g->setColor(RING_COLOR);
            if(rings->tick * this->scale >= uiStyle().px(MIN_RING_GAP)) {
                for(f32 radius = rings->tick; radius <= reach; radius += rings->tick) {
                    g->drawArcf(center.x, center.y, radius * this->scale, 0.f, 2.f * PI_F);
                }
            }
            g->setColor(RING_NOW_COLOR);
            g->drawArcf(center.x, center.y, rings->radius * this->scale, 0.f, 2.f * PI_F);
        }
    }
    // the picked edge on the selected slider: a red ring
    if(const auto edge = compose.getPickedEdgePos()) {
        const BasicSkinImage &ring = objects.view.skin->i_hitcircleselect;
        const f32 ringScale = objects.view.rawHitcircleDiameter * this->scale / (128.f * ring.scale());
        const vec2 at = objects.view.osuCoords2Pixels(*edge);
        g->setColor(PICKED);
        g->pushTransform();
        {
            g->scale(ringScale, ringScale);
            g->translate(at.x, at.y);
            g->drawImage(ring);
        }
        g->popTransform();
    }
    if(const auto guide = compose.getMoveGuide()) this->drawGuide(guide->first, guide->second);
    this->drawControlPoints();

    if(this->gesture == Gesture::BOX) {
        const vec2 a = objects.view.osuCoords2Pixels(this->pressed);
        const vec2 b = mouse->getPos();
        const vec2 from = vec::min(a, b);
        const vec2 size = vec::abs(b - a);
        g->setColor(BOX_FILL);
        g->fillRectf(from.x, from.y, size.x, size.y);
        g->setColor(BOX_OUTLINE);
        g->drawRectf(from.x, from.y, size.x, size.y);
    }
}

void PlayfieldPanel::drawControlPoints() {
    const Compose &compose = this->state.compose;
    const auto shown = compose.controlPoints();
    if(!shown) return;
    const auto hovered = this->isMouseInside() || this->gesture == Gesture::POINT
                             ? compose.controlPointAt(this->toField(mouse->getPos()), this->controlPointReach())
                             : std::nullopt;
    const PlainPlayfieldView &view = this->state.objects->view;
    // the cursor last, while a slider is placed
    const uSz count = shown->points.size() + (shown->aim ? 1 : 0);
    const auto point = [&](uSz i) { return i < shown->points.size() ? shown->points[i] : *shown->aim; };
    const auto at = [&](uSz i) { return view.osuCoords2Pixels(point(i) + shown->offset); };

    g->setColor(CONTROL_LINE);
    for(uSz i = 1; i < count; i++) g->drawLine(at(i - 1), at(i));
    const f32 shadow = uiStyle().px(CONTROL_SHADOW_SIZE);
    const f32 size = uiStyle().px(CONTROL_POINT_SIZE);
    for(uSz i = 0; i < count; i++) {
        const vec2 pos = at(i);
        g->setColor(CONTROL_SHADOW);
        g->fillRectf(pos.x - shadow, pos.y - shadow, shadow * 2.f, shadow * 2.f);
        g->setColor(i > 0 && point(i) == point(i - 1) ? CONTROL_ANCHOR
                    : i == hovered                    ? CONTROL_POINT_HOVERED
                                                      : CONTROL_POINT);
        g->fillRectf(pos.x - size, pos.y - size, size * 2.f, size * 2.f);
    }
}

void PlayfieldPanel::updateRings() {
    const MapObjects &objects = *this->state.objects;
    const Compose &compose = this->state.compose;
    const BasicSkinImage &ring = objects.view.skin->i_hitcircleselect;
    const vec2 size = osu->getSliderFrameBuffer()->getSize();
    const RingsLook look{.image = ring.img,
                         .half = vec2{ring.getSize()} * (objects.view.rawHitcircleDiameter / (256.f * ring.scale())),
                         .scale = this->scale,
                         .offset = this->offset,
                         .size = size};
    const std::pair<u32, u32> now{objects.revision, compose.getSelectionVersion()};
    if((now == this->ringsFor && look == this->drawnLook) || !ring->isGPUReady()) return;
    this->ringsFor = now;

    // a ring around the head (and the slider end) on each selected object, however far it is from the time
    const auto ringsOf = [&](std::span<const Document::Id> ids) {
        std::vector<vec2> centers;
        for(const Document::Id id : ids) {
            const HitObject *object = objects.find(id);
            if(!object || object->isSpinner()) continue;
            centers.push_back(object->getRawPosAt(object->getClickTime()));
            if(object->isSlider()) centers.push_back(object->getRawPosAt(Compose::tailTime(*object)));
        }
        return centers;
    };
    const std::vector<Document::Id> &selection = compose.getSelection();
    std::vector<vec2> centers = ringsOf(selection);

    if(this->ringsTarget && look == this->drawnLook) {
        // more of the same, unmoved objects (a range or a box growing): only the new rings, onto the others
        if(objects.revision == this->drawnRevision && this->ringsShift == vec2{0.f} &&
           std::includes(selection.begin(), selection.end(), this->drawnIds.begin(), this->drawnIds.end())) {
            std::vector<Document::Id> added;
            std::set_difference(selection.begin(), selection.end(), this->drawnIds.begin(), this->drawnIds.end(),
                                std::back_inserter(added));
            this->drawRings(ringsOf(added), false);
            this->drawnIds = selection;
            this->drawnRings = std::move(centers);
            return;
        }
        // all moved by as much (the selection dragged, or not at all): the target as it is, shifted
        if(centers.size() == this->drawnRings.size()) {
            const vec2 by = centers[0] - this->drawnRings[0];
            bool together = true;
            for(uSz i = 1; together && i < centers.size(); i++) {
                together = vec::length(centers[i] - this->drawnRings[i] - by) < 0.01f;
            }
            if(together) {
                this->ringsShift = by;
                if(by == vec2{0.f}) {
                    this->drawnIds = selection;
                    this->drawnRevision = objects.revision;
                }
                return;
            }
        }
    }

    this->drawnIds = selection;
    this->drawnRevision = objects.revision;
    this->drawnRings = std::move(centers);
    this->drawnLook = look;
    this->ringsShift = vec2{0.f};
    if(this->drawnRings.empty()) {
        // a screen-sized target isn't kept without a use
        if(this->ringsTarget) resourceManager->destroyResource(this->ringsTarget);
        this->ringsTarget = nullptr;
        return;
    }
    if(!this->ringsTarget) {
        this->ringsTarget = resourceManager->createRenderTarget(0, 0, (int)size.x, (int)size.y);
    } else if(this->ringsTarget->getSize() != size) {
        this->ringsTarget->rebuild(0, 0, (int)size.x, (int)size.y);
    }
    this->drawRings(this->drawnRings, true);
}

void PlayfieldPanel::drawRings(std::span<const vec2> centers, bool clear) {
    const vec2 half = this->drawnLook.half;
    VertexArrayObject quads;
    for(const vec2 center : centers) {
        const vec2 a = center - half;
        const vec2 b = center + half;
        quads.addVertices(std::array<vec3, 6>{
            vec3{a.x, a.y, 0.f}, {a.x, b.y, 0.f}, {b.x, b.y, 0.f}, {a.x, a.y, 0.f}, {b.x, b.y, 0.f}, {b.x, a.y, 0.f}});
        quads.addTexcoords(
            std::array<vec2, 6>{vec2{0.f, 0.f}, {0.f, 1.f}, {1.f, 1.f}, {0.f, 0.f}, {1.f, 1.f}, {1.f, 0.f}});
    }
    // premultiplied in the target, which drawMarks() puts over the objects as it is
    const BasicSkinImage &ring = this->state.objects->view.skin->i_hitcircleselect;
    this->ringsTarget->enable(clear);
    g->setBlendMode(DrawBlendMode::PREMUL_ALPHA);
    g->setColor(argb(255, 255, 255, 255));
    g->pushTransform();
    {
        g->scale(this->scale, this->scale);
        g->translate(this->offset.x, this->offset.y);
        ring.bind();
        g->drawVAO(&quads);
        ring.unbind();
    }
    g->popTransform();
    g->setBlendMode(DrawBlendMode::ALPHA);
    this->ringsTarget->disable();
}

void PlayfieldPanel::drawBackground() const {
    const DatabaseBeatmap *map = this->state.clock.getMap();
    BGImageHandler *backgrounds = osu->getBackgroundImageHandler();
    const Image *image = !map->getSettings().draw_background ? nullptr
                         : this->backgroundOnDisk
                             ? backgrounds->getLoadBackgroundImage(this->backgroundFolder, this->backgroundFile)
                             : backgrounds->getLoadBackgroundImage(map);
    if(!image || !image->isReady()) return;

    const i32 time = this->state.clock.getTime();
    const auto &breaks = this->state.document->getPrimitives().breaks;
    const bool inBreak =
        Mc::ranges::any_of(breaks, [time](const auto &b) { return time >= b.startTime && time < b.endTime; });
    const f32 dim = this->state.backgroundDim ? this->state.backgroundDim->getFloat() : 0.f;
    const f32 brightness = inBreak ? 1.f : 1.f - dim;

    const f32 scale = Osu::getImageScaleToFillResolution(image, this->getSize());
    const vec2 center = this->getPos() + this->getSize() / 2.f;
    g->setColor(argb(1.f, brightness, brightness, brightness));
    g->pushTransform();
    {
        g->scale(scale, scale);
        g->translate((int)center.x, (int)center.y);
        g->drawImage(image);
    }
    g->popTransform();
}

void PlayfieldPanel::onMouseDownInside(bool left, bool right) {
    if(!this->state.objects) return;
    Compose &compose = this->state.compose;
    this->fit();
    const vec2 pos = this->toField(mouse->getPos());
    // the control points shown (on the selected slider, or the hovered one) come first
    const auto point = compose.getTool() == Compose::Tool::SELECT
                           ? compose.controlPointAt(pos, this->controlPointReach())
                           : std::nullopt;
    if(right) {
        if(point) {
            compose.removeControlPoint(*point);
        } else {
            compose.rightClick(compose.objectAt(pos));
        }
        return;
    }
    if(!left) return;

    // a click without a drag on the selected slider picks a part of it
    this->pressed = pos;
    this->pressedScreen = mouse->getPos();
    this->moving = false;
    this->picking = false;
    switch(compose.getTool()) {
        case Compose::Tool::SELECT: {
            if(point) {
                if(keyboard->isControlDown()) {
                    compose.makeRedAnchor(*point);
                } else {
                    // a click without a drag picks a part of the slider if it was the selected one already
                    const u32 selection = compose.getSelectionVersion();
                    compose.startPointDrag(*point);
                    this->gesture = Gesture::POINT;
                    this->picking = compose.getSelectionVersion() == selection;
                }
                return;
            }
            const auto hit = compose.objectAt(pos);
            if(keyboard->isControlDown()) {
                // on nothing or on the selected slider, a point for it, dragged on the same press
                if(compose.controlPoints() && (!hit || compose.isSelected(hit->id))) {
                    compose.addControlPoint(pos, this->state.snapSettings());
                    this->gesture = Gesture::POINT;
                } else if(hit) {
                    compose.toggle(hit->id);
                } else {
                    // a box that adds to the selection
                    this->kept = compose.getSelection();
                    this->gesture = Gesture::BOX;
                }
                return;
            }
            if(!hit) {
                // a box selects what's in it, nothing else
                compose.clearSelection();
                this->kept.clear();
                this->gesture = Gesture::BOX;
                return;
            }
            // a click never shrinks the selection
            const bool spinner = hit->object->isSpinner();
            this->picking = hit->object->isSlider() && compose.getSelection() == std::vector<Document::Id>{hit->id};
            if(!compose.isSelected(hit->id)) compose.select({hit->id});
            if(CBaseUIDispatch::getClicks(hit->id) == 2) compose.seekToSelection();
            if(!spinner) {
                compose.startMove();
                this->gesture = Gesture::MOVE;
            }
            return;
        }
        case Compose::Tool::CIRCLE:
            compose.placeCircle(pos, this->state.snapSettings());
            return;
        case Compose::Tool::SPINNER:
            compose.startSpinner();
            return;
        case Compose::Tool::SLIDER:
            // where the click is, which the aim in the last frame may not have been
            if(compose.isPlacingSlider()) {
                compose.aimSlider(pos, this->state.snapSettings());
                compose.addSliderPoint();
            } else {
                compose.startSlider(pos, this->state.snapSettings());
            }
            return;
    }
}

void PlayfieldPanel::onCapturedMouseMove() {
    if(!flags::has<MouseButtonFlags::MF_LEFT>(CBaseUIDispatch::getCaptorButtons())) return;
    // the map went while the button was held
    if(!this->state.objects) {
        this->onMouseCancel();
        return;
    }
    const vec2 pos = this->toField(mouse->getPos());
    Compose &compose = this->state.compose;
    if(this->gesture == Gesture::BOX) {
        compose.select(compose.objectsIn(McRect{vec::min(this->pressed, pos), vec::abs(pos - this->pressed)}),
                       this->kept);
        return;
    }
    // a drag once the cursor has left where it was pressed
    if(this->gesture == Gesture::NONE ||
       (!this->moving && vec::distance(mouse->getPos(), this->pressedScreen) < uiStyle().px(MOVE_THRESHOLD))) {
        return;
    }
    this->moving = true;
    if(this->gesture == Gesture::MOVE) {
        compose.move(pos - this->pressed, this->state.snapSettings());
    } else {
        compose.dragPoint(pos, this->state.snapSettings());
    }
}

void PlayfieldPanel::onMouseUpInside(bool left, bool /*right*/) {
    if(!left) return;
    Compose &compose = this->state.compose;
    if(this->picking && !this->moving) {
        if(const auto part = compose.partAt(this->pressed)) compose.pick(*part);
    }
    this->gesture = Gesture::NONE;
    this->picking = false;
    compose.endGesture();
}

void PlayfieldPanel::onMouseUpOutside(bool left, bool right) { this->onMouseUpInside(left, right); }

bool PlayfieldPanel::onWheel(WheelDelta vertical, WheelDelta /*horizontal*/) {
    // Alt and the wheel set the distance snap spacing (with Shift finer)
    if(!keyboard->isAltDown() || !this->state.clock.isAttached()) return false;
    if(vertical.ticks != 0) {
        const f32 step = keyboard->isShiftDown() ? 0.01f : 0.1f;
        this->state.spacing = Snapping::clampSpacing(this->state.spacing + step * (f32)vertical.ticks);
    }
    return true;
}

void PlayfieldPanel::onMouseCancel() {
    this->gesture = Gesture::NONE;
    this->picking = false;
    this->state.compose.endGesture();
}
