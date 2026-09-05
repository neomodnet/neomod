// Copyright (c) 2026, kiwec, 2026, WH, All rights reserved.
#include "UI.h"
#include "UIDebug.h"

#include "noinclude.h"
#include "Bancho.h"
#include "BeatmapInterface.h"
#include "Engine.h"
#include "Graphics.h"
#include "ModFPoSu.h"
#include "Mouse.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "CBaseUIDispatch.h"
#include "UIScreen.h"
#include "Logging.h"
#include "SString.h"

#include "AboutScreen.h"
#include "BeatmapInstallOverlay.h"
#include "Chat.h"
#include "HUD.h"
#include "Lobby.h"
#include "MainMenu.h"
#include "ModSelector.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "OsuDirectScreen.h"
#include "PauseOverlay.h"
#include "PromptOverlay.h"
#include "RankingScreen.h"
#include "RoomScreen.h"
#include "SongBrowser/SongBrowser.h"
#include "SpectatorScreen.h"
#include "TooltipOverlay.h"
#include "UIUserContextMenu.h"
#include "UIContextMenu.h"
#include "UserStatsScreen.h"
#include "VolumeOverlay.h"

#include <ranges>

namespace {
class NullScreen final : public UIScreen {
    NOCOPY_NOMOVE(NullScreen)
   public:
    NullScreen() : UIScreen() {}
    ~NullScreen() final = default;

    forceinline CBaseUIElement *setVisible(bool /*visible*/) final {
        this->bVisible = false;
        return this;
    }
    forceinline bool isVisible() final { return false; }
};
}  // namespace

// getXBase is out-of-line here (not inline in UI.h): the derived->UIScreen* conversion needs complete types
#define X(rank, F, Type, member) \
    UIScreen *UI::get##Type##Base() const { return this->member; }
UI_SCREEN_REGISTRY(X)
#undef X

UI *ui{nullptr};

UI::UI() {
    static_assert(
        [] {
            std::array<bool, NUM_SCREENS> seen{};
            for(const size_t i : LAYER_ORDER) {
                if(i >= NUM_SCREENS || seen[i]) return false;
                seen[i] = true;
            }
            return true;
        }(),
        "LAYER_ORDER must be a permutation of the screen indices");
    static_assert(SCREEN_NAMES[LAYER_ORDER[OVERLAY_BAND_BEGIN]] == "pauseoverlay");
    static_assert(SCREEN_NAMES[LAYER_ORDER[PLAY_OVERLAYS_END - 1]] == "optionsoverlay");
    static_assert(SCREEN_NAMES[LAYER_ORDER[EXTRAS_SPLICE]] == "tooltipoverlay");
    // EARLY_SCREENS: dummy + notification are the first two slots, built in the ctor below
    static_assert(SCREEN_NAMES[0] == "dummy" && SCREEN_NAMES[1] == "notificationoverlay");

    ui = this;
    this->screens[0] = this->active_screen = this->dummy = new NullScreen();
    this->screens[1] = this->notificationoverlay = new NotificationOverlay();
}

UI::~UI() {
    this->debuglayer.reset();

    for(auto *overlay : this->extra_overlays) {
        SAFE_DELETE(overlay);
    }
    this->extra_overlays.clear();

    // destroy screens in reverse order
    for(sSz i = static_cast<sSz>(this->screens.size()) - 1; i >= 0; --i) {
        SAFE_DELETE(this->screens[i]);
    }
    this->screens = {};
    // ui = nullptr in ~Osu
}

bool UI::init() {
    // construct the non-early screens and apply each screen's flags from the registry; the
    // null-guard skips the rows the ctor already built (dummy/notification).
    size_t screenit = EARLY_SCREENS;
#define X(rank, F, Type, member)                               \
    if(this->member == nullptr) {                              \
        this->screens[screenit++] = this->member = new Type(); \
    }                                                          \
    this->member->bModal = !!((F) & UIF_MODAL);                \
    this->member->bCloseOnScreenSwitch = !!((F) & UIF_CLOSE);
    UI_SCREEN_REGISTRY(X)  // NOLINT(misc-redundant-expression)
#undef X
    assert(screenit == NUM_SCREENS);

    this->active_screen = Osu::isKioskMode() ? static_cast<UIScreen *>(this->dummy) : this->mainmenu;

    // debug
    this->debuglayer = std::make_unique<UIDebug>(this);

    return true;
}

