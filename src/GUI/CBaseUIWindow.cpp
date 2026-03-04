// Copyright (c) 2014, PG, All rights reserved.
#include "CBaseUIWindow.h"

#include <utility>

#include "AnimationHandler.h"
#include "CBaseUIButton.h"
#include "CBaseUIContainer.h"
#include "ConVar.h"
#include "Cursors.h"
#include "Engine.h"
#include "Environment.h"
#include "Mouse.h"
#include "RenderTarget.h"
#include "ResourceManager.h"
#include "Font.h"
#include "MakeDelegateWrapper.h"

CBaseUIWindow::CBaseUIWindow(float xPos, float yPos, float xSize, float ySize, const UString &name)
    : CBaseUIElement(xPos, yPos, xSize, ySize, name) {
    const float dpiScale = env->getDPIScale();

    int titleBarButtonSize = 13 * dpiScale;
    int titleBarButtonGap = 6 * dpiScale;

    // titlebar
    this->bDrawTitleBarLine = true;
    this->titleFont = resourceManager->loadFont("weblysleekuisb.ttf", "FONT_WINDOW_TITLE", 13.0f, true, env->getDPI());
    this->iTitleBarHeight = this->titleFont->getHeight() + 12 * dpiScale;
    if(this->iTitleBarHeight < titleBarButtonSize) this->iTitleBarHeight = titleBarButtonSize + 4 * dpiScale;

    this->titleBarContainer =
        new CBaseUIContainer(this->getPos().x, this->getPos().y, this->getSize().x, this->iTitleBarHeight, "titlebarcontainer");

    this->closeButton = new CBaseUIButton(
        this->getSize().x - titleBarButtonSize - (this->iTitleBarHeight - titleBarButtonSize) / 2.0f,
        this->iTitleBarHeight / 2.0f - titleBarButtonSize / 2.0f, titleBarButtonSize, titleBarButtonSize, "", "");
    this->closeButton->setClickCallback(SA::MakeDelegate<&CBaseUIWindow::close>(this));
    this->closeButton->setDrawFrame(false);

    this->minimizeButton = new CBaseUIButton(
        this->getSize().x - titleBarButtonSize * 2 - (this->iTitleBarHeight - titleBarButtonSize) / 2.0f -
            titleBarButtonGap,
        this->iTitleBarHeight / 2.0f - titleBarButtonSize / 2.0f, titleBarButtonSize, titleBarButtonSize, "", "");
    this->minimizeButton->setVisible(false);
    this->minimizeButton->setDrawFrame(false);
    this->minimizeButton->setClickCallback(SA::MakeDelegate<&CBaseUIWindow::minimize>(this));

    this->titleBarContainer->addBaseUIElement(this->minimizeButton);
    this->titleBarContainer->addBaseUIElement(this->closeButton);

    // main container
    this->container =
        new CBaseUIContainer(this->getPos().x, this->getPos().y + this->titleBarContainer->getSize().y, this->getSize().x,
                             this->getSize().y - this->titleBarContainer->getSize().y, "maincontainer");

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
    this->bIsOpen = false;
    this->bAnimIn = false;
    this->bResizeable = true;
    this->bCoherenceMode = false;
    this->fAnimation = 0.0f;

    this->bDrawFrame = true;
    this->bDrawBackground = true;
    this->bRoundedRectangle = false;

    this->setTitle(name);
    this->setVisible(false);

    // for very small resolutions on engine start
    if(this->getPos().y + this->getSize().y > engine->getScreenHeight()) {
        this->setSizeY(engine->getScreenHeight() - 12 * dpiScale);
    }
}

CBaseUIWindow::~CBaseUIWindow() {
    /// SAFE_DELETE(this->shadow);
    SAFE_DELETE(this->container);
    SAFE_DELETE(this->titleBarContainer);
}

