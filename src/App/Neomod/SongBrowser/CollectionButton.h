#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "CarouselButton.h"

class CollectionButton final : public CarouselButton {
    NOCOPY_NOMOVE(CollectionButton)
   public:
    explicit CollectionButton(BeatmapCarousel &carousel);
    ~CollectionButton() override = default;

    void draw() override;
    void updateLayoutEx() override;

    [[nodiscard]] Color getActiveBackgroundColor() const override;
    [[nodiscard]] Color getInactiveBackgroundColor() const override;

   private:
    static constexpr const float fTitleScale{0.35f};
};
