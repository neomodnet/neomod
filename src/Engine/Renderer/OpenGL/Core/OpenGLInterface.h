#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#ifndef LEGACYOPENGLINTERFACE_H
#define LEGACYOPENGLINTERFACE_H

#include "config.h"

#ifdef MCENGINE_FEATURE_OPENGL
#include "SDLGLInterface.h"

class Image;

class OpenGLInterface final : public SDLGLInterface {
    NOCOPY_NOMOVE(OpenGLInterface)
   public:
    OpenGLInterface() = delete;
    OpenGLInterface(void *window);
    ~OpenGLInterface() override;

    // scene
    void beginScene() override;
    void endScene() override;

    // depth buffer
    void clearDepthBuffer() final;

    // color
    void setColor(Color color) final;
    void setAlpha(float alpha) final;

    // 2d primitive drawing
    void drawPixels(int x, int y, int width, int height, DrawPixelsType type, const void *pixels) final;
    void drawPixel(int x, int y) final;
    void drawLinef(float x1, float y1, float x2, float y2) final;
    void drawRectf(const RectOptions &opt) final;
    using Graphics::fillRectf;
    void fillRectf(const FillRectOptions &opt) final;
    void drawArcf(float cx, float cy, float radius, float startAngle, float endAngle) final;
    void fillGradient(int x, int y, int width, int height, Color topLeftColor, Color topRightColor,
                      Color bottomLeftColor, Color bottomRightColor) final;

    void drawQuad(int x, int y, int width, int height) final;
    void drawQuad(vec2 topLeft, vec2 topRight, vec2 bottomRight, vec2 bottomLeft, Color topLeftColor,
                  Color topRightColor, Color bottomRightColor, Color bottomLeftColor) final;

    // 2d resource drawing
    void drawImage(const Image *image, AnchorPoint anchor = AnchorPoint::CENTER, float edgeSoftness = 0.0f,
                   McRect clipRect = {}) final;
    void drawString(McFont *font, std::string_view text, std::optional<TextFX> effects = std::nullopt) final;

    // 3d type drawing
    void drawVAO(VertexArrayObject *vao) final;

    // 2d clipping
    void setClipRect(McRect clipRect) final;
    void pushClipRect(McRect clipRect) final;
    void popClipRect() final;

    // viewport modification
    void pushViewport() final;
    void setViewport(int x, int y, int width, int height) final;
    void popViewport() final;

    // renderer settings
    void setClipping(bool enabled) final;
    void setAlphaTesting(bool enabled) final;
    void setAlphaTestFunc(DrawCompareFunc alphaFunc, float ref) final;
    void setColorWriting(bool r, bool g, bool b, bool a) final;
    void setColorInversion(bool enabled) final;
    void setAntialiasing(bool aa) final;
    void setWireframe(bool enabled) final;

    // renderer actions
    void flush() final;

    // renderer info
    [[nodiscard]] vec2 getResolution() const final { return this->vResolution; }
    [[nodiscard]] inline const char *getName() const final { return "OpenGL Legacy"; }

    // callbacks
    void onResolutionChange(vec2 newResolution) final;

    // factory
    Image *createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) final;
    Image *createImage(i32 width, i32 height, bool mipmapped, bool keepInSystemMemory) final;
    RenderTarget *createRenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType) final;
    Shader *createShaderFromFile(std::string vertexShaderFilePath,
                                 std::string fragmentShaderFilePath) final;                      // DEPRECATED
    Shader *createShaderFromSource(std::string vertexShader, std::string fragmentShader) final;  // DEPRECATED
    VertexArrayObject *createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                               bool keepInSystemMemory) final;

   protected:
    bool init() final;

    void onTransformUpdate() final;
    std::vector<u8> getScreenshot(bool withAlpha = false) final;

   private:
    std::unique_ptr<Shader> smoothClipShader{nullptr};
    void initSmoothClipShader();

    // renderer
    bool bInScene{false};
    vec2 vResolution{0.f};

    // persistent vars
    bool bAntiAliasing{true};
    //float fZ{1};
    //float fClearZ{1};

    // clipping
    std::vector<McRect> clipRectStack;
};

#endif

#endif
