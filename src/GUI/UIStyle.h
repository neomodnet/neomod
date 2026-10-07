#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Color.h"
#include "Rect.h"
#include "types.h"

#include <string_view>

class McFont;
enum class TEXT_JUSTIFICATION : u8;

// what the styled widgets look like: the values (palette, metrics, font) and the drawing of their decorations by role,
// in one place, so that the look can change without touching them. lengths are in units of getScale() pixels, which the
// app keeps at its UI scale (the font follows the same DPI). widgets ask uiStyle() whenever they lay out or draw
class UIStyle {
   public:
    struct Palette {
        Color background{argb(255, 24, 24, 28)};
        Color panel{argb(255, 34, 34, 41)};
        Color border{argb(255, 52, 52, 62)};
        Color control{argb(255, 46, 46, 56)};
        Color controlHovered{argb(255, 60, 60, 72)};
        Color accent{argb(255, 64, 140, 200)};
        Color text{argb(255, 230, 230, 235)};
        Color textDim{argb(255, 150, 150, 162)};
        Color textDisabled{argb(255, 96, 96, 106)};
    } palette;

    // (units)
    struct Metrics {
        float padding{8.f};          // between a panel's edge and what it holds
        float gap{6.f};              // between neighbours
        float controlPadding{10.f};  // between a control's edge and its text, horizontally
        float controlHeight{28.f};
    } metrics;

    // pixels per unit
    [[nodiscard]] float getScale() const { return this->scale; }
    void setScale(float scale);
    // goes up whenever something that layouts depend on changes (the scale, and with it the font)
    [[nodiscard]] u32 getGeneration() const { return this->generation; }

    [[nodiscard]] float px(float units) const { return units * this->scale; }
    [[nodiscard]] McFont *font() const;

    enum class ControlState : u8 { NORMAL, HOVERED, PRESSED, DISABLED };

    void drawPanel(const McRect &rect) const;
    void drawControl(const McRect &rect, ControlState state) const;
    // text in a rect, vertically centered
    void drawText(const McRect &rect, std::string_view text, TEXT_JUSTIFICATION justification, ControlState state,
                  bool dim = false) const;

   private:
    float scale{1.f};
    u32 generation{0};
};

// the current style
UIStyle &uiStyle();