void CBaseUIWindow::draw() {
    if(!this->bVisible) return;

    // TODO: structure
    /*
    if (!anim::isAnimating(&m_fAnimation))
            m_shadow->draw();
    else
    {
            m_shadow->setColor(argb((int)((this->fAnimation)*255.0f), 255, 255, 255));

            // HACKHACK: shadows can't render inside a 3DScene
            m_shadow->renderOffscreen();

            g->push3DScene(McRect(this->getPos(), this->getSize()));
                    g->rotate3DScene(0, (this->bAnimIn ? -1 : 1) * (1-m_fAnimation)*10, 0);
                    g->translate3DScene(0, 0, -(1-m_fAnimation)*100);
                    m_shadow->draw();
            g->pop3DScene();
    }
    */

    // draw window
    // if (anim::isAnimating(&m_fAnimation) && !m_bCoherenceMode)
    //	m_rt->enable();

    {
        // draw background
        if(this->bDrawBackground) {
            g->setColor(this->backgroundColor);

            if(this->bRoundedRectangle) {
                // int border = 0;
                g->fillRoundedRect(this->getPos(), this->getSize() + 1.f, 6);
            } else
                g->fillRect(this->getPos(), this->getSize() + 1.f);
        }

        // draw frame
        if(this->bDrawFrame) {
            if(this->frameDarkColor != 0 || this->frameBrightColor != 0)
                g->drawRect(this->getPos(), this->getSize(), this->frameDarkColor,
                            this->frameBrightColor, this->frameBrightColor, this->frameDarkColor);
            else {
                g->setColor(/*m_bEnabled ? 0xffffff00 : */ this->frameColor);
                g->drawRect(this->getPos(), this->getSize());
            }
        }

        // draw window contents
        g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + 2, this->getSize().x - 1, this->getSize().y - 1));
        {
            // draw main container
            g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + 2, this->getSize().x - 1, this->getSize().y - 1));
            {
                this->container->draw();
                this->drawCustomContent();
            }
            g->popClipRect();

            // draw title bar background
            if(this->bDrawBackground && !this->bRoundedRectangle) {
                g->setColor(this->backgroundColor);
                g->fillRect(this->getPos().x, this->getPos().y, this->getSize().x, this->iTitleBarHeight);
            }

            // draw title bar line
            if(this->bDrawTitleBarLine) {
                g->setColor(this->frameColor);
                g->drawLine(this->getPos().x, this->getPos().y + this->iTitleBarHeight, this->getPos().x + this->getSize().x,
                            this->getPos().y + this->iTitleBarHeight);
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
            g->pushClipRect(McRect(this->getPos().x + 1, this->getPos().y + 2, this->getSize().x - 1, this->iTitleBarHeight));
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

            // draw minimize button '_'
            if(this->minimizeButton->isVisible()) {
                g->setColor(this->minimizeButton->getFrameColor());
                g->drawLine(this->minimizeButton->getPos().x + 2,
                            this->minimizeButton->getPos().y + this->minimizeButton->getSize().y - 2,
                            this->minimizeButton->getPos().x + this->minimizeButton->getSize().x - 1,
                            this->minimizeButton->getPos().y + this->minimizeButton->getSize().y - 2);
            }
        }
        g->popClipRect();
    }

    // TODO: structure
    if(this->fAnimation.animating() && !this->bCoherenceMode) {
        /*
        m_rt->disable();


        m_rt->setColor(argb((int)(this->fAnimation*255.0f), 255, 255, 255));

        g->push3DScene(McRect(this->getPos(), this->getSize()));
                g->rotate3DScene((this->bAnimIn ? -1 : 1) * (1-m_fAnimation)*10, 0, 0);
                g->translate3DScene(0, 0, -(1-m_fAnimation)*100);
                m_rt->draw(this->getPos().x, this->getPos().y);
        g->pop3DScene();
        */
    }
}

