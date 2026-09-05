#pragma once
// Copyright (c) 2014, PG, All rights reserved.
#include "CBaseUIElement.h"
#include "Color.h"

#include <array>
#include <memory>

class CBaseUIButton;
class CBaseUIContainer;

class McFont;

class CBaseUIWindow : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIWindow)
   public:
    CBaseUIWindow(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {});
    ~CBaseUIWindow() override;

    void draw() override;
    virtual void drawCustomContent() { ; }
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    // actions
    void close();
    void open();

    // set
    CBaseUIWindow *setSizeToContent(int horizontalBorderSize = 1, int verticalBorderSize = 1);
    CBaseUIWindow *setTitleBarHeight(int height) {
        this->iTitleBarHeight = height;
        this->updateTitleBarMetrics();
        return this;
    }
    CBaseUIWindow *setTitle(std::string text);
    CBaseUIWindow *setTitleFont(McFont *titleFont) {
        this->titleFont = titleFont;
        this->updateTitleBarMetrics();
        return this;
    }
    CBaseUIWindow *setResizeLimit(int maxWidth, int maxHeight) {
        this->vResizeLimit = vec2(maxWidth, maxHeight);
        return this;
    }
    CBaseUIWindow *setResizeable(bool resizeable) {
        this->bResizeable = resizeable;
        return this;
    }
    CBaseUIWindow *setDrawTitleBarLine(bool drawTitleBarLine) {
        this->bDrawTitleBarLine = drawTitleBarLine;
        return this;
    }
    CBaseUIWindow *setDrawFrame(bool drawFrame) {
        this->bDrawFrame = drawFrame;
        return this;
    }
    CBaseUIWindow *setDrawBackground(bool drawBackground) {
        this->bDrawBackground = drawBackground;
        return this;
    }
    CBaseUIWindow *setRoundedRectangle(bool roundedRectangle) {
        this->bRoundedRectangle = roundedRectangle;
        return this;
    }

    CBaseUIWindow *setBackgroundColor(Color backgroundColor) {
        this->backgroundColor = backgroundColor;
        return this;
    }
    CBaseUIWindow *setFrameColor(Color frameColor) {
        this->frameColor = frameColor;
        return this;
    }
    CBaseUIWindow *setFrameBrightColor(Color frameBrightColor) {
        this->frameBrightColor = frameBrightColor;
        return this;
    }
    CBaseUIWindow *setFrameDarkColor(Color frameDarkColor) {
        this->frameDarkColor = frameDarkColor;
        return this;
    }
    CBaseUIWindow *setTitleColor(Color titleColor) {
        this->titleColor = titleColor;
        return this;
    }

    // get
    bool isBusy() override;
    bool isActive() override;
    [[nodiscard]] inline bool isMoving() const { return this->bMoving; }
    [[nodiscard]] inline bool isResizing() const { return this->bResizing; }
    [[nodiscard]] inline CBaseUIContainer *getContainer() const { return this->container.get(); }
    [[nodiscard]] inline CBaseUIContainer *getTitleBarContainer() const { return this->titleBarContainer.get(); }
    inline int getTitleBarHeight() { return this->iTitleBarHeight; }

    // events
    void onMouseDownInside(bool left = true, bool right = false) override;
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseUpOutside(bool left = true, bool right = false) override;
    void onMouseCancel() override;
    void onMouseOutside() override;
    void onCapturedMouseMove() override;
    bool onWheel(int deltaVertical, int deltaHorizontal) override;

    void onMoved() override;
    void onResized() override;

    virtual void onResolutionChange(vec2 newResolution);

    void onEnabled() override;
    void onDisabled() override;

    [[nodiscard]] std::span<CBaseUIElement *const> getAllChildren() const override { return this->children; }

   protected:
    void updateTitleBarMetrics();
    void updateResizeAndMoveLogic(bool captureMouse);
    void updateWindowLogic();

    virtual void onClosed() { ; }

    inline CBaseUIButton *getCloseButton() { return this->closeButton; }

   private:
    // title bar
    std::unique_ptr<CBaseUIContainer> titleBarContainer;
    CBaseUIButton *closeButton;  // owned by the title bar container
    McFont *titleFont;
    std::string sTitle;
    float fTitleFontWidth;
    float fTitleFontHeight;
    int iTitleBarHeight;

    // main container
    std::unique_ptr<CBaseUIContainer> container;

    // title bar + main container, for tree walkers
    std::array<CBaseUIElement *, 2> children{};

    // resizing
    vec2 vResizeLimit{0.f};
    vec2 vLastSize{0.f};

    // moving
    vec2 vMousePosBackup{0.f};
    vec2 vLastPos{0.f};

    // colors
    Color frameColor;
    Color frameBrightColor;
    Color frameDarkColor;
    Color backgroundColor;
    Color titleColor;

    enum class RESIZETYPE : uint8_t {
        UNKNOWN = 0,
        TOPLEFT = 1,
        LEFT = 2,
        BOTLEFT = 3,
        BOT = 4,
        BOTRIGHT = 5,
        RIGHT = 6,
        TOPRIGHT = 7,
        TOP = 8,
    };

    RESIZETYPE iResizeType : 4;

    bool bResizeable;

    bool bDrawFrame;
    bool bDrawBackground;
    bool bRoundedRectangle;
    bool bResizing;

    bool bDrawTitleBarLine;
    bool bMoving;
};