void UI::update() {
    // tick pass: logic/animations always run, for every screen, regardless of input consumption
    // NOLINTNEXTLINE(modernize-loop-convert)
    for(uSz i = 0; i < this->extra_overlays.size(); ++i) {
        this->extra_overlays[i]->tick();
    }

    bool ticked_active_screen = false;
    for(auto *screen : this->screens) {
        screen->tick();
        if(screen == this->active_screen) ticked_active_screen = true;
    }
    if(!ticked_active_screen) this->active_screen->tick();

    // input pass: walk LAYER_ORDER top -> bottom (= reverse draw order, with the extra
    // overlays spliced in last-pushed-first); each screen/overlay is one hit-candidate
    // priority group for the button dispatch below, first-walked = top-most. consumption
    // stops the walk exactly as before, only the order changed
    CBaseUIEventCtx &c{this->update_ctx};
    c.clear();

    bool updated_active_screen = false;
    bool walked_extras = this->extra_overlays.empty();
    for(sSz li = static_cast<sSz>(NUM_SCREENS) - 1; li >= 0 && !c.mouse_consumed(); --li) {
        if(li < static_cast<sSz>(EXTRAS_SPLICE) && !walked_extras) {
            walked_extras = true;
            for(sSz oi = static_cast<sSz>(this->extra_overlays.size()) - 1; oi >= 0 && !c.mouse_consumed(); --oi) {
                auto *overlay = this->extra_overlays[oi];
                c.beginHitGroup();
                overlay->updateInput(c);
                // a visible modal layer is the floor of the walk: nothing below it sees the mouse
                if(overlay->isModal() && overlay->isVisible()) c.consume_mouse();
            }
            if(c.mouse_consumed()) break;
        }

        auto *screen = this->screens[LAYER_ORDER[li]];
        c.beginHitGroup();
        screen->updateInput(c);
        if(screen->isModal() && screen->isVisible()) c.consume_mouse();
        if(screen == this->active_screen) updated_active_screen = true;
    }

    if(!updated_active_screen && !c.mouse_consumed()) {
        c.beginHitGroup();
        this->active_screen->updateInput(c);
    }

    CBaseUIDispatch::dispatchEvents(c, CBaseUIDispatch::Root::APP);

    if constexpr(Env::cfg(BUILD::DEBUG)) {
        if(unlikely(cv::ui_validate_ticks.getBool())) this->validateTicks();
    }
}

void UI::validateTicks() const {
    const u64 frame = engine->getFrameCount();
    for(const auto *screen : this->screens) {
        if(screen->getLastTickFrame() != frame)
            logRaw("UITEST FAIL tick-skipped screen={}", CBaseUIDebug::elemName(screen));
    }
    for(const auto *overlay : this->extra_overlays) {
        if(overlay->getLastTickFrame() != frame)
            logRaw("UITEST FAIL tick-skipped overlay={}", CBaseUIDebug::elemName(overlay));
    }
}

// draws LAYER_ORDER[from, to); the active screen is skipped because it already drew at
// the bottom of the frame (base band), and every screen self-gates on visibility
void UI::drawLayerRange(size_t from, size_t to) {
    for(size_t li = from; li < to; ++li) {
        auto *screen = this->screens[LAYER_ORDER[li]];
        if(screen != this->active_screen) screen->draw();
    }
}

void UI::drawExtraOverlays() {
    // first-pushed first = last-pushed draws topmost (matches the reversed input walk)
    // NOLINTNEXTLINE(modernize-loop-convert)
    for(uSz i = 0; i < this->extra_overlays.size(); ++i) {
        this->extra_overlays[i]->draw();
    }
}

