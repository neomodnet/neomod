#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.
#include "CBaseUITextbox.h"
#include "Delegate.h"

class UIDropdownTextbox final : public CBaseUITextbox {
    NOCOPY_NOMOVE(UIDropdownTextbox)
   public:
    UIDropdownTextbox(float xPos = 0.0f, float yPos = 0.0f, float xSize = 0.0f, float ySize = 0.0f,
                      std::string name = {});
    ~UIDropdownTextbox() override = default;

    void draw() override;
    [[nodiscard]] std::optional<CURSORTYPE> getCursorShape() override;

    using DropdownCallback = SA::delegate<void()>;
    void setDropdownCallback(DropdownCallback dropdownCallback) {
        this->dropdownCallback = std::move(dropdownCallback);
    }
    void setShowDropdown(bool showDropdown) { this->bShowDropdown = showDropdown; }

   protected:
    void onMouseDownInside(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT) override;
    void onMouseUpInside(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT) override;
    void onCapturedMouseMove() override;

   private:
    [[nodiscard]] McRect getDropdownRect() const;
    [[nodiscard]] bool isMouseInsideDropdown() const;

    DropdownCallback dropdownCallback = {};
    bool bShowDropdown{false};
    bool bDropdownPressed{false};
};
