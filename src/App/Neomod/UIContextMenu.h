#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUIButton.h"
#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"

#include "Delegate.h"

#include <memory>
#include <string>
#include <vector>

class CBaseUIContainer;

class UIContextMenuButton;
class UIContextMenuTextbox;

class UIContextMenu final : public CBaseUIScrollView {
    NOCOPY_NOMOVE(UIContextMenu)
   public:
    void clampToBottomScreenEdge();
    void clampToRightScreenEdge();

   public:
    UIContextMenu(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {},
                  CBaseUIScrollView *parent = nullptr);
    ~UIContextMenu() override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void onKeyUp(KeyboardEvent &e) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    using ButtonClickCallback = SA::delegate<void(std::string_view, int)>;
    void setClickCallback(ButtonClickCallback clickCallback) { this->clickCallback = std::move(clickCallback); }

    void begin(int minWidth = 0, bool bigStyle = false, bool searchable = false);
    UIContextMenuButton *addButtonJustified(const std::string &text,
                                            TEXT_JUSTIFICATION j = TEXT_JUSTIFICATION::CENTERED, int id = -1);
    inline UIContextMenuButton *addButton(const std::string &text, int id = -1) {
        return this->addButtonJustified(text, TEXT_JUSTIFICATION::LEFT, id);
    };
    UIContextMenuTextbox *addTextbox(const std::string &text, int id = -1);

    enum class EndStyle : u8 {
        CLAMP_TOP = (1 << 0),
        CLAMP_BOT = (1 << 1),
        CLAMP_LEFT = (1 << 2),
        CLAMP_RIGHT = (1 << 3),
        STANDALONE_SCROLL = (1 << 4),
        REPOSITION_ONSCREEN = (1 << 5),
        CLAMP_VERTICAL = CLAMP_TOP | CLAMP_BOT,
        CLAMP_HORIZONTAL = CLAMP_LEFT | CLAMP_RIGHT,
        CLAMP_BOUNDS = CLAMP_VERTICAL | CLAMP_HORIZONTAL,
    };
    void end(bool invertAnimation, EndStyle style);

    // compatibility wrapper to avoid messing with a bunch of code
    inline void end(bool invertAnimation, bool clampUnderflowAndOverflowAndEnableScrollingIfNecessary) {
        return this->end(invertAnimation,
                         clampUnderflowAndOverflowAndEnableScrollingIfNecessary
                             ? (EndStyle)((u8)EndStyle::CLAMP_VERTICAL | (u8)EndStyle::STANDALONE_SCROLL)
                             : EndStyle{0});
    }

    CBaseUIElement *setVisible(bool visible) override {
        // HACHACK: this->bVisible is always true, since we want to be able to put a context menu in a scrollview.
        //          When scrolling, scrollviews call setVisible(false) to clip items, and that breaks the menu.
        (void)visible;
        return this;
    }
    void setVisible2(bool visible2);

    bool isVisible() override { return this->bVisible2; }

    static constexpr const Color defaultBGColor{0xff222222};
    static constexpr const Color defaultFrameColor{0xffffffff};

   private:
    void onMouseDownOutside(bool left = true, bool right = false) override;

    void onClick(UIContextMenuButton *button);
    void onHitEnter(UIContextMenuTextbox *textbox);

    void updateSearch();
    void highlight(UIContextMenuButton *button);

    std::vector<CBaseUIElement *> selfDeletionCrashWorkaroundScheduledElementDeleteHack;

    CBaseUIScrollView *parent = nullptr;
    UIContextMenuTextbox *containedTextbox = nullptr;
    ButtonClickCallback clickCallback = {};

    std::vector<std::unique_ptr<UIContextMenuButton>> searchButtons;
    UIContextMenuTextbox *searchTextbox = nullptr;
    CBaseUIScrollView *searchList = nullptr;
    UIContextMenuButton *highlighted = nullptr;
    std::string sSearch;

    i32 iYCounter = 0;
    i32 iWidthCounter = 0;
    AnimFloat fAnimation;

    bool bVisible2 = false;
    bool bInvertAnimation = false;

    bool bBigStyle;
};

MAKE_FLAG_ENUM(UIContextMenu::EndStyle)

class UIContextMenuButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(UIContextMenuButton)
   public:
    UIContextMenuButton(float xPos, float yPos, float xSize, float ySize, std::string name, std::string text, int id);
    ~UIContextMenuButton() override { ; }

    void updateInput(CBaseUIEventCtx &c) override;

    void onMouseInside() override;
    void onMouseDownInside(bool left = true, bool right = false) override;

    [[nodiscard]] inline int getID() const { return this->iID; }

    void setTooltipText(std::string_view text);

   private:
    int iID;

    std::vector<std::string> tooltipTextLines;
};

class UIContextMenuTextbox final : public CBaseUITextbox {
    NOCOPY_NOMOVE(UIContextMenuTextbox)
   public:
    UIContextMenuTextbox(float xPos, float yPos, float xSize, float ySize, std::string name, int id);
    ~UIContextMenuTextbox() override { ; }

    [[nodiscard]] inline int getID() const { return this->iID; }

    // how far the text is from the left edge
    void setTextOffsetX(int textOffsetX) { this->iTextOffsetX = textOffsetX; }

   private:
    int iID;
};
