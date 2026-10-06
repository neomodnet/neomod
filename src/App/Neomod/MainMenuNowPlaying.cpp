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
#include "MusicTrack.h"
#include "Osu.h"
#include "OsuConVars.h"
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
constexpr f32 PANEL_MAX_WIDTH{400.f};
constexpr f32 PAD{8.f};
constexpr f32 TITLE_HEIGHT{32.f};  // with the progress line below it, all there is of the panel while it's collapsed
constexpr f32 ICON_GAP{8.f};
constexpr f32 PIN_WIDTH{28.f};
constexpr f32 BUTTON_HEIGHT{36.f};
constexpr f32 SIDE_BUTTON_WIDTH{44.f};
constexpr f32 MAIN_BUTTON_WIDTH{52.f};
constexpr f32 SEEK_HEIGHT{14.f};  // the clickable strip along the bottom edge, the bar itself is at its bottom
constexpr f32 SEEK_BAR_HEIGHT{5.f};
constexpr f32 COLLAPSED_BAR_HEIGHT{2.f};

// unless it's pinned open, the panel collapses into its title when left alone
constexpr f64 EXPAND_LINGER{1.5};  // after the cursor left it
constexpr f64 EXPAND_INTRO{8.};    // after the game started, so the controls don't stay a secret

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

// shows how far the song is along the bottom edge and seeks it: a click jumps there, a drag moves the position along
// until released. just a thin line while the panel is collapsed
class NowPlaying::SeekBar final : public CBaseUIElement {
    NOCOPY_NOMOVE(SeekBar)
   public:
    SeekBar(const NowPlaying &panel) : CBaseUIElement(0, 0, 0, 0, "mainmenu_seekbar"), panel(panel) {}
    ~SeekBar() override = default;

    void draw() override;
    void updateInput(CBaseUIEventCtx &c) override;

    // where the bar puts the song: where it plays, or where it's being dragged to
    [[nodiscard]] f64 getShownPercent(const MusicTrack &music) const {
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

    const NowPlaying &panel;
};

void NowPlaying::SeekBar::draw() {
    if(!this->isVisible()) return;

    const f32 scale = Osu::getUIScale();
    const f32 barHeight =
        std::round(std::lerp(COLLAPSED_BAR_HEIGHT, SEEK_BAR_HEIGHT, (f32)this->panel.expandAnim) * scale);
    const McRect &rect = this->getRect();
    const f32 y = rect.getMaxY() - barHeight;
    const auto fill = [&](f32 width, f32 alpha) {
        g->setColor(argb(alpha, 1.f, 1.f, 1.f));
        g->fillRectf(rect.getX(), y, width, barHeight);
    };

    fill(rect.getWidth(), this->isMouseInside() || this->bActive ? 0.3f : 0.15f);

    const MusicTrack *music = osu->getMusicTrack();
    if(!music->isReady() || music->getLengthMS() == 0) return;

    // how far a click would seek
    if(this->isMouseInside() && !this->bActive) fill(rect.getWidth() * this->getCursorPercent(), 0.25f);

    fill(rect.getWidth() * (f32)this->getShownPercent(*music), 0.9f);
}

void NowPlaying::SeekBar::updateInput(CBaseUIEventCtx &c) {
    CBaseUIElement::updateInput(c);
    if(!this->isMouseInside() && !this->bActive) return;

    const MusicTrack *music = osu->getMusicTrack();
    if(!music->isReady()) return;

    // the time a click would seek to
    auto *tooltips = ui->getTooltipOverlay();
    tooltips->begin();
    tooltips->addLine(format_time(this->getCursorPercent() * music->getLengthMS() / 1000.));
    tooltips->end();
}

void NowPlaying::SeekBar::onMouseDownInside(bool /*left*/, bool /*right*/) {
    // an enclosing scrollview must not turn the drag into a scroll
    this->lockCapture();
}

void NowPlaying::SeekBar::onMouseUpInside(bool /*left*/, bool /*right*/) { this->seek(); }
void NowPlaying::SeekBar::onMouseUpOutside(bool /*left*/, bool /*right*/) { this->seek(); }

void NowPlaying::SeekBar::seek() const {
    MusicTrack *music = osu->getMusicTrack();
    if(!music->isReady()) return;
    music->setPosition((i32)std::round(this->getCursorPercent() * music->getLengthMS()));
}

// pins the panel open (main_menu_music_controls_pinned): the pin stands upright while it's pinned and lies tilted while
// the panel collapses by itself
class NowPlaying::PinButton final : public UIIconButton {
    NOCOPY_NOMOVE(PinButton)
   public:
    PinButton()
        : UIIconButton(Icons::THUMB_TACK, "mainmenu_pin"), pinned(cv::main_menu_music_controls_pinned.getBool()) {
        this->iconRotation = this->pinned ? 0.f : TILT;
        this->tooltipText = tooltipFor(this->pinned);
        this->setClickCallback(
            [] { cv::main_menu_music_controls_pinned.setValue(!cv::main_menu_music_controls_pinned.getBool()); });
    }
    ~PinButton() override = default;

