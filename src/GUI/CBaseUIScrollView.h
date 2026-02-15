#pragma once
// Copyright (c) 2013, PG, All rights reserved.
#include "Color.h"

#include "CBaseUIContainer.h"
#include <memory>

class CBaseUIScrollView : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIScrollView)
   public:
    CBaseUIScrollView(f32 xPos = 0, f32 yPos = 0, f32 xSize = 0, f32 ySize = 0, const UString &name = {});
    ~CBaseUIScrollView() override;

    void invalidate();
    void freeElements();

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void onKeyUp(KeyboardEvent &e) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    // scrolling
    void scrollY(int delta, bool animated = true);
    void scrollX(int delta, bool animated = true);
    void scrollToY(int scrollPosY, bool animated = true);
    void scrollToX(int scrollPosX, bool animated = true);
    void scrollToElement(CBaseUIElement *element, int xOffset = 0, int yOffset = 0, bool animated = true);

    void scrollToLeft();
    void scrollToRight();
    void scrollToBottom();
    void scrollToTop();

    // set
    CBaseUIScrollView *setDrawBackground(bool drawBackground) {
        this->bDrawBackground = drawBackground;
        return this;
    }
    CBaseUIScrollView *setDrawFrame(bool drawFrame) {
        this->bDrawFrame = drawFrame;
        return this;
    }
    CBaseUIScrollView *setDrawScrollbars(bool drawScrollbars) {
        this->bDrawScrollbars = drawScrollbars;
        return this;
    }

    CBaseUIScrollView *setBackgroundColor(Color backgroundColor) {
        this->backgroundColor = backgroundColor;
        return this;
    }
    CBaseUIScrollView *setFrameColor(Color frameColor) {
        this->frameColor = frameColor;
        return this;
    }
    CBaseUIScrollView *setFrameBrightColor(Color frameBrightColor) {
        this->frameBrightColor = frameBrightColor;
        return this;
    }
    CBaseUIScrollView *setFrameDarkColor(Color frameDarkColor) {
        this->frameDarkColor = frameDarkColor;
        return this;
    }
    CBaseUIScrollView *setScrollbarColor(Color scrollbarColor) {
        this->scrollbarColor = scrollbarColor;
        return this;
    }

    CBaseUIScrollView *setHorizontalScrolling(bool horizontalScrolling) {
        this->bHorizontalScrolling = horizontalScrolling;
        return this;
    }

    CBaseUIScrollView *setHorizontalClipping(bool horizontalClipping) {
        this->bHorizontalClipping = horizontalClipping;
        return this;
    }

    CBaseUIScrollView *setVerticalScrolling(bool verticalScrolling) {
        this->bVerticalScrolling = verticalScrolling;
        return this;
    }

    CBaseUIScrollView *setVerticalClipping(bool verticalClipping) {
        this->bVerticalClipping = verticalClipping;
        return this;
    }

    CBaseUIScrollView *setScrollSizeToContent(int border = 5);
    CBaseUIScrollView *setScrollResistance(int scrollResistanceInPixels) {
        this->iScrollResistance = scrollResistanceInPixels;
        return this;
    }

    CBaseUIScrollView *setBlockScrolling(bool block) {
        this->bBlockScrolling = block;
        return this;
    }  // means: disable scrolling, not scrolling in 'blocks'

    CBaseUIScrollView *setScrollbarOnLeft(bool onLeft) {
        this->bScrollbarOnLeft = onLeft;
        return this;
    }

    CBaseUIScrollView *setAutoscroll(bool autoscroll) {
        this->sticky = autoscroll;
        return this;
    }

    void setScrollMouseWheelMultiplier(f32 scrollMouseWheelMultiplier) {
        this->fScrollMouseWheelMultiplier = scrollMouseWheelMultiplier;
    }
    void setScrollbarSizeMultiplier(f32 scrollbarSizeMultiplier) {
        this->fScrollbarSizeMultiplier = scrollbarSizeMultiplier;
    }

    // get
    [[nodiscard]] inline f64 getRelPosY() const { return this->vScrollPos.y; }
    [[nodiscard]] inline f64 getRelPosX() const { return this->vScrollPos.x; }
    [[nodiscard]] inline dvec2 getScrollSize() const { return this->vScrollSize; }
    [[nodiscard]] inline dvec2 getVelocity() const { return (this->vScrollPos - this->vVelocity); }

    [[nodiscard]] inline bool isAtBottom() const {
        return (this->getSize().y - this->vScrollPos.y) >= this->vScrollSize.y;
    }
    [[nodiscard]] inline bool isScrolling() const { return this->bScrolling; }
    bool isBusy() override;

    // events
    void onResized() override;
    void onMouseDownOutside(bool left = true, bool right = false) override;
    void onMouseDownInside(bool left = true, bool right = false) override;
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseUpOutside(bool left = true, bool right = false) override;

    void onFocusStolen() override;
    void onEnabled() override;
    void onDisabled() override;

    // main container
    using DrawOrderComparator = bool (*)(const CBaseUIElement *a, const CBaseUIElement *b);
    class CBaseUIScrollViewContainer : public CBaseUIContainer {
        NOCOPY_NOMOVE(CBaseUIScrollViewContainer)
       public:
        using CBaseUIContainer::CBaseUIContainer;
        CBaseUIScrollViewContainer(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, UString name = {});
        CBaseUIScrollViewContainer() = delete;
        ~CBaseUIScrollViewContainer() override;

        CBaseUIContainer *removeBaseUIElement(CBaseUIElement *element);
        CBaseUIContainer *deleteBaseUIElement(CBaseUIElement *element);

        void freeElements() override;
        void invalidate() override;

        void update(CBaseUIEventCtx &c) override;
        void draw() override;

        bool isBusy() override;
        bool isActive() override;

        inline void setDrawOrderComparatorFunc(DrawOrderComparator comparator) { this->drawOrderCmp = comparator; }

       private:
        friend class CBaseUIScrollView;

        // default is just by random insertion order
        DrawOrderComparator drawOrderCmp{nullptr};

        // these elements must correspond to items in the superclass' vElements container!
        // this is kind of a hack to avoid iterating over a bunch of not-visible elements
        std::vector<CBaseUIElement *> vVisibleElements;

        // we need to break out of certain iteration loops (e.g. update()) if the container we're iterating through has been cleared
        bool invalidateUpdate{false};
    };

    CBaseUIScrollViewContainer container;

    // backdoor to allow broken nested scrollview shenanigans to circumvent clipping
    void forceInvalidateClipping() {
        this->bClippingDirty = true;
        this->previousClippingVisibleElements = 0;
    }

   protected:
    void onMoved() override;

    void updateClipping();
    void updateScrollbars();

    void scrollToYInt(int scrollPosY, bool animated = true, bool slow = true);
    void scrollToXInt(int scrollPosX, bool animated = true, bool slow = true);

    // vars
    Color backgroundColor{0xff000000};
    Color frameColor{0xffffffff};
    Color frameBrightColor{0};
    Color frameDarkColor{0};
    Color scrollbarColor{0xaaffffff};

    dvec2 vScrollPos{1., 1.};
    dvec2 vScrollPosBackup{0.};
    dvec2 vMouseBackup{0.};

    f32 fScrollMouseWheelMultiplier{1.f};
    f32 fScrollbarSizeMultiplier{1.f};
    McRect verticalScrollbar;
    McRect horizontalScrollbar;

    // scroll logic
    dvec2 vScrollSize{1., 1.};
    dvec2 vMouseBackup2{0.};
    dvec2 vMouseBackup3{0.};
    dvec2 vVelocity{0., 0.};
    dvec2 vKineticAverage{0.};

    uSz previousClippingVisibleElements{0};
    uSz previousClippingTotalElements{0};

    int iPrevScrollDeltaX{0};
    int iScrollResistance;

    bool bAutoScrollingX{false};
    bool bAutoScrollingY{false};

    bool bScrollResistanceCheck{false};
    bool bScrolling{false};
    bool bScrollbarScrolling{false};
    bool bScrollbarIsVerticalScrolling{false};
    bool bBlockScrolling{false};
    bool bHorizontalScrolling{false};
    bool bVerticalScrolling{true};
    bool bFirstScrollSizeToContent{true};

    // vars
    bool bDrawFrame{true};
    bool bDrawBackground{true};
    bool bDrawScrollbars{true};

    // When you scrolled to the bottom, and new content is added, setting this
    // to true makes it so you'll stay at the bottom.
    // Useful in places where you're waiting on new content, like chat logs.
    bool sticky{false};

    bool bHorizontalClipping{true};
    bool bVerticalClipping{true};
    bool bScrollbarOnLeft{false};

    bool bClippingDirty{true};  // start true for initial update
};
