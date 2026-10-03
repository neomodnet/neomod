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

#include "BaseEnvironment.h"
#include "ModernGraphicsShared.h"
#include "Hashing.h"
#include "SyncMutex.h"

#include <vector>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <memory>
#include <algorithm>

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
typedef struct SDL_GPUFence SDL_GPUFence;
typedef struct SDL_GPUShader SDL_GPUShader;

// can't forward declare unsized enums, these correspond to the SDL_ prefixed enums of the same name
using SDLGPUPrimitiveType = u8;
using SDLGPUTextureFormat = u8;
using SDLGPUSampleCount = u8;

using SDL_PropertiesID = u32;

struct SDLGPUSimpleVertex {
    vec3 pos;
    std::array<u8, 4> col;  // rgba8 unorm, the shaders see a normalized vec4
    vec2 tex;

    [[nodiscard]] static constexpr std::array<u8, 4> packColor(Color c) { return {c.r, c.g, c.b, c.a}; }
    [[nodiscard]] static constexpr std::array<u8, 4> packColorMul(Color c, Color m) {
        constexpr auto mul = [](u32 a, u32 b) { return static_cast<u8>((a * b + 127) / 255); };
        return {mul(c.r, m.r), mul(c.g, m.g), mul(c.b, m.b), mul(c.a, m.a)};
    }
};
static_assert(sizeof(SDLGPUSimpleVertex) == 24);

class SDLGPUInterface final : public ModernGraphicsShared {
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

    // 2d primitive drawing (implemented in ModernGraphicsShared)

    // 2d resource drawing
    void drawImage(const Image *image, AnchorPoint anchor, float edgeSoftness, McRect clipRect) override;
    void drawString(McFont *font, std::string_view text, std::optional<TextFX> effects = std::nullopt) override;

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
    [[nodiscard]] inline vec2 getResolution() const override { return m_viewport.size; }

    [[nodiscard]] inline std::string_view getName() const override { return m_rendererName; }
    [[nodiscard]] inline std::string_view getVendor() override { return m_gpuVendor; }
    [[nodiscard]] inline std::string_view getModel() override { return m_gpuModel; }
    [[nodiscard]] inline std::string_view getVersion() override { return m_gpuDriverVersion; }

    // TODO? (how)
    [[nodiscard]] inline int getVRAMTotal() override { return 0; }
    [[nodiscard]] inline int getVRAMRemaining() override { return 0; }

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
    // texture binding state per texture unit (set by SDLGPUImage/SDLGPURenderTarget::bind/unbind)
    // atomic because releaseTexture/releaseSampler may be called from loader threads
    [[nodiscard]] inline SDL_GPUTexture *getBoundTexture(u32 unit) const {
        return m_boundTextures[unit].load(std::memory_order_relaxed);
    }
    [[nodiscard]] inline SDL_GPUSampler *getBoundSampler(u32 unit) const {
        return m_boundSamplers[unit].load(std::memory_order_relaxed);
    }
    inline void setBoundTexture(u32 unit, SDL_GPUTexture *tex) {
        m_boundTextures[unit].store(tex, std::memory_order_relaxed);
    }
    inline void setBoundSampler(u32 unit, SDL_GPUSampler *sampler) {
        m_boundSamplers[unit].store(sampler, std::memory_order_relaxed);
    }

    // release a texture/sampler and clear the bound state if it matches
    void releaseTexture(SDL_GPUTexture *&tex);
    void releaseSampler(SDL_GPUSampler *&sampler);

    // render target support
    void pushRenderTarget(SDL_GPUTexture *colorTex, SDL_GPUTexture *depthTex, bool doClear, Color clearCol,
                          SDL_GPUTexture *resolveTex = nullptr, SDLGPUSampleCount sampleCount = 0);
    void popRenderTarget();

    // record a baked VAO draw into the deferred command list
    void recordBakedDraw(SDL_GPUBuffer *buffer, u32 firstVertex, u32 vertexCount, DrawPrimitive primitive);

    // shader switching
    void setActiveShader(SDLGPUShader *shader);
    [[nodiscard]] inline SDLGPUShader *getActiveShader() const { return m_activeShader; }
    void clearActiveShader(SDLGPUShader *shader);

    // shared upload transfer buffer pool (any thread may acquire/release, acquired buffers are always idle)
    // outAllocSize receives the actual allocation size of the returned buffer (for passing back to release)
    SDL_GPUTransferBuffer *acquireUploadTransferBuffer(u32 minSize, u32 &outAllocSize);
    // fence: acquired when submitting the command buffer that reads from buf (null if nothing was submitted). the
    // pool takes ownership and parks the buffer until the fence signals, so the caller never waits on the GPU.
    // buf and size are zeroed
    void releaseUploadTransferBuffer(SDL_GPUTransferBuffer *&buf, u32 &size, SDL_GPUFence *fence = nullptr);

