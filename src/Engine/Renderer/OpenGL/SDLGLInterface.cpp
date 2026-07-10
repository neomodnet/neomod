// Copyright (c) 2025, WH, All rights reserved.
#include "SDLGLInterface.h"

#if defined(MCENGINE_FEATURE_GLES32) || defined(MCENGINE_FEATURE_OPENGL)

#include "OpenGLHeaders.h"

#include <SDL3/SDL_video.h>

#include "Engine.h"
#include "OpenGLSync.h"
#include "Logging.h"
#include "Environment.h"
#include "ConVar.h"
#include "LaunchArgs.h"

namespace cv {
static ConVar debug_opengl_v("debug_opengl_v", false, CLIENT | HIDDEN,
                             [](float val) -> void { SDLGLInterface::setGLLog(!!static_cast<int>(val)); });
static ConVar r_gl_block_immediate(
    "r_gl_block_immediate", true, CLIENT,
    "whether to wait on OpenGL commands to finish on the GPU immediately or before the next draw batch");
}  // namespace cv

#ifndef MCENGINE_PLATFORM_WASM
#ifdef MCENGINE_FEATURE_GLES32
#include "glad/glad_egl.h"
#endif

namespace {  // static
#ifdef MCENGINE_FEATURE_GLES32
bool EGLLoaded{false};
bool GLESLoaded{false};
#else
bool GLLoaded{false};
#endif
}  // namespace
#endif  // MCENGINE_PLATFORM_WASM

// resolve GL functions (static, called before construction)
void SDLGLInterface::load() {
#ifndef MCENGINE_PLATFORM_WASM
    int loadedGLVer = 0;
    void *eglDisplay = SDL_EGL_GetCurrentDisplay();
#ifdef MCENGINE_FEATURE_GLES32
    int loadedEGLVer = gladLoaderLoadEGL(!!eglDisplay ? eglDisplay : EGL_NO_DISPLAY);
    if(!loadedEGLVer) {
        debugLog("WARNING: glad failed to load EGL, GL ES may fail to load");
    } else {
        EGLLoaded = true;
        debugLog("gladLoaderLoadEGL({:p}) loaded version {:d}.{:d}",
                 (void *)(!!eglDisplay ? eglDisplay : EGL_NO_DISPLAY), GLAD_VERSION_MAJOR(loadedEGLVer),
                 GLAD_VERSION_MINOR(loadedEGLVer));
    }
    loadedGLVer = gladLoaderLoadGLES2();
    GLESLoaded = loadedGLVer > 0;
#else
    loadedGLVer = gladLoaderLoadGL();
    // if we have opengl 1.1, we need to bail
    GLLoaded = GLAD_VERSION_MAJOR(loadedGLVer) >= 2;
#endif
    if(!loadedGLVer) {
        debugLog("glad load error");
        engine->showMessageErrorFatal("OpenGL Error", "Couldn't load OpenGL!\nThe engine will exit now.");
        engine->shutdown();
        return;
    }
    debugLog("glad loaded GL{} {:d}.{:d}, EGL: {:s}", Env::cfg(REND::GLES32) ? " ES" : "",
             GLAD_VERSION_MAJOR(loadedGLVer), GLAD_VERSION_MINOR(loadedGLVer), !!eglDisplay ? "true" : "false");
#endif
    debugLog("GL_VERSION string: {}", reinterpret_cast<const char *>(glGetString(GL_VERSION)));

    if((Env::cfg(BUILD::DEBUG) || Env::cfg(OS::WASM)) || Mc::LaunchArgs::has_arg(Mc::LaunchArgs::MISC_GL_VERBOSE)) {
        dumpGLContextInfo();
    }

    if(Mc::LaunchArgs::has_arg(Mc::LaunchArgs::MISC_GL_DEBUG)) {
        cv::debug_opengl_v.setValue(true);
    }
}

#ifdef MCENGINE_PLATFORM_WASM
// nothing
void SDLGLInterface::unload() {}
#else
void SDLGLInterface::unload() {
#ifdef MCENGINE_FEATURE_GLES32
    if(EGLLoaded) {
        gladLoaderUnloadEGL();
        EGLLoaded = false;
    }
    if(GLESLoaded) {
        gladLoaderUnloadGLES2();
        GLESLoaded = false;
    }
#else
    if(GLLoaded) {
        gladLoaderUnloadGL();
        GLLoaded = false;
    }
#endif
}
#endif

