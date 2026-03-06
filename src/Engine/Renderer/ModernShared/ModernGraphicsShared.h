// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "config.h"

#if (defined(MCENGINE_FEATURE_GLES32) || defined(MCENGINE_FEATURE_DIRECTX11) || defined(MCENGINE_FEATURE_SDLGPU))

#include "Graphics.h"

// intermediate layer shared amongst dx11/gles3/sdlgpu
class ModernGraphicsShared : public Graphics {
   public:
    // 2d primitive drawing
    void drawPixels(int x, int y, int width, int height, DrawPixelsType type, const void *pixels) override;
    void drawPixel(int x, int y) override;
    void drawLinef(float x1, float y1, float x2, float y2) override;
    void drawRectf(const RectOptions &opt) override;
    void fillRectf(float x, float y, float width, float height) override;
    void fillRoundedRect(int x, int y, int width, int height, int radius) override;
    void fillGradient(int x, int y, int width, int height, Color topLeftColor, Color topRightColor,
                      Color bottomLeftColor, Color bottomRightColor) override;

    void drawQuad(int x, int y, int width, int height) override;
    void drawQuad(vec2 topLeft, vec2 topRight, vec2 bottomRight, vec2 bottomLeft, Color topLeftColor,
                  Color topRightColor, Color bottomRightColor, Color bottomLeftColor) override;

    virtual void setTexturing(bool enabled, bool force = false) = 0;
};

#endif
