#pragma once
// Copyright (c) 2014, PG, All rights reserved.
#include "CBaseUIElement.h"
#include "Color.h"
#include "Graphics_fwd.h"
class McFont;

class CBaseUILabel : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUILabel)
   public:
    CBaseUILabel(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {},
                 std::string text = {});
    ~CBaseUILabel() override = default;

    void draw() override;

    // cancer
    void setRelSizeX(float x) { this->relRect.setSize({x, this->relRect.getSize().y}); }

    // set
    CBaseUILabel *setDrawFrame(bool drawFrame) {
        this->bDrawFrame = drawFrame;
        return this;
    }
    CBaseUILabel *setDrawBackground(bool drawBackground) {
        this->bDrawBackground = drawBackground;
        return this;
    }
    CBaseUILabel *setDrawTextShadow(bool drawTextShadow) {
        this->bDrawTextShadow = drawTextShadow;
        return this;
    }
    CBaseUILabel *setFrameColor(Color frameColor) {
        this->frameColor = frameColor;
        return this;
    }
    CBaseUILabel *setBackgroundColor(Color backgroundColor) {
        this->backgroundColor = backgroundColor;
        return this;
    }
    // NOTE: setAutoscaleFX and setDrawTextShadow also modify behavior!
    CBaseUILabel *setTextFX(TextFX fx) {
        this->tfx = fx;
        return this;
    }
    CBaseUILabel *setAutoscaleFX(bool autoscale) {
        this->bAutoscaleFX = autoscale;
        return this;
    }
    CBaseUILabel *setTextColor(Color textColor) {
        this->tfx.col_text = textColor;
        return this;
    }
    CBaseUILabel *setShadowColor(Color shadowColor) {
        this->tfx.col_shadow = shadowColor;
        return this;
    }
    CBaseUILabel *setText(std::string text) {
        this->sText = std::move(text);
        this->updateStringMetrics();
        return this;
    }
    CBaseUILabel *setFont(McFont *font) {
        this->font = font;
        this->updateStringMetrics();
        return this;
    }

    CBaseUILabel *setSizeToContent(int horizontalBorderSize = 1, int verticalBorderSize = 1) {
        this->setSize(this->fStringWidth + 2 * horizontalBorderSize, this->fStringHeight + 2 * verticalBorderSize);
        return this;
    }
    CBaseUILabel *setWidthToContent(int horizontalBorderSize = 1) {
        this->setSizeX(this->fStringWidth + 2 * horizontalBorderSize);
        return this;
    }
    CBaseUILabel *setTextJustification(TEXT_JUSTIFICATION textJustification) {
        this->textJustification = textJustification;
        return this;
    }
    CBaseUILabel *setScale(float newScale) {
        this->fScale = newScale;
        return this;
    }

    // get
    [[nodiscard]] inline Color getFrameColor() const { return this->frameColor; }
    [[nodiscard]] inline Color getBackgroundColor() const { return this->backgroundColor; }
    [[nodiscard]] inline Color getTextColor() const { return this->tfx.col_text; }
    [[nodiscard]] inline const TextFX &getTextFX() const { return this->tfx; }
    [[nodiscard]] inline McFont *getFont() const { return this->font; }
    [[nodiscard]] inline std::string_view getText() const { return this->sText; }
    [[nodiscard]] inline float getStringWidth() const { return this->fStringWidth; }

    void onResized() override { this->updateStringMetrics(); }

   protected:
    virtual void drawText();

    void updateStringMetrics();

    std::string sText;
    McFont *font;

    float fStringWidth{0.f};
    float fStringHeight{0.f};

    Color frameColor{0xffffffff};
    Color backgroundColor{0xff000000};
    TextFX tfx{
        .col_text{0xffffffff},           //
        .col_shadow{0xff000000},         //
        .offs_px = 1.f,                  // NOTE: will be scaled by font dpi (if autoscale is enabled)
        .col_outline{argb(0, 0, 0, 0)},  //
        .outline_px = 1.f,               // NOTE: will be scaled by font dpi (if autoscale is enabled)
        .shadow_softness_px = 0.f        // NOTE: will be scaled by font dpi (if autoscale is enabled)
    };

    float fScale{1.f};
    TEXT_JUSTIFICATION textJustification{TEXT_JUSTIFICATION::LEFT};

    bool bDrawFrame{true};
    bool bDrawBackground{true};

    // TODO: this should probably be enabled by default,
    // but need to look over everything to make sure we're not double-drawing shadows
    bool bDrawTextShadow{false};

    bool bAutoscaleFX{true};
};