void UI::draw() {
    // if we are not using the native window resolution, draw into the buffer
    const bool isBufferedDraw = (g->getResolution() != osu->getVirtScreenSize());
    if(isBufferedDraw) {
        osu->backBuffer->enable();
    }

    // base band: the active screen draws below every overlay layer (self-gated, so a
    // no-op during gameplay where no base screen is visible)
    this->active_screen->draw();

    f32 cursorAlpha = 1.f;
    if(f32 cursorIdleFadeTime = cv::cursor_idle_time_before_fade.getFloat(); cursorIdleFadeTime > 0.f) {
        // fade out cursor if it hasn't been moved for over 15 seconds
        if(osu->isInPlayMode() || vec::length(mouse->getRawDelta()) > 0.) {
            this->lastCursorMoveTime = engine->getTime();
        } else {
            const f32 fadeDuration = 1.f;
            const f32 idleTime = (f32)(engine->getTime() - this->lastCursorMoveTime);
            cursorAlpha *=
                std::clamp<f32>(cv::cursor_idle_time_before_fade.getFloat() - idleTime + fadeDuration, 0.f, 1.f);
        }
    }

    const bool isFPoSu = (cv::mod_fposu.getBool());

    // draw everything in the correct order
    if(osu->isInPlayMode()) {  // if we are playing a beatmap
        if(isFPoSu) osu->playfieldBuffer->enable();

        // draw playfield (incl. flashlight/smoke etc.)
        osu->map_iface->draw();

        if(!isFPoSu) this->hud->draw();

        // quick retry fadeout overlay
        if(osu->fQuickRetryTime != 0.0f && osu->bQuickRetryDown) {
            float alphaPercent = 1.0f - (osu->fQuickRetryTime - engine->getTime()) / cv::quick_retry_delay.getFloat();
            if(engine->getTime() > osu->fQuickRetryTime) alphaPercent = 1.0f;

            g->setColor(argb((int)(255 * alphaPercent), 0, 0, 0));
            g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
        }

        // pause..options render into the playfield buffer under FPoSu (3D-projected
        // with the playfield); the layers above are real-screen
        this->drawLayerRange(OVERLAY_BAND_BEGIN, PLAY_OVERLAYS_END);

        if(!isFPoSu) {
            this->hud->drawFps();
        }

        if(isFPoSu && cv::draw_cursor_ripples.getBool()) {
            this->hud->drawCursorRipples();
        }

        // apply fading cursor alpha multiplier
        if(!(this->pauseoverlay->isVisible() || osu->map_iface->isContinueScheduled() ||
             !cv::mod_fadingcursor.getBool())) {
            cursorAlpha *=
                1.0f -
                std::clamp<float>((float)osu->score->getCombo() / cv::mod_fadingcursor_combo.getFloat(), 0.0f, 1.0f);
        }

        // draw FPoSu cursor trail
        if(isFPoSu && cv::fposu_draw_cursor_trail.getBool()) {
            const vec2 trailpos = osu->map_iface->isPaused() ? mouse->getPos() : osu->map_iface->getCursorPos();
            this->hud->drawCursorTrail(trailpos, cursorAlpha);
        }

        if(isFPoSu) {
            osu->playfieldBuffer->disable();
            osu->fposu->draw();
            this->hud->draw();
            this->hud->drawFps();
        }

        // draw debug info on top of everything else
        if(cv::debug_draw_timingpoints.getBool()) osu->map_iface->drawDebug();

        this->drawLayerRange(PLAY_OVERLAYS_END, EXTRAS_SPLICE);  // prompt, beatmapinstall (self-gated)
        this->drawExtraOverlays();

    } else {  // if we are not playing

        this->drawLayerRange(OVERLAY_BAND_BEGIN, EXTRAS_SPLICE);
        this->drawExtraOverlays();

        this->hud->drawFps();
    }

    this->drawLayerRange(EXTRAS_SPLICE, NUM_SCREENS);  // tooltip, volume, notification

    // loading spinner for some async tasks
    if((osu->bSkinLoadScheduled && osu->getSkin() != osu->skinScheduledToLoad)) {
        this->hud->drawLoadingSmall("");
    }

    if(osu->isInPlayMode()) {
        // draw cursor (gameplay)
        const bool paused = osu->map_iface->isPaused();
        const vec2 cursorPos =
            isFPoSu ? (osu->getVirtScreenSize() / 2.0f) : (paused ? mouse->getPos() : osu->map_iface->getCursorPos());
        const bool drawSecondTrail = !paused && (cv::mod_autoplay.getBool() || cv::mod_autopilot.getBool() ||
                                                 osu->map_iface->is_watching || BanchoState::spectating);
        const bool updateAndDrawTrail = !isFPoSu;
        this->hud->drawCursor(cursorPos, cursorAlpha, drawSecondTrail, updateAndDrawTrail);
    } else {
        // draw cursor (menus)
        this->hud->drawCursor(mouse->getPos(), cursorAlpha);
    }

    // draw build info on top of everything else
    HUD::drawRuntimeInfo();

    // if we are not using the native window resolution
    if(isBufferedDraw) {
        // draw a scaled version from the buffer to the screen
        osu->backBuffer->disable();

        const vec2 offset{vec2{g->getResolution() - osu->getVirtScreenSize()} * 0.5f};
        g->setBlending(false);
        if(cv::letterboxing.getBool()) {
            osu->backBuffer->draw(offset.x * (1.0f + cv::letterboxing_offset_x.getFloat()),
                                  offset.y * (1.0f + cv::letterboxing_offset_y.getFloat()), osu->getVirtScreenWidth(),
                                  osu->getVirtScreenHeight());
        } else {
            if(cv::resolution_keep_aspect_ratio.getBool()) {
                const float scale = Osu::getRectScaleToFitResolution(osu->backBuffer->getSize(), g->getResolution());
                const float scaledWidth = osu->backBuffer->getWidth() * scale;
                const float scaledHeight = osu->backBuffer->getHeight() * scale;
                osu->backBuffer->draw(std::max(0.0f, g->getResolution().x / 2.0f - scaledWidth / 2.0f) *
                                          (1.0f + cv::letterboxing_offset_x.getFloat()),
                                      std::max(0.0f, g->getResolution().y / 2.0f - scaledHeight / 2.0f) *
                                          (1.0f + cv::letterboxing_offset_y.getFloat()),
                                      scaledWidth, scaledHeight);
            } else {
                osu->backBuffer->draw(0, 0, g->getResolution().x, g->getResolution().y);
            }
        }
        g->setBlending(true);
    }
}