void CBaseUIWindow::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUIElement::update(c);

    // after the close animation is finished, set invisible
    if(this->fAnimation == 0.0f && this->bVisible) this->setVisible(false);

    // window logic comes first
    if(!this->titleBarContainer->isBusy() && !this->container->isBusy() && this->bEnabled && this->isMouseInside())
        this->updateWindowLogic();

    // the main two containers
    this->titleBarContainer->update(c);
    this->container->update(c);

    // moving
    if(this->bMoving) this->setPos(this->vLastPos + (mouse->getPos() - this->vMousePosBackup));

    // resizing
    if(this->bResizing) {
        switch(this->iResizeType) {
            case RESIZETYPE::UNKNOWN:
                break;
            case RESIZETYPE::TOPLEFT:
                this->setPos(
                    std::clamp<float>(this->vLastPos.x + (mouse->getPos().x - this->vMousePosBackup.x), -this->getSize().x,
                                      this->vLastPos.x + this->vLastSize.x - this->vResizeLimit.x),
                    std::clamp<float>(this->vLastPos.y + (mouse->getPos().y - this->vMousePosBackup.y), -this->getSize().y,
                                      this->vLastPos.y + this->vLastSize.y - this->vResizeLimit.y));
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

CBaseUIWindow *CBaseUIWindow::setTitle(UString text) {
    this->sTitle = std::move(text);
    this->updateTitleBarMetrics();
    return this;
}

void CBaseUIWindow::updateWindowLogic() {
    if(!mouse->isLeftDown()) {
        this->bMoving = false;
        this->bResizing = false;
    }

    // handle resize & move cursor
    if(!this->titleBarContainer->isBusy() && !this->container->isBusy() && !this->bResizing && !this->bMoving) {
        if(!mouse->isLeftDown()) this->udpateResizeAndMoveLogic(false);
    }
}

void CBaseUIWindow::udpateResizeAndMoveLogic(bool captureMouse) {
    if(this->bCoherenceMode) return;  // NOTE: resizing in coherence mode is handled in main_Windows.cpp

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
        McRect resizeBottomLeft =
            McRect(this->getPos().x, this->getPos().y + this->getSize().y - resizeHandleSize, resizeHandleSize, resizeHandleSize);
        McRect resizeBottom =
            McRect(this->getPos().x, this->getPos().y + this->getSize().y - resizeHandleSize, this->getSize().x, resizeHandleSize);
        McRect resizeBottomRight =
            McRect(this->getPos().x + this->getSize().x - resizeHandleSize, this->getPos().y + this->getSize().y - resizeHandleSize,
                   resizeHandleSize, resizeHandleSize);
        McRect resizeRight =
            McRect(this->getPos().x + this->getSize().x - resizeHandleSize, this->getPos().y, resizeHandleSize, this->getSize().y);
        McRect resizeTopRight =
            McRect(this->getPos().x + this->getSize().x - resizeHandleSize, this->getPos().y, resizeHandleSize, resizeHandleSize);
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

    // resizing and moving have priority over window contents
    if(this->bResizing || this->bMoving) this->container->stealFocus();
}

void CBaseUIWindow::close() {
    if(this->fAnimation.animating()) return;

    this->bAnimIn = false;
    this->fAnimation = 1.0f;
    this->fAnimation.set(0.0f, cv::ui_window_animspeed.getFloat(), anim::QuadInOut);

    this->onClosed();
}

void CBaseUIWindow::open() {
    if(this->fAnimation.animating() || this->bVisible) return;

    this->setVisible(true);

    if(!this->bCoherenceMode) {
        this->bAnimIn = true;
        this->fAnimation = 0.001f;
        this->fAnimation.set(1.0f, cv::ui_window_animspeed.getFloat(), anim::QuadOut);
    } else
        this->fAnimation = 1.0f;
}

void CBaseUIWindow::minimize() {
    if(this->bCoherenceMode) env->minimizeWindow();
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

CBaseUIWindow *CBaseUIWindow::enableCoherenceMode() {
    this->bCoherenceMode = true;

    this->minimizeButton->setVisible(true);
    this->setPos(0, 0);
    this->setSize(engine->getScreenWidth() - 1, engine->getScreenHeight() - 1);
    /// env->setWindowSize(this->getSize().x+1, this->getSize().y+1);

    return this;
}

void CBaseUIWindow::onMouseDownInside(bool /*left*/, bool /*right*/) {
    this->bBusy = true;
    CBaseUIEventCtx c;
    this->titleBarContainer->update(c);  // why is this called here lol?
    if(!this->titleBarContainer->isBusy()) this->udpateResizeAndMoveLogic(true);
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

void CBaseUIWindow::updateTitleBarMetrics() {
    this->closeButton->setRelPos(this->getSize().x - this->closeButton->getSize().x -
                                     (this->iTitleBarHeight - this->closeButton->getSize().x) / 2.0f,
                                 this->iTitleBarHeight / 2.0f - this->closeButton->getSize().y / 2.0f);
    this->minimizeButton->setRelPos(this->getSize().x - this->minimizeButton->getSize().x * 2 -
                                        (this->iTitleBarHeight - this->minimizeButton->getSize().x) / 2.0f - 6,
                                    this->iTitleBarHeight / 2.0f - this->minimizeButton->getSize().y / 2.0f);

    this->fTitleFontWidth = this->titleFont->getStringWidth(this->sTitle);
    this->fTitleFontHeight = this->titleFont->getHeight();
    this->titleBarContainer->setSize(this->getSize().x, this->iTitleBarHeight);
}

void CBaseUIWindow::onMoved() {
    this->titleBarContainer->setPos(this->getPos());
    this->container->setPos(this->getPos().x, this->getPos().y + this->titleBarContainer->getSize().y);

    this->updateTitleBarMetrics();

    // if (!m_bCoherenceMode)
    //	m_rt->setPos(this->getPos());
    /// m_shadow->setPos(this->getPos().x-m_shadow->getRadius(), this->getPos().y-m_shadow->getRadius());
}

void CBaseUIWindow::onResized() {
    this->updateTitleBarMetrics();

    this->container->setSize(this->getSize().x, this->getSize().y - this->titleBarContainer->getSize().y);

    // if (!m_bCoherenceMode)
    //	m_rt->rebuild(this->getPos().x, this->getPos().y, this->getSize().x+1, this->getSize().y+1);
    /// m_shadow->setSize(this->getSize().x+m_shadow->getRadius()*2, this->getSize().y+m_shadow->getRadius()*2+4);
}

void CBaseUIWindow::onResolutionChange(vec2 newResolution) {
    if(this->bCoherenceMode) this->setSize(newResolution.x - 1, newResolution.y - 1);
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

void CBaseUIWindow::onClosed() {
    if(this->bCoherenceMode) engine->shutdown();
}

bool CBaseUIWindow::isBusy() {
    return (this->bBusy || this->titleBarContainer->isBusy() || this->container->isBusy()) && this->bVisible;
}

bool CBaseUIWindow::isActive() {
    return (this->titleBarContainer->isActive() || this->container->isActive()) && this->bVisible;
}
