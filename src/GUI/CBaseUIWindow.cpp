// Copyright (c) 2014, PG, All rights reserved.
#include "CBaseUIWindow.h"

#include <algorithm>
#include <utility>

#include "CBaseUIButton.h"
#include "CBaseUIContainer.h"
#include "Cursors.h"
#include "Engine.h"
#include "Environment.h"
#include "Mouse.h"
#include "ResourceManager.h"
#include "Font.h"
#include "MakeDelegateWrapper.h"
#include "Graphics.h"

CBaseUIWindow::CBaseUIWindow(float xPos, float yPos, float xSize, float ySize, std::string name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, std::move(name)) {
    const float dpiScale = env->getDPIScale();

    int titleBarButtonSize = 13 * dpiScale;

    // titlebar
    this->bDrawTitleBarLine = true;
    this->titleFont = resourceManager->loadFont("weblysleekuisb", "FONT_WINDOW_TITLE", 13.0f, true, env->getDPI());
    this->iTitleBarHeight = this->titleFont->getHeight() + 12 * dpiScale;
    if(this->iTitleBarHeight < titleBarButtonSize) this->iTitleBarHeight = titleBarButtonSize + 4 * dpiScale;

    this->titleBarContainer = std::make_unique<CBaseUIContainer>(this->getPos().x, this->getPos().y, this->getSize().x,
                                                                 this->iTitleBarHeight, "titlebarcontainer");

    this->closeButton = new CBaseUIButton(
        this->getSize().x - titleBarButtonSize - (this->iTitleBarHeight - titleBarButtonSize) / 2.0f,
        this->iTitleBarHeight / 2.0f - titleBarButtonSize / 2.0f, titleBarButtonSize, titleBarButtonSize, "", "");
    this->closeButton->setClickCallback(SA::MakeDelegate<&CBaseUIWindow::close>(this));
    this->closeButton->setDrawFrame(false);

    this->titleBarContainer->addBaseUIElement(this->closeButton);

    // main container
    this->container = std::make_unique<CBaseUIContainer>(
        this->getPos().x, this->getPos().y + this->titleBarContainer->getSize().y, this->getSize().x,
        this->getSize().y - this->titleBarContainer->getSize().y, "maincontainer");
    this->children = {this->titleBarContainer.get(), this->container.get()};

    // colors
    this->frameColor = 0xffffffff;
    this->backgroundColor = 0xff000000;
    this->frameBrightColor = 0;
    this->frameDarkColor = 0;
    this->titleColor = 0xffffffff;

    // events
    this->vResizeLimit = vec2(100, 90) * dpiScale;
    this->bMoving = false;
    this->bResizing = false;
    this->iResizeType = RESIZETYPE::UNKNOWN;

    // window properties
    this->bResizeable = true;

    this->bDrawFrame = true;
    this->bDrawBackground = true;
    this->bRoundedRectangle = false;

    this->setTitle(this->sName);
    this->setVisible(false);

    // for very small resolutions on engine start
    if(this->getPos().y + this->getSize().y > engine->getScreenHeight()) {
        this->setSizeY(engine->getScreenHeight() - 12 * dpiScale);
    }
}

CBaseUIWindow::~CBaseUIWindow() = default;

