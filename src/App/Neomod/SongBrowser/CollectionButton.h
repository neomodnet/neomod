#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "CarouselButton.h"

class CollectionButton final : public CarouselButton {
    NOCOPY_NOMOVE(CollectionButton)
   public:
    CollectionButton();
    ~CollectionButton() override = default;

    void draw() override;

    [[nodiscard]] Color getActiveBackgroundColor() const override;
    [[nodiscard]] Color getInactiveBackgroundColor() const override;

   private:
    static constexpr const float fTitleScale{0.35f};
};
