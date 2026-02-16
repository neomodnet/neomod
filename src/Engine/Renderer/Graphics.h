#pragma once
// Copyright (c) 2012, PG, All rights reserved.

#include "noinclude.h"
#include "types.h"
#include "Color.h"
#include "Matrices.h"
#include "Rect.h"
#include "Vectors.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <optional>
#include <functional>

class ConVar;
class UString;

class Image;
class McFont;
class Shader;
class RenderTarget;
class VertexArrayObject;

// shadow offset pixels to the bottom right
struct TextShadow {
    Color col_text{rgb(255, 255, 255)};
    Color col_shadow{rgb(0, 0, 0)};
    int offs_px{1};  // not scaled to display DPI
};

enum class AnchorPoint : uint8_t {
    CENTER,        // Default - image centered on x,y
    TOP_LEFT,      // x,y at top left corner
    TOP_RIGHT,     // x,y at top right corner
    BOTTOM_LEFT,   // x,y at bottom left corner
    BOTTOM_RIGHT,  // x,y at bottom right corner
    TOP,           // x,y at top center
    BOTTOM,        // x,y at bottom center
    LEFT,          // x,y at middle left
    RIGHT          // x,y at middle right
};

enum class DrawPrimitive : uint8_t { LINES, LINE_STRIP, TRIANGLES, TRIANGLE_FAN, TRIANGLE_STRIP, QUADS };

enum class DrawUsageType : uint8_t { STATIC, DYNAMIC, STREAM };

enum class DrawPixelsType : uint8_t { UBYTE, FLOAT };

enum class MultisampleType : uint8_t { X0, X2, X4, X8, X16 };

enum class TextureWrapMode : uint8_t { CLAMP, REPEAT };

enum class TextureFilterMode : uint8_t { NONE, LINEAR, MIPMAP };

enum class DrawBlendMode : uint8_t {
    ALPHA,         // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) (default)
    ADDITIVE,      // glBlendFunc(GL_SRC_ALPHA, GL_ONE)
    PREMUL_ALPHA,  // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                   // GL_ONE_MINUS_SRC_ALPHA)
    PREMUL_COLOR   // glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
};

enum class DrawCompareFunc : uint8_t { NEVER, LESS, EQUAL, LESSEQUAL, GREATER, NOTEQUAL, GREATEREQUAL, ALWAYS };

class Graphics {
    NOCOPY_NOMOVE(Graphics)

   public:
    struct RectOptions {
        float x{0.f}, y{0.f}, width{0.f}, height{0.f}, lineThickness{1.f};
        Color top{(Color)-1}, right{(Color)-1}, bottom{(Color)-1}, left{(Color)-1};
        bool withColor{false};
    };

    struct ScreenshotParams {
        std::string savePath;  // if dataCB supplied, savePath is ignored
        std::function<void(std::vector<u8>)> dataCB;
        bool withAlpha;
    };

   public:
    friend class Engine;

    Graphics();
    virtual ~Graphics() = default;

    // scene
    virtual void beginScene() = 0;
    virtual void endScene() = 0;

    // depth buffer
    virtual void clearDepthBuffer() = 0;

    // color
    virtual void setColor(Color color) = 0;
    virtual void setAlpha(float alpha) = 0;

    // 2d primitive drawing
    virtual void drawPixels(int x, int y, int width, int height, DrawPixelsType type, const void *pixels) = 0;
    virtual void drawPixel(int x, int y) = 0;
    virtual void drawLinef(float x1, float y1, float x2, float y2) = 0;

    virtual void drawRectf(const RectOptions &opt) = 0;  // this is the main drawrect function
    virtual void fillRectf(float x, float y, float width, float height) = 0;
    virtual void fillRoundedRect(int x, int y, int width, int height, int radius) = 0;
    virtual void fillGradient(int x, int y, int width, int height, Color topLeftColor, Color topRightColor,
                              Color bottomLeftColor, Color bottomRightColor) = 0;

    // passthroughs to convert various kinds of arguments to the base implementation
    inline void drawRectf(float x, float y, float width, float height, bool withColor = false, Color top = -1,
                          Color right = -1, Color bottom = -1, Color left = -1) {
        return this->drawRectf(RectOptions{.x = x,
                                           .y = y,
                                           .width = width,
                                           .height = height,
                                           .top = top,
                                           .right = right,
                                           .bottom = bottom,
                                           .left = left,
                                           .withColor = withColor});
    }