void CBaseUIWindow::draw() {
    if(!this->bVisible) return;

    // draw background
    if(this->bDrawBackground) {
        g->setColor(this->backgroundColor);

        if(this->bRoundedRectangle) {
            g->fillRoundedRect(this->getPos(), this->getSize() + 1.f, 6);
        } else
            g->fillRect(this->getPos(), this->getSize() + 1.f);
    }

    // draw frame
    if(this->bDrawFrame) {
        if(this->frameDarkColor != 0 || this->frameBrightColor != 0)
            g->drawRect(this->getPos(), this->getSize(), this->frameDarkColor, this->frameBrightColor,
                        this->frameBrightColor, this->frameDarkColor);
        else {
            g->setColor(this->frameColor);
            g->drawRect(this->getPos(), this->getSize());
        }
    }

    // draw window contents
    g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + 2, this->getSize().x - 1, this->getSize().y - 1));
    {
        // draw main container (clipped to below the title bar: the background is filled once, above, so that it
        // can be translucent)
        g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + this->iTitleBarHeight, this->getSize().x - 1,
                               this->getSize().y - this->iTitleBarHeight));
        {
            this->container->draw();
            this->drawCustomContent();
        }
        g->popClipRect();

        // draw title bar line
        if(this->bDrawTitleBarLine) {
            g->setColor(this->frameColor);
            g->drawLine(this->getPos().x, this->getPos().y + this->iTitleBarHeight,
                        this->getPos().x + this->getSize().x, this->getPos().y + this->iTitleBarHeight);
        }

        // draw title
        g->setColor(this->titleColor);
        g->pushTransform();
        {
            g->translate((int)(this->getPos().x + this->getSize().x / 2.0f - this->fTitleFontWidth / 2.0f),
                         (int)(this->getPos().y + this->fTitleFontHeight / 2.0f + this->iTitleBarHeight / 2.0f));
            g->drawString(this->titleFont, this->sTitle);
        }
        g->popTransform();

        // draw title bar container
        g->pushClipRect(
            McRect(this->getPos().x + 1, this->getPos().y + 2, this->getSize().x - 1, this->iTitleBarHeight));
        {
            this->titleBarContainer->draw();
        }
        g->popClipRect();

        // draw close button 'x'
        g->setColor(this->closeButton->getFrameColor());
        g->drawLine(this->closeButton->getPos().x + 1, this->closeButton->getPos().y + 1,
                    this->closeButton->getPos().x + this->closeButton->getSize().x,
                    this->closeButton->getPos().y + this->closeButton->getSize().y);
        g->drawLine(this->closeButton->getPos().x + 1,
                    this->closeButton->getPos().y + this->closeButton->getSize().y - 1,
                    this->closeButton->getPos().x + this->closeButton->getSize().x, this->closeButton->getPos().y);
    }
    g->popClipRect();
}

void CBaseUIWindow::tick() {
    CBaseUIElement::tick();
    this->titleBarContainer->tick();
    this->container->tick();
}

void CBaseUIWindow::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUIElement::updateInput(c);

    // window logic comes first
    if(!this->titleBarContainer->isBusy() && !this->container->isBusy() && this->bEnabled && this->isMouseInside())
        this->updateWindowLogic();

    // the main two containers
    {
        CBaseUIEventCtx::HitPathScope scope(c, this);
        this->titleBarContainer->updateInput(c);
        this->container->updateInput(c);
    }
}

