//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		raw SDL_gpu graphics interface
//
// $NoKeywords: $sdlgpui
//===============================================================================//

#pragma once
#ifndef SDLGPUINTERFACE_H
#define SDLGPUINTERFACE_H
#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU

#include "Graphics.h"
#include "Hashing.h"
#include "SyncMutex.h"

#include <array>
#include <bit>

class SDLGPUShader;
class SDLGPUVertexArrayObject;
class SDLGPURenderTarget;
class SDLGPUImage;

typedef struct SDL_Window SDL_Window;
typedef struct SDL_GPUCommandBuffer SDL_GPUCommandBuffer;
typedef struct SDL_GPUSampler SDL_GPUSampler;
typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_GPUTexture SDL_GPUTexture;
typedef struct SDL_GPURenderPass SDL_GPURenderPass;
typedef struct SDL_GPUGraphicsPipeline SDL_GPUGraphicsPipeline;
typedef struct SDL_GPUBuffer SDL_GPUBuffer;
typedef struct SDL_GPUTransferBuffer SDL_GPUTransferBuffer;
typedef struct SDL_GPUShader SDL_GPUShader;

// can't forward declare unsized enums, these correspond to the SDL_ prefixed enums of the same name
using SDLGPUPrimitiveType = u8;
using SDLGPUTextureFormat = u8;
using SDLGPUSampleCount = u8;

using SDL_PropertiesID = u32;

struct SDLGPUSimpleVertex {
    vec3 pos;
    vec4 col;
    vec2 tex;
};

struct PipelineKey {
    SDL_GPUShader *vertexShader;
    SDL_GPUShader *fragmentShader;
    SDLGPUPrimitiveType primitiveType;
    DrawBlendMode blendMode;
    SDLGPUSampleCount sampleCount;
    u8 stencilState;
    bool blendingEnabled;
    bool depthTestEnabled;
    bool depthWriteEnabled;
    bool wireframe;
    bool cullingEnabled;
    u8 colorWriteMask;  // packed RGBA bits

    bool operator==(const PipelineKey &) const = default;
};

// clang-format off
struct PipelineKeyHash {
    using is_avalanching = void;
    [[nodiscard]] auto operator()(const PipelineKey &k) const noexcept -> u64 {
        // mix all fields into a single hash
        u64 h = 0;
        h ^= Hash::flat::hash<u64>{}(reinterpret_cast<uintptr_t>(k.vertexShader));
        h ^= Hash::flat::hash<u64>{}(reinterpret_cast<uintptr_t>(k.fragmentShader)) * 0x9e3779b97f4a7c15ULL;
        h ^= Hash::flat::hash<u64>{}((u64)k.primitiveType) * 0x517cc1b727220a95ULL;
        h ^= Hash::flat::hash<u64>{}((u64)k.sampleCount) * 0x3c6ef372fe94f82aULL;
        u64 packed = (u64)k.blendMode
                        | ((u64)k.stencilState << 8)
                        | ((u64)k.blendingEnabled << 16)
                        | ((u64)k.depthTestEnabled << 17)
                        | ((u64)k.depthWriteEnabled << 18)
                        | ((u64)k.wireframe << 19)
                        | ((u64)k.cullingEnabled << 20)
                        | ((u64)k.colorWriteMask << 24);
        h ^= Hash::flat::hash<u64>{}(packed) * 0x6c62272e07bb0142ULL;
        return h;
    }
};
// clang-format on

class SDLGPUInterface final : public Graphics {
    NOCOPY_NOMOVE(SDLGPUInterface)
   public:
    SDLGPUInterface(SDL_Window *window);
    ~SDLGPUInterface() override;

    // scene
    void beginScene() override;
    void endScene() override;

    // depth buffer
    void clearDepthBuffer() override;

    // color
    void setColor(Color color) override;
    void setAlpha(float alpha) override;

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

    // 2d resource drawing
    void drawImage(const Image *image, AnchorPoint anchor, float edgeSoftness, McRect clipRect) override;
    void drawString(McFont *font, const UString &text, std::optional<TextShadow> shadow = std::nullopt) override;

    // 3d type drawing
    void drawVAO(VertexArrayObject *vao) override;

    // 2d clipping
    void setClipRect(McRect clipRect) override;
    void pushClipRect(McRect clipRect) override;
    void popClipRect() override;

    // viewport
    void pushViewport() override;
    void setViewport(int x, int y, int width, int height) override;
    void popViewport() override;

    // stencil buffer
    void pushStencil() override;
    void fillStencil(bool inside) override;
    void popStencil() override;

    // renderer settings
    void setClipping(bool enabled) override;
    void setAlphaTesting(bool enabled) override;
    void setAlphaTestFunc(DrawCompareFunc alphaFunc, float ref) override;
    void setBlending(bool enabled) override;
    void setBlendMode(DrawBlendMode blendMode) override;
    void setDepthBuffer(bool enabled) override;
    void setColorWriting(bool r, bool g, bool b, bool a) override;
    void setColorInversion(bool enabled) override;
    void setCulling(bool enabled) override;
    void setVSync(bool enabled) override;
    void setAntialiasing(bool enabled) override;
    void setWireframe(bool enabled) override;