SDLGLInterface::SDLGLInterface(SDL_Window *window) : GLGraphicsBackend(), window(window) {}

bool SDLGLInterface::init() {
    load();
    const bool success =
#ifndef MCENGINE_PLATFORM_WASM
#ifdef MCENGINE_FEATURE_OPENGL
        GLLoaded;
#else
        GLESLoaded;
#endif
#else   // MCENGINE_FEATURE_OPENGL
        true;
#endif  // MCENGINE_PLATFORM_WASM
    this->syncobj = std::make_unique<OpenGLSync>();

    return success;
}

SDLGLInterface::~SDLGLInterface() { unload(); }

void SDLGLInterface::beginScene() {
    // block on frame queue (if enabled)
    if(!cv::r_gl_block_immediate.getBool()) {
        this->syncobj->begin();
    }
}

void SDLGLInterface::endScene() {
    SDL_GL_SwapWindow(this->window);

    // create sync obj for the gl commands this frame (if enabled)
    this->syncobj->end();

    // blocking immediately here means we will wait until the
    // last possible moment before acting on new user input for the next frame
    if(cv::r_gl_block_immediate.getBool()) {
        this->syncobj->begin();
    }
}

void SDLGLInterface::pushStencil() {
    // init and clear
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);

    // set mask
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 1, 1);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
}

void SDLGLInterface::fillStencil(bool inside) {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, inside ? 0 : 1, 1);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void SDLGLInterface::popStencil() { glDisable(GL_STENCIL_TEST); }

void SDLGLInterface::setBlending(bool enabled) {
    Graphics::setBlending(enabled);

    if(enabled)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
}

void SDLGLInterface::setDepthBuffer(bool enabled) {
    if(enabled)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
}

void SDLGLInterface::setCulling(bool culling) {
    if(culling)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
}

void SDLGLInterface::setBlendMode(DrawBlendMode blendMode) {
    Graphics::setBlendMode(blendMode);

    // only MAX uses a non-default blend equation, so unconditionally restore ADD for the others
    glBlendEquation(blendMode == DrawBlendMode::MAX ? GL_MAX : GL_FUNC_ADD);
    switch(blendMode) {
        case DrawBlendMode::ALPHA:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case DrawBlendMode::ADDITIVE:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case DrawBlendMode::PREMUL_ALPHA:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case DrawBlendMode::PREMUL_COLOR:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case DrawBlendMode::MAX:
            glBlendFunc(GL_ONE, GL_ONE);
            break;
    }
}

void SDLGLInterface::setVSync(bool vsync) {
    if(!SDL_GL_SetSwapInterval(vsync ? 1 : 0)) {
        debugLog("Could not {} vsync: {}", vsync ? "enable" : "disable", SDL_GetError());
    }
}

std::string SDLGLInterface::getVendor() {
    static const GLubyte *vendor = nullptr;
    if(!vendor) vendor = glGetString(GL_VENDOR);
    return reinterpret_cast<const char *>(vendor);
}

std::string SDLGLInterface::getModel() {
    static const GLubyte *model = nullptr;
    if(!model) model = glGetString(GL_RENDERER);
    return reinterpret_cast<const char *>(model);
}

std::string SDLGLInterface::getVersion() {
    static const GLubyte *version = nullptr;
    if(!version) version = glGetString(GL_VERSION);
    return reinterpret_cast<const char *>(version);
}

int SDLGLInterface::getVRAMTotal() {
    static GLint totalMem[4]{-1, -1, -1, -1};

    if(totalMem[0] == -1) {
        glGetIntegerv(GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, totalMem);
        if(!(totalMem[0] > 0 && glGetError() != GL_INVALID_ENUM)) totalMem[0] = 0;
    }
    return totalMem[0];
}

int SDLGLInterface::getVRAMRemaining() {
    GLint nvidiaMemory[4]{-1, -1, -1, -1};
    GLint atiMemory[4]{-1, -1, -1, -1};

    glGetIntegerv(GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, nvidiaMemory);

    if(nvidiaMemory[0] > 0) return nvidiaMemory[0];

    glGetIntegerv(TEXTURE_FREE_MEMORY_ATI, atiMemory);
    return atiMemory[0];
}