void CBaseUIWindow::onCapturedMouseMove() {
    if(!this->bActive) return;

    // moving
    if(this->bMoving) this->setPos(this->vLastPos + (mouse->getPos() - this->vMousePosBackup));

    // resizing
    if(this->bResizing) {
        switch(this->iResizeType) {
            case RESIZETYPE::UNKNOWN:
                break;
            case RESIZETYPE::TOPLEFT:
                this->setPos(
                    std::clamp<float>(this->vLastPos.x + (mouse->getPos().x - this->vMousePosBackup.x),
                                      -this->getSize().x, this->vLastPos.x + this->vLastSize.x - this->vResizeLimit.x),
                    std::clamp<float>(this->vLastPos.y + (mouse->getPos().y - this->vMousePosBackup.y),
                                      -this->getSize().y, this->vLastPos.y + this->vLastSize.y - this->vResizeLimit.y));
                this->setSize(std::clamp<float>(this->vLastSize.x + (this->vMousePosBackup.x - mouse->getPos().x),
                                                this->vResizeLimit.x, engine->getScreenWidth()),
                              std::clamp<float>(this->vLastSize.y + (this->vMousePosBackup.y - mouse->getPos().y),
                                                this->vResizeLimit.y, engine->getScreenHeight()));
                break;

            case RESIZETYPE::LEFT:
                this->setPosX(std::clamp<float>(this->vLastPos.x + (mouse->getPos().x - this->vMousePosBackup.x),
                                                -this->getSize().x,
                                                this->vLastPos.x + this->vLastSize.x - this->vResizeLimit.x));
                this->setSizeX(std::clamp<float>(this->vLastSize.x + (this->vMousePosBackup.x - mouse->getPos().x),
                                                 this->vResizeLimit.x, engine->getScreenWidth()));
                break;

            case RESIZETYPE::BOTLEFT:
                this->setPosX(std::clamp<float>(this->vLastPos.x + (mouse->getPos().x - this->vMousePosBackup.x),
                                                -this->getSize().x,
                                                this->vLastPos.x + this->vLastSize.x - this->vResizeLimit.x));
                this->setSizeX(std::clamp<float>(this->vLastSize.x + (this->vMousePosBackup.x - mouse->getPos().x),
                                                 this->vResizeLimit.x, engine->getScreenWidth()));
                this->setSizeY(std::clamp<float>(this->vLastSize.y + (mouse->getPos().y - this->vMousePosBackup.y),
                                                 this->vResizeLimit.y, engine->getScreenHeight()));
                break;

            case RESIZETYPE::BOT:
                this->setSizeY(std::clamp<float>(this->vLastSize.y + (mouse->getPos().y - this->vMousePosBackup.y),
                                                 this->vResizeLimit.y, engine->getScreenHeight()));
                break;

            case RESIZETYPE::BOTRIGHT:
                this->setSize(std::clamp<float>(this->vLastSize.x + (mouse->getPos().x - this->vMousePosBackup.x),
                                                this->vResizeLimit.x, engine->getScreenWidth()),
                              std::clamp<float>(this->vLastSize.y + (mouse->getPos().y - this->vMousePosBackup.y),
                                                this->vResizeLimit.y, engine->getScreenHeight()));
                break;

            case RESIZETYPE::RIGHT:
                this->setSizeX(std::clamp<float>(this->vLastSize.x + (mouse->getPos().x - this->vMousePosBackup.x),
                                                 this->vResizeLimit.x, engine->getScreenWidth()));
                break;

            case RESIZETYPE::TOPRIGHT:
                this->setPosY(std::clamp<float>(this->vLastPos.y + (mouse->getPos().y - this->vMousePosBackup.y),
                                                -this->getSize().y,
                                                this->vLastPos.y + this->vLastSize.y - this->vResizeLimit.y));
                this->setSizeY(std::clamp<float>(this->vLastSize.y + (this->vMousePosBackup.y - mouse->getPos().y),
                                                 this->vResizeLimit.y, engine->getScreenHeight()));
                this->setSizeX(std::clamp<float>(this->vLastSize.x + (mouse->getPos().x - this->vMousePosBackup.x),
                                                 this->vResizeLimit.x, engine->getScreenWidth()));
                break;

            case RESIZETYPE::TOP:
                this->setPosY(std::clamp<float>(this->vLastPos.y + (mouse->getPos().y - this->vMousePosBackup.y),
                                                -this->getSize().y,
                                                this->vLastPos.y + this->vLastSize.y - this->vResizeLimit.y));
                this->setSizeY(std::clamp<float>(this->vLastSize.y + (this->vMousePosBackup.y - mouse->getPos().y),
                                                 this->vResizeLimit.y, engine->getScreenHeight()));
                break;
        }
    }
}

void CBaseUIWindow::onKeyDown(KeyboardEvent &e) {
    if(!this->bVisible) return;
    this->container->onKeyDown(e);
}

void CBaseUIWindow::onKeyUp(KeyboardEvent &e) {
    if(!this->bVisible) return;
    this->container->onKeyUp(e);
}

void CBaseUIWindow::onChar(KeyboardEvent &e) {
    if(!this->bVisible) return;
    this->container->onChar(e);
}

CBaseUIWindow *CBaseUIWindow::setTitle(std::string text) {
    this->sTitle = std::move(text);
    this->updateTitleBarMetrics();
    return this;
}

void CBaseUIWindow::updateWindowLogic() {
    // handle resize & move cursor
    if(!this->titleBarContainer->isBusy() && !this->container->isBusy() && !this->bResizing && !this->bMoving) {
        if(!mouse->isLeftDown()) this->updateResizeAndMoveLogic(false);
    }
}

