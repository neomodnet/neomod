#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "AnimationHandler.h"
#include "CBaseUIButton.h"
#include "KeyboardEvent.h"
#include "Registration.h"
#include "UIScreen.h"

#include <memory>
#include <string_view>

enum ToastTypeColor : u32 {
    CHAT_TOAST = 0xff8a2be2,
    INFO_TOAST = 0xffffdd00,
    ERROR_TOAST = 0xffdd0000,
    SUCCESS_TOAST = 0xff00ff00,
    STATUS_TOAST = 0xff003bff,
};

class ToastElement final : public CBaseUIButton {
    NOCOPY_NOMOVE(ToastElement)
   public:
    enum class TYPE : uint8_t { PERMANENT, SYSTEM, CHAT };
    const TYPE type;

    static constexpr f64 DEFAULT_TOAST_TIMEOUT{10.};

    ToastElement(std::string text, Color borderColor, TYPE type);
    ~ToastElement() override = default;

    void draw() override;
    void onClicked(bool left = true, bool right = false) override;
    void updateLayout();

    [[nodiscard]] f64 getTimeRemaining() const;
    [[nodiscard]] bool hasTimedOut() const;

    inline void setTimeout(f64 timeout) { this->timeout = std::max(timeout, 0.5); }
    void freezeTimeout();  // stop the timeout at the currently remaining time
    void dismiss();        // times out right away

    u64 callbackId{0};  // NotificationOverlay's click callback for it, 0 if none

   private:
    std::vector<std::string> lines;

    f64 creation_time;
    f64 timeout{DEFAULT_TOAST_TIMEOUT};  // relative to creation time
};

class NotificationOverlay final : public UIScreen {
    NOCOPY_NOMOVE(NotificationOverlay)
   public:
    NotificationOverlay();
    ~NotificationOverlay() override;

    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;
    void draw() override;
    void onResolutionChange(vec2 newResolution) override;

    void onChar(KeyboardEvent &e) override;

    // any thread
    void addToast(std::string text, Color borderColor, ToastElement::TYPE type = ToastElement::TYPE::SYSTEM);
    // `callback` runs (on the main thread) when the toast is clicked; resetting the Registration also dismisses it
    using ToastClickCallback = std::function<void()>;
    Mc::Registration addToast(std::string text, Color borderColor, ToastClickCallback callback,
                              ToastElement::TYPE type = ToastElement::TYPE::SYSTEM);

    void addNotification(std::string text, Color textColor = 0xffffffff, bool waitForKey = false,
                         float duration = -1.0f);

    void stopWaitingForKey(bool stillConsumeNextChar = false);

    inline bool isWaitingForKey() { return this->bWaitForKey || this->bConsumeNextChar; }

   private:
    void addToastElement(std::string text, Color borderColor, ToastElement::TYPE type, u64 callbackId);
    void onToastClicked(u64 callbackId);
    void endToastCallback(u64 callbackId, Mc::Registration::End how);
    void updateVisibility();
    // stacks the toasts up from the bottom right corner, the newest at the bottom
    void layoutToasts();
    // convar callbacks
    void onToastCallback(std::string_view args);
    void onNotificationCallback(std::string_view args);

    struct NOTIFICATION {
        std::string text = "";
        Color textColor = argb(255, 255, 255, 255);

        float time = 0.f;
        AnimFloat alpha;
        AnimFloat backgroundAnim;
        AnimFloat fallAnim;
    };

    void drawNotificationText(const NOTIFICATION &n);
    void drawNotificationBackground(const NOTIFICATION &n);

    struct Mutex;
    std::unique_ptr<Mutex> notifMtx;
    std::vector<std::unique_ptr<ToastElement>> toasts;
    // the toasts' click callbacks, here rather than in the toasts so that a click runs a copy (it may end its own
    // Registration) and a reset takes effect even for a toast that's being clicked
    struct ToastCallback {
        u64 id;
        ToastClickCallback callback;
        bool detached{false};
    };
    std::vector<ToastCallback> toastCallbacks;
    u64 lastToastCallbackId{0};

    NOTIFICATION notification1;
    NOTIFICATION notification2;

    bool bWaitForKey{false};
    bool bConsumeNextChar{false};
};