    // 4 == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
    static constexpr SDLGPUTextureFormat DEFAULT_TEXTURE_FORMAT{4};

    void setTexturing(bool enabled, bool force = false) override;

   protected:
    std::vector<u8> getScreenshot(bool withAlpha) override;
    bool init() override;
    void onTransformUpdate() override;

   private:
    void createPipeline();
    void rebuildPipeline();
    void flushDrawCommands();
    void addRenderPassBoundary();
    struct Bounds;
    void recordDraw(SDL_GPUBuffer *bakedBuffer, u32 first, u32 count, bool textured, const Bounds *ndcBounds);
    void resetPendingDraws();
    bool createDepthTexture(u32 width, u32 height);

    // default shader fragment uniforms (misc = texturing/inversion/colorless flags, col = tint), written on change
    void applyDefaultFragState(bool texturing, bool colorless, Color col);

    void initSmoothClipShader();
    void onFramecountNumChanged(float maxFramesInFlight);

    static SDLGPUPrimitiveType primitiveToSDLGPUPrimitive(DrawPrimitive prim);

    SDL_Window *m_window;
    SDL_GPUDevice *m_device{nullptr};

    // cached properties for renderer queries
    SDL_PropertiesID m_devProps{0};

    std::string m_rendererName{"SDLGPUInterface"};
    std::string m_gpuVendor{"?"};
    std::string m_gpuModel{"?"};
    std::string m_gpuDriverVersion{"?"};

    // shaders
    std::unique_ptr<SDLGPUShader> m_defaultShader{nullptr};
    SDLGPUShader *m_activeShader{nullptr};  // always points to default or custom
    std::unique_ptr<SDLGPUShader> m_smoothClipShader{nullptr};

    // pipeline cache (keyed by state)
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

    // immediate vertices are written straight into the mapped transfer buffer (up to MAX_STAGING_VERTS per flush),
    // flushDrawCommands() unmaps it and uploads m_stagingCount vertices into m_vertexBuffer
    static constexpr uSz MAX_STAGING_VERTS{(8ULL * 1024 * 1024) / sizeof(SDLGPUSimpleVertex)};
    SDLGPUSimpleVertex *m_stagingMapped{nullptr};
    u32 m_stagingCount{0};

    SDL_GPUBuffer *m_vertexBuffer{nullptr};
    SDL_GPUTransferBuffer *m_transferBuffer{nullptr};

    // immediate draws are indexed (absolute u32 into m_vertexBuffer), which keeps the vertex count of strips, fans and
    // quads as they become triangle lists and lets a draw join an earlier command without moving its vertices: a
    // command owns a chain of chunks of m_indices that flushDrawCommands() lays out back to back in m_indexBuffer
    static constexpr uSz MAX_STAGING_INDICES{MAX_STAGING_VERTS * 3};  // strips and fans need 3 indices per vertex
    static constexpr u32 NO_CHUNK{~0u};
    struct IndexChunk {
        u32 first;  // into m_indices
        u32 count;
        u32 next;  // into m_indexChunks
    };
    std::unique_ptr<u32[]> m_indices;
    u32 m_indexCount{0};
    std::vector<IndexChunk> m_indexChunks;

    SDL_GPUBuffer *m_indexBuffer{nullptr};
    SDL_GPUTransferBuffer *m_indexTransferBuffer{nullptr};

    // how many commands back recordDraw() looks for one that a draw can join
    static constexpr uSz REORDER_WINDOW{8};

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

    // pixel-space footprint of a draw or command, see recordDraw()
    struct Bounds {
        vec2 min;
        vec2 max;

        [[nodiscard]] bool overlaps(const Bounds &o, float pad) const {
            return min.x <= o.max.x + pad && o.min.x <= max.x + pad && min.y <= o.max.y + pad && o.min.y <= max.y + pad;
        }
        void add(const Bounds &o) {
            min = {std::min(min.x, o.min.x), std::min(min.y, o.min.y)};
            max = {std::max(max.x, o.max.x), std::max(max.y, o.max.y)};
        }
    };

    struct TextureBinding {
        SDL_GPUTexture *texture;
        SDL_GPUSampler *sampler;

        [[nodiscard]] bool operator==(const TextureBinding &) const = default;
    };

    // deferred draw batching
    struct DrawCommand {
        // vertex range for baked draws. immediate draws reference their index chunks instead and get their index
        // range assigned here by flushDrawCommands() once the chunks are laid out
        u32 first;
        u32 count;
        u32 firstChunk;
        u32 lastChunk;

        SDL_GPUBuffer *bakedBuffer;  // nullptr for immediate (uses the shared staging buffers)

        SDL_GPUGraphicsPipeline *pipeline;