    inline void drawLine(int x1, int y1, int x2, int y2) {
        return this->drawLinef((float)x1 + 0.5f, (float)y1 + 0.5f, (float)x2 + 0.5f, (float)y2 + 0.5f);
    }
    inline void drawLine(vec2 pos1, vec2 pos2) { return this->drawLinef(pos1.x, pos1.y, pos2.x, pos2.y); }
    inline void drawRectf(float x, float y, float width, float height, Color top, Color right, Color bottom,
                          Color left) {
        return this->drawRectf(x, y, width, height, true, top, right, bottom, left);
    }
    inline void drawRect(int x, int y, int width, int height) {
        return this->drawRectf((float)x + 0.5f, (float)y + 0.5f, (float)width, (float)height);
    }
    inline void drawRect(int x, int y, int width, int height, Color top, Color right, Color bottom, Color left) {
        return this->drawRectf((float)x + 0.5f, (float)y + 0.5f, (float)width, (float)height, top, right, bottom, left);
    }
    inline void drawRect(vec2 pos, vec2 size) {
        return this->drawRect((int)pos.x, (int)pos.y, (int)size.x, (int)size.y);
    }
    inline void drawRect(vec2 pos, vec2 size, Color top, Color right, Color bottom, Color left) {
        return this->drawRect((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, top, right, bottom, left);
    }

    inline void fillRect(int x, int y, int width, int height) {
        return this->fillRectf((float)x, (float)y, (float)width, (float)height);
    }
    inline void fillRect(vec2 pos, vec2 size) {
        return this->fillRect((int)pos.x, (int)pos.y, (int)size.x, (int)size.y);
    }
    inline void fillRoundedRect(vec2 pos, vec2 size, int radius) {
        return this->fillRoundedRect((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, radius);
    }
    inline void fillGradient(vec2 pos, vec2 size, Color topLeftColor, Color topRightColor, Color bottomLeftColor,
                             Color bottomRightColor) {
        return this->fillGradient((int)pos.x, (int)pos.y, (int)size.x, (int)size.y, topLeftColor, topRightColor,
                                  bottomLeftColor, bottomRightColor);
    }

    virtual void drawQuad(int x, int y, int width, int height) = 0;
    virtual void drawQuad(vec2 topLeft, vec2 topRight, vec2 bottomRight, vec2 bottomLeft, Color topLeftColor,
                          Color topRightColor, Color bottomRightColor, Color bottomLeftColor) = 0;

    // 2d resource drawing
    virtual void drawImage(const Image *image, AnchorPoint anchor = AnchorPoint::CENTER, float edgeSoftness = 0.0f,
                           McRect clipRect = {}) = 0;
    virtual void drawString(McFont *font, const UString &text, std::optional<TextShadow> shadow = std::nullopt) = 0;

    // 3d type drawing
    virtual void drawVAO(VertexArrayObject *vao) = 0;

    // 2d clipping
    virtual void setClipRect(McRect clipRect) = 0;
    virtual void pushClipRect(McRect clipRect) = 0;
    virtual void popClipRect() = 0;

    // viewport modification
    virtual void pushViewport() = 0;
    virtual void setViewport(int x, int y, int width, int height) = 0;
    inline void setViewport(vec2 size) { return setViewport(0, 0, (int)size.x, (int)size.y); }
    virtual void popViewport() = 0;

    // stencil buffer
    virtual void pushStencil() = 0;
    virtual void fillStencil(bool inside) = 0;
    virtual void popStencil() = 0;

    // renderer settings
    virtual void setClipping(bool enabled) = 0;
    virtual void setAlphaTesting(bool enabled) = 0;
    virtual void setAlphaTestFunc(DrawCompareFunc alphaFunc, float ref) = 0;
    virtual void setBlending(bool enabled) { this->bBlendingEnabled = enabled; }
    [[nodiscard]] inline bool getBlending() const { return this->bBlendingEnabled; }
    virtual void setBlendMode(DrawBlendMode blendMode) { this->currentBlendMode = blendMode; }
    [[nodiscard]] inline DrawBlendMode getBlendMode() const { return this->currentBlendMode; }
    virtual void setDepthBuffer(bool enabled) = 0;
    virtual void setColorWriting(bool r, bool g, bool b, bool a) = 0;
    inline void setColorWriting(bool enabled) { this->setColorWriting(enabled, enabled, enabled, enabled); }
    virtual void setColorInversion(bool enabled) = 0;
    virtual void setCulling(bool enabled) = 0;
    virtual void setVSync(bool enabled) = 0;
    virtual void setAntialiasing(bool enabled) = 0;
    virtual void setWireframe(bool enabled) = 0;

    // renderer actions
    virtual void flush() = 0;

    // can be called any time
    void takeScreenshot(ScreenshotParams params);
    inline void takeScreenshot(std::string_view savePath) {
        return takeScreenshot(ScreenshotParams{std::string{savePath}, {}, false});
    }

    // renderer info
    virtual const char *getName() const = 0;
    [[nodiscard]] virtual vec2 getResolution() const = 0;
    virtual UString getVendor() = 0;
    virtual UString getModel() = 0;
    virtual UString getVersion() = 0;
    virtual int getVRAMTotal() = 0;
    virtual int getVRAMRemaining() = 0;

    // callbacks
    virtual void onResolutionChange(vec2 newResolution) = 0;
    virtual void onRestored() {}  // optionally recreate swapchain

    // factory
    virtual Image *createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) = 0;
    virtual Image *createImage(i32 width, i32 height, bool mipmapped, bool keepInSystemMemory) = 0;
    virtual RenderTarget *createRenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType) = 0;
    virtual Shader *createShaderFromFile(std::string vertexShaderFilePath, std::string fragmentShaderFilePath) = 0;
    virtual Shader *createShaderFromSource(std::string vertexShader, std::string fragmentShader) = 0;
    virtual VertexArrayObject *createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                                       bool keepInSystemMemory) = 0;

   public:
    // provided core functions (api independent)

    // matrices & transforms
    void pushTransform();
    void popTransform();
    void forceUpdateTransform();

    // 2D
    // TODO: rename these to translate2D() etc.
    void translate(float x, float y, float z = 0);
    void translate(vec2 translation) { this->translate(translation.x, translation.y); }
    void translate(vec3 translation) { this->translate(translation.x, translation.y, translation.z); }
    void rotate(float deg, float x = 0, float y = 0, float z = 1);
    void rotate(float deg, vec3 axis) { this->rotate(deg, axis.x, axis.y, axis.z); }
    void scale(float x, float y, float z = 1);
    void scale(vec2 scaling) { this->scale(scaling.x, scaling.y, 1); }
    void scale(vec3 scaling) { this->scale(scaling.x, scaling.y, scaling.z); }

    // 3D
    void translate3D(float x, float y, float z);
    void translate3D(vec3 translation) { this->translate3D(translation.x, translation.y, translation.z); }
    void rotate3D(float deg, float x, float y, float z);
    void rotate3D(float deg, vec3 axis) { this->rotate3D(deg, axis.x, axis.y, axis.z); }
    void setWorldMatrix(Matrix4 &worldMatrix);
    void setWorldMatrixMul(Matrix4 &worldMatrix);
    void setProjectionMatrix(Matrix4 &projectionMatrix);

    Matrix4 getWorldMatrix();
    Matrix4 getProjectionMatrix();
    inline Matrix4 getMVP() const { return this->MP; }

    // 3d gui scenes
    void push3DScene(McRect region);
    void pop3DScene();
    void translate3DScene(float x, float y, float z = 0);
    void rotate3DScene(float rotx, float roty, float rotz);
    void offset3DScene(float x, float y, float z = 0);

   protected:
    virtual bool init() { return true; }   // must be called after the OS implementation constructor
    virtual void onTransformUpdate() = 0;  // called if matrices have changed and need to be (re-)applied/uploaded

    // takeScreenshot will queue this up to be called at the end of draw()
    virtual std::vector<u8> getScreenshot(bool withAlpha = false) = 0;

    void updateTransform(bool force = false);
    void checkStackLeaks();

    void processPendingScreenshot();
    std::vector<ScreenshotParams> pendingScreenshots;

    // transforms
    std::vector<Matrix4> worldTransformStack;
    std::vector<Matrix4> projectionTransformStack;

    std::vector<std::array<int, 4>> viewportStack;
    std::vector<vec2> resolutionStack;

    // 3d gui scenes
    std::vector<bool> scene3d_stack;
    Matrix4 scene3d_world_matrix;
    Matrix4 scene3d_projection_matrix;

    // transforms
    Matrix4 projectionMatrix;
    Matrix4 worldMatrix;
    Matrix4 MP;

    McRect scene3d_region;
    vec3 v3dSceneOffset{0.f};

    // info
    DrawBlendMode currentBlendMode{DrawBlendMode::ALPHA};
    bool bBlendingEnabled{true};
    bool bTransformUpToDate;
    bool bIs3dScene;
};

// define/managed in Engine.cpp, declared here for convenience
extern std::unique_ptr<Graphics> g;