    // renderer actions
    void flush() override;

    // renderer info
    const char *getName() const override;
    [[nodiscard]] inline vec2 getResolution() const override { return m_viewport.size; }
    UString getVendor() override;
    UString getModel() override;
    UString getVersion() override;
    int getVRAMTotal() override;
    int getVRAMRemaining() override;

    // callbacks
    void onResolutionChange(vec2 newResolution) override;
    void onRestored() override;

    // factory
    Image *createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) override;
    Image *createImage(i32 width, i32 height, bool mipmapped, bool keepInSystemMemory) override;
    RenderTarget *createRenderTarget(int x, int y, int width, int height, MultisampleType msType) override;
    Shader *createShaderFromFile(std::string vertexShaderFilePath, std::string fragmentShaderFilePath) override;
    Shader *createShaderFromSource(std::string vertexShader, std::string fragmentShader) override;
    VertexArrayObject *createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                               bool keepInSystemMemory) override;

    // sdlgpu-specific accessors
    // texture binding state (set by SDLGPUImage::bind/unbind)
    inline SDL_GPUTexture *getBoundTexture() const { return m_boundTexture; }
    inline SDL_GPUSampler *getBoundSampler() const { return m_boundSampler; }
    inline void setBoundTexture(SDL_GPUTexture *tex) { m_boundTexture = tex; }
    inline void setBoundSampler(SDL_GPUSampler *sampler) { m_boundSampler = sampler; }

    // render target support
    void pushRenderTarget(SDL_GPUTexture *colorTex, SDL_GPUTexture *depthTex, bool clearColor, Color clearCol,
                          SDL_GPUTexture *resolveTex = nullptr, SDLGPUSampleCount sampleCount = 0);
    void popRenderTarget();

    // record a baked VAO draw into the deferred command list
    void recordBakedDraw(SDL_GPUBuffer *buffer, u32 firstVertex, u32 vertexCount, DrawPrimitive primitive);

    // shader switching
    void setActiveShader(SDLGPUShader *shader);
    inline SDLGPUShader *getActiveShader() const { return m_activeShader; }

    // shared upload transfer buffer pool (loader threads acquire, main thread releases)
    // outAllocSize receives the actual allocation size of the returned buffer (for passing back to release)
    SDL_GPUTransferBuffer *acquireUploadTransferBuffer(u32 minSize, u32 &outAllocSize);
    // arguments are zeroed
    void releaseUploadTransferBuffer(SDL_GPUTransferBuffer *&buf, u32 &size);

    // SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
    static const SDLGPUTextureFormat DEFAULT_TEXTURE_FORMAT;

    void setTexturing(bool enabled, bool force = false);

   protected:
    std::vector<u8> getScreenshot(bool withAlpha) override;
    bool init() override;
    void onTransformUpdate() override;

   private:
    void createPipeline();
    void rebuildPipeline();
    void flushDrawCommands();
    void addRenderPassBoundary();
    void recordDraw(SDL_GPUBuffer *bakedBuffer, u32 vertexOffset, u32 vertexCount);
    bool createDepthTexture(u32 width, u32 height);

    void initSmoothClipShader();
    void onFramecountNumChanged(float maxFramesInFlight);

    static SDLGPUPrimitiveType primitiveToSDLGPUPrimitive(DrawPrimitive prim);

    SDL_Window *m_window;
    SDL_GPUDevice *m_device{nullptr};

    // cached properties for renderer queries
    SDL_PropertiesID m_devProps{0};
    std::string m_rendererName{"SDLGPUInterface"};

    // shaders
    std::unique_ptr<SDLGPUShader> m_defaultShader{nullptr};
    SDLGPUShader *m_activeShader{nullptr};  // always points to default or custom
    std::unique_ptr<SDLGPUShader> m_smoothClipShader{nullptr};

    // pipeline cache (keyed by state)
    Hash::flat::map<PipelineKey, SDL_GPUGraphicsPipeline *, PipelineKeyHash> m_pipelineCache;
    SDL_GPUGraphicsPipeline *m_currentPipeline{nullptr};

    // per-frame command buffer + render pass
    SDL_GPUCommandBuffer *m_cmdBuf{nullptr};
    SDL_GPURenderPass *m_renderPass{nullptr};

    // backbuffer texture (we render here, then blit to swapchain at present time)
    // swapchain textures are write-only in SDL_GPU; this pattern matches SDL_Renderer's GPU backend
    SDL_GPUTexture *m_backbuffer{nullptr};
    u32 m_backbufferWidth{0};
    u32 m_backbufferHeight{0};

    // depth texture
    SDL_GPUTexture *m_depthTexture{nullptr};
    u32 m_depthTextureWidth{0};
    u32 m_depthTextureHeight{0};

    // vertex staging buffer for deferred batching
    static constexpr size_t MAX_STAGING_VERTS{65536};
    std::vector<SDLGPUSimpleVertex> m_stagingVertices;
    SDL_GPUBuffer *m_vertexBuffer{nullptr};
    SDL_GPUTransferBuffer *m_transferBuffer{nullptr};

    struct Viewport {
        vec2 pos;
        vec2 size;

        [[nodiscard]] bool operator==(const Viewport &) const = default;
    };

    struct Scissor {
        ivec2 pos;
        ivec2 size;

        [[nodiscard]] bool operator==(const Scissor &) const = default;
    };

    // deferred draw batching
    struct DrawCommand {
        u32 vertexOffset;
        u32 vertexCount;
        SDL_GPUBuffer *bakedBuffer;  // nullptr for immediate (uses shared staging buffer)

        SDL_GPUGraphicsPipeline *pipeline;

        SDL_GPUTexture *texture;
        SDL_GPUSampler *sampler;

        // uniform block snapshots
        struct UniformBlock {
            alignas(16) std::array<u8, 80> data;
            u32 slot;
            u32 size;
            bool isVertex;  // true=vertex, false=fragment
        };
        u8 numUniformBlocks;
        UniformBlock uniformBlocks[4];

        // viewport
        Viewport viewport;

        // scissor
        bool scissorEnabled;
        Scissor scissor;

        // stencil
        u8 stencilRef;
    };
    std::vector<DrawCommand> m_pendingDraws;

    // temporary vertex conversion buffer (reused per drawVAO call)
    std::vector<SDLGPUSimpleVertex> m_vertices;

    // pipeline state that requires rebuild
    int m_iStencilState{0};  // 0=off, 1=writing mask, 2=testing
    SDLGPUPrimitiveType m_currentPrimitiveType;
    bool m_bDepthTestEnabled{false};
    bool m_bDepthWriteEnabled{false};
    bool m_bScissorEnabled{false};
    bool m_bCullingEnabled{false};
    bool m_bWireframe{false};
    bool m_bColorWriteR{true}, m_bColorWriteG{true}, m_bColorWriteB{true}, m_bColorWriteA{true};
    bool m_bPipelineDirty{true};

    // state
    Viewport m_viewport{.pos = {0.f, 0.f}, .size = {1.f, 1.f}};

    Color m_color{(Color)-1};
    int m_iMaxFrameLatency{1};
    bool m_bTexturingEnabled{false};
    bool m_bColorInversion{false};
    bool m_bVSync{false};

    // cached present mode support (queried once at init)
    bool m_bSupportsSDRComposition{false};
    bool m_bSupportsImmediate{false};
    bool m_bSupportsMailbox{false};

    // 1x1 white dummy texture+sampler (bound when texturing is disabled)
    SDL_GPUTexture *m_dummyTexture{nullptr};
    SDL_GPUSampler *m_dummySampler{nullptr};

    // currently bound texture+sampler (set by SDLGPUImage)
    SDL_GPUTexture *m_boundTexture{nullptr};
    SDL_GPUSampler *m_boundSampler{nullptr};

    // stacks
    std::vector<McRect> m_clipRectStack;

    // render target stack
    struct RenderTargetState {
        SDL_GPUTexture *colorTarget;
        SDL_GPUTexture *depthTarget;
        SDL_GPUTexture *resolveTarget;
        SDLGPUSampleCount sampleCount;  // SDL_GPU_SAMPLECOUNT_1 == 0
        // clear flags consumed by the next flushDrawCommands()
        Color clearColor;
        bool pendingClearColor;
        bool pendingClearDepth;
        bool pendingClearStencil;

        [[nodiscard]] bool hasClears() const { return pendingClearColor || pendingClearDepth || pendingClearStencil; }
    };
    std::vector<RenderTargetState> m_renderTargetStack;

    // render pass boundaries for deferred RT switching
    // each boundary marks the start of a new render pass at a given draw index
    struct RenderPassBoundary {
        RenderTargetState state;
        u32 drawIndex;
    };
    std::vector<RenderPassBoundary> m_renderPassBoundaries;

    RenderTargetState m_curRTState{
        .colorTarget = nullptr,
        .depthTarget = nullptr,
        .resolveTarget = nullptr,
        .sampleCount = 0,
        .clearColor = 0xff000000,
        .pendingClearColor = false,
        .pendingClearDepth = false,
        .pendingClearStencil = false,
    };

    // upload transfer buffer pool, bucketed by power-of-2 size class.
    // index = countr_zero(size) - POOL_MIN_LOG2
    static constexpr u32 UPLOAD_POOL_BUDGET = 512 * 1024 * 1024;  // max idle VRAM in pool
    // (is 512MB too much? eh, if you're using this renderer you probably have a good enough GPU, dunno how to query this)
    static constexpr u32 POOL_MIN_LOG2 = 2;      // 4 bytes (1x1 RGBA)
    static constexpr u32 POOL_NUM_CLASSES = 27;  // 2^2 .. 2^28 (256MB)
    Sync::mutex m_uploadTransferPoolMutex;
    std::array<std::vector<SDL_GPUTransferBuffer *>, POOL_NUM_CLASSES> m_uploadTransferPool{};
    u32 m_uploadTransferPoolBytes{0};

    // stats (TODO? unused)
    int m_iStatsNumDrawCalls{0};
};

#endif

#endif
