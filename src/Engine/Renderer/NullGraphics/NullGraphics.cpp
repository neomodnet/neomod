// Copyright (c) 2026, WH, All rights reserved.
// dummy graphics backend for headless mode (no rendering)
#include "NullGraphics.h"

#include "Graphics_private.h"
#include "NullImage.h"
#include "NullRenderTarget.h"
#include "NullShader.h"
#include "NullVertexArrayObject.h"

#include "Font.h"

// scene
void NullGraphics::beginScene() {}
void NullGraphics::endScene() {}

// depth buffer
void NullGraphics::clearDepthBuffer() {}

// color
void NullGraphics::setColor(Color color) { m_data->color = color; }
void NullGraphics::setAlpha(float /*alpha*/) {}

// 2d primitive drawing
void NullGraphics::drawPixels(int /*x*/, int /*y*/, int /*width*/, int /*height*/, DrawPixelsType /*type*/,
                              const void * /*pixels*/) {}
void NullGraphics::drawPixel(int /*x*/, int /*y*/) {}
void NullGraphics::drawLinef(float /*x1*/, float /*y1*/, float /*x2*/, float /*y2*/) {}
void NullGraphics::drawRectf(const RectOptions & /*opt*/) {}
void NullGraphics::fillRectf(const FillRectOptions & /*opt*/) {}
void NullGraphics::drawArcf(float /*cx*/, float /*cy*/, float /*radius*/, float /*startAngle*/, float /*endAngle*/) {}
void NullGraphics::fillGradient(int /*x*/, int /*y*/, int /*width*/, int /*height*/, Color /*topLeftColor*/,
                                Color /*topRightColor*/, Color /*bottomLeftColor*/, Color /*bottomRightColor*/) {}

void NullGraphics::drawQuad(int /*x*/, int /*y*/, int /*width*/, int /*height*/) {}
void NullGraphics::drawQuad(vec2 /*topLeft*/, vec2 /*topRight*/, vec2 /*bottomRight*/, vec2 /*bottomLeft*/,
                            Color /*topLeftColor*/, Color /*topRightColor*/, Color /*bottomRightColor*/,
                            Color /*bottomLeftColor*/) {}

// 2d resource drawing
void NullGraphics::drawImage(const Image * /*image*/, AnchorPoint /*anchor*/, float /*edgeSoftness*/,
                             McRect /*clipRect*/) {}
void NullGraphics::drawString(McFont *font, std::string_view text, std::optional<TextFX> effects) {
    updateTransform();

    font->drawString(text, effects);
}

// 3d type drawing
void NullGraphics::drawVAO(VertexArrayObject * /*vao*/) {}

// 2d clipping
void NullGraphics::setClipRect(McRect /*clipRect*/) {}
void NullGraphics::pushClipRect(McRect /*clipRect*/) {}
void NullGraphics::popClipRect() {}

// viewport
void NullGraphics::pushViewport() {}
void NullGraphics::setViewport(int /*x*/, int /*y*/, int /*width*/, int /*height*/) {}
void NullGraphics::popViewport() {}

// stencil buffer
void NullGraphics::pushStencil() {}
void NullGraphics::fillStencil(bool /*inside*/) {}
void NullGraphics::popStencil() {}

// renderer settings
void NullGraphics::setClipping(bool /*enabled*/) {}
void NullGraphics::setAlphaTesting(bool /*enabled*/) {}
void NullGraphics::setAlphaTestFunc(DrawCompareFunc /*alphaFunc*/, float /*ref*/) {}
void NullGraphics::setDepthBuffer(bool /*enabled*/) {}
void NullGraphics::setColorWriting(bool /*r*/, bool /*g*/, bool /*b*/, bool /*a*/) {}
void NullGraphics::setColorInversion(bool /*enabled*/) {}
void NullGraphics::setCulling(bool /*enabled*/) {}
void NullGraphics::setVSync(bool /*enabled*/) {}
void NullGraphics::setAntialiasing(bool /*enabled*/) {}
void NullGraphics::setWireframe(bool /*enabled*/) {}

// renderer actions
void NullGraphics::flush() {}
std::vector<u8> NullGraphics::getScreenshot(bool /*withAlpha*/) { return {}; }

// renderer info
std::string_view NullGraphics::getName() const { return "NullGraphics"; }
[[nodiscard]] vec2 NullGraphics::getResolution() const { return {1280.f, 720.f}; }
std::string_view NullGraphics::getVendor() { return ""; }
std::string_view NullGraphics::getModel() { return ""; }
std::string_view NullGraphics::getVersion() { return ""; }
int NullGraphics::getVRAMTotal() { return 0; }
int NullGraphics::getVRAMRemaining() { return 0; }

// callbacks
void NullGraphics::onResolutionChange(vec2 /*newResolution*/) {}

// factory
Image *NullGraphics::createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) {
    return new NullImage(std::move(filePath), mipmapped, keepInSystemMemory);
}
Image *NullGraphics::createImage(i32 width, i32 height, bool mipmapped, bool keepInSystemMemory) {
    return new NullImage(width, height, mipmapped, keepInSystemMemory);
}
RenderTarget *NullGraphics::createRenderTarget(int x, int y, int width, int height, MultisampleType msType) {
    return new NullRenderTarget(x, y, width, height, msType);
}
Shader *NullGraphics::createShaderFromSource(std::string_view /*vertexShader*/, std::string_view /*fragmentShader*/) {
    return new NullShader();
}
VertexArrayObject *NullGraphics::createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                                         bool keepInSystemMemory) {
    return new NullVertexArrayObject(primitive, usage, keepInSystemMemory);
}

void NullGraphics::onTransformUpdate() {}
