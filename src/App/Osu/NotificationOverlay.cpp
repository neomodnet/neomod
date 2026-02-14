// Copyright (c) 2016, PG, All rights reserved.
#include "NotificationOverlay.h"

#include <utility>

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

namespace cv {
static ConVar test_notification("test_notification", ""sv, CLIENT | NOLOAD | NOSAVE, [](std::string_view text) -> void {
    if(ui && ui->getNotificationOverlay()) {
        ui->getNotificationOverlay()->addNotification(text);
        test_notification.setValue("", false);
    }
});
static ConVar test_toast("test_toast", ""sv, CLIENT | NOLOAD | NOSAVE, [](std::string_view text) -> void {
    if(ui && ui->getNotificationOverlay()) {
        ui->getNotificationOverlay()->addToast(text, INFO_TOAST);
        test_toast.setValue("", false);
    }
});
}  // namespace cv

NotificationOverlay::NotificationOverlay() : UIScreen() {
    this->bWaitForKey = false;
    this->bWaitForKeyDisallowsLeftClick = false;
    this->bConsumeNextChar = false;
    this->keyListener = nullptr;
}

NotificationOverlay::~NotificationOverlay() = default;

static f64 TOAST_WIDTH = 350.0;
static f64 TOAST_INNER_X_MARGIN = 5.0;
static f64 TOAST_INNER_Y_MARGIN = 5.0;
static f64 TOAST_OUTER_Y_MARGIN = 10.0;
static f64 TOAST_SCREEN_BOTTOM_MARGIN = 20.0;
static f64 TOAST_SCREEN_RIGHT_MARGIN = 10.0;

ToastElement::ToastElement(const UString &text, Color borderColor_arg, ToastElement::TYPE type)
    : CBaseUIButton(0, 0, 0, 0, "", "") {
    this->setGrabClicks(true);

    // TODO: animations

    this->text = text;
    this->type = type;
    this->borderColor = borderColor_arg;
    this->creationTime = engine->getTime();

    this->updateLayout();
}

void ToastElement::onClicked(bool left, bool right) {
    // Set creationTime to -10 so toast is deleted in NotificationOverlay::update
    this->creationTime = -10.0;

    CBaseUIButton::onClicked(left, right);
}

void ToastElement::draw() {
    f32 alpha = 0.9;
    alpha *= std::max(0.0, (this->creationTime + 9.5) - engine->getTime());

    // background
    g->setColor(Color(this->isMouseInside() ? 0xff222222 : 0xff111111).setA(alpha));
    g->fillRect(this->getPos(), this->getSize());

    // border
    g->setColor(Color(this->isMouseInside() ? rgb(255, 255, 255) : this->borderColor).setA(alpha));
    g->drawBorder(this->getPos(), this->getSize(), Osu::getUIScale());

    // text
    f64 y = this->getPos().y;
    for(const auto &line : this->lines) {
        y += (this->font->getHeight() * 1.5);
        g->setColor(Color(0xffffffff).setA(alpha));

        g->pushTransform();
        g->translate(this->getPos().x + TOAST_INNER_X_MARGIN, y);
        g->drawString(this->font, line);
        g->popTransform();
    }
}

void ToastElement::updateLayout() {
    this->lines = this->font->wrap(text, TOAST_WIDTH - TOAST_INNER_X_MARGIN * 2.0);
    this->setSize(TOAST_WIDTH, (this->font->getHeight() * 1.5 * this->lines.size()) + (TOAST_INNER_Y_MARGIN * 2.0));
}

void NotificationOverlay::update(CBaseUIEventCtx &c) {
    bool chat_toasts_visible = cv::notify_during_gameplay.getBool();
    chat_toasts_visible |= !osu->isInPlayMode();
    chat_toasts_visible |= ui->getPauseOverlay()->isVisible();

    bool a_toast_is_hovered = false;
    const vec2 &screen{osu->getVirtScreenSize()};
    f64 bottom_y = screen.y - TOAST_SCREEN_BOTTOM_MARGIN;
    for(const auto &t : this->toasts | std::views::reverse) {
        if(t->type == ToastElement::TYPE::CHAT && !chat_toasts_visible) continue;

        bottom_y -= TOAST_OUTER_Y_MARGIN + t->getSize().y;
        t->setPos(screen.x - (TOAST_SCREEN_RIGHT_MARGIN + TOAST_WIDTH), bottom_y);
        t->update(c);
        a_toast_is_hovered |= t->isMouseInside();
    }

    // Delay toast disappearance
    for(const auto &t : this->toasts) {
        bool delay_toast = t->type == ToastElement::TYPE::PERMANENT;
        delay_toast |= t->type == ToastElement::TYPE::CHAT && !chat_toasts_visible;
        delay_toast |= a_toast_is_hovered;
        delay_toast |= !env->winFocused();

        if(delay_toast) {
            t->creationTime += engine->getFrameTime();
        }
    }

    f64 current_time = engine->getTime();
    for(auto it = this->toasts.begin(); it != this->toasts.end(); it++) {
        const auto &toast = *it;
        if(toast->creationTime + 10.0 < current_time) {
            this->toasts.erase(it);
            return;
        }
    }
}

