// Copyright (c) 2026, WH, All rights reserved.
#include "MapSettingsOverlay.h"

#include "Bancho.h"
#include "BeatmapInterface.h"
#include "CBaseUIEventCtx.h"
#include "Chat.h"
#include "ContainerRanges.h"
#include "ConVar.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "Font.h"
#include "Graphics.h"
#include "i18n.h"
#include "Icons.h"
#include "MakeDelegateWrapper.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "Overrides.h"
#include "PauseOverlay.h"
#include "Rect.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UICheckbox.h"
#include "UIIconButton.h"
#include "UISlider.h"
#include "Vectors.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

// geometry, in virtual-screen pixels at scale=1 (multiplied by Osu::getUIScale())
constexpr f32 HEADER_HEIGHT{32.f};  // all there is of the panel while it's collapsed
constexpr f32 ACCENT_HEIGHT{2.f};
constexpr f32 PAD{12.f};
constexpr f32 ROW_HEIGHT{30.f};
constexpr int ROWS{3};
constexpr f32 CONTENT_MAX_WIDTH{800.f};
constexpr f32 COLUMN_GAP{40.f};
constexpr f32 VALUE_WIDTH{64.f};
constexpr f32 RESET_WIDTH{26.f};

// how far the cursor has to move before it can open the panel
constexpr f32 MOUSE_MOVE_THRESHOLD{30.f};

// the offset slider's range (the keybinds go further)
constexpr f32 OFFSET_RANGE{300.f};

constexpr Color ACCENT{argb(255, 0, 114, 255)};
constexpr Color ACCENT_TRACK{argb(255, 70, 70, 70)};

// why the map can't change a convar right now (nullptr if it can)
const char *lockReason(const ConVar &cvar) {
    if(cvar.getMaster() == CvarEditor::SERVER) {
        return cvar.isLocked() ? _("This setting is locked in multiplayer rooms.")
                               : _("This setting is forced by the server.");
    }
    if(!Osu::isConVarEditable(cvar, CvarEditor::CONTEXT)) {
        return _("This setting can't be changed during a multiplayer match.");
    }
    return nullptr;
}

void drawText(McFont *font, const std::string &text, f32 x, f32 centerY, f32 alpha) {
    g->pushTransform();
    {
        g->translate(std::round(x), std::round(centerY + font->getHeight() / 2.f));
        g->drawString(font, text,
                      TextFX{.col_text = argb(alpha, 1.f, 1.f, 1.f),
                             .col_shadow = argb(0.6f * alpha, 0.f, 0.f, 0.f),
                             .offs_px = std::round((f32)font->getDPI() / 96.0f),
                             .shadow_softness_px = 1.f});
    }
    g->popTransform();
}

}  // namespace

MapSettingsOverlay::MapSettingsOverlay() : UIScreen() {
    this->font = engine->getDefaultFont();

    const auto makeSlider = [this](const char *name, f32 min, f32 max, f32 keyDelta) {
        auto *slider = new UISlider(0, 0, 0, 0, name);
        slider->setOrientation(true)
            ->setBounds(min, max)
            ->setKeyDelta(keyDelta)
            ->setLiveUpdate(true)
            ->setAllowMouseWheel(true)
            ->setDrawFrame(false)
            ->setDrawBackground(false)
            ->setFrameColor(rgb(200, 200, 200));
        this->addBaseUIElement(slider);
        return slider;
    };
    const auto makeReset = [this](std::string name, const char *tooltip) {
        auto *reset = new UIIconButton(Icons::UNDO, std::move(name));
        reset->setIconHeight(0.5f)->setTooltipText(tooltip);
        this->addBaseUIElement(reset);
        return reset;
    };

    this->dimSlider = makeSlider("mapsettings_dim", 0.f, 1.f, 0.05f);
    this->dimSlider->setChangeCallback(SA::MakeDelegate<&MapSettingsOverlay::onDimChange>(this));
    this->dimReset = makeReset("mapsettings_dim_reset", _("Use the usual setting"));
    this->dimReset->setClickCallback([this] { this->setMapConvar(cv::background_dim, std::nullopt); });

    this->offsetSlider = makeSlider("mapsettings_offset", -OFFSET_RANGE, OFFSET_RANGE, 1.f);
    this->offsetSlider->setChangeCallback(SA::MakeDelegate<&MapSettingsOverlay::onOffsetChange>(this));
    this->offsetReset = makeReset("mapsettings_offset_reset", _("Reset"));
    this->offsetReset->setClickCallback([this] { this->setLocalOffset(0); });

    // (named after their convars)
    const auto makeToggle = [&](const char *text, ConVar *cvar, bool inverted) -> Toggle {
        const std::string name = fmt::format("mapsettings_{}", cvar->getName());
        auto *checkbox = new UICheckbox(0, 0, 0, 0, name, text);
        checkbox->setDrawFrame(false);
        checkbox->setDrawBackground(false);
        checkbox->setChangeCallback(SA::MakeDelegate<&MapSettingsOverlay::onToggleChange>(this));
        this->addBaseUIElement(checkbox);
        UIIconButton *reset = makeReset(name + "_reset", _("Use the usual setting"));
        reset->setClickCallback([this, cvar] { this->setMapConvar(*cvar, std::nullopt); });
        return {.cvar = cvar, .inverted = inverted, .checkbox = checkbox, .reset = reset};
    };
    this->toggles = {
        makeToggle(_("Storyboard"), &cv::draw_storyboard, false),
        makeToggle(_("Beatmap skin"), &cv::ignore_beatmap_skins, true),
        makeToggle(_("Beatmap hitsounds"), &cv::ignore_beatmap_samples, true),
    };

    this->updateLayout();
}

