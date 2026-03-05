//================ Copyright (c) 2017, PG, All rights reserved. =================//
//
// Purpose:		raw DirectX 11 graphics interface
//
// $NoKeywords: $dx11i
//===============================================================================//

#pragma once
#ifndef DIRECTX11INTERFACE_H
#define DIRECTX11INTERFACE_H
#include "config.h"

#ifdef MCENGINE_FEATURE_DIRECTX11

#include "BaseEnvironment.h"
#include "ModernGraphicsShared.h"

class DirectX11Shader;

struct IDXGIFactory2;
struct IDXGISwapChain1;

#if defined(__GNUC__) || defined(__clang__)
MC_DO_PRAGMA(GCC diagnostic ignored "-Wpragmas")
MC_DO_PRAGMA(GCC diagnostic ignored "-Wextern-c-compat")
MC_DO_PRAGMA(GCC diagnostic push)
#endif

#include "d3d11.h"

#if defined(__GNUC__) || defined(__clang__)
MC_DO_PRAGMA(GCC diagnostic pop)
#endif

namespace dynutils {
using lib_obj = struct lib_obj;
}

// calling convention handling
// libdxvk-native's header is wrong, it's __stdcall internally
#ifdef _WIN32
#define D3D11_CALL WINAPI
#else
#define D3D11_CALL __stdcall
#endif

using D3D11CreateDevice_t = HRESULT D3D11_CALL(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
                                               const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
                                               D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);

class DirectX11Interface final : public ModernGraphicsShared {
    NOCOPY_NOMOVE(DirectX11Interface)
   public:
    struct SimpleVertex {
        vec3 pos;
        vec4 col;
        vec2 tex;
    };

   public:
    DirectX11Interface(HWND hwnd);
    ~DirectX11Interface() override;

    // scene
    void beginScene() override;
    void endScene() override;

    // depth buffer
    void clearDepthBuffer() override;

    // color
    void setColor(Color color) override;
    void setAlpha(float alpha) override;

    // 2d primitive drawing (implemented in ModernGraphicsShared)
    void drawPixel(int x, int y) override;  // TODO: add shared implementation

    // 2d resource drawing
    void drawImage(const Image *image, AnchorPoint anchor = AnchorPoint::CENTER, float edgeSoftness = 0.0f,
                   McRect clipRect = {}) final;
    void drawString(McFont *font, const UString &text, std::optional<TextShadow> shadow = std::nullopt) override;

    // 3d type drawing
    void drawVAO(VertexArrayObject *vao) override;

    // 2d clipping
    void setClipRect(McRect clipRect) override;
    void pushClipRect(McRect clipRect) override;
    void popClipRect() override;

    // viewport modification
    void pushViewport() override;
    void setViewport(int x, int y, int width, int height) override;
    void popViewport() override;

    // TODO (?): unused currently
    [[deprecated("not implemented")]] void pushStencil() override { ; }
    [[deprecated("not implemented")]] void fillStencil(bool /*inside*/) override { ; }
    [[deprecated("not implemented")]] void popStencil() override { ; }

    // renderer settings
    void setClipping(bool enabled) override;
    [[deprecated("not implemented")]] void setAlphaTesting(bool enabled) override;
    [[deprecated("not implemented")]] void setAlphaTestFunc(DrawCompareFunc alphaFunc, float ref) override;
    void setBlending(bool enabled) override;
    void setBlendMode(DrawBlendMode blendMode) override;
    void setDepthBuffer(bool enabled) override;
    void setCulling(bool culling) override;
    void setColorWriting(bool r, bool g, bool b, bool a) final;
    void setColorInversion(bool enabled) final;
    void setAntialiasing(bool aa) override;
    void setWireframe(bool enabled) override;

    // renderer actions
    void flush() override;

