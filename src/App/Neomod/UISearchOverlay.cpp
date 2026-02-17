// Copyright (c) 2017, PG, All rights reserved.
#include "UISearchOverlay.h"

#include <utility>

#include "Engine.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "Font.h"
#include "Graphics.h"

UISearchOverlay::UISearchOverlay(float xPos, float yPos, float xSize, float ySize, UString name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
    this->font = engine->getDefaultFont();

    this->iOffsetRight = 0;
    this->bDrawNumResults = true;

    this->iNumFoundResults = -1;

    this->bSearching = false;
}

void UISearchOverlay::draw() {
    /*
    g->setColor(0xaaaaaaaa);
    g->fillRect(this->getPos(), this->getSize());
    */

    // draw search text and background
    const float searchTextScale = 1.0f;
    McFont *searchTextFont = this->font;

    const UString searchText1 = "Search: ";
    const UString searchText2 = "Type to search!";
    const UString noMatchesFoundText1 = "No matches found. Hit ESC to reset.";
    const UString noMatchesFoundText2 = "Hit ESC to reset.";
    const UString searchingText2 = "Searching, please wait ...";

    UString combinedSearchText = searchText1;
    combinedSearchText.append(searchText2);

    UString offsetText = (this->iNumFoundResults < 0 ? combinedSearchText : noMatchesFoundText1);

    bool hasSearchSubTextVisible = this->sSearchString.length() > 0 && this->bDrawNumResults;

    const float searchStringWidth = searchTextFont->getStringWidth(this->sSearchString);
    const float offsetTextStringWidth = searchTextFont->getStringWidth(offsetText);

    const int offsetTextWidthWithoutOverflow =
        offsetTextStringWidth * searchTextScale + (searchTextFont->getHeight() * searchTextScale) + this->iOffsetRight;

    // calc global x offset for overflowing line (don't wrap, just move everything to the left)
    int textOverflowXOffset = 0;
    {
        const int actualXEnd = (int)(this->getPos().x + this->getSize().x - offsetTextStringWidth * searchTextScale -
                                     (searchTextFont->getHeight() * searchTextScale) * 0.5f - this->iOffsetRight) +
                               (int)(searchTextFont->getStringWidth(searchText1) * searchTextScale) +
                               (int)(searchStringWidth * searchTextScale);
        if(actualXEnd > osu->getVirtScreenWidth()) textOverflowXOffset = actualXEnd - osu->getVirtScreenWidth();
    }

    // draw background
    {
        const float lineHeight = (searchTextFont->getHeight() * searchTextScale);
        float numLines = 1.0f;
        {
            if(hasSearchSubTextVisible)
                numLines = 4.0f;
            else
                numLines = 3.0f;

            if(this->sHardcodedSearchString.length() > 0) numLines += 1.5f;
        }
        const float height = lineHeight * numLines;
        const int offsetTextWidthWithOverflow = offsetTextWidthWithoutOverflow + textOverflowXOffset;

        g->setColor(argb(this->sSearchString.length() > 0 ? 100 : 30, 0, 0, 0));
        g->fillRect(this->getPos().x + this->getSize().x - offsetTextWidthWithOverflow, this->getPos().y,
                    offsetTextWidthWithOverflow, height);
    }

    // draw text
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(0, (int)(searchTextFont->getHeight() / 2.0f));
        g->scale(searchTextScale, searchTextScale);
        g->translate(
            (int)(this->getPos().x + this->getSize().x - offsetTextStringWidth * searchTextScale -
                  (searchTextFont->getHeight() * searchTextScale) * 0.5f - this->iOffsetRight - textOverflowXOffset),
            (int)(this->getPos().y + (searchTextFont->getHeight() * searchTextScale) * 1.5f));

        // draw search text and text
        g->pushTransform();
        {
            g->translate(1, 1);
            g->setColor(0xff000000);
            g->drawString(searchTextFont, searchText1);
            g->translate(-1, -1);
            g->setColor(0xff00ff00);
            g->drawString(searchTextFont, searchText1);

            if(this->sHardcodedSearchString.length() > 0) {
                const float searchText1Width = searchTextFont->getStringWidth(searchText1) * searchTextScale;

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

                g->translate(0, searchTextFont->getHeight() * searchTextScale * 1.5f);
            }

            g->translate((int)(searchTextFont->getStringWidth(searchText1) * searchTextScale), 0);
            g->translate(1, 1);
            g->setColor(0xff000000);
            if(this->sSearchString.length() < 1)
                g->drawString(searchTextFont, searchText2);
            else
                g->drawString(searchTextFont, this->sSearchString);

            g->translate(-1, -1);
            g->setColor(0xffffffff);
            if(this->sSearchString.length() < 1)
                g->drawString(searchTextFont, searchText2);
            else
                g->drawString(searchTextFont, this->sSearchString);
        }
        g->popTransform();

        // draw number of matches
        if(hasSearchSubTextVisible) {
            g->translate(0, (int)((searchTextFont->getHeight() * searchTextScale) * 1.5f *
                                  (this->sHardcodedSearchString.length() > 0 ? 2.0f : 1.0f)));
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
