// Copyright (c) 2026, WH, All rights reserved.
#include "MainMenuNowPlaying.h"

#include "BeatmapInterface.h"
#include "DatabaseBeatmap.h"
#include "Engine.h"
#include "Font.h"
#include "Graphics.h"
#include "i18n.h"
#include "Icons.h"
#include "MainMenu.h"
#include "Mouse.h"
#include "Osu.h"
#include "Sound.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UIIconButton.h"
#include "UniString.h"

#include "fmt/format.h"

#include <algorithm>
#include <cmath>

namespace neomod::mainmenu {
namespace {

// panel geometry, in virtual-screen pixels at scale=1 (multiplied by Osu::getUIScale())
constexpr f32 PANEL_WIDTH{320.f};
constexpr f32 PAD{12.f};
constexpr f32 CORNER_RADIUS{8.f};
constexpr f32 TITLE_TOP{8.f};
constexpr f32 TITLE_HEIGHT{30.f};
constexpr f32 NOTE_GAP{8.f};
constexpr f32 BUTTON_HEIGHT{36.f};
constexpr f32 SIDE_BUTTON_WIDTH{44.f};
constexpr f32 MAIN_BUTTON_WIDTH{52.f};
constexpr f32 SEEK_HEIGHT{14.f};  // the clickable strip, the bar itself is SEEK_BAR_HEIGHT in its middle
constexpr f32 SEEK_BAR_HEIGHT{5.f};
constexpr f32 BOTTOM_PAD{6.f};

// a title too long for the panel rests at its start, then scrolls left until the copy following it took its place
constexpr f64 MARQUEE_REST{3.};
constexpr f32 MARQUEE_SPEED{30.f};  // per second
constexpr f32 MARQUEE_GAP{48.f};
constexpr f32 MARQUEE_FADE{20.f};
constexpr int MARQUEE_FADE_STEPS{5};

constexpr f32 TIME_TEXT_SCALE{0.8f};

std::string format_time(f64 secs) {
    const auto s = (u32)secs;
    return fmt::format("{}:{:02d}", s / 60, s % 60);
}

}  // namespace

// shows how far the song is and seeks it: a click jumps there, a drag moves the position along until released
class NowPlaying::SeekBar final : public CBaseUIElement {
    NOCOPY_NOMOVE(SeekBar)
   public:
    SeekBar() : CBaseUIElement(0, 0, 0, 0, "mainmenu_seekbar") {}
    ~SeekBar() override = default;

    void draw() override;
    void updateInput(CBaseUIEventCtx &c) override;

    // where the bar puts the song: where it plays, or where it's being dragged to
    [[nodiscard]] f64 getShownPercent(const Sound &music) const {
        return this->bActive ? this->getCursorPercent() : music.getPositionPct();
    }

   protected:
    void onMouseDownInside(bool left, bool right) override;
    void onMouseUpInside(bool left, bool right) override;
    void onMouseUpOutside(bool left, bool right) override;

