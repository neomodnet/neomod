#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "Color.h"

#include <array>
#include <functional>
#include <optional>
#include <string>

class CBaseUINumberField;
class CBaseUIStyledTextbox;

// a colour to set: a square with the saturations (left to right) and values (bottom to top) for one hue, the hues in a
// strip under it, then the red, green and blue channels as number fields and the hex code as a text field. the caller
// hears of a colour once it's set: when a drag in the square or the strip ends, when what was typed is taken. Tab goes
// through its fields
class CBaseUIColorPicker final : public CBaseUIBox {
    NOCOPY_NOMOVE(CBaseUIColorPicker)
   public:
    explicit CBaseUIColorPicker(std::string name = {});
    ~CBaseUIColorPicker() override;

    // shows it, opaque (the change callback isn't called)
    CBaseUIColorPicker *setColor(Color colour);
    // what it shows: during a drag not yet what its user was told
    [[nodiscard]] Color getColor() const { return this->colour; }
    // a colour to compare with, shown beside the one being set; a click on it sets it
    CBaseUIColorPicker *setReference(Color colour);
    // called with the colour the user set
    CBaseUIColorPicker *setChangeCallback(std::function<void(Color)> callback);
    // a drag is changing it, or one of its fields is being typed in
    [[nodiscard]] bool isAdjusting();

    void tick() override;
    void onKeyDown(KeyboardEvent &e) override;

   private:
    class Square;
    class Strip;
    class Preview;

    // the colour of these, all from 0 to 1
    [[nodiscard]] static Color fromHSV(float hue, float saturation, float value);
    // `c` becomes the colour shown: the fields get it, and the square and the strip unless they made it (`keepHSV`)
    void show(Color c, bool keepHSV);
    // tells the caller about the colour, if it isn't the one they know
    void settle();

    std::function<void(Color)> changeCallback;
    Color colour{0xff000000};
    Color settled{0xff000000};  // the colour its user knows
    std::optional<Color> reference;
    // where the colour is in the strip and the square: a grey keeps the hue it had, black its saturation too
    float hue{0.f};
    float saturation{0.f};
    float value{0.f};

    Square *square;
    Strip *strip;
    std::array<CBaseUINumberField *, 3> channels{};  // red, green, blue
    CBaseUIStyledTextbox *hex;
};
