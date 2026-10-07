#pragma once
// Copyright (c) 2012, PG, All rights reserved.

// TODO: fix vertical sliders
// TODO: this entire class is a mess

#include "AnimationHandler.h"
#include "CBaseUIElement.h"
#include "Color.h"
#include "Delegate.h"

class CBaseUISlider : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUISlider)
   public:
    CBaseUISlider(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {});
    ~CBaseUISlider() override = default;

    void draw() override;
    void tick() override;

    void onKeyDown(KeyboardEvent &e) override;

    void fireChangeCallback();

    CBaseUISlider *setOrientation(bool horizontal) {
        this->bHorizontal = horizontal;
        this->onResized();
        return this;
    }

    CBaseUISlider *setDrawFrame(bool drawFrame) {
        this->bDrawFrame = drawFrame;
        return this;
    }
    CBaseUISlider *setDrawBackground(bool drawBackground) {
        this->bDrawBackground = drawBackground;
        return this;
    }

    CBaseUISlider *setFrameColor(Color frameColor) {
        this->frameColor = frameColor;
        return this;
    }
    CBaseUISlider *setBackgroundColor(Color backgroundColor) {
        this->backgroundColor = backgroundColor;
        return this;
    }

    CBaseUISlider *setBlockSize(float xSize, float ySize) {
        this->vBlockSize.x = xSize;
        this->vBlockSize.y = ySize;
        return this;
    }

    CBaseUISlider *setLineOutlineSize(int size) {
        if(size >= 0) this->iLineOutlineSize = size;
        return this;
    }

    CBaseUISlider *setOutlineColor(Color outlineColor) {
        this->outlineColor = outlineColor;
        return this;
    }

    // callbacks, either void or with ourself as the argument
    using SliderChangeCallback = SA::delegate<void(CBaseUISlider *)>;
    CBaseUISlider *setChangeCallback(const SliderChangeCallback &changeCallback) {
        this->sliderChangeCallback = changeCallback;
        return this;
    }

    CBaseUISlider *setAllowMouseWheel(bool allowMouseWheel) {
        this->bAllowMouseWheel = allowMouseWheel;
        return this;
    }
    CBaseUISlider *setAnimated(bool animated) {
        this->bAnimated = animated;
        return this;
    }
    CBaseUISlider *setLiveUpdate(bool liveUpdate) {
        this->bLiveUpdate = liveUpdate;
        return this;
    }
    CBaseUISlider *setBounds(float minValue, float maxValue);
    CBaseUISlider *setKeyDelta(float keyDelta) {
        this->fKeyDelta = keyDelta;
        return this;
    }
    CBaseUISlider *setValue(float value, bool animate = true, bool call_callback = true);
    CBaseUISlider *setInitialValue(float value);

    inline float getFloat() const { return this->fCurValue; }
    inline int getInt() const { return (int)this->fCurValue; }
    inline bool getBool() const { return (bool)this->fCurValue; }
    inline float getMax() const { return this->fMaxValue; }
    inline float getMin() const { return this->fMinValue; }
    float getPercent() const;

    inline int getLineOutlineSize() const { return this->iLineOutlineSize; }
    inline Color getLineOutlineColor() const { return this->outlineColor; }

    // TODO: DEPRECATED, don't use this function anymore, use setChangeCallback() instead
    bool hasChanged();

    void onFocusStolen() override;
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseUpOutside(bool left = true, bool right = false) override;
    void onMouseDownInside(bool left = true, bool right = false) override;
    void onMouseCancel() override;
    void onCapturedMouseMove() override;
    bool onWheel(WheelDelta vertical, WheelDelta horizontal) override;
    void onResized() override;

   protected:
    virtual void drawBlock();

    // draws the sliding line from (x1,y1) to (x2,y2), with the configured outline behind it (if any)
    void drawSlidingLine(float x1, float y1, float x2, float y2);

    void updateBlockPos();

    SliderChangeCallback sliderChangeCallback;

    vec2 vBlockSize;
    AnimVec2 vBlockPos{0.f, 0.f};
    [[nodiscard]] vec2 blockPos() const { return vec2{this->vBlockPos}; }
    vec2 vGrabBackup{0.f};

    // to avoid "fighting" between externally set values and mouse-based slider values, if the mouse position hasn't moved
    vec2 vLastMousePos{0.f};

    float fMinValue, fMaxValue, fCurValue, fCurPercent;
    float fPrevValue;
    float fKeyDelta;
    float fLastSoundPlayTime{0.f};

    Color frameColor, backgroundColor;

    // 0 == no outline
    // size is # of pixels extended out from the line (e.g. size 2 is 5 pixels across)
    int iLineOutlineSize{0};
    Color outlineColor{0xff000000};

    bool bDrawFrame;
    bool bDrawBackground;
    bool bHorizontal;
    bool bHasChanged;
    bool bAnimated;
    bool bLiveUpdate;
    bool bAllowMouseWheel;
};
