#pragma once
// Copyright (c) 2017, PG, All rights reserved.
#include "CBaseUIElement.h"
#include "TextInput.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>

class McFont;

// the search a screen types into without a focused text field (the text target it offers): it shows the search text
// with what the IME composes after it, the hardcoded filter and the match count. the screen gives it the keys that
// edit it (onEditKey) and hears of each change
class UISearchOverlay final : public CBaseUIElement, public TextInputClient {
   public:
    UISearchOverlay(float xPos, float yPos, float xSize, float ySize, std::string name);

    void draw() override;

    void onTextInput(std::string_view text) override;
    void onTextComposition(const TextComposition &composition) override;
    // where the search text was last drawn
    [[nodiscard]] TextInputArea getTextInputArea() override { return this->textInputArea; }

    // Backspace/Delete (with Ctrl: back to the start of the last word), Escape (clears it) and Ctrl+V (pastes);
    // consumes what edits the search, so an Escape with nothing searched is left to the screen
    void onEditKey(KeyboardEvent &e);

    [[nodiscard]] const std::string &getSearchString() const { return this->sSearchString; }
    // clears it without telling the screen
    void clear() { this->sSearchString.clear(); }
    void setChangeCallback(std::function<void()> callback) { this->changeCallback = std::move(callback); }
    // typed text is dropped while this says so (a hotkey on a key that types is held)
    void setTypingBlocked(std::function<bool()> blocked) { this->typingBlocked = std::move(blocked); }

    void setDrawNumResults(bool drawNumResults) { this->bDrawNumResults = drawNumResults; }
    void setOffsetRight(int offsetRight) { this->iOffsetRight = offsetRight; }
    void setHardcodedSearchString(std::string hardcodedSearchString) {
        this->sHardcodedSearchString = std::move(hardcodedSearchString);
    }
    void setNumFoundResults(int numFoundResults) { this->iNumFoundResults = numFoundResults; }

    void setSearching(bool searching) { this->bSearching = searching; }

   private:
    void changed() {
        if(this->changeCallback) this->changeCallback();
    }

    McFont *font;

    int iOffsetRight;
    bool bDrawNumResults;

    std::string sSearchString;
    std::string sHardcodedSearchString;
    int iNumFoundResults;

    bool bSearching;

    TextInputArea textInputArea{};

    std::function<void()> changeCallback;
    std::function<bool()> typingBlocked;
};