void NotificationOverlay::draw() {
    bool chat_toasts_visible = cv::notify_during_gameplay.getBool();
    chat_toasts_visible |= !osu->isInPlayMode();
    chat_toasts_visible |= ui->getPauseOverlay()->isVisible();

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
    f64 scale = Osu::getUIScale();

    TOAST_WIDTH = 350.0 * scale;
    TOAST_INNER_X_MARGIN = 5.0 * scale;
    TOAST_INNER_Y_MARGIN = 5.0 * scale;
    TOAST_OUTER_Y_MARGIN = 10.0 * scale;
    TOAST_SCREEN_BOTTOM_MARGIN = 20.0 * scale;
    TOAST_SCREEN_RIGHT_MARGIN = 10.0 * scale;

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
        g->setColor(argb(n.alpha, 0.f, 0.f, 0.f));

        g->translate((int)(osu->getVirtScreenWidth() / 2 - stringWidth / 2 + 1),
                     (int)(osu->getVirtScreenHeight() / 2 + font->getHeight() / 2 + n.fallAnim * height * 0.15f + 1));
        g->drawString(font, n.text);

        g->setColor(Color(n.textColor).setA(n.alpha));

        g->translate(-1, -1);
        g->drawString(font, n.text);
    }
    g->popTransform();
}

void NotificationOverlay::drawNotificationBackground(const NotificationOverlay::NOTIFICATION &n) {
    McFont *font = osu->getSubTitleFont();
    int height = font->getHeight() * 2 * n.backgroundAnim;

    g->setColor(argb(n.alpha * 0.75f, 0.f, 0.f, 0.f));

    g->fillRect(0, osu->getVirtScreenHeight() / 2 - height / 2, osu->getVirtScreenWidth(), height);
}

void NotificationOverlay::onKeyDown(KeyboardEvent &e) {
    if(!this->isVisible()) return;

    // escape always stops waiting for a key
    if(e.getScanCode() == KEY_ESCAPE) {
        if(this->bWaitForKey) e.consume();

        this->stopWaitingForKey();
    }

    // key binding logic
    if(this->bWaitForKey) {
        // HACKHACK: prevent left mouse click bindings if relevant
        if(Env::cfg(OS::WINDOWS) && this->bWaitForKeyDisallowsLeftClick &&
           e.getScanCode() == 0x01)  // 0x01 == VK_LBUTTON
            this->stopWaitingForKey();
        else {
            this->stopWaitingForKey(true);

            debugLog("keyCode = {:d}", e.getScanCode());

            if(this->keyListener != nullptr) this->keyListener->onKey(e);
        }

        e.consume();
    }

    if(this->bWaitForKey) e.consume();
}

void NotificationOverlay::onKeyUp(KeyboardEvent &e) {
    if(!this->isVisible()) return;

    if(this->bWaitForKey) e.consume();
}

void NotificationOverlay::onChar(KeyboardEvent &e) {
    if(this->bWaitForKey || this->bConsumeNextChar) e.consume();

    this->bConsumeNextChar = false;
}

void NotificationOverlay::addNotification(UString text, Color textColor, bool waitForKey, float duration) {
    const float notificationDuration = (duration < 0.0f ? cv::notification_duration.getFloat() : duration);

    // swap effect
    if(this->isVisible()) {
        this->notification2.text = this->notification1.text;
        this->notification2.textColor = 0xffffffff;

        this->notification2.time = 0.0f;
        this->notification2.alpha = 0.5f;
        this->notification2.backgroundAnim = 1.0f;
        this->notification2.fallAnim = 0.0f;

        anim::deleteExistingAnimation(&this->notification1.alpha);

        anim::moveQuadIn(&this->notification2.fallAnim, 1.0f, 0.2f, 0.0f, true);
        anim::moveQuadIn(&this->notification2.alpha, 0.0f, 0.2f, 0.0f, true);
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

    // animations
    if(this->isVisible())
        this->notification1.alpha = 1.0f;
    else
        anim::moveLinear(&this->notification1.alpha, 1.0f, 0.075f, true);

    if(!waitForKey) anim::moveQuadOut(&this->notification1.alpha, 0.0f, fadeOutTime, notificationDuration, false);

    anim::moveQuadOut(&this->notification1.backgroundAnim, 1.0f, 0.15f, 0.0f, true);
}

void NotificationOverlay::addToast(const UString &text, Color borderColor, ToastClickCallback callback,
                                   ToastElement::TYPE type) {
    auto toast = std::make_unique<ToastElement>(text, borderColor, type);
    if constexpr(Env::cfg(BUILD::DEBUG)) {
        // also log it
        // TODO: debug channels/separate files
        debugLog(text.toUtf8());
    }

    if(!!callback) {
        toast->setClickCallback(std::move(callback));
    }
    this->toasts.push_back(std::move(toast));
}

void NotificationOverlay::stopWaitingForKey(bool stillConsumeNextChar) {
    this->bWaitForKey = false;
    this->bWaitForKeyDisallowsLeftClick = false;
    this->bConsumeNextChar = stillConsumeNextChar;
}

bool NotificationOverlay::isVisible() {
    return engine->getTime() < this->notification1.time || engine->getTime() < this->notification2.time ||
           this->bWaitForKey;
}
