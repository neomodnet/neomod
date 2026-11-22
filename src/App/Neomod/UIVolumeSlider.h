#pragma once
// Copyright (c) 2018, PG, All rights reserved.
#include "CBaseUISlider.h"

#include <array>

class McFont;
class Image;

class UIVolumeSlider final : public CBaseUISlider {
    NOCOPY_NOMOVE(UIVolumeSlider)
   public:
    enum class TYPE : uint8_t { MASTER, MUSIC, EFFECTS, SPEED };

   public:
    UIVolumeSlider(float xPos, float yPos, float xSize, float ySize, std::string name);
    ~UIVolumeSlider() override;

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
    AnimFloat fSelectionAnim;

    TYPE type;
    bool bSelected;

    bool bWentMouseInside;
};
