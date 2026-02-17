#pragma once
// Copyright (c) 2018, PG, All rights reserved.
#include "CBaseUISlider.h"

class McFont;
class Image;

class UIVolumeSlider final : public CBaseUISlider {
   public:
    enum class TYPE : uint8_t { MASTER, MUSIC, EFFECTS };

   public:
    UIVolumeSlider(float xPos, float yPos, float xSize, float ySize, UString name);

    void setType(TYPE type) { this->type = type; }
    void setSelected(bool selected);

    bool checkWentMouseInside();

    float getMinimumExtraTextWidth();

    [[nodiscard]] inline bool isSelected() const { return this->bSelected; }

   private:
    void drawBlock() override;

    void onMouseInside() override;

    struct Resources {
        std::array<Image*, 3> disabled{};
        std::array<Image*, 3> enabled{};
    };
    static Resources imageResources;
    static bool resourcesReady;

    McFont* font;
    float fSelectionAnim;

    TYPE type;
    bool bSelected;

    bool bWentMouseInside;
};