std::unordered_map<DrawPrimitive, int> SDLGLInterface::primitiveToOpenGLMap = {
    {DrawPrimitive::LINES, GL_LINES},
    {DrawPrimitive::LINE_STRIP, GL_LINE_STRIP},
    {DrawPrimitive::LINE_LOOP, GL_LINE_LOOP},
    {DrawPrimitive::TRIANGLES, GL_TRIANGLES},
    {DrawPrimitive::TRIANGLE_FAN, GL_TRIANGLE_FAN},
    {DrawPrimitive::TRIANGLE_STRIP, GL_TRIANGLE_STRIP},
    {DrawPrimitive::QUADS, Env::cfg(REND::GLES32) ? 0 : GL_QUADS},
};

std::unordered_map<DrawCompareFunc, int> SDLGLInterface::compareFuncToOpenGLMap = {
    {DrawCompareFunc::NEVER, GL_NEVER},         {DrawCompareFunc::LESS, GL_LESS},
    {DrawCompareFunc::EQUAL, GL_EQUAL},         {DrawCompareFunc::LESSEQUAL, GL_LEQUAL},
    {DrawCompareFunc::GREATER, GL_GREATER},     {DrawCompareFunc::NOTEQUAL, GL_NOTEQUAL},
    {DrawCompareFunc::GREATEREQUAL, GL_GEQUAL}, {DrawCompareFunc::ALWAYS, GL_ALWAYS},
};

std::unordered_map<DrawUsageType, unsigned int> SDLGLInterface::usageToOpenGLMap = {
    {DrawUsageType::STATIC, GL_STATIC_DRAW},
    {DrawUsageType::DYNAMIC, GL_DYNAMIC_DRAW},
    {DrawUsageType::STREAM, GL_STREAM_DRAW},
};

void SDLGLInterface::dumpGLContextInfo() {
    std::string info = "Initial OpenGL Context:\n";
    int current;
    for(int i = 0; auto [enm, str] : std::array<std::pair<SDL_GLAttr, std::string_view>, 28>{
                       {{SDL_GL_RED_SIZE, "GL_RED_SIZE"sv},
                        {SDL_GL_GREEN_SIZE, "GL_GREEN_SIZE"sv},
                        {SDL_GL_BLUE_SIZE, "GL_BLUE_SIZE"sv},
                        {SDL_GL_ALPHA_SIZE, "GL_ALPHA_SIZE"sv},
                        {SDL_GL_BUFFER_SIZE, "GL_BUFFER_SIZE"sv},
                        {SDL_GL_DOUBLEBUFFER, "GL_DOUBLEBUFFER"sv},
                        {SDL_GL_DEPTH_SIZE, "GL_DEPTH_SIZE"sv},
                        {SDL_GL_STENCIL_SIZE, "GL_STENCIL_SIZE"sv},
                        {SDL_GL_ACCUM_RED_SIZE, "GL_ACCUM_RED_SIZE"sv},
                        {SDL_GL_ACCUM_GREEN_SIZE, "GL_ACCUM_GREEN_SIZE"sv},
                        {SDL_GL_ACCUM_BLUE_SIZE, "GL_ACCUM_BLUE_SIZE"sv},
                        {SDL_GL_ACCUM_ALPHA_SIZE, "GL_ACCUM_ALPHA_SIZE"sv},
                        {SDL_GL_STEREO, "GL_STEREO"sv},
                        {SDL_GL_MULTISAMPLEBUFFERS, "GL_MULTISAMPLEBUFFERS"sv},
                        {SDL_GL_MULTISAMPLESAMPLES, "GL_MULTISAMPLESAMPLES"sv},
                        {SDL_GL_ACCELERATED_VISUAL, "GL_ACCELERATED_VISUAL"sv},
                        {SDL_GL_RETAINED_BACKING, "GL_RETAINED_BACKING"sv},
                        {SDL_GL_CONTEXT_MAJOR_VERSION, "GL_CONTEXT_MAJOR_VERSION"sv},
                        {SDL_GL_CONTEXT_MINOR_VERSION, "GL_CONTEXT_MINOR_VERSION"sv},
                        {SDL_GL_CONTEXT_FLAGS, "GL_CONTEXT_FLAGS"sv},
                        {SDL_GL_CONTEXT_PROFILE_MASK, "GL_CONTEXT_PROFILE_MASK"sv},
                        {SDL_GL_SHARE_WITH_CURRENT_CONTEXT, "GL_SHARE_WITH_CURRENT_CONTEXT"sv},
                        {SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, "GL_FRAMEBUFFER_SRGB_CAPABLE"sv},
                        {SDL_GL_CONTEXT_RELEASE_BEHAVIOR, "GL_CONTEXT_RELEASE_BEHAVIOR"sv},
                        {SDL_GL_CONTEXT_RESET_NOTIFICATION, "GL_CONTEXT_RESET_NOTIFICATION"sv},
                        {SDL_GL_CONTEXT_NO_ERROR, "GL_CONTEXT_NO_ERROR"sv},
                        {SDL_GL_FLOATBUFFERS, "GL_FLOATBUFFERS"sv},
                        {SDL_GL_EGL_PLATFORM, "GL_EGL_PLATFORM"sv}}}) {
        if(SDL_GL_GetAttribute(enm, &current)) {
            i++;
            info += fmt::format(" {:<30}: {:<3}", str, current);
            if(!(i % 4)) info.push_back('\n');
        }
    }
    if(info.back() == '\n') info.pop_back();  // remove trailing newline
    logRaw(info);
}

