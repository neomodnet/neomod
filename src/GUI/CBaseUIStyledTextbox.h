#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUITextbox.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

// a line of text to edit, drawn by the style (see UIStyle). a visit lasts from its first key until the focus leaves, and
// its user hears of the text once, when a visit ends with another text than it started with: Enter ends it, Escape or
// Ctrl+Z ends it with the text it started with. while it has the focus it takes the keys that type or edit and leaves
// the rest to whatever handles keys after it: the function keys, Tab, and what's pressed with Ctrl or Cmd that it has
// no use for
class CBaseUIStyledTextbox : public CBaseUITextbox {
    NOCOPY_NOMOVE(CBaseUIStyledTextbox)
   public:
    explicit CBaseUIStyledTextbox(std::string name = {});
    ~CBaseUIStyledTextbox() override;

    // how wide it wants to be (units); it's as high as the style's controls
    CBaseUIStyledTextbox *setNaturalWidth(float units);
    // called with the text when a visit ends with another one than it started with (never for setText())
    CBaseUIStyledTextbox *setChangeCallback(std::function<void(std::string_view)> callback);
    // which characters it takes, typed or pasted (control characters never)
    CBaseUIStyledTextbox *setFilter(std::function<bool(char32_t)> filter);
    // takes the focus with its text selected, to type over (what going to it with Tab does)
    virtual void visit();
    // ends the visit where it is and keeps the focus: for whoever acts on its text while it's being typed in
    virtual void commit();

    void draw() override;
    void onKeyDown(KeyboardEvent &e) override;
    void onTextInput(std::string_view text) override;
    void onTextComposition(const TextComposition &composition) override;
    void onFocusStolen() override;
    void onResized() override;
    [[nodiscard]] vec2 getNaturalSize() override;

   protected:
    // ends the visit with the text it started with
    virtual void revert();

   private:
    [[nodiscard]] bool takes(char32_t character) const;
    // after a key or a character the base class handled: drops what the filter refuses
    void edited(const std::string &before);

    std::function<void(std::string_view)> changeCallback;
    std::function<bool(char32_t)> filter;
    std::optional<std::string> visitText;  // what the visit started with
    float naturalWidth{140.f};
};
