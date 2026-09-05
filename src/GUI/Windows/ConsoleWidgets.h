#pragma once
// Copyright (c) 2011, PG & 2026, WH, All rights reserved.
#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"
#include "MakeDelegateWrapper.h"

#include <string>
#include <string_view>
#include <utility>

class CBaseUIButton;
class ConsoleSuggestionList;

namespace Mc {
// integer text scale for the console font (tahoma 8 @ 96 dpi without antialiasing, only crisp in whole steps)
[[nodiscard]] float consoleLogScale(float dpiScale);
}  // namespace Mc

// the command input. it drives the suggestion popup (the owner's element, placed by the owner): every edit rebuilds
// it, the top match is previewed in grey behind the text, up/down/tab cycle it (up/down cycle the command history
// while there are no suggestions), enter submits, losing the keyboard dismisses it
class ConsoleTextbox final : public CBaseUITextbox {
    NOCOPY_NOMOVE(ConsoleTextbox)
   public:
    ConsoleTextbox(float xPos, float yPos, float xSize, float ySize, std::string name,
                   ConsoleSuggestionList *suggestions);
    ~ConsoleTextbox() override = default;

    void onKeyDown(KeyboardEvent &e) override;
    void onFocusStolen() override;

    CBaseUITextbox *setText(std::string text) override;

    // runs the text as a command and clears the box
    void submit();

   protected:
    void drawText() override;
    void onMouseDownOutside(bool left, bool right) override;

   private:
    // puts a completion or a history entry into the box without touching the suggestions (tab keeps cycling)
    void complete(std::string text);
    void onSuggestionPicked(std::string command);

    ConsoleSuggestionList *suggestions;
    std::string sSuggestionsInput;  // the text the suggestions were last synced to
};

// the suggestion popup: one button per matching convar, with keyboard cycling. it sizes itself to its rows (at most 4)
// and is visible while it has any; the owner places and animates it
class ConsoleSuggestionList final : public CBaseUIScrollView {
    NOCOPY_NOMOVE(ConsoleSuggestionList)
   public:
    ConsoleSuggestionList(float xPos, float yPos, float xSize, float ySize, std::string name);
    ~ConsoleSuggestionList() override = default;

    // replaces the buttons with the matches for the input text
    void rebuild(std::string_view input);
    // dismisses the popup
    void clear();

    // moves the selection down (dir > 0) or up, wrapping around, and returns the completed command for the input box
    std::string cycle(int dir);

    // called with the completed command when a suggestion is clicked
    void setCommandCallback(SA::delegate<void(std::string)> callback) { this->commandCallback = std::move(callback); }

    [[nodiscard]] int getCount() const { return static_cast<int>(this->container.getElements().size()); }
    [[nodiscard]] std::string_view getTopCommand() const;
    [[nodiscard]] float getRowHeight() const;

   private:
    void onSuggestionClicked(CBaseUIButton *button);
    [[nodiscard]] float getDPIScale() const;

    SA::delegate<void(std::string)> commandCallback;
    int iSelected{-1};
};
