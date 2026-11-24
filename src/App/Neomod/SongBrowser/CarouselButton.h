#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include <utility>

#include "AnimationHandler.h"
#include "CBaseUIButton.h"
#include "CarouselData.h"

class BeatmapCarousel;
class DatabaseBeatmap;
class SongBrowser;
class UIContextMenu;

// draws a carousel row. BeatmapCarousel only has buttons for the rows near the view and hands them to other rows
// as the view moves: the row data lives in SongBrowser, a button only keeps its animations.
// subclasses draw a kind of node, and get the node they show through assign()
class CarouselButton : public CBaseUIButton {
    NOCOPY_NOMOVE(CarouselButton)
    friend BeatmapCarousel;

   public:
    CarouselButton(float xPos, float yPos, float xSize, float ySize, std::nullptr_t = {});
    ~CarouselButton() override;
    void deleteAnimations();

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    virtual void updateLayoutEx();

    CarouselButton *setVisible(bool visible) override;

    inline void onMouseUpInside(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT) override {
        CBaseUIButton::onMouseUpInside(buttons);
        if(flags::has<MouseButtonFlags::MF_RIGHT>(buttons)) {
            return this->onRightMouseUpInside();
        }
    }

    // shows node from now on. shownBefore tells whether the node was visible since the last resetAnimations()
    virtual void assign(CarouselNode node, bool shownBefore);
    virtual void resetAnimations();

    void setTargetRelPosY(float targetRelPosY);

    inline void setOffsetPercent(float offsetPercent) { this->fOffsetPercent = offsetPercent; }

    [[nodiscard]] inline CarouselNode getNode() const { return this->node; }
    [[nodiscard]] inline float getActualWidth() const {
        return this->getSize().x - 2.f * actualScaledOffsetWithMargin.x;
    }
    [[nodiscard]] inline vec2 getActualSize() const { return this->getSize() - 2.f * actualScaledOffsetWithMargin; }
    [[nodiscard]] inline vec2 getActualPos() const { return this->getPos() + actualScaledOffsetWithMargin; }

    [[nodiscard]] virtual Color getActiveBackgroundColor() const;
    [[nodiscard]] virtual Color getInactiveBackgroundColor() const;

    [[nodiscard]] bool isSelected() const;

    // rebuildSongButtons optimizations
    static void updateResolution();
    [[nodiscard]] static inline vec2 getScaledBaseSize() { return scaledBaseSize; }
    [[nodiscard]] static inline f32 getScaledRowStep() {
        return scaledBaseSize.y - 2.f * actualScaledOffsetWithMargin.y;
    }

   protected:
    void drawMenuButtonBackground();

    void onRightMouseUpInside();
    void onClicked(MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT) override;

    void onMouseInside() override;
    void onMouseOutside() override;

    enum class MOVE_AWAY_STATE : uint8_t { MOVE_CENTER, MOVE_UP, MOVE_DOWN };

    void setMoveAwayState(MOVE_AWAY_STATE moveAwayState, bool animate = true);

    // constant shared
    static constexpr const int marginPixelsX{9};
    static constexpr const int marginPixelsY{9};
    static inline const vec2 baseSize{699.0f, 103.0f};
    static constexpr const float baseOsuPixelsScale{0.624f};

    // dynamic but shared
    static inline vec2 actualScaledOffsetWithMargin{vec2((int)(marginPixelsX), (int)(marginPixelsY))};
    static inline vec2 scaledBaseSize{vec2(1.f, 1.f)};
    static inline float lastHoverSoundTime{0.f};
    static inline float bgImageScale{1.f};

    CarouselNode node;

    float fTargetRelPosY{0.f};
    float fOffsetPercent{0.f};
    AnimFloat fHoverOffsetAnimation;
    AnimFloat fHoverMoveAwayAnimation;
    AnimFloat fCenterOffsetAnimation;
    AnimFloat fCenterOffsetVelocityAnimation;

    bool bWasAnimationEverStarted{false};

    MOVE_AWAY_STATE moveAwayState{MOVE_AWAY_STATE::MOVE_CENTER};
};