    void tick() override {
        UIIconButton::tick();

        const bool pinned = cv::main_menu_music_controls_pinned.getBool();
        if(pinned == this->pinned) return;
        this->pinned = pinned;
        this->iconRotation.set(pinned ? 0.f : TILT, 0.15f, anim::QuadOut);
        this->tooltipText = tooltipFor(pinned);
    }

   private:
    static constexpr f32 TILT{45.f};
    static const char *tooltipFor(bool pinned) { return pinned ? _("Collapse when idle") : _("Keep expanded"); }

    bool pinned;
};

NowPlaying::NowPlaying(MainMenu *mm)
    : CBaseUIContainer(0, 0, 0, 0, "mainmenu_nowplaying"), expandedUntil(engine->getTime() + EXPAND_INTRO) {
    this->font = engine->getDefaultFont();
    this->iconFont = osu->getFontIcons();

    this->pinButton = new PinButton();
    this->pinButton->setIconHeight(0.45f);
    this->addBaseUIElement(this->pinButton);

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

    this->seekBar = new SeekBar(*this);
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
        this->marqueeTime = 0.;
        this->marqueeScrollFinished = false;
    }

    const f64 now = engine->getTime();
    if(this->isMouseInside() || this->seekBar->isActive()) {
        this->expandedUntil = std::max(this->expandedUntil, now + EXPAND_LINGER);
    }
    const bool expand = cv::main_menu_music_controls_pinned.getBool() || now < this->expandedUntil;
    if(expand != this->expanded) {
        this->expanded = expand;
        this->expandAnim.set(expand ? 1.f : 0.f, expand ? 0.15f : 0.25f, anim::QuadOut);
    }

    // the controls are only there while (partly) unrolled, and the seek bar only takes input once it's all the way
    const bool unrolled = this->expandAnim > 0.f;
    this->prevButton->setVisible(unrolled);
    this->pauseButton->setVisible(unrolled);
    this->nextButton->setVisible(unrolled);
    this->seekBar->setEnabled(this->expandAnim >= 1.f);

    // a long title keeps scrolling while the panel is expanded, but while it's collapsed only goes around once (per
    // song, or after the panel was expanded) before it comes to rest at its start
    if(this->expanded) this->marqueeScrollFinished = false;
    if(!this->marqueeScrollFinished) {
        const f32 scale = Osu::getUIScale();
        const f64 cycle =
            MARQUEE_REST + (this->font->getStringWidth(this->title) + MARQUEE_GAP * scale) / (MARQUEE_SPEED * scale);
        this->marqueeTime += engine->getFrameTime();
        if(this->marqueeTime >= cycle) {
            this->marqueeScrollFinished = !this->expanded;
            this->marqueeTime = this->marqueeScrollFinished ? 0. : this->marqueeTime - cycle;
        }
    }
}

void NowPlaying::updateLayout() {
    const f32 scale = Osu::getUIScale();
    const f32 width =
        std::min(((f32)osu->getVirtScreenWidth() - (PAD * 2)) * scale, std::round(PANEL_MAX_WIDTH * scale));
    const f32 border = std::round(scale);
    const f32 titleHeight = std::round(TITLE_HEIGHT * scale);
    const f32 buttonHeight = std::round(BUTTON_HEIGHT * scale);
    const f32 seekHeight = std::round(SEEK_HEIGHT * scale);
    const f32 collapsedHeight = titleHeight + std::round(COLLAPSED_BAR_HEIGHT * scale);
    this->setSize(
        width, std::round(std::lerp(collapsedHeight, titleHeight + buttonHeight + seekHeight, (f32)this->expandAnim)));

    const f32 pinWidth = std::round(PIN_WIDTH * scale);
    this->pinButton->setRelPos(width - border - pinWidth, 0)->setSize(pinWidth, titleHeight);

    const f32 mainWidth = std::round(MAIN_BUTTON_WIDTH * scale);
    const f32 sideWidth = std::round(SIDE_BUTTON_WIDTH * scale);
    const f32 mainX = std::round((width - mainWidth) / 2.f);
    this->pauseButton->setRelPos(mainX, titleHeight)->setSize(mainWidth, buttonHeight);
    this->prevButton->setRelPos(mainX - sideWidth, titleHeight)->setSize(sideWidth, buttonHeight);
    this->nextButton->setRelPos(mainX + mainWidth, titleHeight)->setSize(sideWidth, buttonHeight);

    // the bottom edge moves down as the panel unrolls, and the seek bar with it
    this->seekBar->setRelPos(border, this->getSize().y - border - seekHeight)
        ->setSize(width - 2.f * border, seekHeight);

    this->update_pos();
}

