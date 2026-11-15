#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUIContainer.h"

#include <cstddef>
#include <cassert>

class KeyboardEvent;
struct UI;
class UIScreen : public CBaseUIContainer {
    NOCOPY_NOMOVE(UIScreen)
    friend struct UI;  // UI::init() sets the declared bModal/bCloseOnScreenSwitch from its screen registry
   public:
    UIScreen() { this->bVisible = false; }
    ~UIScreen() override = default;

    // stamps lastTickFrame for the ui_validate_ticks check; overrides must call through
    void tick() override;

    virtual void onResolutionChange(vec2 newResolution) { (void)newResolution; }

    [[nodiscard]] u64 getLastTickFrame() const { return this->lastTickFrame; }

    // declared layer-stack flags, constructor-set; the stack reads these instead
    // of screens sniffing each other:
    // - modal: while visible, the input/key walk stops below this layer
    // - closeOnScreenSwitch: hidden on every base-screen swap (UI::setScreen/hide)
    [[nodiscard]] bool isModal() const { return this->bModal; }
    [[nodiscard]] bool closesOnScreenSwitch() const { return this->bCloseOnScreenSwitch; }

    // the element typed text goes to when the walk down the layers gets here, or null to go on below. one that isn't
    // a text field takes none: returning the layer itself keeps the text from the layers below. focus is the focused
    // element if it's in this layer. by default the focus of a visible layer gets the text, and a modal one keeps it
    [[nodiscard]] virtual CBaseUIElement *getTextTarget(CBaseUIElement *focus);

   protected:
    bool bModal{false};
    bool bCloseOnScreenSwitch{false};

   private:
    u64 lastTickFrame{0};
};

class UIOverlay : public UIScreen {
    NOCOPY_NOMOVE(UIOverlay)
   private:
    UIScreen *parent{nullptr};

   public:
    UIOverlay() : UIScreen() {}
    UIOverlay(UIScreen *parent);
    ~UIOverlay() override = default;

    UIScreen *getParent();

    // setParent(nullptr) will make getParent return the current active screen
    void setParent(UIScreen *parent);
};
