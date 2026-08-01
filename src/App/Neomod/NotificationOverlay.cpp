// Copyright (c) 2016, PG, All rights reserved.
#include "NotificationOverlay.h"

#include "AnimationHandler.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "KeyBindings.h"
#include "Osu.h"
#include "Graphics.h"
#include "PauseOverlay.h"
#include "Logging.h"
#include "UI.h"
#include "SyncMutex.h"

#include <ranges>
#include <utility>

namespace {

bool should_chat_toasts_be_visible() {
    return cv::notify_during_gameplay.getBool() ||  //
           !osu->isInPlayMode() ||                  //
           ui->getPauseOverlay()->isVisible();
}

void draw_border_rect(Graphics *g, int x, int y, int width, int height, float thickness) {
    return g->drawRectf(Graphics::RectOptions{
        .x = (float)x + thickness / 2.f,
        .y = (float)y + thickness / 2.f,
        .width = (float)width - thickness,
        .height = (float)height - thickness,
        .lineThickness = thickness,
        .withColor = false,
    });
}

void draw_border_rect(Graphics *g, vec2 pos, vec2 size, float thickness) {
    return draw_border_rect(g, (int)pos.x, (int)pos.y, (int)size.x, (int)size.y, thickness);
}

}  // namespace

namespace cv::cmd {
static ConVar notify("notify", CLIENT | SERVER | NOLOAD | NOSAVE);
static ConVar toast("toast", CLIENT | SERVER | NOLOAD | NOSAVE);
}  // namespace cv::cmd

void NotificationOverlay::onToastCallback(std::string_view args) {
    this->addToast(std::string{args}, INFO_TOAST);
    cv::cmd::toast.setValue("", false);
}

void NotificationOverlay::onNotificationCallback(std::string_view args) {
    this->addNotification(std::string{args});
    cv::cmd::notify.setValue("", false);
}

struct NotificationOverlay::Mutex : public Sync::mutex {};

NotificationOverlay::NotificationOverlay() : UIScreen(), notifMtx(new Mutex()) {
    cv::cmd::toast.setCallback(SA::MakeDelegate<&NotificationOverlay::onToastCallback>(this));
    cv::cmd::notify.setCallback(SA::MakeDelegate<&NotificationOverlay::onNotificationCallback>(this));
}

NotificationOverlay::~NotificationOverlay() {
    cv::cmd::notify.removeCallback();
    cv::cmd::toast.removeCallback();
}

static constexpr f32 DEF_TOAST_WIDTH{350.0f};
static constexpr f32 DEF_TOAST_INNER_X_MARGIN{5.0f};
static constexpr f32 DEF_TOAST_INNER_Y_MARGIN{5.0f};
static constexpr f32 DEF_TOAST_OUTER_Y_MARGIN{10.0f};
static constexpr f32 DEF_TOAST_SCREEN_BOTTOM_MARGIN{20.0f};
static constexpr f32 DEF_TOAST_SCREEN_RIGHT_MARGIN{10.0f};

static f32 TOAST_WIDTH{DEF_TOAST_WIDTH};
static f32 TOAST_INNER_X_MARGIN{DEF_TOAST_INNER_X_MARGIN};
static f32 TOAST_INNER_Y_MARGIN{DEF_TOAST_INNER_Y_MARGIN};
static f32 TOAST_OUTER_Y_MARGIN{DEF_TOAST_OUTER_Y_MARGIN};
static f32 TOAST_SCREEN_BOTTOM_MARGIN{DEF_TOAST_SCREEN_BOTTOM_MARGIN};
static f32 TOAST_SCREEN_RIGHT_MARGIN{DEF_TOAST_SCREEN_RIGHT_MARGIN};

ToastElement::ToastElement(std::string text, Color borderColor, ToastElement::TYPE type)
    : CBaseUIButton(0.f, 0.f, 0.f, 0.f, "", std::move(text)), type(type) {
    this->setDrawsOnTop(true);
    this->setHandleRightMouse(true);

    // TODO: animations

    this->setFrameColor(borderColor);
    this->creation_time = engine->getTime();

    this->updateLayout();
}

void ToastElement::freezeTimeout() { this->creation_time += engine->getFrameTime(); }
f64 ToastElement::getTimeRemaining() const { return (this->creation_time + this->timeout) - engine->getTime(); }
bool ToastElement::hasTimedOut() const { return this->getTimeRemaining() <= 0.; }

void ToastElement::updateLayout() {
    this->lines = this->font->wrap(this->getText(), TOAST_WIDTH - TOAST_INNER_X_MARGIN * 2.0f);
    this->setSize(TOAST_WIDTH, (this->font->getHeight() * 1.5f * this->lines.size()) + (TOAST_INNER_Y_MARGIN * 2.0f));
}

