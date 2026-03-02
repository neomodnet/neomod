#pragma once
// Copyright (c) 2013, PG, All rights reserved.

#include "KeyboardListener.h"
#include "Vectors.h"
#include "Rect.h"
#include "UString.h"

#include <utility>
#include <memory>

// convar callback to avoid hammering atomic convar reads
namespace CBaseUIDebug {
void onDumpElemsChangeCallback(float newvalue);
}  // namespace CBaseUIDebug

// Guidelines for avoiding hair pulling:
// - Don't use m_vmSize
// - When an element is standalone, use getPos/setPos
// - In a container or in a scrollview, use getRelPos/setRelPos and call update_pos() on the container

enum class TEXT_JUSTIFICATION : uint8_t { LEFT, CENTERED, RIGHT };

class CBaseUIContainer;

struct CBaseUIEventCtx {
    bool propagate_clicks{true};
    bool propagate_hover{true};  // TODO: does not work quite right yet

    void consume_mouse() { this->propagate_clicks = this->propagate_hover = false; }

    [[nodiscard]] bool mouse_consumed() const {
        return this->propagate_clicks == this->propagate_hover && (this->propagate_hover == false);
    }
};

class CBaseUIElement : public KeyboardListener {
    NOCOPY_NOMOVE(CBaseUIElement)
   public:
    static inline const UString emptyUString{US_("")};
    CBaseUIElement(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::nullptr_t = {})
        : rect(xPos, yPos, xSize, ySize), relRect(this->rect) {}

    CBaseUIElement(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, UString name = {})
        : sName(likely(name.isEmpty()) ? nullptr : std::make_unique<UString>(std::move(name))),
          rect(xPos, yPos, xSize, ySize),
          relRect(this->rect) {}
    ~CBaseUIElement() override = default;

    // main
    virtual void draw() = 0;
    virtual void update(CBaseUIEventCtx &c);

    // keyboard input
    void onKeyUp(KeyboardEvent &e) override { (void)e; }
    void onKeyDown(KeyboardEvent &e) override { (void)e; }
    void onChar(KeyboardEvent &e) override { (void)e; }

    // getters
    [[nodiscard]] constexpr const UString &getName() const { return this->sName ? *this->sName : emptyUString; }

    [[nodiscard]] constexpr const McRect &getRect() const { return this->rect; }

    [[nodiscard]] constexpr const vec2 &getPos() const { return this->rect.getPos(); }
    [[nodiscard]] constexpr const vec2 &getSize() const { return this->rect.getSize(); }

    [[nodiscard]] constexpr const McRect &getRelRect() const { return this->relRect; }

    [[nodiscard]] constexpr const vec2 &getRelPos() const { return this->relRect.getPos(); }
    [[nodiscard]] constexpr const vec2 &getRelSize() const { return this->relRect.getSize(); }

    virtual bool isActive() { return this->bActive || this->isBusy(); }
    virtual bool isVisible() { return this->bVisible; }

    // engine rectangle contains rect
    static bool isVisibleOnScreen(const McRect &rect);
    [[nodiscard]] static forceinline bool isVisibleOnScreen(CBaseUIElement *elem) {
        return CBaseUIElement::isVisibleOnScreen(elem->getRect());
    }

    [[nodiscard]] forceinline bool isVisibleOnScreen() const {
        return CBaseUIElement::isVisibleOnScreen(this->getRect());
    }

    virtual bool isEnabled() { return this->bEnabled; }
    virtual bool isBusy() { return this->bBusy && this->isVisible(); }
    virtual bool isMouseInside() { return this->bMouseInside && this->isVisible(); }

    inline CBaseUIElement *setPos(vec2 newPos) {
        if(newPos != this->rect.getPos()) {
            this->rect.setPos(newPos);
            this->onMoved();
        }
        return this;
    }
    inline CBaseUIElement *setPos(float xPos, float yPos) { return this->setPos({xPos, yPos}); }
    inline CBaseUIElement *setPosX(float xPos) { return this->setPos({xPos, this->rect.getPos().y}); }
    inline CBaseUIElement *setPosY(float yPos) { return this->setPos({this->rect.getPos().x, yPos}); }
    inline CBaseUIElement *setRelPos(vec2 newRelPos) {
        this->relRect.setPos(newRelPos);
        return this;
    }
    inline CBaseUIElement *setRelPos(float xPos, float yPos) { return this->setRelPos({xPos, yPos}); }
    inline CBaseUIElement *setRelPosX(float xPos) {
        this->relRect.setPosX(xPos);
        return this;
    }
    inline CBaseUIElement *setRelPosY(float yPos) {
        this->relRect.setPosY(yPos);
        return this;
    }

