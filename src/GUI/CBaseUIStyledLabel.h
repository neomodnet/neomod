#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIElement.h"

#include <string>
#include <string_view>

// one line of text in the style's font and colours (see UIStyle), vertically centered in its rect. text wider than the
// rect is cut with an ellipsis: at its end, or in its middle for text whose end matters (setCutInMiddle())
class CBaseUIStyledLabel : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIStyledLabel)
   public:
    explicit CBaseUIStyledLabel(std::string text = {}, std::string name = {});
    ~CBaseUIStyledLabel() override;

    CBaseUIStyledLabel *setText(std::string text);
    CBaseUIStyledLabel *setJustification(TEXT_JUSTIFICATION justification);
    CBaseUIStyledLabel *setDim(bool dim);
    CBaseUIStyledLabel *setCutInMiddle(bool middle);
    [[nodiscard]] std::string_view getText() const { return this->text; }
    // what's drawn: the text, or as much of it as fits
    [[nodiscard]] std::string_view getShownText() const { return this->cut.empty() ? this->text : this->cut; }

    void draw() override;
    void onResized() override;
    [[nodiscard]] vec2 getNaturalSize() override;

   private:
    void fit();

    std::string text;
    std::string cut;  // a text wider than the rect, cut to fit (empty: it fits)
    TEXT_JUSTIFICATION justification{TEXT_JUSTIFICATION::LEFT};
    bool dim{false};
    bool cutInMiddle{false};
};
