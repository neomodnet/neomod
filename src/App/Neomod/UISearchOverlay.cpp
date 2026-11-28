// Copyright (c) 2017, PG, All rights reserved.
#include "UISearchOverlay.h"
#include "Algorithms.h"

#include <cmath>
#include <cwctype>
#include <string>
#include <utility>

#include "fmt/format.h"

#include "Engine.h"
#include "Environment.h"
#include "Keyboard.h"
#include "KeyboardEvent.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "Font.h"
#include "Graphics.h"
#include "i18n.h"
#include "UniString.h"

UISearchOverlay::UISearchOverlay(float xPos, float yPos, float xSize, float ySize, std::string name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
    // pure-draw decoration spanning the panel: hit-transparent so it never wins the single top-most
    // hover/click/wheel over the real widgets beneath it (handle-off alone is not enough now that
    // candidacy is rect-based and no longer handle-gated)
    this->bClickThroughSelf = true;

    this->font = engine->getDefaultFont();

    this->iOffsetRight = 0;
    this->bDrawNumResults = true;

    this->iNumFoundResults = -1;

    this->bSearching = false;
}

void UISearchOverlay::draw() {
    // draw search text and background
    McFont *searchTextFont = this->font;
    const float lineHeight = searchTextFont->getHeight();

    const std::string_view searchText1 = _("Search: ");
    const std::string_view searchText2 = _("Type to search!");
    const std::string_view noMatchesFoundText1 = _("No matches found. Hit ESC to reset.");
    const std::string_view noMatchesFoundText2 = _("Hit ESC to reset.");
    const std::string_view searchingText2 = _("Searching, please wait ...");

    // the search text, with what the IME composes for it after it
    const TextComposition *composition = keyboard->getComposition(this);
    std::string composedText;
    std::string_view drawnText = this->sSearchString;
    if(composition != nullptr) drawnText = composedText = this->sSearchString + composition->text;

    const bool hasHardcodedSearch = !this->sHardcodedSearchString.empty();
    const bool hasSearchSubTextVisible = !this->sSearchString.empty() && this->bDrawNumResults;

    const float searchText1Width = searchTextFont->getStringWidth(searchText1);
    const float searchWidth = searchTextFont->getStringWidth(this->sSearchString);
    const float drawnWidth = composition != nullptr ? searchTextFont->getStringWidth(drawnText) : searchWidth;

    // right-aligned by the longer fixed line; a search text running past the screen moves everything left (no wrapping)
    const float lineWidth = searchTextFont->getStringWidth(
        this->iNumFoundResults < 0 ? fmt::format("{}{}", searchText1, searchText2) : noMatchesFoundText1);
    const float lineLeft =
        this->getPos().x + this->getSize().x - lineWidth - lineHeight * 0.5f - (float)this->iOffsetRight;
    const int overflow =
        Mc::max(0, (int)lineLeft + (int)searchText1Width + (int)drawnWidth - (int)osu->getVirtScreenWidth());
    const int lineX = (int)(lineLeft - (float)overflow);
    const int lineY = (int)(lineHeight / 2.0f) + (int)(this->getPos().y + lineHeight * 1.5f);

    // the IME candidates go by where the search text ends, which is where a composition starts
    {
        const float textX = (float)(lineX + (int)searchText1Width);
        const float textY = (float)lineY + (hasHardcodedSearch ? lineHeight * 1.5f : 0.0f);
        const float composedWidth = composition != nullptr
                                        ? searchTextFont->getStringWidth(std::string_view{composition->text}.substr(
                                              0, composition->cursor + composition->clauseLength))
                                        : 0.0f;
        this->textInputArea = {.rect = McRect{textX + searchWidth, textY - lineHeight,
                                              Mc::max(drawnWidth - searchWidth, 1.0f), lineHeight * 1.5f},
                               .caretX = textX + searchWidth + composedWidth};
    }

    // draw background
    {
        float numLines = hasSearchSubTextVisible ? 4.0f : 3.0f;
        if(hasHardcodedSearch) numLines += 1.5f;
        const int width = (int)(lineWidth + lineHeight + (float)this->iOffsetRight) + overflow;

        g->setColor(argb(drawnText.empty() ? 30 : 100, 0, 0, 0));
        g->fillRect(this->getPos().x + this->getSize().x - (float)width, this->getPos().y, width,
                    lineHeight * numLines);
    }

    // draw text
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(lineX, lineY);

        // draw search text and text
        g->pushTransform();
        {
            g->translate(1, 1);
            g->setColor(0xff000000);
            g->drawString(searchTextFont, searchText1);
            g->translate(-1, -1);
            g->setColor(0xff00ff00);
            g->drawString(searchTextFont, searchText1);

            if(hasHardcodedSearch) {
                g->pushTransform();
                {
                    g->translate(searchText1Width, 0);

                    g->translate(1, 1);
                    g->setColor(0xff000000);
                    g->drawString(searchTextFont, this->sHardcodedSearchString);
                    g->translate(-1, -1);
                    g->setColor(0xff34ab94);
                    g->drawString(searchTextFont, this->sHardcodedSearchString);
                }
                g->popTransform();

                g->translate(0, lineHeight * 1.5f);
            }

            g->translate((int)searchText1Width, 0);
            const std::string_view shown = drawnText.empty() ? std::string_view{searchText2} : drawnText;
            g->translate(1, 1);
            g->setColor(0xff000000);
            g->drawString(searchTextFont, shown);

            g->translate(-1, -1);
            g->setColor(0xffffffff);
            g->drawString(searchTextFont, shown);

            // what the IME composes after the search text is underlined, the part it converts thicker
            if(composition != nullptr) {
                const auto at = [&](i32 pos) {
                    return searchWidth +
                           searchTextFont->getStringWidth(std::string_view{composition->text}.substr(0, pos));
                };
                const float dpiScale = (float)searchTextFont->getDPI() / 96.0f;
                const float thickness = Mc::max(1.0f, std::round(dpiScale));
                const float underlineY = std::round(2 * dpiScale);
                g->fillRectf(searchWidth, underlineY, drawnWidth - searchWidth, thickness);
                if(composition->clauseLength > 0)
                    g->fillRectf(at(composition->cursor), underlineY,
                                 at(composition->cursor + composition->clauseLength) - at(composition->cursor),
                                 thickness * 2);
            }
        }
        g->popTransform();

        // draw number of matches
        if(hasSearchSubTextVisible) {
            g->translate(0, (int)(lineHeight * 1.5f * (hasHardcodedSearch ? 2.0f : 1.0f)));
            g->translate(1, 1);

            if(this->bSearching) {
                g->setColor(0xff000000);
                g->drawString(searchTextFont, searchingText2);
                g->translate(-1, -1);
                g->setColor(0xffffffff);
                g->drawString(searchTextFont, searchingText2);
            } else {
                g->setColor(0xff000000);
                g->drawString(searchTextFont, this->iNumFoundResults > -1
                                                  ? (this->iNumFoundResults > 0
                                                         ? fmt::format("{:d} match{:s} found!", this->iNumFoundResults,
                                                                       this->iNumFoundResults == 1 ? "" : "es")
                                                         : noMatchesFoundText1)
                                                  : noMatchesFoundText2);
                g->translate(-1, -1);
                g->setColor(0xffffffff);
                g->drawString(searchTextFont, this->iNumFoundResults > -1
                                                  ? (this->iNumFoundResults > 0
                                                         ? fmt::format("{:d} match{:s} found!", this->iNumFoundResults,
                                                                       this->iNumFoundResults == 1 ? "" : "es")
                                                         : noMatchesFoundText1)
                                                  : noMatchesFoundText2);
            }
        }
    }
    g->popTransform();
}

