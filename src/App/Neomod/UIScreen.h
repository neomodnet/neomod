#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUIContainer.h"

#include <cstddef>
#include <cassert>

class KeyboardEvent;
struct UI;
class UIScreen : public CBaseUIContainer {
    NOCOPY_NOMOVE(UIScreen)
   public:
    UIScreen() { this->bVisible = false; }
    ~UIScreen() override = default;

    virtual void onResolutionChange(vec2 newResolution) { (void)newResolution; }
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