MapSettingsOverlay::~MapSettingsOverlay() = default;

void MapSettingsOverlay::onPlayStart() {
    // (a match never waits for the panel: it's a strip there for as long as the match waits for its players)
    if(cv::map_settings_hold_time.getFloat() <= 0.f || BanchoState::is_in_a_multi_room()) return;

    this->bPeeking = true;
    this->accent.stop();
    this->accent = 1.f;
}

f32 MapSettingsOverlay::getFullHeight() const {
    return std::round((HEADER_HEIGHT + PAD * 2.f + ROW_HEIGHT * ROWS) * Osu::getUIScale());
}

void MapSettingsOverlay::tick() {
    UIScreen::tick();

    // (the input pass comes after this one)
    const bool wasHovered = std::exchange(this->bHovered, false);

    BeatmapInterface *bmi = osu->getMapInterface();
    if(!osu->isInPlayMode() || !bmi->getBeatmap() || bmi->getKind() == BeatmapInterface::PlayKind::SPECTATE) {
        this->bPeeking = false;
        this->bAvailable = false;
        this->targetHeight = 0.f;
        this->shownHeight.stop();
        this->shownHeight = 0.f;
        this->accent.stop();
        this->setVisible(false);
        return;
    }

    // it opens on hover whenever the settings can be changed without holding anything up: from a strip while paused or
    // while a match waits for its players, from the bare edge while a continue waits for its click (which keeps the
    // screen clear). the chat and the gameplay mod selector have the bottom of the screen to themselves
    const bool strip =
        ui->getPauseOverlay()->isVisible() || (BanchoState::is_playing_a_multi_map() && bmi->isLoading());
    const bool offered = strip || bmi->isContinueScheduled();
    const bool available =
        (this->bPeeking || offered) && !ui->getChat()->isVisible() && !ui->getModSelector()->isVisible();
    if(available && !this->bAvailable) {
        this->mousePosWhenAvailable = mouse->getPos();
        this->bMouseMoved = false;
    }
    this->bAvailable = available;

    const f32 scale = Osu::getUIScale();
    if(!this->bMouseMoved) {
        this->bMouseMoved = vec::length(mouse->getPos() - this->mousePosWhenAvailable) > MOUSE_MOVE_THRESHOLD * scale;
    }
    // (held as well while the cursor is off it, e.g. a slider being dragged past its end)
    const bool inUse = available && ((wasHovered && this->bMouseMoved) || this->isBusy());

    // the play's wait for it is the accent running out: held full while loading or in use, then over
    // map_settings_hold_time
    if(this->bPeeking) {
        if(bmi->isActuallyLoading() || inUse) {
            this->accent.stop();
            this->accent = 1.f;
        } else if(!this->accent.animating()) {
            if(this->accent > 0.f) {
                this->accent.set(0.f, cv::map_settings_hold_time.getFloat(), anim::Linear);
            } else {
                this->bPeeking = false;
            }
        }
    }

    const f32 target = !available                ? 0.f
                       : this->bPeeking || inUse ? this->getFullHeight()
                       : strip                   ? std::round(HEADER_HEIGHT * scale)
                                                 : 0.f;
    // (going away, it stays as it was: run out after a play's wait for it, full otherwise)
    if(!this->bPeeking && target > 0.f) this->accent = 1.f;
    if(target != this->targetHeight) {
        this->shownHeight.set(target, target > this->targetHeight ? 0.2f : 0.3f, anim::QuadOut);
        this->targetHeight = target;
    }

    // its rect is the part that's out, the rest is clipped away (see draw())
    const f32 shown = std::round((f32)this->shownHeight);
    this->setVisible(shown > 0.f);
    this->setPos(0.f, (f32)osu->getVirtScreenHeight() - shown);
    this->setSize((f32)osu->getVirtScreenWidth(), shown);
    if(this->bVisible) this->syncWidgets();
}

