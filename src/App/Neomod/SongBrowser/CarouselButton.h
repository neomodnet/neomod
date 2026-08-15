#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include <utility>
#include <atomic>

#include "AnimationHandler.h"
#include "CBaseUIButton.h"

class BeatmapCarousel;
class DatabaseBeatmap;
class SongBrowser;
class SongButton;
class UIContextMenu;

enum class CarouselButtonType : u8 {
    COLLECTION_BUTTON,
    SONG_BUTTON,
};

class CarouselButton : public CBaseUIButton {
    NOCOPY_NOMOVE(CarouselButton)
   public:
    // RTTI helpers (TODO: ugly and slow to use RTTI for such a fundamental thing)

    template <typename T>
    constexpr forceinline T *as() {
        return dynamic_cast<T *>(this);
    }
    template <typename T>
    constexpr forceinline const T *as() const {
        return dynamic_cast<const T *>(this);
    }

   public:
    CarouselButton(float xPos, float yPos, float xSize, float ySize, std::nullptr_t = {});
    CarouselButton(float xPos, float yPos, float xSize, float ySize, std::string name = {});
    ~CarouselButton() override;
    void deleteAnimations();

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    virtual void updateLayoutEx();

    CarouselButton *setVisible(bool visible) override;

    inline void onMouseUpInside(bool left = true, bool right = false) override {
        CBaseUIButton::onMouseUpInside(left, right);
        if(right) {
            return this->onRightMouseUpInside();
        }
    }

    // i hate how difficult it is to understand a sequence of unnamed boolean arguments
    struct SelOpts {
        bool noCallbacks{false};
        bool noSelectBottomChild{false};
        bool parentUnselected{false};
    };
    void select(SelOpts opts = {false, false, false});
    void deselect();

    virtual void resetAnimations();

    void setTargetRelPosY(float targetRelPosY);

    void setChildren(std::vector<SongButton *> children);
    void addChild(SongButton *child);
    void addChildren(std::vector<SongButton *> children);

    inline void setOffsetPercent(float offsetPercent) { this->fOffsetPercent = offsetPercent; }
    inline void setHideIfSelected(bool hideIfSelected) { this->bHideIfSelected = hideIfSelected; }
    inline void setIsSearchMatch(bool isSearchMatch) {
        this->bIsSearchMatch.store(isSearchMatch, std::memory_order_relaxed);
    }

    [[nodiscard]] inline float getActualWidth() const {
        return this->getSize().x - 2.f * actualScaledOffsetWithMargin.x;
    }
    [[nodiscard]] inline vec2 getActualSize() const { return this->getSize() - 2.f * actualScaledOffsetWithMargin; }
    [[nodiscard]] inline vec2 getActualPos() const { return this->getPos() + actualScaledOffsetWithMargin; }
    [[nodiscard]] inline std::vector<SongButton *> &getChildren() { return this->children; }
    [[nodiscard]] inline const std::vector<SongButton *> &getChildren() const { return this->children; }

    [[nodiscard]] virtual const DatabaseBeatmap *getDatabaseBeatmap() const { return nullptr; }
    [[nodiscard]] virtual Color getActiveBackgroundColor() const;
    [[nodiscard]] virtual Color getInactiveBackgroundColor() const;

    [[nodiscard]] inline bool isSelected() const { return this->bSelected; }
    [[nodiscard]] inline bool isHiddenIfSelected() const { return this->bHideIfSelected; }
    [[nodiscard]] inline bool isSearchMatch() const { return this->bIsSearchMatch.load(std::memory_order_relaxed); }

    // rebuildSongButtons optimizations
    CarouselButtonType btnType;
    static void updateResolution();
    [[nodiscard]] bool isIndependentDiffButton() const;

   protected:
    [[nodiscard]] bool childrenNeedSorting() const;

    void drawMenuButtonBackground();

    virtual void onSelected(bool /*wasSelected*/, SelOpts /*opts*/) { ; }

    virtual void onRightMouseUpInside() { ; }
    void onClicked(bool left = true, bool right = false) override;

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
    static inline float currentUIScale{1.f};

    std::vector<SongButton *> children;

    // used to avoid an expensive cast. should only be used by SongDifficultyButton
    SongButton *parentSongButton{nullptr};

    float fTargetRelPosY;
    float fOffsetPercent{0.f};
    AnimFloat fHoverOffsetAnimation;
    AnimFloat fHoverMoveAwayAnimation;
    AnimFloat fCenterOffsetAnimation;
    AnimFloat fCenterOffsetVelocityAnimation;

    u8 lastChildSortStarPrecalcIdx{0xFF};

    std::atomic<bool> bIsSearchMatch{true};

    bool bHideIfSelected{false};
    bool bSelected{false};
    bool bChildrenNeedSorting{true};

    bool bWasAnimationEverStarted{false};

    MOVE_AWAY_STATE moveAwayState{MOVE_AWAY_STATE::MOVE_CENTER};
};