    // renderer info
    inline const char *getName() const override { return "DirectX11"; }
    vec2 getResolution() const override { return this->vResolution; }
    UString getVendor() override;
    UString getModel() override;
    UString getVersion() override;
    int getVRAMTotal() override;
    int getVRAMRemaining() override;

    // device settings
    void setVSync(bool vsync) override;

    // callbacks
    void onResolutionChange(vec2 newResolution) override;
    void onRestored() override;

    // factory
    Image *createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) override;
    Image *createImage(int width, int height, bool mipmapped, bool keepInSystemMemory) override;
    RenderTarget *createRenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType) override;
    Shader *createShaderFromFile(std::string vertexShaderFilePath, std::string fragmentShaderFilePath) override;
    Shader *createShaderFromSource(std::string vertexShader, std::string fragmentShader) override;
    VertexArrayObject *createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                               bool keepInSystemMemory) override;

    // ILLEGAL:
    void setActiveShader(DirectX11Shader *shader) { this->activeShader = shader; }
    inline ID3D11Device *getDevice() const { return this->device; }
    inline ID3D11DeviceContext *getDeviceContext() const { return this->deviceContext; }
    inline DirectX11Shader *getShaderGeneric() const { return this->shaderTexturedGeneric; }
    inline DirectX11Shader *getActiveShader() const { return this->activeShader; }
    void setTexturing(bool enabled, bool force = false) override;

   protected:
    bool init() final;

    void onTransformUpdate() final;
    std::vector<u8> getScreenshot(bool withAlpha) override;

    // frame latency
    void onSyncBehaviorChanged(const float newValue);
    void onFramecountNumChanged(const float newValue);

   private:
    static int primitiveToDirectX(DrawPrimitive primitive);
    [[deprecated("not implemented")]] static int compareFuncToDirectX(DrawCompareFunc compareFunc);

    // clipping for drawImage
    void initSmoothClipShader();

    DXGI_MODE_DESC queryCurrentSwapchainDesc() const;

    // batched vertex upload and drawing
    void uploadAndDrawVertexBatch(D3D_PRIMITIVE_TOPOLOGY topology);

   private:
    // state and initialization settings
    static constexpr size_t MAX_VERTEX_BUFFER_VERTS{16384};

    D3D11_BUFFER_DESC vertexBufferDesc{
        .ByteWidth = static_cast<UINT>(sizeof(SimpleVertex) * MAX_VERTEX_BUFFER_VERTS),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_VERTEX_BUFFER,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,  // due to USAGE_DYNAMIC
        .MiscFlags = 0,
        .StructureByteStride = 0,
    };

    static constexpr const D3D11_RENDER_TARGET_BLEND_DESC INIT_RTBLEND{
        .BlendEnable = true,
        .SrcBlend = D3D11_BLEND_SRC_ALPHA,
        .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
        .BlendOp = D3D11_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA,
        .DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
        .BlendOpAlpha = D3D11_BLEND_OP_ADD,
        .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
    };

    D3D11_BLEND_DESC blendDesc{.AlphaToCoverageEnable = FALSE,
                               .IndependentBlendEnable = FALSE,
                               .RenderTarget{INIT_RTBLEND, INIT_RTBLEND, INIT_RTBLEND, INIT_RTBLEND, INIT_RTBLEND,
                                             INIT_RTBLEND, INIT_RTBLEND, INIT_RTBLEND}};

    D3D11_DEPTH_STENCIL_DESC depthStencilDesc{.DepthEnable = FALSE,
                                              .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO,
                                              .DepthFunc = D3D11_COMPARISON_LESS,
                                              .StencilEnable = FALSE,
                                              .StencilReadMask = 0,   // see OMSetDepthStencilState()
                                              .StencilWriteMask = 0,  // see OMSetDepthStencilState()
                                              .FrontFace{
                                                  .StencilFailOp = D3D11_STENCIL_OP_ZERO,
                                                  .StencilDepthFailOp = D3D11_STENCIL_OP_ZERO,
                                                  .StencilPassOp = D3D11_STENCIL_OP_ZERO,
                                                  .StencilFunc = D3D11_COMPARISON_ALWAYS,
                                              },
                                              .BackFace{
                                                  .StencilFailOp = D3D11_STENCIL_OP_ZERO,
                                                  .StencilDepthFailOp = D3D11_STENCIL_OP_ZERO,
                                                  .StencilPassOp = D3D11_STENCIL_OP_ZERO,
                                                  .StencilFunc = D3D11_COMPARISON_ALWAYS,
                                              }};

    D3D11_RASTERIZER_DESC rasterizerDesc{
        .FillMode = D3D11_FILL_SOLID,
        .CullMode = D3D11_CULL_NONE,
        .FrontCounterClockwise = TRUE,
        .DepthBias = D3D11_DEFAULT_DEPTH_BIAS,
        .DepthBiasClamp = D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
        .SlopeScaledDepthBias = D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
        .DepthClipEnable = TRUE,  // (clipping, not depth buffer!)
        .ScissorEnable = FALSE,
        .MultisampleEnable = FALSE,
        .AntialiasedLineEnable = FALSE,
    };

    // backbuffer descriptor
    DXGI_MODE_DESC swapChainModeDesc{
        .Width = 0,  // to be initialized after swapchain creation
        .Height = 0,
        .RefreshRate = {.Numerator = 0, .Denominator = 1},
        // NOTE: DXGI_FORMAT_R8G8B8A8_UNORM has the broadest compatibility range
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
        .Scaling = DXGI_MODE_SCALING_CENTERED,
    };

   private:
    // renderer
    vec2 vResolution{};  // to be initialized after swapchain creation

    // device context
    HWND hwnd{};

    // swapchain
    IDXGISwapChain1 *swapChain{nullptr};
    IDXGIFactory2 *dxgiFactory{nullptr};
    IDXGIAdapter *dxgiAdapter{nullptr};
    IDXGIDevice *dxgiDevice{nullptr};
    IDXGIDevice1 *dxgiDevice1{nullptr};  // may remain NULL

    // d3d
    ID3D11Device *device{nullptr};
    ID3D11DeviceContext *deviceContext{nullptr};
    ID3D11RenderTargetView *frameBuffer{nullptr};
    ID3D11Texture2D *frameBufferDepthStencilTexture{nullptr};
    ID3D11DepthStencilView *frameBufferDepthStencilView{nullptr};

    ID3D11RasterizerState *rasterizerState{nullptr};
    ID3D11DepthStencilState *depthStencilState{nullptr};
    ID3D11BlendState *blendState{nullptr};
    DirectX11Shader *shaderTexturedGeneric{nullptr};

    std::vector<SimpleVertex> vertices;
    ID3D11Buffer *vertexBuffer{nullptr};
    size_t iVertexBufferNumVertexOffsetCounter{0};

    // clipping
    std::vector<McRect> clipRectStack;

    // clipping for drawImage
    std::unique_ptr<Shader> smoothClipShader{nullptr};

    // persistent vars
    DirectX11Shader *activeShader{nullptr};
    Color color{(Color)-1};
    bool bVSync{false};
    bool bColorInversion{false};
    bool bTexturingEnabled{false};
    bool bWasMinimized{false};  // hack?
    const bool bFlipping;
    const UINT swapChainCreateFlags;

    // frame latency
    bool bFrameLatencyDisabled{false};
    unsigned int iMaxFrameLatency{1};

    // stats
    int iStatsNumDrawCalls{0};

    // dynloading
    static dynutils::lib_obj *s_d3d11Handle;
    static D3D11CreateDevice_t *s_d3dCreateDeviceFunc;

    static bool loadLibs();

    struct OcclusionListener;
    friend struct OcclusionListener;
    std::unique_ptr<OcclusionListener> minimizeListener;
};

#endif

#endif