namespace {
// for scripted testing: log who consumed a key event (same format family as CBaseUIDebug::traceEvent)
forceinline void traceKeyConsumed(std::string_view evt, CBaseUIElement *consumer) {
    if(unlikely(CBaseUIDebug::traceLevel() > 0))
        logRaw("uitrace evt={} consumed_by={}", evt, CBaseUIDebug::elemName(consumer));
}
}  // namespace

// key routing walks the same top -> bottom layer order as the input pass; the first
// consumer ends the walk, and a visible modal layer floors it (keys it declined die
// there instead of reaching the layers beneath). layers hidden when the press began
// (hiddenAtPress) are skipped but still floor; extra overlays aren't filtered (nothing
// pushes one from a key)
void UI::routeKey(KeyboardEvent &e, void (CBaseUIElement::*handler)(KeyboardEvent &), std::string_view traceName) {
    if(e.isConsumed()) return;

    bool walked_extras = this->extra_overlays.empty();
    for(sSz li = static_cast<sSz>(NUM_SCREENS) - 1; li >= 0; --li) {
        if(li < static_cast<sSz>(EXTRAS_SPLICE) && !walked_extras) {
            walked_extras = true;
            for(sSz oi = static_cast<sSz>(this->extra_overlays.size()) - 1; oi >= 0; --oi) {
                auto *overlay = this->extra_overlays[oi];
                (overlay->*handler)(e);
                if(e.isConsumed()) {
                    traceKeyConsumed(traceName, overlay);
                    return;
                }
                if(overlay->isModal() && overlay->isVisible()) return;
            }
        }

        const size_t si = LAYER_ORDER[li];
        auto *screen = this->screens[si];
        if(!this->hiddenAtPress[si]) {
            (screen->*handler)(e);
            if(e.isConsumed()) {
                traceKeyConsumed(traceName, screen);
                return;
            }
        }
        if(screen->isModal() && screen->isVisible()) return;
    }
}

