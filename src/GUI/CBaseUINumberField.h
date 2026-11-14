#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIStyledTextbox.h"
#include "types.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>

// a number to set, drawn as a text field. a drag to the left or right changes it by steps (with Shift by its fine
// step); a click types it in instead: Enter or the focus leaving takes what was typed, kept within the range, and
// anything that's no number leaves it as it was. Up and Down step it while it's typed in. its user hears of a value
// once it's set: when a drag ends, when what was typed is taken
class CBaseUINumberField : public CBaseUIStyledTextbox {
    NOCOPY_NOMOVE(CBaseUINumberField)
   public:
    explicit CBaseUINumberField(std::string name = {});
    ~CBaseUINumberField() override;

    // what the user can set it to
    CBaseUINumberField *setRange(f64 min, f64 max);
    // what a step of a drag or of the arrow keys changes it by (fine: with Shift)
    CBaseUINumberField *setStep(f64 step, f64 fine);
    // how many decimals a value the user sets has at most
    CBaseUINumberField *setDecimals(int decimals);
    // shown after the number while it isn't being typed in
    CBaseUINumberField *setSuffix(std::string suffix);
    // a value that shows as `text` while it isn't being typed in (0 standing for "automatic", say)
    CBaseUINumberField *setNamedValue(f64 value, std::string text);
    // shows it as it is (the range and the decimals only bind what the user sets). while the user types, what's typed
    // stays. a value that's no number (NaN) shows as nothing, for what there are several of: it can be typed over,
    // not dragged or stepped
    CBaseUINumberField *setValue(f64 value);
    // what it shows: during a drag, and after steps while it's typed in, not yet what its user was told
    [[nodiscard]] f64 getValue() const { return this->value; }
    // called with the value the user set, once a drag ended or what was typed was taken
    CBaseUINumberField *setChangeCallback(std::function<void(f64)> callback);
    // a drag is changing it
    [[nodiscard]] bool isAdjusting() const { return this->dragging; }

    void visit() override;
    void commit() override;
    [[nodiscard]] std::optional<CURSORTYPE> getCursorShape() override;
    void onKeyDown(KeyboardEvent &e) override;
    void onFocusStolen() override;

   protected:
    void revert() override;
    void onMouseDownInside(bool left = true, bool right = false) override;
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseUpOutside(bool left = true, bool right = false) override;
    void onMouseCancel() override;
    void onCapturedMouseMove() override;

   private:
    // the value as text, with the suffix unless it's for typing
    [[nodiscard]] std::string format(bool typing) const;
    // within the range, with at most the decimals
    [[nodiscard]] f64 fit(f64 v) const;
    // shows `v`, which its user isn't told of yet
    void show(f64 v);
    // tells its user of the value, if it isn't the one they know
    void settle();
    // what was typed becomes the value, if it's a number
    void takeTyped();

    std::function<void(f64)> changeCallback;
    std::string suffix;
    std::optional<std::pair<f64, std::string>> named;
    f64 value{0.0};
    f64 settled{0.0};  // the value its user knows
    f64 min{0.0};
    f64 max{100.0};
    f64 step{1.0};
    f64 fine{0.1};
    int decimals{0};

    // a press that may become a drag, and the drag: where and at which value it started, how many steps the cursor was
    // from there at the last update, and the steps made since the press
    bool pressed{false};
    bool dragging{false};
    float pressX{0.f};
    f64 pressValue{0.0};
    i32 seenSteps{0};
    i32 steps{0};
    i32 fineSteps{0};
    // typing, and the text that started with (left as it is, it changes nothing)
    bool typing{false};
    std::string typingText;
};