void MapSettingsOverlay::syncWidgets() {
    const MapSettings &settings = osu->getMapInterface()->getBeatmap()->getSettings();
    const auto isOwn = [&settings](const ConVar &cvar) {
        return Mc::ranges::contains(settings.convars, cvar.getName(), &std::pair<std::string, std::string>::first);
    };

    const bool dimEditable = !lockReason(cv::background_dim);
    if(!this->dimSlider->isActive()) this->dimSlider->setValue(cv::background_dim.getFloat(), false, false);
    this->dimSlider->setEnabled(dimEditable);
    this->dimReset->setVisible(dimEditable && isOwn(cv::background_dim));

    if(!this->offsetSlider->isActive()) this->offsetSlider->setValue(settings.local_offset, false, false);
    this->offsetReset->setVisible(settings.local_offset != 0);

    for(Toggle &toggle : this->toggles) {
        const bool editable = !lockReason(*toggle.cvar);
        toggle.checkbox->setChecked(toggle.cvar->getBool() != toggle.inverted, false);
        toggle.checkbox->setEnabled(editable);
        toggle.checkbox->setTextColor(editable ? rgb(255, 255, 255) : rgb(128, 128, 128));
        toggle.reset->setVisible(editable && isOwn(*toggle.cvar));
    }
}

void MapSettingsOverlay::onDimChange(CBaseUISlider *slider) {
    this->setMapConvar(cv::background_dim, slider->getFloat());
}

void MapSettingsOverlay::onOffsetChange(CBaseUISlider *slider) {
    this->setLocalOffset((i16)std::lround(slider->getFloat()));
}

void MapSettingsOverlay::onToggleChange(CBaseUICheckbox *checkbox) {
    const auto it = Mc::ranges::find(this->toggles, checkbox, &Toggle::checkbox);
    if(it != this->toggles.end()) this->setMapConvar(*it->cvar, checkbox->isChecked() != it->inverted ? 1. : 0.);
}

void MapSettingsOverlay::setMapConvar(const ConVar &cvar, std::optional<double> value) {
    BeatmapInterface *bmi = osu->getMapInterface();
    DatabaseBeatmap *map = bmi->getBeatmapMutable();
    if(!map) return;

    MapSettings settings = map->getSettings();
    std::erase_if(settings.convars, [&cvar](const auto &entry) { return entry.first == cvar.getName(); });
    if(value) settings.convars.emplace_back(cvar.getName(), fmt::format("{:g}", *value));
    db->setSettings(map, std::move(settings));
    bmi->applyMapSettings();
}

void MapSettingsOverlay::setLocalOffset(i16 offset) {
    DatabaseBeatmap *map = osu->getMapInterface()->getBeatmapMutable();
    if(!map || offset == map->getSettings().local_offset) return;

    MapSettings settings = map->getSettings();
    settings.local_offset = offset;
    db->setSettings(map, std::move(settings));
}

void MapSettingsOverlay::updateInput(CBaseUIEventCtx &c) {
    // (with nothing of it out, it still opens from the bottom edge)
    if(!this->bVisible && !this->bAvailable) return;

    const uSz firstCandidate = c.hitCandidates.size();
    UIScreen::updateInput(c);

    // what's out of the panel takes the cursor, and the bottom edge does while it's available, but not where a layer
    // above it has the cursor or a continue waits for its click
    const vec2 cursor = mouse->getPos();
    const bool onPanel = this->getRect().contains(cursor) ||
                         (this->bAvailable &&
                          cursor.y >= (f32)osu->getVirtScreenHeight() - std::round(HEADER_HEIGHT * Osu::getUIScale()));
    const bool covered = Mc::ranges::any_of(std::span(c.hitCandidates).first(firstCandidate),
                                            [](const auto &cand) { return !cand.clipped && !cand.wheelOnly; });
    if(!onPanel || covered || osu->getMapInterface()->isCursorOnContinuePoint()) {
        for(uSz i = firstCandidate; i < c.hitCandidates.size(); i++) c.hitCandidates[i].clipped = true;
        return;
    }
    this->bHovered = true;
    c.consume_mouse();

    // a row that can't be changed says why, and one whose change would make the play unranked says so
    const ConVar *rowCvar = this->dimSlider->getRect().contains(cursor) ? &cv::background_dim : nullptr;
    for(const Toggle &toggle : this->toggles) {
        if(toggle.checkbox->getRect().contains(cursor)) rowCvar = toggle.cvar;
    }
    if(!rowCvar) return;

    const char *line = lockReason(*rowCvar);
    if(!line && rowCvar->isFlagSet(cv::GAMEPLAY) && osu->getMapInterface()->bLeadInStarted) {
        line = _("Changing this mid-play makes the play unranked.");
    }
    if(!line) return;

    TooltipOverlay *tooltips = ui->getTooltipOverlay();
    tooltips->begin();
    tooltips->addLine(line);
    tooltips->end();
}