void UI::onKeyDown(KeyboardEvent &key) {
    // a repeat continues the press, it keeps the snapshot from the first keydown
    if(!key.isRepeat()) {
        for(size_t i = 0; i < NUM_SCREENS; ++i) this->hiddenAtPress[i] = !this->screens[i]->isVisible();
    }
    this->routeKey(key, &CBaseUIElement::onKeyDown, "keydown");
    // arrow-bound volume is the global fallback gesture (mirrors the dispatch wheel sink): offered
    // the keydown only when no layer above consumed it - a hovered slider's left/right, a screen
    // navigating with arrows. volume's always-on half (mute/media keys, the arrow binds while the
    // bar shows) is handled in VolumeOverlay::onKeyDown up in the walk.
    // TODO: remove dispatcher wheel sink complexity and do something similar for mouse wheel here
    if(!key.isConsumed()) {
        this->volumeoverlay->onArrowVolumeFallback(key);
        if(key.isConsumed()) traceKeyConsumed("keydown", this->volumeoverlay);
    }
}

void UI::onKeyUp(KeyboardEvent &key) {
    this->routeKey(key, &CBaseUIElement::onKeyUp, "keyup");
    this->hiddenAtPress.reset();
}

void UI::onChar(KeyboardEvent &e) { this->routeKey(e, &CBaseUIElement::onChar, "char"); }

void UI::onResolutionChange(vec2 newResolution) {
    // NOLINTNEXTLINE(modernize-loop-convert)
    for(uSz i = 0; i < this->extra_overlays.size(); ++i) {
        this->extra_overlays[i]->onResolutionChange(newResolution);
    }

    for(auto *screen : this->screens) {
        screen->onResolutionChange(newResolution);
    }
}

void UI::hide() {
    this->active_screen->setVisible(false);
    // close the "temporary" closeOnScreenSwitch overlays
    for(auto *screen : this->screens) {
        if(screen->closesOnScreenSwitch() && screen->isVisible()) screen->setVisible(false);
    }
}

void UI::show() { this->active_screen->setVisible(true); }

void UI::setScreen(UIScreen *screen) {
    assert(screen);

    if(screen != this->active_screen && this->active_screen->isVisible()) {
        this->hide();
    } else {
        // closeOnScreenSwitch sweep (visibility-gated on this path, matching the old
        // hardcoded pause/options/prompt close; none of those is ever the setScreen target)
        for(auto *s : this->screens) {
            if(s != screen && s->closesOnScreenSwitch() && s->isVisible()) s->setVisible(false);
        }
    }

    this->active_screen = screen;
    this->show();
}

UIOverlay *UI::pushOverlay(std::unique_ptr<UIOverlay> overlay) {
    assert(overlay);

    UIOverlay *raw = overlay.release();
    assert(!std::ranges::contains(this->extra_overlays, raw));
    this->extra_overlays.push_back(raw);

    // set the overlay visible immediately, and tick it once so a push during the input pass
    // doesn't leave it un-ticked this frame (ui_validate_ticks)
    raw->setVisible(true);
    raw->tick();
    return raw;
}

bool UI::peekOverlay(UIOverlay *overlay) const {
    if(auto it = std::ranges::find(this->extra_overlays, overlay); it != this->extra_overlays.end()) {
        return true;
    }
    return false;
}

std::unique_ptr<UIOverlay> UI::popOverlay(UIOverlay *overlay) {
    if(auto it = std::ranges::find(this->extra_overlays, overlay); it != this->extra_overlays.end()) {
        std::unique_ptr<UIOverlay> overlay_out;
        overlay_out.reset(*it);

        // remove it
        this->extra_overlays.erase(it);
        UIScreen *parent = overlay->getParent();
        this->setScreen(parent);

        return overlay_out;
    } else {
        assert(false && "UI::popOverlay: double-popped overlay");
    }
    std::unreachable();
}