void UISearchOverlay::onTextInput(std::string_view text) {
    if(this->typingBlocked && this->typingBlocked()) return;
    this->sSearchString.append(text);
    this->changed();
}

void UISearchOverlay::onTextComposition(const TextComposition & /*composition*/) {
    // nothing to keep: draw() reads it from the keyboard
}

void UISearchOverlay::onEditKey(KeyboardEvent &e) {
    const bool control = (e.getModifiers() & KEYMOD_CONTROL) != 0;
    if(!this->sSearchString.empty() && (e == KEY_DELETE || e == KEY_BACKSPACE)) {
        e.consume();
        std::u32string search{UniString::to_utf32(this->sSearchString)};
        if(control) {
            // delete everything from the current caret position to the left, until after the first
            // non-space character (but including it)
            bool foundNonSpaceChar = false;
            while(!search.empty()) {
                const bool whitespace = std::iswspace(static_cast<wint_t>(search.back())) != 0;
                if(foundNonSpaceChar && whitespace) break;

                if(!whitespace) foundNonSpaceChar = true;

                search.pop_back();
            }
        } else {
            search.pop_back();
        }
        this->sSearchString = UniString::to_utf8(search);
        this->changed();
    } else if(!this->sSearchString.empty() && e == KEY_ESCAPE) {
        e.consume();
        this->sSearchString.clear();
        this->changed();
    } else if(control && e == KEY_V) {
        // paste clipboard support
        if(const std::string_view clipstring = env->getClipBoardText(); !clipstring.empty()) {
            e.consume();
            this->sSearchString.append(clipstring);
            this->changed();
        }
    }
}