   private:
    [[nodiscard]] f32 getCursorPercent() const {
        return std::clamp((mouse->getPos().x - this->getPos().x) / this->getSize().x, 0.f, 1.f);
    }
    void seek() const;
};

void NowPlaying::SeekBar::draw() {
    if(!this->isVisible()) return;

    const f32 scale = Osu::getUIScale();
    const f32 barHeight = std::round(SEEK_BAR_HEIGHT * scale);
    const McRect &rect = this->getRect();
    const f32 y = std::round(rect.getCenter().y - barHeight / 2.f);
    const bool highlighted = this->isMouseInside() || this->bActive;
    const auto fill = [&](f32 width, f32 alpha) {
        g->setColor(argb(alpha, 1.f, 1.f, 1.f));
        g->fillRectf({.x = rect.getX(),
                      .y = y,
                      .width = width,
                      .height = barHeight,
                      .cornerRadius = std::min(barHeight, width) / 2.f});
    };

    fill(rect.getWidth(), highlighted ? 0.35f : 0.25f);

    const Sound *music = osu->getMapInterface()->getMusic();
    if(!music || !music->isReady() || music->getLengthUS() == 0) return;

    // how far a click would seek
    if(this->isMouseInside() && !this->bActive) fill(rect.getWidth() * this->getCursorPercent(), 0.25f);

    const f32 fillWidth = rect.getWidth() * (f32)this->getShownPercent(*music);
    fill(fillWidth, 0.9f);

    if(highlighted) {
        const f32 knob = std::round(barHeight * 2.2f);
        g->setColor(argb(1.f, 1.f, 1.f, 1.f));
        g->fillRectf({.x = std::round(rect.getX() + fillWidth - knob / 2.f),
                      .y = std::round(rect.getCenter().y - knob / 2.f),
                      .width = knob,
                      .height = knob,
                      .cornerRadius = knob / 2.f});
    }
}

void NowPlaying::SeekBar::updateInput(CBaseUIEventCtx &c) {
    CBaseUIElement::updateInput(c);
    if(!this->isMouseInside() && !this->bActive) return;

    const Sound *music = osu->getMapInterface()->getMusic();
    if(!music || !music->isReady()) return;

    // the time a click would seek to
    auto *tooltips = ui->getTooltipOverlay();
    tooltips->begin();
    tooltips->addLine(format_time(this->getCursorPercent() * music->getLengthS()));
    tooltips->end();
}

void NowPlaying::SeekBar::onMouseDownInside(bool /*left*/, bool /*right*/) {
    // an enclosing scrollview must not turn the drag into a scroll
    this->lockCapture();
}

void NowPlaying::SeekBar::onMouseUpInside(bool /*left*/, bool /*right*/) { this->seek(); }
void NowPlaying::SeekBar::onMouseUpOutside(bool /*left*/, bool /*right*/) { this->seek(); }

void NowPlaying::SeekBar::seek() const {
    Sound *music = osu->getMapInterface()->getMusic();
    if(!music || !music->isReady()) return;
    music->setPositionS(this->getCursorPercent() * music->getLengthS());
}

NowPlaying::NowPlaying(MainMenu *mm) : CBaseUIContainer(0, 0, 0, 0, "mainmenu_nowplaying") {
    this->font = engine->getDefaultFont();
    this->iconFont = osu->getFontIcons();

    this->prevButton = new UIIconButton(Icons::STEP_BACKWARD, "mainmenu_prev");
    this->prevButton->setIconHeight(0.5f)->setTooltipText(_("Previous song"));
    this->prevButton->setClickCallback([mm] { mm->selectPreviousRandomBeatmap(); });
    this->addBaseUIElement(this->prevButton);

    this->pauseButton = new PauseButton("mainmenu_pause");
    this->pauseButton->setIconHeight(0.62f);
    this->addBaseUIElement(this->pauseButton);

    this->nextButton = new UIIconButton(Icons::STEP_FORWARD, "mainmenu_next");
    this->nextButton->setIconHeight(0.5f)->setTooltipText(_("Next song"));
    this->nextButton->setClickCallback([mm] { mm->selectRandomBeatmap(); });
    this->addBaseUIElement(this->nextButton);

    this->seekBar = new SeekBar();
    this->addBaseUIElement(this->seekBar);
}

NowPlaying::~NowPlaying() = default;

void NowPlaying::tick() {
    CBaseUIContainer::tick();

    const DatabaseBeatmap *map = osu->getMapInterface()->getBeatmap();
    this->setVisible(map != nullptr);

    std::string title = map ? fmt::format("{} - {}", map->getArtist(), map->getTitle()) : std::string{};
    if(title != this->title) {
        this->title = std::move(title);
        this->titleChangeTime = engine->getTime();
    }
}

void NowPlaying::updateLayout() {
    const f32 scale = Osu::getUIScale();
    const f32 buttonsY = (TITLE_TOP + TITLE_HEIGHT) * scale;
    const f32 seekY = buttonsY + BUTTON_HEIGHT * scale;
    this->setSize(PANEL_WIDTH * scale, seekY + (SEEK_HEIGHT + BOTTOM_PAD) * scale);

    const f32 center = this->getSize().x / 2.f;
    const vec2 mainSize{MAIN_BUTTON_WIDTH * scale, BUTTON_HEIGHT * scale};
    const vec2 sideSize{SIDE_BUTTON_WIDTH * scale, BUTTON_HEIGHT * scale};
    this->pauseButton->setRelPos(center - mainSize.x / 2.f, buttonsY)->setSize(mainSize);
    this->prevButton->setRelPos(center - mainSize.x / 2.f - sideSize.x, buttonsY)->setSize(sideSize);
    this->nextButton->setRelPos(center + mainSize.x / 2.f, buttonsY)->setSize(sideSize);

    this->seekBar->setRelPos(PAD * scale, seekY)->setSize(this->getSize().x - 2.f * PAD * scale, SEEK_HEIGHT * scale);

    this->update_pos();
}

void NowPlaying::draw() {
    if(!this->isVisible()) return;

    const f32 scale = Osu::getUIScale();
    const McRect &rect = this->getRect();
    g->setColor(argb(0.45f, 0.f, 0.f, 0.f));
    g->fillRectf({.x = rect.getX(),
                  .y = rect.getY(),
                  .width = rect.getWidth(),
                  .height = rect.getHeight(),
                  .cornerRadius = CORNER_RADIUS * scale});
    g->setColor(argb(0.15f, 1.f, 1.f, 1.f));
    g->drawRectf(Graphics::RectOptions{.x = rect.getX() + scale / 2.f,
                                       .y = rect.getY() + scale / 2.f,
                                       .width = rect.getWidth() - scale,
                                       .height = rect.getHeight() - scale,
                                       .lineThickness = scale,
                                       .cornerRadius = CORNER_RADIUS * scale});

    this->drawTitle();
    this->drawTimes();

    CBaseUIContainer::draw();
}

void NowPlaying::drawTimes() {
    const Sound *music = osu->getMapInterface()->getMusic();
    if(!music || !music->isReady() || music->getLengthUS() == 0) return;

    const f32 scale = Osu::getUIScale();
    const f32 baseline = std::round(this->getPos().y + (TITLE_TOP + TITLE_HEIGHT + BUTTON_HEIGHT / 2.f) * scale +
                                    this->font->getHeight() * TIME_TEXT_SCALE / 2.f);
    const auto drawAt = [&](const std::string &text, f32 x) {
        g->pushTransform();
        {
            g->scale(TIME_TEXT_SCALE, TIME_TEXT_SCALE);
            g->translate(std::round(x), baseline);
            g->drawString(this->font, text,
                          TextFX{.col_text = argb(0.7f, 1.f, 1.f, 1.f),
                                 .col_shadow = argb(0.4f, 0.f, 0.f, 0.f),
                                 .offs_px = std::round((f32)this->font->getDPI() / 96.0f),
                                 .shadow_softness_px = 1.f});
        }
        g->popTransform();
    };

    const std::string total = format_time(music->getLengthS());
    drawAt(format_time(this->seekBar->getShownPercent(*music) * music->getLengthS()), this->getPos().x + PAD * scale);
    drawAt(total,
           this->getPos().x + this->getSize().x - PAD * scale - this->font->getStringWidth(total) * TIME_TEXT_SCALE);
}

void NowPlaying::drawTitle() {
    if(this->title.empty()) return;

    const f32 scale = Osu::getUIScale();
    const std::string note = UniString::to_utf8(std::u32string_view{&Icons::MUSIC, 1});
    const f32 noteScale = this->font->getHeight() / this->iconFont->getGlyphHeight(Icons::MUSIC);
    const f32 noteWidth = this->iconFont->getStringWidth(note) * noteScale;
    const f32 noteGap = NOTE_GAP * scale;

    const f32 left = this->getPos().x + PAD * scale;
    const f32 width = this->getSize().x - 2.f * PAD * scale;
    const f32 baseline =
        std::round(this->getPos().y + (TITLE_TOP + TITLE_HEIGHT / 2.f) * scale + this->font->getHeight() / 2.f);
    const f32 textWidth = this->font->getStringWidth(this->title);

    // the note and the title are centered together, unless the title has to scroll past the note
    const bool fits = noteWidth + noteGap + textWidth <= width;
    const f32 noteX = fits ? std::round(left + (width - noteWidth - noteGap - textWidth) / 2.f) : left;
    g->pushTransform();
    {
        g->scale(noteScale, noteScale);
        g->translate(noteX, baseline);
        g->drawString(this->iconFont, note,
                      TextFX{.col_text = argb(0.7f, 1.f, 1.f, 1.f),
                             .col_shadow = argb(0.6f, 0.f, 0.f, 0.f),
                             .offs_px = std::round((f32)this->iconFont->getDPI() / 96.0f),
                             .shadow_softness_px = 1.f});
    }
    g->popTransform();

    const f32 textX = noteX + noteWidth + noteGap;
    const auto drawAt = [&](f32 x, f32 alpha) {
        g->pushTransform();
        {
            g->translate(x, baseline);
            g->drawString(this->font, this->title,
                          TextFX{.col_text = argb(alpha, 1.f, 1.f, 1.f),
                                 .col_shadow = argb(0.6f * alpha, 0.f, 0.f, 0.f),
                                 .offs_px = std::round((f32)this->font->getDPI() / 96.0f),
                                 .shadow_softness_px = 1.f});
        }
        g->popTransform();
    };

    if(fits) {
        drawAt(textX, 1.f);
        return;
    }

    const f32 speed = MARQUEE_SPEED * scale;
    const f32 period = textWidth + MARQUEE_GAP * scale;
    const f64 t = std::fmod(engine->getTime() - this->titleChangeTime, MARQUEE_REST + period / speed);
    const f32 offset = t < MARQUEE_REST ? 0.f : (f32)(t - MARQUEE_REST) * speed;

    const auto drawClipped = [&](f32 x0, f32 x1, f32 alpha) {
        x0 = std::round(x0);
        x1 = std::round(x1);
        if(x1 <= x0) return;
        g->pushClipRect(McRect(x0, this->getPos().y, x1 - x0, this->getSize().y));
        for(const f32 x : {textX - offset, textX - offset + period}) {
            if(x < x1 && x + textWidth > x0) drawAt(x, alpha);
        }
        g->popClipRect();
    };

    // rather than cutting glyphs off, the edges fade out in steps. on the left only while the title moves, and only
    // as far as it moved (so the start of either copy doesn't pop in or out of the fade)
    const f32 right = left + width;
    const f32 fadeRight = MARQUEE_FADE * scale;
    const f32 fadeLeft = std::min({fadeRight, offset, period - offset});
    drawClipped(textX + fadeLeft, right - fadeRight, 1.f);
    for(int i = 0; i < MARQUEE_FADE_STEPS; i++) {
        const f32 alpha = ((f32)i + 0.5f) / MARQUEE_FADE_STEPS;
        const f32 from = (f32)i / MARQUEE_FADE_STEPS;
        const f32 to = (f32)(i + 1) / MARQUEE_FADE_STEPS;
        drawClipped(textX + fadeLeft * from, textX + fadeLeft * to, alpha);
        drawClipped(right - fadeRight * to, right - fadeRight * from, alpha);
    }
}

}  // namespace neomod::mainmenu
