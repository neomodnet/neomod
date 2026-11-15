#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "noinclude.h"
#include "types.h"
#include "Rect.h"

#include <string>
#include <string_view>

// the kind of text a text input client takes; the os may pick an input method or on-screen keyboard layout by it
enum class TextInputType : u8 { TEXT = 0, PASSWORD = 1, NUMBER = 2 };

// what an IME is composing: shown where the caret is until it's committed (as text input) or dropped. cursor and
// clauseLength are byte offsets into text: the IME caret, and the part after it that is being converted
struct TextComposition {
    std::string text;
    i32 cursor{0};
    i32 clauseLength{0};
};

// where the text being edited is and where its caret is (same space), for placing the IME candidate window
struct TextInputArea {
    McRect rect;
    f32 caretX{0.f};
    bool operator==(const TextInputArea &) const = default;
};

// something typed text goes to: a text field, or a screen that types into its search without one. while it's the text
// target (Keyboard::setTextTarget), the os text input (IME, on-screen keyboard) is on for it and what's typed comes here
class TextInputClient {
    NOCOPY_NOMOVE(TextInputClient)
   public:
    TextInputClient() = default;
    virtual ~TextInputClient();

    // committed text: typed characters, a finished composition, a pick from an emoji panel
    virtual void onTextInput(std::string_view text) = 0;
    // the composition changed (empty text: it ended); Keyboard::getComposition() has it until then
    virtual void onTextComposition(const TextComposition &composition) = 0;
    // in the space of the ui root the client is in
    [[nodiscard]] virtual TextInputArea getTextInputArea() = 0;
    [[nodiscard]] virtual TextInputType getTextInputType() const { return TextInputType::TEXT; }
};