    inline CBaseUIElement *setSize(vec2 newSize) {
        if(newSize != this->rect.getSize()) {
            this->rect.setSize(newSize);
            this->onResized();
            this->onMoved();
        }
        return this;
    }
    inline CBaseUIElement *setSize(float xSize, float ySize) { return this->setSize({xSize, ySize}); }
    inline CBaseUIElement *setSizeX(float xSize) { return this->setSize({xSize, this->rect.getSize().y}); }
    inline CBaseUIElement *setSizeY(float ySize) { return this->setSize({this->rect.getSize().x, ySize}); }

    inline CBaseUIElement *setRect(McRect rect) {
        this->rect = rect;
        return this;
    }

    inline CBaseUIElement *setRelRect(McRect rect) {
        this->relRect = rect;
        return this;
    }

    virtual CBaseUIElement *setVisible(bool visible) {
        this->bVisible = visible;
        return this;
    }
    virtual CBaseUIElement *setActive(bool active) {
        this->bActive = active;
        return this;
    }
    virtual CBaseUIElement *setKeepActive(bool keepActive) {
        this->bKeepActive = keepActive;
        return this;
    }
    virtual CBaseUIElement *setEnabled(bool enabled) {
        if(enabled != this->bEnabled) {
            this->bEnabled = enabled;
            if(this->bEnabled) {
                this->onEnabled();
            } else {
                this->onDisabled();
            }
        }
        return this;
    }
    virtual CBaseUIElement *setBusy(bool busy) {
        this->bBusy = busy;
        return this;
    }
    virtual CBaseUIElement *setName(UString name) {
        if(!name.isEmpty()) {
            if(this->sName) {
                *this->sName = std::move(name);
            } else {
                this->sName = std::make_unique<UString>(std::move(name));
            }
        } else {
            this->sName.reset();
        }
        return this;
    }
    virtual CBaseUIElement *setHandleLeftMouse(bool handle) {
        this->bHandleLeftMouse = handle;
        return this;
    }
    virtual CBaseUIElement *setHandleRightMouse(bool handle) {
        this->bHandleRightMouse = handle;
        return this;
    }

    // TODO: remove this, changes behavior in more ways than just mouse handling
    virtual CBaseUIElement *setGrabClicks(bool grabClicks) {
        this->grabs_clicks = grabClicks;
        return this;
    }

    // actions
    virtual void stealFocus();

    void dumpElem() const;  // debug
   protected:
    friend class CBaseUIContainer;

    // events
    virtual void onResized() { ; }
    virtual void onMoved() { ; }

    virtual void onFocusStolen() { ; }
    virtual void onEnabled() { ; }
    virtual void onDisabled() { ; }

    virtual void onMouseInside() { ; }
    virtual void onMouseOutside() { ; }
    virtual void onMouseDownInside(bool /*left*/ = true, bool /*right*/ = false) { ; }
    virtual void onMouseDownOutside(bool /*left*/ = true, bool /*right*/ = false) { ; }
    virtual void onMouseUpInside(bool /*left*/ = true, bool /*right*/ = false) { ; }
    virtual void onMouseUpOutside(bool /*left*/ = true, bool /*right*/ = false) { ; }

    // vars
    std::unique_ptr<UString> sName{nullptr};  // not worth storing a full name for each element when it's usually empty

    // position and size
    McRect rect;
    McRect relRect;

    // vec2 &vPos;    // reference to rect.vMin
    // vec2 &vSize;   // reference to rect.vSize
    // vec2 &vmPos;   // reference to relRect.vMin
    // vec2 &vmSize;  // reference to relRect.vSize

    // attributes

   private:
    uint8_t mouseInsideCheck : 2{0};
    uint8_t mouseUpCheck : 2{0};

   protected:
    bool grabs_clicks : 1{false};  // TODO: remove this (confusing behavior)
    bool bVisible : 1{true};
    bool bActive : 1{false};  // we are doing something, e.g. textbox is blinking and ready to receive input
    bool bBusy : 1{false};    // we demand the focus to be kept on us, e.g. click-drag scrolling in a scrollview
    bool bEnabled : 1{true};

    bool bKeepActive : 1{false};  // once clicked, don't lose m_bActive, we have to manually release it (e.g. textbox)
    bool bMouseInside : 1{false};

    bool bHandleLeftMouse : 1{true};
    bool bHandleRightMouse : 1{false};
};