void ToastElement::onClicked(bool left, bool right) {
    // Change creation_time so that the fadeout is triggered immediately
    const f64 time_remaining_until_fadeout =
        std::max<f64>(1., ((this->creation_time + (this->timeout - 0.5)) - engine->getTime()) - 1.);
    this->creation_time -= time_remaining_until_fadeout;

    // TODO: toast click callbacks should also take left/right so that they can respond to them independently
    // (e.g. right click -> dismissed -> do something else)
    // for now just avoid calling onClicked (which calls the callback) entirely if we got a right click
    if(left) {
        CBaseUIButton::onClicked(left, right);
    }
}

void ToastElement::draw() {
    const f32 alpha =
        0.9f * static_cast<f32>(std::max(0.0, (this->creation_time + (this->timeout - 0.5)) - engine->getTime()));

    // background
    g->setColor(Color(this->isMouseInside() ? 0xff222222 : 0xff111111).setA(alpha));
    g->fillRect(this->getPos(), this->getSize());

    // border
    g->setColor(Color(this->isMouseInside() ? rgb(255, 255, 255) : this->frameColor).setA(alpha));
    draw_border_rect(g, this->getPos(), this->getSize(), Osu::getUIScale());

    // text
    f32 y = this->getPos().y;
    for(const auto &line : this->lines) {
        y += (this->font->getHeight() * 1.5f);
        g->setColor(Color(0xffffffff).setA(alpha));

        g->pushTransform();
        g->translate(this->getPos().x + TOAST_INNER_X_MARGIN, y);
        g->drawString(this->font, line);
        g->popTransform();
    }
}

void NotificationOverlay::tick() {
    Sync::scoped_lock lk(*this->notifMtx);
    this->updateVisibility();
    UIScreen::tick();
    if(this->toasts.empty()) return;

    const bool chat_toasts_visible = should_chat_toasts_be_visible();
    const vec2 screen = osu->getVirtScreenSize();

    bool a_toast_is_hovered = false;
    f32 bottom_y = screen.y - TOAST_SCREEN_BOTTOM_MARGIN;
    for(auto rtit = this->toasts.rbegin(); rtit != this->toasts.rend(); ++rtit) {
        const auto &t = *rtit;
        if(t->type == ToastElement::TYPE::CHAT && !chat_toasts_visible) continue;

        bottom_y -= TOAST_OUTER_Y_MARGIN + t->getSize().y;
        t->setPos(screen.x - (TOAST_SCREEN_RIGHT_MARGIN + TOAST_WIDTH), bottom_y);
        t->tick();
        a_toast_is_hovered |= t->isMouseInside();
    }

    for(auto tit = this->toasts.begin(); tit != this->toasts.end();) {
        const auto &t = *tit;
        if(t->hasTimedOut()) {
            // remove timed out toasts
            tit = this->toasts.erase(tit);
            continue;
        }
        ++tit;

        const bool delay_toast = t->getTimeRemaining() > 1.55 &&               // don't delay if we are fading out
                                 (a_toast_is_hovered ||                        //
                                  t->type == ToastElement::TYPE::PERMANENT ||  //
                                  (t->type == ToastElement::TYPE::CHAT && !chat_toasts_visible) ||  //
                                  !env->winFocused());

        if(delay_toast) {
            // prevent some toasts from timing out depending on the above conditions
            t->freezeTimeout();
        }
    }
}

void NotificationOverlay::updateInput(CBaseUIEventCtx &c) {
    Sync::scoped_lock lk(*this->notifMtx);
    if(!this->isVisible() && this->toasts.empty()) return;
    UIScreen::updateInput(c);
    if(this->toasts.empty()) return;

    const bool chat_toasts_visible = should_chat_toasts_be_visible();
    for(auto rtit = this->toasts.rbegin(); rtit != this->toasts.rend(); ++rtit) {
        const auto &t = *rtit;
        if(t->type == ToastElement::TYPE::CHAT && !chat_toasts_visible) continue;
        t->updateInput(c);
    }
}

void NotificationOverlay::draw() {
    Sync::scoped_lock lk(*this->notifMtx);
    const bool chat_toasts_visible = should_chat_toasts_be_visible();

    for(const auto &t : this->toasts) {
        if(t->type == ToastElement::TYPE::CHAT && !chat_toasts_visible) continue;

        t->draw();
    }

    if(!this->isVisible()) return;

    if(this->bWaitForKey) {
        g->setColor(Color(0x22ffffff).setA((this->notification1.backgroundAnim / 0.5f) * 0.13f));

        g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
    }

    this->drawNotificationBackground(this->notification2);
    this->drawNotificationBackground(this->notification1);
    this->drawNotificationText(this->notification2);
    this->drawNotificationText(this->notification1);
}