        // one per texture unit the shader samples, null after those
        std::array<TextureBinding, MAX_TEXTURE_UNITS> textures;

        // viewport
        Viewport viewport;

        // scissor
        Scissor scissor;

        // union of the draws in the command, only meaningful when hasBounds
        Bounds bounds;

        // uniform snapshot range (into m_uniformSnapshots) this draw needs pushed
        u32 uniformFirst;
        u8 uniformCount;

        // stencil
        u8 stencilRef;

        // scissor state
        bool scissorEnabled;

        bool hasBounds;
    };
    std::vector<DrawCommand> m_pendingDraws;

    // uniform block snapshots referenced by the draw commands. recordDraw() reuses the previous range while the active
    // shader's uniform generation is unchanged, so runs of draws with identical uniforms share one snapshot
    struct UniformSnapshot {
        u32 dataOffset;  // into m_uniformData
        u16 size;
        u8 slot;
        bool isVertex;  // true=vertex, false=fragment
    };
    std::vector<UniformSnapshot> m_uniformSnapshots;
    std::vector<u8> m_uniformData;
    SDLGPUShader *m_lastSnapshotShader{nullptr};
    u32 m_lastSnapshotGeneration{0};
    u32 m_lastSnapshotFirst{0};
    u8 m_lastSnapshotCount{0};

    // pipeline state that requires rebuild
    int m_stencilState{0};  // 0=off, 1=writing mask, 2=testing
    SDLGPUPrimitiveType m_currentPrimitiveType;
    bool m_depthTestEnabled{false};
    bool m_depthWriteEnabled{false};
    bool m_scissorEnabled{false};
    bool m_cullingEnabled{false};
    bool m_wireframeEnabled{false};

    u8 m_colorWriteMask{(1u << 0) | (1u << 1) | (1u << 2) | (1u << 3)};
    bool m_isPipelineDirty{true};

    // state
    Viewport m_viewport{.pos = {0.f, 0.f}, .size = {1.f, 1.f}};

    int m_maxFrameLatency{Env::cfg(OS::MAC) ? 2 : 1};
    bool m_colorInversion{false};
    bool m_vsyncEnabled{false};

    // default shader fragment uniforms as last written. immediate draws keep them constant (always sampling, white
    // col, m_color goes into the vertices instead) so that consecutive draws can merge, see drawVAO()
    struct DefaultFragState {
        bool texturing;
        bool inversion;
        bool colorless;
        Color col;
        [[nodiscard]] bool operator==(const DefaultFragState &) const = default;
    };
    DefaultFragState m_defaultFragState{.texturing = false, .inversion = false, .colorless = false, .col = 0};
    // whether the default shader's mvp currently holds the identity (immediate draws are pre-transformed on the cpu)
    bool m_defaultMVPIsIdentity{false};

    // cached present mode support (queried once at init)
    bool m_supportsSDRComposition{false};
    bool m_supportsImmediate{false};
    bool m_supportsMailbox{false};

    // 1x1 textures for draws without a real texture: transparent black when a texture is expected but none is bound
    // (entirely transparent images), white for untextured immediate draws (the default shader always samples)
    SDL_GPUTexture *m_dummyTexture{nullptr};
    SDL_GPUTexture *m_whiteTexture{nullptr};
    SDL_GPUSampler *m_dummySampler{nullptr};

    // currently bound texture+sampler per texture unit (set by SDLGPUImage/SDLGPURenderTarget)
    // atomic: releaseTexture/releaseSampler may CAS from loader threads
    std::array<std::atomic<SDL_GPUTexture *>, MAX_TEXTURE_UNITS> m_boundTextures{};
    std::array<std::atomic<SDL_GPUSampler *>, MAX_TEXTURE_UNITS> m_boundSamplers{};

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
    static_assert(2 << (POOL_NUM_CLASSES + 1) == UPLOAD_POOL_BUDGET);

    Sync::mutex m_uploadTransferPoolMutex;
    std::array<std::vector<SDL_GPUTransferBuffer *>, POOL_NUM_CLASSES> m_uploadTransferPool{};
    u32 m_uploadTransferPoolBytes{0};

    // released buffers waiting for their upload to finish, swept back into the pool by the next acquire
    struct ParkedUploadBuffer {
        SDL_GPUFence *fence;
        SDL_GPUTransferBuffer *buf;
        u32 size;
    };
    std::vector<ParkedUploadBuffer> m_parkedUploadBuffers;

    // stats
    int m_statsNumDrawCalls{0};
    int m_statsNumUniformUploads{0};
    int m_statsNumVertexUploads{0};
    int m_statsNumIndexUploads{0};
    int m_statsNumRenderPasses{0};

    // headless mode cache
    bool m_isHeadless{false};
    std::vector<SDL_GPUFence *> m_headlessFrameFences;  // oldest first, see endScene()
};

#endif

#endif
