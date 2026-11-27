#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "SongButton.h"

class SongDifficultyButton final : public SongButton {
    NOCOPY_NOMOVE(SongDifficultyButton)
   public:
    using SongButton::SongButton;
    ~SongDifficultyButton() override = default;

    void draw() override;
    void tick() override;

    void assign(CarouselNode node, bool shownBefore) override;
    void resetAnimations() override;

    [[nodiscard]] Color getInactiveBackgroundColor() const override;

   private:
    [[nodiscard]] bool isIndented() const;

    static constexpr const float fDiffScale{0.18f};

    AnimFloat fOffsetPercentAnim;

    enum class OffsetState : u8 { UNINITIALIZED, SELECTED, DESELECTED };
    OffsetState lastOffsetState{OffsetState::UNINITIALIZED};
};
