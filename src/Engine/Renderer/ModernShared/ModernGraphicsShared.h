// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "config.h"

#if (defined(MCENGINE_FEATURE_GLES32) || defined(MCENGINE_FEATURE_DIRECTX11) || defined(MCENGINE_FEATURE_SDLGPU))

#include "Graphics.h"

#include <memory>
#include <span>
#include <vector>

// intermediate layer shared amongst dx11/gles3/sdlgpu
class ModernGraphicsShared : public Graphics {
   public:
    ~ModernGraphicsShared() override;

    // rewrites QUADS and TRIANGLE_FAN vertex data into a TRIANGLES list, for backends that can't draw them natively.
    // returns the primitive to draw the outputs with; other primitives come back unchanged and leave the outputs empty
    static DrawPrimitive convertToTriangles(DrawPrimitive primitive, std::span<const vec3> vertices,
                                            std::span<const vec2> texcoords, std::span<const Color> colors,
                                            std::vector<vec3> &outVertices, std::vector<vec2> &outTexcoords,
                                            std::vector<Color> &outColors);

    // color
    void setColor(Color color) override;
    void setAlpha(float alpha) override;

    // 2d primitive drawing
    void drawPixels(int x, int y, int width, int height, DrawPixelsType type, const void *pixels) override;
    void drawPixel(int x, int y) override;
    void drawLinef(float x1, float y1, float x2, float y2) override;
    void drawRectf(const RectOptions &opt) override;
    using Graphics::fillRectf;
    void fillRectf(const FillRectOptions &opt) override;
    void drawArcf(float cx, float cy, float radius, float startAngle, float endAngle) override;
    void fillGradient(int x, int y, int width, int height, Color topLeftColor, Color topRightColor,
                      Color bottomLeftColor, Color bottomRightColor) override;

    void drawQuad(int x, int y, int width, int height) override;
    void drawQuad(vec2 topLeft, vec2 topRight, vec2 bottomRight, vec2 bottomLeft, Color topLeftColor,
                  Color topRightColor, Color bottomRightColor, Color bottomLeftColor) override;

    // 2d resource drawing
    void drawImage(const Image *image, AnchorPoint anchor = AnchorPoint::CENTER, float edgeSoftness = 0.0f,
                   McRect clipRect = {}) final;
    void drawString(McFont *font, std::string_view text, std::optional<TextFX> effects = std::nullopt) final;

    // 2d clipping
    void pushClipRect(McRect clipRect) final;
    void popClipRect() final;

   protected:
    virtual void setTexturing(bool enabled, bool force = false) = 0;

    // creates the backend smoothclip shader, called by drawImage() on first use
    [[nodiscard]] virtual Shader *createSmoothClipShader() = 0;
    // the clip rect corner with the lowest coordinates, in the framebuffer space read by the smoothclip shader
    [[nodiscard]] virtual vec2 getSmoothClipMin(McRect clipRect) const = 0;

    std::unique_ptr<Shader> smoothClipShader;
    std::vector<McRect> clipRectStack;

    // helper for arc drawing
    static void addArcVertices(VertexArrayObject &vao, float cx, float cy, float radius, float startAngle,
                               float endAngle);
};

#endif
