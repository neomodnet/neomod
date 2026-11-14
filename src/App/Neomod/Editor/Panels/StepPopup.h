#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "Document.h"
#include "noinclude.h"
#include "types.h"

#include <functional>
#include <string>
#include <vector>

class CBaseUINumberField;
class CBaseUIPopupLayer;
class CBaseUIStyledButton;
class CBaseUIStyledLabel;
struct EditorState;

// a popup of values that make one step together, shown on the map while they change: `show` makes the step for them
// through the Compose previews when the popup opens and after each change, and says whether it can be kept. Apply or
// Enter keeps it, Cancel or Escape takes it back. a click outside closes it too, with the preview still going: whoever
// opened it ends that (Compose::endPreview())
class StepPopup final : public CBaseUIBox {
    NOCOPY_NOMOVE(StepPopup)
   public:
    using Show = std::function<bool(const StepPopup &)>;

    StepPopup(EditorState &state, CBaseUIPopupLayer &popups, std::string name, std::string title, Show show);
    ~StepPopup() override;

    // a number to set (number(i) is the i-th one added)
    CBaseUINumberField *addNumber(const std::string &name, std::string label, f64 value, f64 min, f64 max, f64 step,
                                  int decimals);
    // buttons of which one is chosen (choice(i))
    void addChoice(const std::string &name, std::string label, std::vector<std::string> options, i32 chosen);
    // a button that's on or off (toggle(i))
    void addToggle(const std::string &name, std::string label, bool on);
    // starts the preview of `action` and shows the popup at `pos`, its first number field typed in
    void open(Document::Action action, vec2 pos);

    [[nodiscard]] f64 number(uSz i) const;
    [[nodiscard]] i32 choice(uSz i) const;
    [[nodiscard]] bool toggle(uSz i) const;
    [[nodiscard]] CBaseUINumberField *numberField(uSz i) const { return this->numbers[i]; }

    void tick() override;
    void draw() override;
    void onKeyDown(KeyboardEvent &e) override;

   private:
    struct Choice {
        std::vector<CBaseUIStyledButton *> buttons;
        i32 chosen;
    };

    void addRow(std::string label, CBaseUIElement *control);
    // show() again if a value changed since it last ran
    void update();
    void close(bool keep);

    EditorState &state;
    CBaseUIPopupLayer &popups;
    Show show;
    CBaseUIBox *rows;
    CBaseUIStyledButton *apply;
    std::vector<CBaseUIStyledLabel *> labels;
    std::vector<CBaseUINumberField *> numbers;
    std::vector<Choice> choices;
    std::vector<CBaseUIStyledButton *> toggles;
    std::vector<f64> shown;  // the values show() last ran with
    u32 id{0};               // its id in the popup layer, which owns it while it's open
};
