#pragma once
// Copyright (c) 2014, PG, All rights reserved.
#include "CBaseUIButton.h"
#include "Delegate.h"

class CBaseUICheckbox : public CBaseUIButton {
    NOCOPY_NOMOVE(CBaseUICheckbox)
   public:
    CBaseUICheckbox(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {},
                    std::string text = {});
    ~CBaseUICheckbox() override = default;

    void draw() override;

    inline float getBlockSize() { return this->getSize().y / 2; }
    inline float getBlockBorder() { return this->getSize().y / 4; }
    [[nodiscard]] inline bool isChecked() const { return this->bChecked; }

    CBaseUICheckbox *setChecked(bool checked, bool fireChangeEvent = true);
    CBaseUICheckbox *setSizeToContent(int horizontalBorderSize = 1, int verticalBorderSize = 1) override;
    CBaseUICheckbox *setWidthToContent(int horizontalBorderSize = 1);
    [[nodiscard]] vec2 getNaturalSize() override;

    using CheckboxChangeCallback = SA::delegate<void(CBaseUICheckbox *)>;
    CBaseUICheckbox *setChangeCallback(const CheckboxChangeCallback &clickCallback) {
        this->changeCallback = clickCallback;
        return this;
    }

   protected:
    virtual void onPressed();

    bool bChecked;
    CheckboxChangeCallback changeCallback;
};
