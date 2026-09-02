#pragma once
// Copyright (c) 2025, WH, All rights reserved.
#ifndef SDLGLINTERFACE_H
#define SDLGLINTERFACE_H

#include "BaseEnvironment.h"

#if defined(MCENGINE_FEATURE_GLES32) || defined(MCENGINE_FEATURE_OPENGL)

#ifdef MCENGINE_FEATURE_GLES32
#include "ModernGraphicsShared.h"
using GLGraphicsBackend = ModernGraphicsShared;
#else
#include "Graphics.h"
using GLGraphicsBackend = Graphics;
#endif

#include <string>
#include <memory>
#include <unordered_map>

#ifndef GLAPIENTRY
#ifdef APIENTRY
#define GLAPIENTRY APIENTRY
#elif defined(MCENGINE_PLATFORM_WINDOWS)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif
#endif

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLchar = char;
using GLsizei = int;

class OpenGLSync;

typedef struct SDL_Window SDL_Window;
// shared superclass for OpenGLInterface and OpenGLES32Interface
class SDLGLInterface : public GLGraphicsBackend {
    NOCOPY_NOMOVE(SDLGLInterface);

   public:
    SDLGLInterface(SDL_Window *window);
    SDLGLInterface() = delete;

    ~SDLGLInterface() override;

    // scene
    void beginScene() override;
    void endScene() override;

    // stencil
    void pushStencil() final;
    void fillStencil(bool inside) final;
    void popStencil() final;

    // renderer settings
    void setDepthBuffer(bool enabled) final;
    void setCulling(bool culling) final;
    void setBlending(bool enabled) final;
    void setBlendMode(DrawBlendMode blendMode) final;

    // device settings
    void setVSync(bool vsync) override;

    // device info
    std::string getVendor() override;
    std::string getModel() override;
    std::string getVersion() override;
    int getVRAMRemaining() override;
    int getVRAMTotal() override;

    [[nodiscard]] bool hasFlippedTextureOrigin() const override { return true; }

    // 1x1 transparent texture, bound in place of entirely transparent images (which are never uploaded)
    [[nodiscard]] inline GLuint getTransparentTexture() const { return this->transparentTexture; }

    static void setGLLog(bool on);

    // debugging
    static void GLAPIENTRY glDebugCB(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                     const GLchar *message, const void * /*userParam*/);

    static std::unordered_map<DrawPrimitive, int> primitiveToOpenGLMap;
    static std::unordered_map<DrawCompareFunc, int> compareFuncToOpenGLMap;
    static std::unordered_map<DrawUsageType, unsigned int> usageToOpenGLMap;

   protected:
    bool init() override;

   private:
    static void load();
    static void unload();
    static void dumpGLContextInfo();

    SDL_Window *window;

    GLuint transparentTexture{0};

    // frame queue management
    std::unique_ptr<OpenGLSync> syncobj;
};

#else
class SDLGLInterface {};
#endif

#endif
