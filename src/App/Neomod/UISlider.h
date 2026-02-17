#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#include "CBaseUISlider.h"

class UISlider final : public CBaseUISlider {
   public:
    UISlider(float xPos, float yPos, float xSize, float ySize, UString name);

    void draw() override;

    // options menu hack
    UISlider *setUpdateRelPosOnChange(bool doReset) {
        this->bNeedsRelativePositioningUpdateOnChange = doReset;
        return this;
    }
    [[nodiscard]] inline bool getUpdateRelPosOnChange() const { return this->bNeedsRelativePositioningUpdateOnChange; }

   private:
    bool bNeedsRelativePositioningUpdateOnChange{false};
};
