#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIButton.h"

#include <string>

// a button with its text in the middle, drawn by the style (see UIStyle)
class CBaseUIStyledButton : public CBaseUIButton {
    NOCOPY_NOMOVE(CBaseUIStyledButton)
   public:
    explicit CBaseUIStyledButton(std::string text = {}, std::string name = {});
    ~CBaseUIStyledButton() override;

    void draw() override;
    [[nodiscard]] vec2 getNaturalSize() override;
};