void MapSettingsOverlay::updateLayout() {
    const f32 scale = Osu::getUIScale();
    const auto screenWidth = (f32)osu->getVirtScreenWidth();

    const f32 contentWidth = std::min(screenWidth - 2.f * PAD * scale, std::round(CONTENT_MAX_WIDTH * scale));
    const f32 columnWidth = std::round((contentWidth - COLUMN_GAP * scale) / 2.f);
    const f32 left = std::round((screenWidth - contentWidth) / 2.f);
    const f32 right = left + columnWidth + std::round(COLUMN_GAP * scale);
    const f32 rowHeight = std::round(ROW_HEIGHT * scale);
    const f32 resetWidth = std::round(RESET_WIDTH * scale);
    const auto rowY = [&](int row) { return std::round((HEADER_HEIGHT + PAD) * scale) + (f32)row * rowHeight; };

    // the names (in whichever language) decide how much of the row is left for the sliders
    const f32 labelWidth = std::round(
        std::max(this->font->getStringWidth(_("Background dim")), this->font->getStringWidth(_("Local offset"))) +
        PAD * scale);
    this->labelX = left;
    const f32 sliderX = left + labelWidth;
    const f32 sliderWidth = columnWidth - labelWidth - std::round(VALUE_WIDTH * scale) - resetWidth;
    const f32 block = std::round(rowHeight * 0.6f);
    for(int row = 0; UISlider *slider : {this->dimSlider, this->offsetSlider}) {
        slider->setBlockSize(block, block);
        slider->setRelPos(sliderX, rowY(row++))->setSize(sliderWidth, rowHeight);
    }
    this->dimReset->setRelPos(left + columnWidth - resetWidth, rowY(0))->setSize(resetWidth, rowHeight);
    this->offsetReset->setRelPos(left + columnWidth - resetWidth, rowY(1))->setSize(resetWidth, rowHeight);

    for(int row = 0; Toggle &toggle : this->toggles) {
        toggle.checkbox->setRelPos(right, rowY(row))->setSize(columnWidth - resetWidth, rowHeight);
        toggle.reset->setRelPos(right + columnWidth - resetWidth, rowY(row++))->setSize(resetWidth, rowHeight);
    }

    this->update_pos();
}

void MapSettingsOverlay::onResolutionChange(vec2 /*newResolution*/) { this->updateLayout(); }

void MapSettingsOverlay::draw() {
    if(!this->bVisible) return;

    const f32 scale = Osu::getUIScale();
    const McRect &rect = this->getRect();
    const f32 headerHeight = std::round(HEADER_HEIGHT * scale);

    // (it unrolls from its top edge: what isn't out yet stays behind whatever it rests on)
    g->pushClipRect(rect);

    g->setColor(rgb(15, 15, 15).setA(0.85f));
    g->fillRect(rect);

    // (the track only shows where the accent has run out)
    const f32 accentHeight = std::round(ACCENT_HEIGHT * scale);
    g->setColor(ACCENT_TRACK);
    g->fillRectf(rect.getX(), rect.getY(), rect.getWidth(), accentHeight);
    g->setColor(ACCENT);
    g->fillRectf(rect.getX(), rect.getY(), rect.getWidth() * (f32)this->accent, accentHeight);

    const DatabaseBeatmap *map = osu->getMapInterface()->getBeatmap();
    const f32 left = rect.getX() + this->labelX;
    const f32 headerCenter = rect.getY() + headerHeight / 2.f;
    const std::string title = _("Beatmap settings");
    drawText(this->font, title, left, headerCenter, 1.f);
    if(map) {
        drawText(this->font, fmt::format("[{}]", map->getDifficultyName()),
                 left + this->font->getStringWidth(title) + std::round(PAD * scale), headerCenter, 0.6f);
    }

    const auto drawRow = [&](UISlider *slider, const std::string &label, const std::string &value) {
        const f32 center = slider->getPos().y + slider->getSize().y / 2.f;
        drawText(this->font, label, left, center, slider->isEnabled() ? 0.9f : 0.4f);
        drawText(this->font, value, slider->getPos().x + slider->getSize().x + std::round(PAD * scale), center, 0.9f);
    };
    drawRow(this->dimSlider, _("Background dim"),
            fmt::format("{:d}%", (int)std::lround(this->dimSlider->getFloat() * 100.f)));
    drawRow(this->offsetSlider, _("Local offset"), tformat("{:d} ms", map ? map->getLocalOffset() : 0));

    UIScreen::draw();

    g->popClipRect();
}