namespace {
std::string glDebugSourceString(GLenum source) {
    switch(source) {
        case GL_DEBUG_SOURCE_API:
            return "API";
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
            return "WINDOW_SYSTEM";
        case GL_DEBUG_SOURCE_SHADER_COMPILER:
            return "SHADER_COMPILER";
        case GL_DEBUG_SOURCE_THIRD_PARTY:
            return "THIRD_PARTY";
        case GL_DEBUG_SOURCE_APPLICATION:
            return "APPLICATION";
        case GL_DEBUG_SOURCE_OTHER:
            return "OTHER";
        default:
            return fmt::format("{:04x}", source);
    }
}
std::string glDebugTypeString(GLenum type) {
    switch(type) {
        case GL_DEBUG_TYPE_ERROR:
            return "ERROR";
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
            return "DEPRECATED_BEHAVIOR";
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
            return "UNDEFINED_BEHAVIOR";
        case GL_DEBUG_TYPE_PORTABILITY:
            return "PORTABILITY";
        case GL_DEBUG_TYPE_PERFORMANCE:
            return "PERFORMANCE";
        case GL_DEBUG_TYPE_OTHER:
            return "OTHER";
        case GL_DEBUG_TYPE_MARKER:
            return "MARKER";
        case GL_DEBUG_TYPE_PUSH_GROUP:
            return "PUSH_GROUP";
        case GL_DEBUG_TYPE_POP_GROUP:
            return "POP_GROUP";
        default:
            return fmt::format("{:04x}", type);
    }
}

std::string glDebugSeverityString(GLenum severity) {
    switch(severity) {
        case GL_DEBUG_SEVERITY_HIGH:
            return "HIGH";
        case GL_DEBUG_SEVERITY_MEDIUM:
            return "MEDIUM";
        case GL_DEBUG_SEVERITY_LOW:
            return "LOW";
        case GL_DEBUG_SEVERITY_NOTIFICATION:
            return "NOTIFICATION";
        default:
            return fmt::format("{:04x}", severity);
    }
}

}  // namespace

void SDLGLInterface::setGLLog(bool on) {
    if(!g || !g.get()) return;
#ifdef MCENGINE_FEATURE_GLES32
    // GLES 3.2 has debug functions as core, always available
#else
    if(!(!!glDebugMessageCallbackARB || !!glDebugMessageCallback)) return;
#endif
    if(on) {
        glEnable(GL_DEBUG_OUTPUT);
    } else {
        glDisable(GL_DEBUG_OUTPUT);
    }
}

void GLAPIENTRY SDLGLInterface::glDebugCB(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                          const GLchar *message, const void * /*userParam*/) {
    logRaw("[GLDebugCB]");
    logRaw("    message: {}", std::string(message, length));
    logRaw("    time: {:.4f}", engine->getTime());
    logRaw("    id: {}", id);
    logRaw("    source: {}", glDebugSourceString(source));
    logRaw("    type: {}", glDebugTypeString(type));
    logRaw("    severity: {}", glDebugSeverityString(severity));
}

#endif
