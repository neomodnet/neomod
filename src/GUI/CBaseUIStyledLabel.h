#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIElement.h"

#include <string>
#include <string_view>

// one line of text in the style's font and colours (see UIStyle), vertically centered in its rect
class CBaseUIStyledLabel : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIStyledLabel)
   public:
    explicit CBaseUIStyledLabel(std::string text = {}, std::string name = {});
    ~CBaseUIStyledLabel() override;

    CBaseUIStyledLabel *setText(std::string text);
    CBaseUIStyledLabel *setJustification(TEXT_JUSTIFICATION justification);
    CBaseUIStyledLabel *setDim(bool dim);
    [[nodiscard]] std::string_view getText() const { return this->text; }

    void draw() override;
    [[nodiscard]] vec2 getNaturalSize() override;

   private:
    std::string text;
    TEXT_JUSTIFICATION justification{TEXT_JUSTIFICATION::LEFT};
    bool dim{false};
};
