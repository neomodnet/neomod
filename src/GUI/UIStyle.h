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
        Color accentHovered{argb(255, 84, 160, 220)};
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
        float rowHeight{24.f};       // a row of a menu or a list
        float separatorHeight{7.f};  // a menu's separator row
        float markWidth{18.f};       // the space a row keeps for a mark (see drawMark)
        float popupPadding{4.f};     // between a popup's edge and its rows
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
    // checked: the chosen one of a group, or a toggle that's on
    void drawControl(const McRect &rect, ControlState state, bool checked = false) const;
    // text in a rect, vertically centered
    void drawText(const McRect &rect, std::string_view text, TEXT_JUSTIFICATION justification, ControlState state,
                  bool dim = false) const;
    // the surface of a popup (a menu)
    void drawPopup(const McRect &rect) const;
    // what's behind a row of a menu or a list, or a menu bar's title: nothing unless it's hovered (or highlighted by the
    // keyboard) or pressed (an open menu's title)
    void drawRow(const McRect &rect, ControlState state) const;
    // a line across the middle of rect
    void drawSeparator(const McRect &rect) const;
    enum class Mark : u8 { CHECK, SUBMENU, DROPDOWN };
    // a mark in the middle of rect, in the text colour of state: a checked item's tick, a submenu's arrow, a dropdown's
    void drawMark(const McRect &rect, Mark mark, ControlState state) const;

   private:
    float scale{1.f};
    u32 generation{0};
};

// the current style
UIStyle &uiStyle();