void CBaseUIWindow::updateResizeAndMoveLogic(bool captureMouse) {
    // backup
    this->vLastSize = this->getSize();
    this->vMousePosBackup = mouse->getPos();
    this->vLastPos = this->getPos();

    if(this->bResizeable) {
        // reset
        this->iResizeType = RESIZETYPE::UNKNOWN;

        int resizeHandleSize = 5;
        McRect resizeTopLeft = McRect(this->getPos().x, this->getPos().y, resizeHandleSize, resizeHandleSize);
        McRect resizeLeft = McRect(this->getPos().x, this->getPos().y, resizeHandleSize, this->getSize().y);
        McRect resizeBottomLeft = McRect(this->getPos().x, this->getPos().y + this->getSize().y - resizeHandleSize,
                                         resizeHandleSize, resizeHandleSize);
        McRect resizeBottom = McRect(this->getPos().x, this->getPos().y + this->getSize().y - resizeHandleSize,
                                     this->getSize().x, resizeHandleSize);
        McRect resizeBottomRight =
            McRect(this->getPos().x + this->getSize().x - resizeHandleSize,
                   this->getPos().y + this->getSize().y - resizeHandleSize, resizeHandleSize, resizeHandleSize);
        McRect resizeRight = McRect(this->getPos().x + this->getSize().x - resizeHandleSize, this->getPos().y,
                                    resizeHandleSize, this->getSize().y);
        McRect resizeTopRight = McRect(this->getPos().x + this->getSize().x - resizeHandleSize, this->getPos().y,
                                       resizeHandleSize, resizeHandleSize);
        McRect resizeTop = McRect(this->getPos().x, this->getPos().y, this->getSize().x, resizeHandleSize);

        if(resizeTopLeft.contains(this->vMousePosBackup)) {
            if(captureMouse) this->iResizeType = RESIZETYPE::TOPLEFT;

            env->setCursor(CURSORTYPE::CURSOR_SIZE_VH);
        } else if(resizeBottomLeft.contains(this->vMousePosBackup)) {
            if(captureMouse) this->iResizeType = RESIZETYPE::BOTLEFT;

            env->setCursor(CURSORTYPE::CURSOR_SIZE_HV);
        } else if(resizeBottomRight.contains(this->vMousePosBackup)) {
            if(captureMouse) this->iResizeType = RESIZETYPE::BOTRIGHT;

            env->setCursor(CURSORTYPE::CURSOR_SIZE_VH);
        } else if(resizeTopRight.contains(this->vMousePosBackup)) {
            if(captureMouse) this->iResizeType = RESIZETYPE::TOPRIGHT;

            env->setCursor(CURSORTYPE::CURSOR_SIZE_HV);
        } else if(resizeLeft.contains(this->vMousePosBackup)) {
            if(captureMouse) this->iResizeType = RESIZETYPE::LEFT;

            env->setCursor(CURSORTYPE::CURSOR_SIZE_H);
        } else if(resizeRight.contains(this->vMousePosBackup)) {
            if(captureMouse) this->iResizeType = RESIZETYPE::RIGHT;

            env->setCursor(CURSORTYPE::CURSOR_SIZE_H);
        } else if(resizeBottom.contains(this->vMousePosBackup)) {
            if(captureMouse) this->iResizeType = RESIZETYPE::BOT;

            env->setCursor(CURSORTYPE::CURSOR_SIZE_V);
        } else if(resizeTop.contains(this->vMousePosBackup)) {
            if(captureMouse) this->iResizeType = RESIZETYPE::TOP;

            env->setCursor(CURSORTYPE::CURSOR_SIZE_V);
        } else if(const CURSORTYPE cursor = env->getCursor();
                  cursor >= CURSORTYPE::CURSOR_SIZE_H && cursor <= CURSORTYPE::CURSOR_SIZE_VH) {
            // back over the body: the resize cursor from the handles must not stick (children set
            // their own, e.g. the textbox's text cursor, after this probe)
            env->setCursor(CURSORTYPE::CURSOR_NORMAL);
        }
    }

    // handle resizing
    if(this->iResizeType > RESIZETYPE::UNKNOWN)
        this->bResizing = true;
    else if(captureMouse) {
        // handle moving
        McRect titleBarGrab = McRect(this->getPos().x, this->getPos().y, this->getSize().x, this->iTitleBarHeight);
        if(titleBarGrab.contains(this->vMousePosBackup)) this->bMoving = true;
    }
}

void CBaseUIWindow::close() {
    if(!this->bVisible) return;

    this->setVisible(false);
    this->onClosed();
}

void CBaseUIWindow::open() {
    if(this->bVisible) return;

    this->setVisible(true);
}

