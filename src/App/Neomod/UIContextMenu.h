#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUIButton.h"
#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"

#include "Delegate.h"

class CBaseUIContainer;

class UIContextMenuButton;
class UIContextMenuTextbox;

class UIContextMenu final : public CBaseUIScrollView {
   public:
    void clampToBottomScreenEdge();
    void clampToRightScreenEdge();

   public:
    UIContextMenu(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, const UString &name = {},
                  CBaseUIScrollView *parent = nullptr);

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void onKeyUp(KeyboardEvent &e) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    using ButtonClickCallback = SA::delegate<void(const UString &, int)>;
    void setClickCallback(ButtonClickCallback clickCallback) { this->clickCallback = std::move(clickCallback); }

    void begin(int minWidth = 0, bool bigStyle = false);
    UIContextMenuButton *addButtonJustified(const UString &text, TEXT_JUSTIFICATION j = TEXT_JUSTIFICATION::CENTERED,
                                            int id = -1);
    inline UIContextMenuButton *addButton(const UString &text, int id = -1) {
        return this->addButtonJustified(text, TEXT_JUSTIFICATION::LEFT, id);
    };
    UIContextMenuTextbox *addTextbox(const UString &text, int id = -1);

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

    std::vector<CBaseUIElement *> selfDeletionCrashWorkaroundScheduledElementDeleteHack;

    CBaseUIScrollView *parent = nullptr;
    UIContextMenuTextbox *containedTextbox = nullptr;
    ButtonClickCallback clickCallback = {};

    i32 iYCounter = 0;
    i32 iWidthCounter = 0;
    f32 fAnimation = 0.f;

    bool bVisible2 = false;
    bool bInvertAnimation = false;

    bool bBigStyle;
};

MAKE_FLAG_ENUM(UIContextMenu::EndStyle)

class UIContextMenuButton final : public CBaseUIButton {
    NOCOPY_NOMOVE(UIContextMenuButton)
   public:
    UIContextMenuButton(float xPos, float yPos, float xSize, float ySize, UString name, UString text, int id);
    ~UIContextMenuButton() override { ; }

    void update(CBaseUIEventCtx &c) override;

    void onMouseInside() override;
    void onMouseDownInside(bool left = true, bool right = false) override;

    [[nodiscard]] inline int getID() const { return this->iID; }

    void setTooltipText(const UString &text);

   private:
    int iID;

    std::vector<UString> tooltipTextLines;
};

class UIContextMenuTextbox final : public CBaseUITextbox {
    NOCOPY_NOMOVE(UIContextMenuTextbox)
   public:
    UIContextMenuTextbox(float xPos, float yPos, float xSize, float ySize, UString name, int id);
    ~UIContextMenuTextbox() override { ; }

    [[nodiscard]] inline int getID() const { return this->iID; }

   private:
    int iID;
};