void NowPlaying::draw() {
    if(!this->isVisible()) return;

    const f32 scale = Osu::getUIScale();
    const McRect &rect = this->getRect();

    // like the online beatmaps screen's preview panel
    g->setColor(rgb(15, 15, 15).setA(0.85f));
    g->fillRect(rect);
    g->setColor(rgb(80, 80, 80));
    g->drawRectf(Graphics::RectOptions{.x = rect.getX() + scale / 2.f,
                                       .y = rect.getY() + scale / 2.f,
                                       .width = rect.getWidth() - scale,
                                       .height = rect.getHeight() - scale,
                                       .lineThickness = scale,
                                       .withColor = false});

    // whatever isn't unrolled yet stays hidden
    g->pushClipRect(rect);
    {
        // fades in with the controls: while collapsed, the progress line takes its row
        if(this->expandAnim > 0.f) {
            g->setColor(rgb(50, 50, 50).setA(0.85f * this->expandAnim));
            g->fillRect((int)(rect.getX() + PAD * scale), (int)(rect.getY() + std::round(TITLE_HEIGHT * scale)),
                        (int)(rect.getWidth() - 2.f * PAD * scale), 1);
        }

        this->drawTitle();
        this->drawTimes();
        CBaseUIContainer::draw();
    }
    g->popClipRect();
}

void NowPlaying::drawTimes() {
    if(this->expandAnim <= 0.f) return;

    const MusicTrack *music = osu->getMusicTrack();
    if(!music->isReady() || music->getLengthMS() == 0) return;

    const f32 scale = Osu::getUIScale();
    const f32 baseline = std::round(this->getPos().y + std::round(TITLE_HEIGHT * scale) + BUTTON_HEIGHT * scale / 2.f +
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

    const f64 lengthS = music->getLengthMS() / 1000.;
    const std::string total = format_time(lengthS);
    drawAt(format_time(this->seekBar->getShownPercent(*music) * lengthS), this->getPos().x + PAD * scale);
    drawAt(total,
           this->getPos().x + this->getSize().x - PAD * scale - this->font->getStringWidth(total) * TIME_TEXT_SCALE);
}

void NowPlaying::drawTitle() {
    if(this->title.empty()) return;

    const f32 scale = Osu::getUIScale();
    const f32 left = this->getPos().x + PAD * scale;
    const f32 baseline =
        std::round(this->getPos().y + std::round(TITLE_HEIGHT * scale) / 2.f + this->font->getHeight() / 2.f);

    // whether it plays, in front of the title
    const char32_t state = osu->getMusicTrack()->isPlaying() ? Icons::MUSIC : Icons::PAUSE;
    const std::string stateGlyph = UniString::to_utf8(std::u32string_view{&state, 1});
    const f32 stateScale = this->font->getHeight() / this->iconFont->getGlyphHeight(state);
    g->pushTransform();
    {
        g->scale(stateScale, stateScale);
        g->translate(left, baseline);
        g->drawString(this->iconFont, stateGlyph,
                      TextFX{.col_text = argb(0.7f, 1.f, 1.f, 1.f),
                             .col_shadow = argb(0.6f, 0.f, 0.f, 0.f),
                             .offs_px = std::round((f32)this->iconFont->getDPI() / 96.0f),
                             .shadow_softness_px = 1.f});
    }
    g->popTransform();

    const f32 textX = std::round(left + this->iconFont->getStringWidth(stateGlyph) * stateScale + ICON_GAP * scale);
    const f32 right = this->getPos().x + this->pinButton->getRelPos().x;
    const f32 textWidth = this->font->getStringWidth(this->title);
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

    if(textX + textWidth <= right) {
        drawAt(textX, 1.f);
        return;
    }

    const f32 period = textWidth + MARQUEE_GAP * scale;
    const f32 offset =
        this->marqueeTime < MARQUEE_REST ? 0.f : (f32)(this->marqueeTime - MARQUEE_REST) * MARQUEE_SPEED * scale;

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