CBaseUIWindow *CBaseUIWindow::setSizeToContent(int horizontalBorderSize, int verticalBorderSize) {
    const std::vector<CBaseUIElement *> &elements = this->container->getElements();
    if(elements.size() < 1) return this;

    vec2 newSize = vec2(horizontalBorderSize, verticalBorderSize);

    for(auto el : elements) {
        int xReach = el->getRelPos().x + el->getSize().x + horizontalBorderSize;
        int yReach = el->getRelPos().y + el->getSize().y + verticalBorderSize;
        if(xReach > newSize.x) newSize.x = xReach;
        if(yReach > newSize.y) newSize.y = yReach;
    }
    newSize.y = newSize.y + this->titleBarContainer->getSize().y;

    this->setSize(newSize);

    return this;
}

void CBaseUIWindow::onMouseDownInside(bool /*left*/, bool /*right*/) {
    this->bBusy = true;
    this->updateResizeAndMoveLogic(true);
    // a started move/resize follows the cursor via captured moves and may leave the rect freely
    if(this->bResizing || this->bMoving) this->lockCapture();
}

void CBaseUIWindow::onMouseUpInside(bool /*left*/, bool /*right*/) {
    this->bBusy = false;
    this->bResizing = false;
    this->bMoving = false;
}

void CBaseUIWindow::onMouseUpOutside(bool /*left*/, bool /*right*/) {
    this->bBusy = false;
    this->bResizing = false;
    this->bMoving = false;
}

void CBaseUIWindow::onMouseCancel() {
    this->bBusy = false;
    this->bResizing = false;
    this->bMoving = false;
}

void CBaseUIWindow::onMouseOutside() {
    CBaseUIElement::onMouseOutside();
    // a resize cursor from the frame handles must not follow the pointer out of the window
    if(!this->bResizing) env->setCursor(CURSORTYPE::CURSOR_NORMAL);
}

bool CBaseUIWindow::onWheel(int /*deltaVertical*/, int /*deltaHorizontal*/) {
    // a window is an opaque surface: a wheel over its frame must not scroll whatever lies beneath
    // (scrollable children are visited after us, so they still get first refusal)
    return true;
}

void CBaseUIWindow::updateTitleBarMetrics() {
    this->closeButton->setRelPos(this->getSize().x - this->closeButton->getSize().x -
                                     (this->iTitleBarHeight - this->closeButton->getSize().x) / 2.0f,
                                 this->iTitleBarHeight / 2.0f - this->closeButton->getSize().y / 2.0f);

    this->fTitleFontWidth = this->titleFont->getStringWidth(this->sTitle);
    this->fTitleFontHeight = this->titleFont->getHeight();
    this->titleBarContainer->setSize(this->getSize().x, this->iTitleBarHeight);
}

void CBaseUIWindow::onMoved() {
    this->titleBarContainer->setPos(this->getPos());
    this->container->setPos(this->getPos().x, this->getPos().y + this->titleBarContainer->getSize().y);

    this->updateTitleBarMetrics();
}

void CBaseUIWindow::onResized() {
    this->updateTitleBarMetrics();

    this->container->setSize(this->getSize().x, this->getSize().y - this->titleBarContainer->getSize().y);
}

void CBaseUIWindow::onResolutionChange(vec2 newResolution) {
    // keep the window on screen
    this->setSize(std::min(this->getSize().x, newResolution.x), std::min(this->getSize().y, newResolution.y));
    this->setPos(std::clamp(this->getPos().x, 0.f, newResolution.x - this->getSize().x),
                 std::clamp(this->getPos().y, 0.f, newResolution.y - this->getSize().y));
}

void CBaseUIWindow::onEnabled() {
    this->container->setEnabled(true);
    this->titleBarContainer->setEnabled(true);
}

void CBaseUIWindow::onDisabled() {
    this->bBusy = false;
    this->container->setEnabled(false);
    this->titleBarContainer->setEnabled(false);
}

bool CBaseUIWindow::isBusy() {
    return (this->bBusy || this->titleBarContainer->isBusy() || this->container->isBusy()) && this->bVisible;
}

bool CBaseUIWindow::isActive() {
    return (this->titleBarContainer->isActive() || this->container->isActive()) && this->bVisible;
}