void NotificationOverlay::onResolutionChange(vec2 /*newResolution*/) {
    Sync::scoped_lock lk(*this->notifMtx);
    const f32 scale = Osu::getUIScale();

    TOAST_WIDTH = DEF_TOAST_WIDTH * scale;
    TOAST_INNER_X_MARGIN = DEF_TOAST_INNER_X_MARGIN * scale;
    TOAST_INNER_Y_MARGIN = DEF_TOAST_INNER_Y_MARGIN * scale;
    TOAST_OUTER_Y_MARGIN = DEF_TOAST_OUTER_Y_MARGIN * scale;
    TOAST_SCREEN_BOTTOM_MARGIN = DEF_TOAST_SCREEN_BOTTOM_MARGIN * scale;
    TOAST_SCREEN_RIGHT_MARGIN = DEF_TOAST_SCREEN_RIGHT_MARGIN * scale;

    for(auto &toast : this->toasts) {
        toast->updateLayout();
    }
}

void NotificationOverlay::drawNotificationText(const NotificationOverlay::NOTIFICATION &n) {
    McFont *font = osu->getSubTitleFont();
    int height = font->getHeight() * 2;
    int stringWidth = font->getStringWidth(n.text);

    g->pushTransform();
    {
        g->setColor(argb((f32)n.alpha, 0.f, 0.f, 0.f));

        g->translate((int)(osu->getVirtScreenWidth() / 2 - stringWidth / 2 + 1),
                     (int)(osu->getVirtScreenHeight() / 2 + font->getHeight() / 2 + n.fallAnim * height * 0.15f + 1));
        g->drawString(font, n.text);

        g->setColor(Color(n.textColor).setA((f32)n.alpha));

        g->translate(-1, -1);
        g->drawString(font, n.text);
    }
    g->popTransform();
}

void NotificationOverlay::drawNotificationBackground(const NotificationOverlay::NOTIFICATION &n) {
    McFont *font = osu->getSubTitleFont();
    int height = font->getHeight() * 2 * n.backgroundAnim;

    g->setColor(argb((f32)n.alpha * 0.75f, 0.f, 0.f, 0.f));

    g->fillRect(0, osu->getVirtScreenHeight() / 2 - height / 2, osu->getVirtScreenWidth(), height);
}

void NotificationOverlay::onChar(KeyboardEvent &e) {
    if(this->bWaitForKey || this->bConsumeNextChar) e.consume();

    this->bConsumeNextChar = false;
}

void NotificationOverlay::addNotification(std::string text, Color textColor, bool waitForKey, float duration) {
    Sync::scoped_lock lk(*this->notifMtx);
    if constexpr(Env::cfg(BUILD::DEBUG)) {
        // also log it
        // TODO: debug channels/separate files
        debugLog(text);
    }
    const float notificationDuration = (duration < 0.0f ? cv::notification_duration.getFloat() : duration);

    // swap effect
    if(this->isVisible()) {
        this->notification2.text = this->notification1.text;
        this->notification2.textColor = 0xffffffff;

        this->notification2.time = 0.0f;
        this->notification2.alpha = 0.5f;
        this->notification2.backgroundAnim = 1.0f;
        this->notification2.fallAnim = 0.0f;

        this->notification1.alpha.stop();

        this->notification2.fallAnim.set(1.0f, 0.2f, anim::QuadIn);
        this->notification2.alpha.set(0.0f, 0.2f, anim::QuadIn);
    }

    // build new notification
    this->bWaitForKey = waitForKey;
    this->bConsumeNextChar = this->bWaitForKey;

    float fadeOutTime = 0.4f;

    this->notification1.text = std::move(text);
    this->notification1.textColor = textColor;

    if(!waitForKey)
        this->notification1.time = engine->getTime() + notificationDuration + fadeOutTime;
    else
        this->notification1.time = 0.0f;

    this->notification1.alpha = 0.0f;
    this->notification1.backgroundAnim = 0.5f;
    this->notification1.fallAnim = 0.0f;

    this->updateVisibility();

    // animations
    if(this->isVisible())
        this->notification1.alpha = 1.0f;
    else
        this->notification1.alpha.set(1.0f, 0.075f, anim::Linear);

    if(!waitForKey) this->notification1.alpha.append(0.0f, fadeOutTime, anim::QuadOut, notificationDuration);

    this->notification1.backgroundAnim.set(1.0f, 0.15f, anim::QuadOut);

    this->updateVisibility();
}

void NotificationOverlay::addToast(ToastOpts opts) {
    Sync::scoped_lock lk(*this->notifMtx);
    if constexpr(Env::cfg(BUILD::DEBUG)) {
        // also log it
        // TODO: debug channels/separate files
        debugLog(opts.text);
    }
    auto toast = std::make_unique<ToastElement>(std::move(opts.text), opts.borderColor, opts.type);
    toast->setTimeout(opts.timeout);

    if(!!opts.callback) {
        toast->setClickCallback(std::move(opts.callback));
    }
    this->toasts.push_back(std::move(toast));

    this->updateVisibility();
}

void NotificationOverlay::stopWaitingForKey(bool stillConsumeNextChar) {
    this->bWaitForKey = false;
    this->bConsumeNextChar = stillConsumeNextChar;
}

void NotificationOverlay::updateVisibility() {
    this->bVisible = engine->getTime() < this->notification1.time || engine->getTime() < this->notification2.time ||
                     this->bWaitForKey;
}
