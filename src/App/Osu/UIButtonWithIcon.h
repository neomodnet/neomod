#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.

#include "CBaseUIContainer.h"

class CBaseUILabel;

class UIButtonWithIcon : public CBaseUIContainer {
   public:
    UIButtonWithIcon(const UString& text, char16_t icon);

    void draw() override;
    void onResized() override;

    template <typename Callable>
    void setClickCallback(Callable&& cb) {
        this->clickCallback = std::forward<Callable>(cb);
    }

    void onMouseUpInside(bool /*left*/, bool /*right*/) override {
        if(this->clickCallback) this->clickCallback();
    }

   private:
    std::function<void()> clickCallback;
    CBaseUILabel* icon;
    CBaseUILabel* text;
};
