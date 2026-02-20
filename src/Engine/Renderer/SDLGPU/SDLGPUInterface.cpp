//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		raw SDL_gpu graphics interface
//
// $NoKeywords: $sdlgpui
//===============================================================================//
#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU

#include <SDL3/SDL_gpu.h>

#include "SDLGPUInterface.h"

#include "SDLGPUImage.h"
#include "SDLGPURenderTarget.h"
#include "SDLGPUShader.h"
#include "SDLGPUVertexArrayObject.h"

#include "MakeDelegateWrapper.h"
#include "Camera.h"
#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"
#include "Font.h"
#include "UString.h"
#include "VertexArrayObject.h"
#include "Environment.h"
#include "SString.h"
#include "ContainerRanges.h"

#include "binary_embed.h"

#include <cstring>

#define DEBUG_SDLGPU false

const SDLGPUTextureFormat SDLGPUInterface::DEFAULT_TEXTURE_FORMAT{SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM};

SDLGPUInterface::SDLGPUInterface(SDL_Window *window)
    : Graphics(), m_window(window), m_currentPrimitiveType(SDL_GPU_PRIMITIVETYPE_TRIANGLELIST) {}

SDLGPUInterface::~SDLGPUInterface() {
    cv::r_sync_max_frames.reset();  // release callback

    if(m_device) {
        SDL_WaitForGPUIdle(m_device);

        if(m_cmdBuf) {
            SDL_CancelGPUCommandBuffer(m_cmdBuf);
            m_cmdBuf = nullptr;
        }

        for(auto &[key, pipeline] : m_pipelineCache) SDL_ReleaseGPUGraphicsPipeline(m_device, pipeline);
        m_pipelineCache.clear();
        if(m_vertexBuffer) SDL_ReleaseGPUBuffer(m_device, m_vertexBuffer);
        if(m_transferBuffer) SDL_ReleaseGPUTransferBuffer(m_device, m_transferBuffer);
        if(m_depthTexture) SDL_ReleaseGPUTexture(m_device, m_depthTexture);
        if(m_backbuffer) SDL_ReleaseGPUTexture(m_device, m_backbuffer);
        if(m_dummySampler) SDL_ReleaseGPUSampler(m_device, m_dummySampler);
        if(m_dummyTexture) SDL_ReleaseGPUTexture(m_device, m_dummyTexture);
        m_smoothClipShader.reset();
        m_defaultShader.reset();
        m_activeShader = nullptr;

        SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
        SDL_DestroyGPUDevice(m_device);
    }
}

bool SDLGPUInterface::init() {
    std::string drivers;
    {
        const int numDrivers = SDL_GetNumGPUDrivers();
        for(int i = 0; i < numDrivers; ++i) {
            drivers += fmt::format("{} ", SDL_GetGPUDriver(i));
        }
        if(!drivers.empty()) {
            drivers.pop_back();
        }
        debugLog("SDLGPUInterface: Available drivers: {}", drivers);
    }

    // create GPU device
    // on windows, try D3D12 (DXIL) first, then fall back to vulkan (SPIRV)
    std::vector<std::pair<std::string, unsigned int>> initOrder;
    const bool vkAvailable = drivers.contains("vulkan");
    const bool d3dAvailable = drivers.contains("direct3d12");
    if(d3dAvailable) {
        initOrder.emplace_back("D3D12", SDL_GPU_SHADERFORMAT_DXIL);
    }
    if(vkAvailable) {
        initOrder.emplace_back("Vulkan", SDL_GPU_SHADERFORMAT_SPIRV);
    }
    if(initOrder.empty()) {
        debugLog("SDLGPUInterface: No compatible drivers available!");
        return false;
    }

    if constexpr(Env::cfg(OS::WINDOWS)) {
        if(vkAvailable && d3dAvailable) {
            auto args = env->getLaunchArgs();
            std::string argvalLower;
            if(args["-sdlgpu"].has_value()) {
                argvalLower = SString::to_lower(args["-sdlgpu"].value());
            } else if(args["-gpu"].has_value()) {
                argvalLower = SString::to_lower(args["-gpu"].value());
            }
            if(argvalLower.contains("vk") || argvalLower.contains("vulkan")) {
                initOrder[0].swap(initOrder[1]);
            }
        }
    }

    if(!(m_device = SDL_CreateGPUDevice(initOrder[0].second, DEBUG_SDLGPU, nullptr))) {
        if(initOrder.size() > 1) {
            debugLog("SDLGPUInterface: {} unavailable ({}), trying {}...", initOrder[0].first, SDL_GetError(),
                     initOrder[1].first);
            if(!(m_device = SDL_CreateGPUDevice(initOrder[1].second, DEBUG_SDLGPU, nullptr))) {
                debugLog("SDLGPUInterface: Failed to create GPU device: {}", SDL_GetError());
                return false;
            }
        } else {
            debugLog("SDLGPUInterface: Failed to create GPU device: {}", SDL_GetError());
            return false;
        }
    }

    const std::string driver = SDL_GetGPUDeviceDriver(m_device);
    m_rendererName = fmt::format("SDLGPUInterface ({})", driver);
    m_devProps = SDL_GetGPUDeviceProperties(m_device);

    debugLog("SDLGPUInterface: GPU driver: {}", driver);

    // claim window
    if(!SDL_ClaimWindowForGPUDevice(m_device, m_window)) {
        debugLog("SDLGPUInterface: Failed to claim window: {}", SDL_GetError());
        return false;
    }

    // this can be B8G8R8A or R8G8B8A, we can't specify it
    SDLGPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(m_device, m_window);

    // cache supported present modes
    m_bSupportsSDRComposition =
        SDL_WindowSupportsGPUSwapchainComposition(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR);
    if(m_bSupportsSDRComposition) {
        m_bSupportsImmediate = SDL_WindowSupportsGPUPresentMode(m_device, m_window, SDL_GPU_PRESENTMODE_IMMEDIATE);
        m_bSupportsMailbox = SDL_WindowSupportsGPUPresentMode(m_device, m_window, SDL_GPU_PRESENTMODE_MAILBOX);
    } else {
        debugLog("SDLGPUInterface: swapchain composition not supported: {}", SDL_GetError());
    }

    if constexpr(Env::cfg(BUILD::DEBUG)) {
        debugLog(
            "SDLGPUInterface: swapchain format {} supports SDR comp.: {} supports immediate: {} supports mailbox: {}",
            swapchainFormat, m_bSupportsSDRComposition, m_bSupportsImmediate, m_bSupportsMailbox);
    }

    // create default shader
    {
        const auto vshPack = std::string(reinterpret_cast<const char *>(SDLGPU_default_vsh),
                                         static_cast<size_t>(SDLGPU_default_vsh_size()));
        const auto fshPack = std::string(reinterpret_cast<const char *>(SDLGPU_default_fsh),
                                         static_cast<size_t>(SDLGPU_default_fsh_size()));

        m_defaultShader.reset(static_cast<SDLGPUShader *>(createShaderFromSource(vshPack, fshPack)));
        m_defaultShader->load();

        if(!m_defaultShader->isReady()) {
            debugLog("SDLGPUInterface: Failed to create default shaders");
            return false;
        }

        m_activeShader = m_defaultShader.get();
    }

    // create vertex buffer
    SDL_GPUBufferCreateInfo bufInfo{
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size = static_cast<Uint32>(sizeof(SDLGPUSimpleVertex) * MAX_STAGING_VERTS),
        .props = 0,
    };
    m_vertexBuffer = SDL_CreateGPUBuffer(m_device, &bufInfo);
    if(!m_vertexBuffer) {
        debugLog("SDLGPUInterface: Failed to create vertex buffer: {}", SDL_GetError());
        return false;
    }

    // create transfer buffer for uploading vertices
    SDL_GPUTransferBufferCreateInfo tbInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<Uint32>(sizeof(SDLGPUSimpleVertex) * MAX_STAGING_VERTS),
        .props = 0,
    };
    m_transferBuffer = SDL_CreateGPUTransferBuffer(m_device, &tbInfo);
    if(!m_transferBuffer) {
        debugLog("SDLGPUInterface: Failed to create transfer buffer: {}", SDL_GetError());
        return false;
    }

    // create 1x1 transparent black dummy texture (SDL_gpu requires all sampler bindings to be satisfied even when unused)
    {
        SDL_GPUTextureCreateInfo texInfo{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = (SDL_GPUTextureFormat)DEFAULT_TEXTURE_FORMAT,
            .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = 1,
            .height = 1,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };
        m_dummyTexture = SDL_CreateGPUTexture(m_device, &texInfo);

        SDL_GPUSamplerCreateInfo sampInfo{};
        sampInfo.min_filter = SDL_GPU_FILTER_NEAREST;
        sampInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
        m_dummySampler = SDL_CreateGPUSampler(m_device, &sampInfo);

        if(m_dummyTexture && m_dummySampler) {
            // upload 1x1 transparent black pixel
            SDL_GPUTransferBufferCreateInfo dummyTbInfo{
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size = 4,
                .props = 0,
            };
            auto *tb = SDL_CreateGPUTransferBuffer(m_device, &dummyTbInfo);
            if(tb) {
                void *mapped = SDL_MapGPUTransferBuffer(m_device, tb, false);
                if(mapped) {
                    const u32 noColor = 0x00000000;
                    std::memcpy(mapped, &noColor, 4);
                    SDL_UnmapGPUTransferBuffer(m_device, tb);
                }

                auto *cmd = SDL_AcquireGPUCommandBuffer(m_device);
                if(cmd) {
                    auto *cp = SDL_BeginGPUCopyPass(cmd);
                    if(cp) {
                        SDL_GPUTextureTransferInfo src{};
                        src.transfer_buffer = tb;
                        src.offset = 0;

                        SDL_GPUTextureRegion dst{};
                        dst.texture = m_dummyTexture;
                        dst.w = 1;
                        dst.h = 1;
                        dst.d = 1;

                        SDL_UploadToGPUTexture(cp, &src, &dst, false);
                        SDL_EndGPUCopyPass(cp);
                    }
                    SDL_SubmitGPUCommandBuffer(cmd);
                }

                SDL_ReleaseGPUTransferBuffer(m_device, tb);
            }
        }
    }

    // create initial backbuffer and viewport
    onResolutionChange(env->getWindowSize());

    // create initial pipeline
    createPipeline();
    m_bPipelineDirty = false;

    if(!SDL_SetGPUAllowedFramesInFlight(m_device, m_iMaxFrameLatency)) {
        debugLog("SDLGPUInterface: Failed to set max frames in flight to {}: {}", m_iMaxFrameLatency, SDL_GetError());
        // it's default to 2 in SDL, so if we failed to change it, set it to 2
        m_iMaxFrameLatency = 2;
    }

    cv::r_sync_max_frames.setDefaultDouble(m_iMaxFrameLatency);
    cv::r_sync_max_frames.setValue(m_iMaxFrameLatency);
    cv::r_sync_max_frames.setCallback(SA::MakeDelegate<&SDLGPUInterface::onFramecountNumChanged>(this));

    return true;
}

void SDLGPUInterface::createPipeline() {
    PipelineKey key{
        .vertexShader = m_activeShader->getVertexShader(),
        .fragmentShader = m_activeShader->getFragmentShader(),
        .primitiveType = m_currentPrimitiveType,
        .blendMode = this->currentBlendMode,
        .sampleCount = m_curRTState.sampleCount,
        .stencilState = (u8)m_iStencilState,
        .blendingEnabled = this->bBlendingEnabled,
        .depthTestEnabled = m_bDepthTestEnabled,
        .depthWriteEnabled = m_bDepthWriteEnabled,
        .wireframe = m_bWireframe,
        .cullingEnabled = m_bCullingEnabled,
        .colorWriteMask = (u8)((m_bColorWriteR ? 1 : 0) | (m_bColorWriteG ? 2 : 0) | (m_bColorWriteB ? 4 : 0) |
                               (m_bColorWriteA ? 8 : 0)),
    };

    auto it = m_pipelineCache.find(key);
    if(it != m_pipelineCache.end()) {
        m_currentPipeline = it->second;
        return;
    }

    // vertex layout: pos(vec3) + col(vec4) + tex(vec2) = 9 floats = 36 bytes
    SDL_GPUVertexAttribute vertexAttributes[3] = {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = 0},
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = sizeof(vec3)},
        {.location = 2,
         .buffer_slot = 0,
         .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
         .offset = sizeof(vec3) + sizeof(vec4)},
    };

    SDL_GPUVertexBufferDescription vertexBufferDesc{
        .slot = 0,
        .pitch = sizeof(SDLGPUSimpleVertex),
        .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
        .instance_step_rate = 0,
    };

    SDL_GPUVertexInputState vertexInputState{
        .vertex_buffer_descriptions = &vertexBufferDesc,
        .num_vertex_buffers = 1,
        .vertex_attributes = vertexAttributes,
        .num_vertex_attributes = 3,
    };

    // blend state
    SDL_GPUColorTargetBlendState blendState{};
    blendState.enable_blend = this->bBlendingEnabled;
    blendState.color_write_mask =
        (m_bColorWriteR ? SDL_GPU_COLORCOMPONENT_R : 0) | (m_bColorWriteG ? SDL_GPU_COLORCOMPONENT_G : 0) |
        (m_bColorWriteB ? SDL_GPU_COLORCOMPONENT_B : 0) | (m_bColorWriteA ? SDL_GPU_COLORCOMPONENT_A : 0);

    if(this->bBlendingEnabled) {
        switch(this->currentBlendMode) {
            case DrawBlendMode::ALPHA:
                blendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
                blendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                break;
            case DrawBlendMode::ADDITIVE:
                blendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                blendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
                blendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                blendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                break;
            case DrawBlendMode::PREMUL_ALPHA:
                blendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                blendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
                blendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                blendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                break;
            case DrawBlendMode::PREMUL_COLOR:
                blendState.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                blendState.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blendState.color_blend_op = SDL_GPU_BLENDOP_ADD;
                blendState.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                blendState.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                blendState.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                break;
        }
    }

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = (SDL_GPUTextureFormat)DEFAULT_TEXTURE_FORMAT;
    colorTarget.blend_state = blendState;

    SDL_GPUGraphicsPipelineTargetInfo targetInfo{};
    targetInfo.color_target_descriptions = &colorTarget;
    targetInfo.num_color_targets = 1;
    targetInfo.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    targetInfo.has_depth_stencil_target = true;

    SDL_GPURasterizerState rasterizerState{};
    rasterizerState.fill_mode = m_bWireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    rasterizerState.cull_mode = m_bCullingEnabled ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
    rasterizerState.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

    SDL_GPUMultisampleState multisampleState{};
    multisampleState.sample_count = (SDL_GPUSampleCount)key.sampleCount;

    SDL_GPUDepthStencilState depthStencilState{};
    depthStencilState.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    depthStencilState.enable_depth_test = m_bDepthTestEnabled;
    depthStencilState.enable_depth_write = m_bDepthWriteEnabled;

    if(m_iStencilState == 1) {
        // writing mask: always pass, replace stencil with 1
        depthStencilState.enable_stencil_test = true;
        depthStencilState.front_stencil_state = {
            .fail_op = SDL_GPU_STENCILOP_REPLACE,
            .pass_op = SDL_GPU_STENCILOP_REPLACE,
            .depth_fail_op = SDL_GPU_STENCILOP_REPLACE,
            .compare_op = SDL_GPU_COMPAREOP_ALWAYS,
        };
        depthStencilState.back_stencil_state = depthStencilState.front_stencil_state;
        depthStencilState.compare_mask = 0xFF;
        depthStencilState.write_mask = 0xFF;
    } else if(m_iStencilState == 2) {
        // test inside: draw where stencil != 0
        depthStencilState.enable_stencil_test = true;
        depthStencilState.front_stencil_state = {
            .fail_op = SDL_GPU_STENCILOP_KEEP,
            .pass_op = SDL_GPU_STENCILOP_KEEP,
            .depth_fail_op = SDL_GPU_STENCILOP_KEEP,
            .compare_op = SDL_GPU_COMPAREOP_NOT_EQUAL,
        };
        depthStencilState.back_stencil_state = depthStencilState.front_stencil_state;
        depthStencilState.compare_mask = 0xFF;
        depthStencilState.write_mask = 0x00;
    } else if(m_iStencilState == 3) {
        // test outside: draw where stencil == 0
        depthStencilState.enable_stencil_test = true;
        depthStencilState.front_stencil_state = {
            .fail_op = SDL_GPU_STENCILOP_KEEP,
            .pass_op = SDL_GPU_STENCILOP_KEEP,
            .depth_fail_op = SDL_GPU_STENCILOP_KEEP,
            .compare_op = SDL_GPU_COMPAREOP_EQUAL,
        };
        depthStencilState.back_stencil_state = depthStencilState.front_stencil_state;
        depthStencilState.compare_mask = 0xFF;
        depthStencilState.write_mask = 0x00;
    }

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = key.vertexShader;
    pipelineInfo.fragment_shader = key.fragmentShader;
    pipelineInfo.vertex_input_state = vertexInputState;
    pipelineInfo.primitive_type = (SDL_GPUPrimitiveType)m_currentPrimitiveType;
    pipelineInfo.rasterizer_state = rasterizerState;
    pipelineInfo.multisample_state = multisampleState;
    pipelineInfo.depth_stencil_state = depthStencilState;
    pipelineInfo.target_info = targetInfo;

    m_currentPipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pipelineInfo);
    if(!m_currentPipeline) {
        debugLog("SDLGPUInterface: Failed to create graphics pipeline: {}", SDL_GetError());
    } else {
        m_pipelineCache.emplace(key, m_currentPipeline);
    }
}

void SDLGPUInterface::rebuildPipeline() {
    if(!m_bPipelineDirty || !m_device) return;
    createPipeline();
    m_bPipelineDirty = false;
}

bool SDLGPUInterface::createDepthTexture(u32 width, u32 height) {
    if(m_depthTexture && m_depthTextureWidth == width && m_depthTextureHeight == height) return true;

    if(m_depthTexture) SDL_ReleaseGPUTexture(m_device, m_depthTexture);

    SDL_GPUTextureCreateInfo depthInfo{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };

    m_depthTexture = SDL_CreateGPUTexture(m_device, &depthInfo);
    if(!m_depthTexture) return false;

    m_depthTextureWidth = width;
    m_depthTextureHeight = height;
    return true;
}

// scene

void SDLGPUInterface::beginScene() {
    // acquire command buffer if we don't have one
    if(!m_cmdBuf && !(m_cmdBuf = SDL_AcquireGPUCommandBuffer(m_device))) {
        debugLog("SDLGPUInterface: Failed to acquire command buffer: {}", SDL_GetError());
        return;
    }

    if(!m_backbuffer) {
        SDL_CancelGPUCommandBuffer(m_cmdBuf);
        m_cmdBuf = nullptr;
        return;
    }

    const u32 w = m_backbufferWidth;
    const u32 h = m_backbufferHeight;

    // make sure depth texture matches
    if(!createDepthTexture(w, h)) {
        SDL_CancelGPUCommandBuffer(m_cmdBuf);
        m_cmdBuf = nullptr;
        return;
    }

    // point at the real backbuffer/depth so flushDrawCommands() needs no fallback
    m_curRTState.colorTarget = m_backbuffer;
    m_curRTState.depthTarget = m_depthTexture;

    // mark that the first flush should clear color and depth
    m_curRTState.pendingClearColor = true;
    m_curRTState.pendingClearDepth = true;
    m_curRTState.pendingClearStencil = false;
    m_curRTState.clearColor = 0xff000000;

    // clear deferred draw state
    m_pendingDraws.clear();
    m_stagingVertices.clear();
    m_renderPassBoundaries.clear();
    addRenderPassBoundary();
    m_renderPass = nullptr;

    // setup default projection (same as DX11: Y-down, depth 0-1)
    Matrix4 defaultProjectionMatrix =
        Camera::buildMatrixOrtho2DDXLH(0, m_viewport.size.x, m_viewport.size.y, 0, -1.0f, 1.0f);

    pushTransform();
    setProjectionMatrix(defaultProjectionMatrix);
    translate(cv::r_globaloffset_x.getFloat(), cv::r_globaloffset_y.getFloat());

    this->updateTransform();

    // reset stats
    m_iStatsNumDrawCalls = 0;
}

void SDLGPUInterface::endScene() {
    if(!m_cmdBuf) return;

    this->popTransform();

    // flush remaining deferred draws
    flushDrawCommands();

    // process screenshots from the backbuffer
    this->processPendingScreenshot();

    // getScreenshot() might have already submitted the command buffer, so get a new one to present
    // TODO: confusing
    if(!m_cmdBuf) m_cmdBuf = SDL_AcquireGPUCommandBuffer(m_device);

    SDL_GPUTexture *swapchainTexture = nullptr;

    // acquire swapchain and blit backbuffer to it for presentation
    u32 sw = 0, sh = 0;
    if(SDL_WaitAndAcquireGPUSwapchainTexture(m_cmdBuf, m_window, &swapchainTexture, &sw, &sh) && swapchainTexture) {
        SDL_GPUBlitInfo blit{};
        blit.source.texture = m_backbuffer;
        blit.source.w = m_backbufferWidth;
        blit.source.h = m_backbufferHeight;
        blit.destination.texture = swapchainTexture;
        blit.destination.w = sw;
        blit.destination.h = sh;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(m_cmdBuf, &blit);
    }

    SDL_SubmitGPUCommandBuffer(m_cmdBuf);

    // acquire a new commandbuffer after submit (see SDL_render_gpu.c)
    m_cmdBuf = SDL_AcquireGPUCommandBuffer(m_device);
}

// depth buffer

void SDLGPUInterface::clearDepthBuffer() {
    if(!m_cmdBuf) return;
    m_curRTState.pendingClearDepth = true;
    addRenderPassBoundary();
}

// color

void SDLGPUInterface::setColor(Color color) {
    if(m_color == color) return;
    m_color = color;

    if(m_bTexturingEnabled) {
        m_defaultShader->setUniform4f("col", color.Rf(), color.Gf(), color.Bf(), color.Af());
    }
}

void SDLGPUInterface::setAlpha(float alpha) {
    if(m_color.Af() == alpha) return;
    m_color.setA(alpha);

    if(m_bTexturingEnabled) {
        m_defaultShader->setUniform4f("col", m_color.Rf(), m_color.Gf(), m_color.Bf(), m_color.Af());
    }
}

// 2d primitive drawing

void SDLGPUInterface::drawPixels(int /*x*/, int /*y*/, int /*width*/, int /*height*/, DrawPixelsType /*type*/,
                                 const void * /*pixels*/) {
    // TODO: implement
}

void SDLGPUInterface::drawPixel(int x, int y) { drawLinef((float)x, (float)y, (float)x + 1.f, (float)y); }

void SDLGPUInterface::drawLinef(float x1, float y1, float x2, float y2) {
    this->updateTransform();
    this->setTexturing(false);

    static VertexArrayObject vao(DrawPrimitive::LINES);
    {
        vao.clear();
        vao.addVertex(x1, y1);
        vao.addVertex(x2, y2);
    }
    this->drawVAO(&vao);
}

void SDLGPUInterface::drawRectf(const RectOptions &opts) {
    this->updateTransform();

    if(opts.lineThickness > 1.0f) {
        this->setTexturing(false);
        const float halfThickness = opts.lineThickness * 0.5f;

        if(opts.withColor) {
            this->setColor(opts.top);
            this->fillRectf(opts.x - halfThickness, opts.y - halfThickness, opts.width + opts.lineThickness,
                            opts.lineThickness);
            this->setColor(opts.bottom);
            this->fillRectf(opts.x - halfThickness, opts.y + opts.height - halfThickness,
                            opts.width + opts.lineThickness, opts.lineThickness);
            this->setColor(opts.left);
            this->fillRectf(opts.x - halfThickness, opts.y + halfThickness, opts.lineThickness,
                            opts.height - opts.lineThickness);
            this->setColor(opts.right);
            this->fillRectf(opts.x + opts.width - halfThickness, opts.y + halfThickness, opts.lineThickness,
                            opts.height - opts.lineThickness);
        } else {
            this->fillRectf(opts.x - halfThickness, opts.y - halfThickness, opts.width + opts.lineThickness,
                            opts.lineThickness);
            this->fillRectf(opts.x - halfThickness, opts.y + opts.height - halfThickness,
                            opts.width + opts.lineThickness, opts.lineThickness);
            this->fillRectf(opts.x - halfThickness, opts.y + halfThickness, opts.lineThickness,
                            opts.height - opts.lineThickness);
            this->fillRectf(opts.x + opts.width - halfThickness, opts.y + halfThickness, opts.lineThickness,
                            opts.height - opts.lineThickness);
        }
    } else {
        if(opts.withColor) {
            this->setColor(opts.top);
            this->drawLinef(opts.x, opts.y, opts.x + opts.width, opts.y);
            this->setColor(opts.left);
            this->drawLinef(opts.x, opts.y, opts.x, opts.y + opts.height);
            this->setColor(opts.bottom);
            this->drawLinef(opts.x, opts.y + opts.height, opts.x + opts.width, opts.y + opts.height + 0.5f);
            this->setColor(opts.right);
            this->drawLinef(opts.x + opts.width, opts.y, opts.x + opts.width, opts.y + opts.height + 0.5f);
        } else {
            this->drawLinef(opts.x, opts.y, opts.x + opts.width, opts.y);
            this->drawLinef(opts.x, opts.y, opts.x, opts.y + opts.height);
            this->drawLinef(opts.x, opts.y + opts.height, opts.x + opts.width, opts.y + opts.height + 0.5f);
            this->drawLinef(opts.x + opts.width, opts.y, opts.x + opts.width, opts.y + opts.height + 0.5f);
        }
    }
}

void SDLGPUInterface::fillRectf(float x, float y, float width, float height) {
    this->updateTransform();
    this->setTexturing(false);

    static VertexArrayObject vao(DrawPrimitive::QUADS);
    {
        vao.clear();
        vao.addVertex(x, y);
        vao.addVertex(x, y + height);
        vao.addVertex(x + width, y + height);
        vao.addVertex(x + width, y);
    }
    this->drawVAO(&vao);
}

void SDLGPUInterface::fillRoundedRect(int x, int y, int width, int height, int /*radius*/) {
    // TODO: implement rounded corners
    this->fillRectf((float)x, (float)y, (float)width, (float)height);
}

void SDLGPUInterface::fillGradient(int x, int y, int width, int height, Color topLeftColor, Color topRightColor,
                                   Color bottomLeftColor, Color bottomRightColor) {
    this->updateTransform();
    this->setTexturing(false);

    static VertexArrayObject vao(DrawPrimitive::QUADS);
    {
        vao.clear();
        vao.addVertex(x, y);
        vao.addColor(topLeftColor);
        vao.addVertex(x + width, y);
        vao.addColor(topRightColor);
        vao.addVertex(x + width, y + height);
        vao.addColor(bottomRightColor);
        vao.addVertex(x, y + height);
        vao.addColor(bottomLeftColor);
    }
    this->drawVAO(&vao);
}

void SDLGPUInterface::drawQuad(int x, int y, int width, int height) {
    this->updateTransform();
    this->setTexturing(true);

    static VertexArrayObject vao(DrawPrimitive::QUADS);
    {
        vao.clear();
        vao.addVertex(x, y);
        vao.addTexcoord(0, 0);
        vao.addVertex(x, y + height);
        vao.addTexcoord(0, 1);
        vao.addVertex(x + width, y + height);
        vao.addTexcoord(1, 1);
        vao.addVertex(x + width, y);
        vao.addTexcoord(1, 0);
    }
    this->drawVAO(&vao);
}

void SDLGPUInterface::drawQuad(vec2 topLeft, vec2 topRight, vec2 bottomRight, vec2 bottomLeft, Color topLeftColor,
                               Color topRightColor, Color bottomRightColor, Color bottomLeftColor) {
    this->updateTransform();
    this->setTexturing(false);

    static VertexArrayObject vao(DrawPrimitive::QUADS);
    {
        vao.clear();
        vao.addVertex(topLeft.x, topLeft.y);
        vao.addColor(topLeftColor);
        vao.addVertex(bottomLeft.x, bottomLeft.y);
        vao.addColor(bottomLeftColor);
        vao.addVertex(bottomRight.x, bottomRight.y);
        vao.addColor(bottomRightColor);
        vao.addVertex(topRight.x, topRight.y);
        vao.addColor(topRightColor);
    }
    this->drawVAO(&vao);
}

// 2d resource drawing

void SDLGPUInterface::drawImage(const Image *image, AnchorPoint anchor, float edgeSoftness, McRect clipRect) {
    if(image == nullptr || !image->isReady()) return;

    const bool clipRectSpecified = vec::length(clipRect.getSize()) != 0;
    bool smoothedEdges = edgeSoftness > 0.0f;

    // initialize smooth clip shader on first use
    if(smoothedEdges) {
        if(!m_smoothClipShader) initSmoothClipShader();
        smoothedEdges = m_smoothClipShader && m_smoothClipShader->isReady();
    }

    const bool fallbackClip = clipRectSpecified && !smoothedEdges;
    if(fallbackClip) this->pushClipRect(clipRect);

    this->updateTransform();
    this->setTexturing(true);

    const float width = (float)image->getWidth();
    const float height = (float)image->getHeight();

    f32 x{}, y{};
    switch(anchor) {
        case AnchorPoint::CENTER:
            x = -width / 2;
            y = -height / 2;
            break;
        case AnchorPoint::TOP_LEFT:
            x = 0;
            y = 0;
            break;
        case AnchorPoint::TOP_RIGHT:
            x = -width;
            y = 0;
            break;
        case AnchorPoint::BOTTOM_LEFT:
            x = 0;
            y = -height;
            break;
        case AnchorPoint::LEFT:
            x = 0;
            y = -height / 2;
            break;
        default:
            x = 0;
            y = 0;
            break;
    }

    if(smoothedEdges && !clipRectSpecified) {
        clipRect = McRect{x, y, width, height};
    }

    if(smoothedEdges) {
        // SDL_gpu uses top-left origin like DX11
        float clipMinX = (clipRect.getX() + m_viewport.pos.x) - .5f;
        float clipMinY = (clipRect.getY() + m_viewport.pos.y) - .5f;
        float clipMaxX = (clipMinX + clipRect.getWidth());
        float clipMaxY = (clipMinY + clipRect.getHeight());

        m_smoothClipShader->enable();
        m_smoothClipShader->setUniform2f("rect_min", clipMinX, clipMinY);
        m_smoothClipShader->setUniform2f("rect_max", clipMaxX, clipMaxY);
        m_smoothClipShader->setUniform1f("edge_softness", edgeSoftness);
        m_smoothClipShader->setUniform4f("col", m_color.Rf(), m_color.Gf(), m_color.Bf(), m_color.Af());
        m_smoothClipShader->setUniformMatrix4fv("mvp", this->MP);
    }

    static VertexArrayObject vao(DrawPrimitive::QUADS);
    {
        vao.clear();
        vao.addVertex(x, y);
        vao.addTexcoord(0, 0);
        vao.addVertex(x, y + height);
        vao.addTexcoord(0, 1);
        vao.addVertex(x + width, y + height);
        vao.addTexcoord(1, 1);
        vao.addVertex(x + width, y);
        vao.addTexcoord(1, 0);
    }

    image->bind();
    this->drawVAO(&vao);
    image->unbind();

    if(smoothedEdges) {
        m_smoothClipShader->disable();
    } else if(fallbackClip) {
        this->popClipRect();
    }

    if(cv::r_debug_drawimage.getBool()) {
        this->setColor(0xbbff00ff);
        Graphics::drawRectf(x, y, width, height);
    }
}

void SDLGPUInterface::drawString(McFont *font, const UString &text, std::optional<TextShadow> shadow) {
    if(font == nullptr || text.length() < 1 || !font->isReady()) return;

    this->updateTransform();
    this->setTexturing(true);

    font->drawString(text, shadow);
}

// 3d type drawing

void SDLGPUInterface::drawVAO(VertexArrayObject *vao) {
    if(vao == nullptr) return;
    if(!m_cmdBuf) return;

    this->updateTransform();

    // if baked, record draw through the deferred system (vao->draw() calls recordBakedDraw)
    if(vao->isReady()) {
        vao->draw();
        return;
    }

    const std::vector<vec3> &vertices = vao->getVertices();
    const std::vector<vec2> &texcoords = vao->getTexcoords();
    const std::vector<Color> &vcolors = vao->getColors();
    // maybe TODO: handle normals (not currently used in app code)

    if(vertices.size() < 2) return;

    // rewrite quads and triangle fans into triangles (SDL_gpu doesn't support them)
    static std::vector<vec3> finalVertices;
    finalVertices = vertices;
    static std::vector<vec2> finalTexcoords;
    finalTexcoords = texcoords;
    static std::vector<vec4> colors;
    colors.clear();
    static std::vector<vec4> finalColors;
    finalColors.clear();

    for(auto vcolor : vcolors) {
        const vec4 color = vec4(vcolor.Rf(), vcolor.Gf(), vcolor.Bf(), vcolor.Af());
        colors.push_back(color);
        finalColors.push_back(color);
    }
    const size_t maxColorIndex = (colors.size() > 0 ? colors.size() - 1 : 0);

    DrawPrimitive primitive = vao->getPrimitive();
    if(primitive == DrawPrimitive::QUADS) {
        finalVertices.clear();
        finalTexcoords.clear();
        finalColors.clear();
        primitive = DrawPrimitive::TRIANGLES;

        if(vertices.size() > 3) {
            for(size_t i = 0; i < vertices.size(); i += 4) {
                finalVertices.push_back(vertices[i + 0]);
                finalVertices.push_back(vertices[i + 1]);
                finalVertices.push_back(vertices[i + 2]);

                if(!texcoords.empty()) {
                    finalTexcoords.push_back(texcoords[i + 0]);
                    finalTexcoords.push_back(texcoords[i + 1]);
                    finalTexcoords.push_back(texcoords[i + 2]);
                }

                if(colors.size() > 0) {
                    finalColors.push_back(colors[std::clamp<size_t>(i + 0, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 1, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 2, 0, maxColorIndex)]);
                }

                finalVertices.push_back(vertices[i + 0]);
                finalVertices.push_back(vertices[i + 2]);
                finalVertices.push_back(vertices[i + 3]);

                if(!texcoords.empty()) {
                    finalTexcoords.push_back(texcoords[i + 0]);
                    finalTexcoords.push_back(texcoords[i + 2]);
                    finalTexcoords.push_back(texcoords[i + 3]);
                }

                if(colors.size() > 0) {
                    finalColors.push_back(colors[std::clamp<size_t>(i + 0, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 2, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 3, 0, maxColorIndex)]);
                }
            }
        }
    } else if(primitive == DrawPrimitive::TRIANGLE_FAN) {
        finalVertices.clear();
        finalTexcoords.clear();
        finalColors.clear();
        primitive = DrawPrimitive::TRIANGLES;

        if(vertices.size() > 2) {
            for(size_t i = 2; i < vertices.size(); i++) {
                finalVertices.push_back(vertices[0]);
                finalVertices.push_back(vertices[i]);
                finalVertices.push_back(vertices[i - 1]);

                if(!texcoords.empty()) {
                    finalTexcoords.push_back(texcoords[0]);
                    finalTexcoords.push_back(texcoords[i]);
                    finalTexcoords.push_back(texcoords[i - 1]);
                }

                if(colors.size() > 0) {
                    finalColors.push_back(colors[std::clamp<size_t>(0, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i - 1, 0, maxColorIndex)]);
                }
            }
        }
    }

    // build SDLGPUSimpleVertex array
    const bool hasTexcoords0 = (finalTexcoords.size() > 0);
    m_vertices.resize(finalVertices.size());
    {
        const bool hasColors = (finalColors.size() > 0);
        const size_t maxCI = (hasColors ? finalColors.size() - 1 : 0);
        const size_t maxTI = (hasTexcoords0 ? finalTexcoords.size() - 1 : 0);
        const vec4 color = vec4(m_color.Rf(), m_color.Gf(), m_color.Bf(), m_color.Af());

        for(size_t i = 0; i < finalVertices.size(); i++) {
            m_vertices[i].pos = finalVertices[i];

            if(hasColors)
                m_vertices[i].col = finalColors[std::clamp<size_t>(i, 0, maxCI)];
            else
                m_vertices[i].col = color;

            if(hasTexcoords0)
                m_vertices[i].tex = finalTexcoords[std::clamp<size_t>(i, 0, maxTI)];
            else
                m_vertices[i].tex = vec2(0.f, 0.f);
        }
    }

    // set primitive type and texturing
    this->setTexturing(hasTexcoords0);

    const SDLGPUPrimitiveType gpuPrimitive = primitiveToSDLGPUPrimitive(primitive);

    if(gpuPrimitive != m_currentPrimitiveType) {
        m_currentPrimitiveType = gpuPrimitive;
        m_bPipelineDirty = true;
    }
    rebuildPipeline();

    // if staging buffer would overflow, flush first
    if(m_stagingVertices.size() + m_vertices.size() > MAX_STAGING_VERTS) {
        flushDrawCommands();
        // re-add initial boundary for the current RT state after flush
        addRenderPassBoundary();
    }

    // append vertices to staging buffer and record a draw command
    const u32 offset = (u32)m_stagingVertices.size();
    Mc::append_range(m_stagingVertices, m_vertices);

    recordDraw(nullptr, offset, (u32)m_vertices.size());
}

void SDLGPUInterface::recordBakedDraw(SDL_GPUBuffer *buffer, u32 firstVertex, u32 vertexCount,
                                      DrawPrimitive primitive) {
    if(!m_cmdBuf || vertexCount == 0) return;

    const SDLGPUPrimitiveType gpuPrimitive = primitiveToSDLGPUPrimitive(primitive);

    if(gpuPrimitive != m_currentPrimitiveType) {
        m_currentPrimitiveType = gpuPrimitive;
        m_bPipelineDirty = true;
    }
    rebuildPipeline();

    recordDraw(buffer, firstVertex, vertexCount);
}

void SDLGPUInterface::recordDraw(SDL_GPUBuffer *bakedBuffer, u32 vertexOffset, u32 vertexCount) {
    if(!m_cmdBuf || vertexCount == 0) return;

    DrawCommand cmd{};
    cmd.vertexOffset = vertexOffset;
    cmd.vertexCount = vertexCount;
    cmd.bakedBuffer = bakedBuffer;
    cmd.pipeline = m_currentPipeline;

    // snapshot texture binding
    if(m_boundTexture && m_boundSampler) {
        cmd.texture = m_boundTexture;
        cmd.sampler = m_boundSampler;
    } else {
        cmd.texture = m_dummyTexture;
        cmd.sampler = m_dummySampler;
    }

    // snapshot uniform blocks from active shader
    cmd.numUniformBlocks = 0;
    m_activeShader->setUniformMatrix4fv("mvp", this->MP);
    for(auto &block : m_activeShader->getUniformBlocks()) {
        if(cmd.numUniformBlocks >= 4) break;
        auto &ub = cmd.uniformBlocks[cmd.numUniformBlocks];
        ub.slot = block.binding;
        ub.isVertex = (block.set == 1);
        ub.size = (u32)std::min(block.buffer.size(), (size_t)80);
        std::memcpy(ub.data.data(), block.buffer.data(), ub.size);
        cmd.numUniformBlocks++;
    }

    // snapshot viewport
    cmd.viewport = m_viewport;

    // snapshot scissor
    cmd.scissorEnabled = m_bScissorEnabled;
    if(m_bScissorEnabled && !m_clipRectStack.empty()) {
        const auto &cr = m_clipRectStack.back();
        Scissor &csc = cmd.scissor;
        csc.pos.x = (i32)cr.getMinX();
        csc.pos.y = (i32)cr.getMinY();
        csc.size.x = (i32)cr.getWidth();
        csc.size.y = (i32)cr.getHeight();
        // clamp for vulkan
        if(csc.pos.x < 0) {
            csc.size.x += csc.pos.x;
            csc.pos.x = 0;
        }
        if(csc.pos.y < 0) {
            csc.size.y += csc.pos.y;
            csc.pos.y = 0;
        }
        if(csc.size.x < 0) csc.size.x = 0;
        if(csc.size.y < 0) csc.size.y = 0;
    }

    // snapshot stencil reference
    cmd.stencilRef = (u8)(m_iStencilState == 1 ? 1 : 0);

    m_pendingDraws.push_back(cmd);
    m_iStatsNumDrawCalls++;
}

void SDLGPUInterface::flushDrawCommands() {
    if(!m_cmdBuf) return;

    // check if there's anything to do
    const bool hasDraws = !m_pendingDraws.empty();
    bool hasClears = false;
    for(auto &b : m_renderPassBoundaries) {
        if(b.state.hasClears()) {
            hasClears = true;
            break;
        }
    }
    if(!hasDraws && !hasClears) {
        m_renderPassBoundaries.clear();
        return;
    }

    // end active render pass if any (shouldn't normally be active between flushes)
    if(m_renderPass) {
        SDL_EndGPURenderPass(m_renderPass);
        m_renderPass = nullptr;
    }

    // check if any draws use the staging buffer (non-baked)
    bool hasImmediateDraws = false;
    for(const auto &cmd : m_pendingDraws) {
        if(!cmd.bakedBuffer) {
            hasImmediateDraws = true;
            break;
        }
    }

    // single copy pass: upload ALL staging vertices to GPU buffer
    if(hasImmediateDraws && !m_stagingVertices.empty()) {
        void *mapped = SDL_MapGPUTransferBuffer(m_device, m_transferBuffer, true);
        if(mapped) {
            std::memcpy(mapped, m_stagingVertices.data(), sizeof(SDLGPUSimpleVertex) * m_stagingVertices.size());
            SDL_UnmapGPUTransferBuffer(m_device, m_transferBuffer);
        }

        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(m_cmdBuf);
        if(copyPass) {
            SDL_GPUTransferBufferLocation src{
                .transfer_buffer = m_transferBuffer,
                .offset = 0,
            };
            SDL_GPUBufferRegion dst{
                .buffer = m_vertexBuffer,
                .offset = 0,
                .size = static_cast<Uint32>(sizeof(SDLGPUSimpleVertex) * m_stagingVertices.size()),
            };
            SDL_UploadToGPUBuffer(copyPass, &src, &dst, true);
            SDL_EndGPUCopyPass(copyPass);
        }
    }

    // replay render passes from boundaries
    const u32 totalDraws = (u32)m_pendingDraws.size();
    const size_t numBoundaries = m_renderPassBoundaries.size();

    for(size_t bi = 0; bi < numBoundaries; bi++) {
        auto &boundary = m_renderPassBoundaries[bi];
        auto &state = boundary.state;
        const u32 drawStart = boundary.drawIndex;
        const u32 drawEnd = (bi + 1 < numBoundaries) ? m_renderPassBoundaries[bi + 1].drawIndex : totalDraws;
        const u32 drawCount = drawEnd - drawStart;

        // skip if no draws and no pending clears
        if(drawCount == 0 && !state.hasClears()) continue;

        // begin render pass with this boundary's RT state
        SDL_GPUTexture *colorTex = state.colorTarget;
        SDL_GPUTexture *depthTex = state.depthTarget;

        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = colorTex;
        colorTarget.load_op = state.pendingClearColor ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        if(state.resolveTarget) {
            // MSAA: resolve multisampled texture into the resolve target when the render pass ends.
            // use RESOLVE_AND_STORE so the MSAA texture retains its content for subsequent render passes
            // (e.g. after clearDepthBuffer() triggers a flush mid-frame)
            colorTarget.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
            colorTarget.resolve_texture = state.resolveTarget;
        } else {
            colorTarget.store_op = SDL_GPU_STOREOP_STORE;
        }
        if(state.pendingClearColor) {
            auto cc = state.clearColor;
            colorTarget.clear_color = {cc.Rf(), cc.Gf(), cc.Bf(), cc.Af()};
        }

        SDL_GPUDepthStencilTargetInfo depthTarget{};
        depthTarget.texture = depthTex;
        depthTarget.load_op = state.pendingClearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depthTarget.store_op = SDL_GPU_STOREOP_STORE;
        if(state.pendingClearDepth) depthTarget.clear_depth = 1.0f;
        depthTarget.stencil_load_op = state.pendingClearStencil ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depthTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        if(state.pendingClearStencil) depthTarget.clear_stencil = 0;

        m_renderPass = SDL_BeginGPURenderPass(m_cmdBuf, &colorTarget, 1, &depthTarget);
        if(!m_renderPass) {
            m_pendingDraws.clear();
            m_stagingVertices.clear();
            m_renderPassBoundaries.clear();
            return;
        }

        // replay draw commands for this render pass, tracking last-bound state to skip redundant binds
        SDL_GPUGraphicsPipeline *lastPipeline = nullptr;
        SDL_GPUTexture *lastTexture = nullptr;
        SDL_GPUSampler *lastSampler = nullptr;
        SDL_GPUBuffer *lastVertexBuffer = nullptr;
        Viewport lastViewport{.pos = {-1.f, -1.f}, .size = {-1.f, -1.f}};
        bool lastScissorEnabled = false;
        Scissor lastScissor{.pos = {-1, -1}, .size = {-1, -1}};
        u8 lastStencilRef = 0xFF;

        for(u32 di = drawStart; di < drawEnd; di++) {
            auto &cmd = m_pendingDraws[di];

            // bind pipeline
            if(cmd.pipeline != lastPipeline) {
                SDL_BindGPUGraphicsPipeline(m_renderPass, cmd.pipeline);
                lastPipeline = cmd.pipeline;
            }

            // set viewport
            if(cmd.viewport != lastViewport) {
                SDL_GPUViewport vp{
                    .x = cmd.viewport.pos.x,
                    .y = cmd.viewport.pos.y,
                    .w = cmd.viewport.size.x,
                    .h = cmd.viewport.size.y,
                    .min_depth = 0.0f,
                    .max_depth = 1.0f,
                };
                SDL_SetGPUViewport(m_renderPass, &vp);
                lastViewport = cmd.viewport;
            }

            // set scissor
            if(cmd.scissorEnabled != lastScissorEnabled || (cmd.scissorEnabled && (cmd.scissor != lastScissor))) {
                if(cmd.scissorEnabled) {
                    SDL_Rect sc{.x = cmd.scissor.pos.x,
                                .y = cmd.scissor.pos.y,
                                .w = cmd.scissor.size.x,
                                .h = cmd.scissor.size.y};
                    SDL_SetGPUScissor(m_renderPass, &sc);
                } else {
                    SDL_Rect fullRect{.x = 0, .y = 0, .w = (int)cmd.viewport.size.x, .h = (int)cmd.viewport.size.y};
                    SDL_SetGPUScissor(m_renderPass, &fullRect);
                }
                lastScissorEnabled = cmd.scissorEnabled;
                lastScissor = cmd.scissor;
            }

            // set stencil reference
            if(cmd.stencilRef != lastStencilRef) {
                SDL_SetGPUStencilReference(m_renderPass, cmd.stencilRef);
                lastStencilRef = cmd.stencilRef;
            }

            // push uniforms (always, they're snapshots and might be different per-draw)
            for(u8 i = 0; i < cmd.numUniformBlocks; i++) {
                auto &ub = cmd.uniformBlocks[i];
                if(ub.isVertex) {
                    SDL_PushGPUVertexUniformData(m_cmdBuf, ub.slot, ub.data.data(), ub.size);
                } else {
                    SDL_PushGPUFragmentUniformData(m_cmdBuf, ub.slot, ub.data.data(), ub.size);
                }
            }

            // bind texture/sampler
            if(cmd.texture != lastTexture || cmd.sampler != lastSampler) {
                SDL_GPUTextureSamplerBinding texBinding{};
                texBinding.texture = cmd.texture;
                texBinding.sampler = cmd.sampler;
                SDL_BindGPUFragmentSamplers(m_renderPass, 0, &texBinding, 1);
                lastTexture = cmd.texture;
                lastSampler = cmd.sampler;
            }

            // bind vertex buffer and draw
            SDL_GPUBuffer *vb = cmd.bakedBuffer ? cmd.bakedBuffer : m_vertexBuffer;
            if(vb != lastVertexBuffer) {
                SDL_GPUBufferBinding vertexBinding{.buffer = vb, .offset = 0};
                SDL_BindGPUVertexBuffers(m_renderPass, 0, &vertexBinding, 1);
                lastVertexBuffer = vb;
            }

            SDL_DrawGPUPrimitives(m_renderPass, cmd.vertexCount, 1, cmd.vertexOffset, 0);
        }

        // end render pass
        SDL_EndGPURenderPass(m_renderPass);
        m_renderPass = nullptr;
    }

    // clear pending state
    m_pendingDraws.clear();
    m_stagingVertices.clear();
    m_renderPassBoundaries.clear();
}

void SDLGPUInterface::addRenderPassBoundary() {
    m_renderPassBoundaries.emplace_back(RenderPassBoundary{
        .state = m_curRTState,
        .drawIndex = (u32)m_pendingDraws.size(),
    });
    m_curRTState.pendingClearColor = false;
    m_curRTState.pendingClearDepth = false;
    m_curRTState.pendingClearStencil = false;
}

// 2d clipping

void SDLGPUInterface::setClipRect(McRect /*clipRect*/) {
    if(cv::r_debug_disable_cliprect.getBool()) return;
    m_bScissorEnabled = true;
    // TODO: is this necessary? maybe this shouldn't be a public API at all (not used in app code currently anyways)
}

void SDLGPUInterface::pushClipRect(McRect clipRect) {
    if(m_clipRectStack.size() > 0)
        m_clipRectStack.push_back(m_clipRectStack.back().intersect(clipRect));
    else
        m_clipRectStack.push_back(clipRect);

    this->setClipRect(m_clipRectStack.back());
}

void SDLGPUInterface::popClipRect() {
    m_clipRectStack.pop_back();

    if(m_clipRectStack.size() > 0)
        this->setClipRect(m_clipRectStack.back());
    else
        this->setClipping(false);
}

// viewport

void SDLGPUInterface::pushViewport() {
    // SDL_gpu doesn't have a GetViewport query, so we track it ourselves
    this->viewportStack.push_back(
        {(int)m_viewport.pos.x, (int)m_viewport.pos.y, (int)m_viewport.size.x, (int)m_viewport.size.y});
}

void SDLGPUInterface::setViewport(int x, int y, int width, int height) {
    m_viewport.pos = {(float)x, (float)y};
    m_viewport.size = {(float)width, (float)height};
}

void SDLGPUInterface::popViewport() {
    if(this->viewportStack.empty()) {
        debugLog("WARNING: viewport stack underflow!");
        return;
    }

    const auto &vp = this->viewportStack.back();
    m_viewport.pos = {(float)vp[0], (float)vp[1]};
    m_viewport.size = {(float)vp[2], (float)vp[3]};

    // viewport is captured per-draw in recordDraw();
    this->viewportStack.pop_back();
}

// stencil buffer

void SDLGPUInterface::pushStencil() {
    if(!m_cmdBuf) return;

    // record boundary with stencil clear instead of flushing
    m_curRTState.pendingClearStencil = true;
    addRenderPassBoundary();

    // stencil writing phase: color off, write 1 where geometry is drawn
    m_iStencilState = 1;
    setColorWriting(false, false, false, false);
    m_bPipelineDirty = true;
}

void SDLGPUInterface::fillStencil(bool inside) {
    // stencil testing phase: color on, test against stencil
    m_iStencilState = inside ? 2 : 3;  // 2 = draw where stencil==0 (inside), 3 = draw where stencil==1 (outside)
    setColorWriting(true, true, true, true);
    m_bPipelineDirty = true;
}

void SDLGPUInterface::popStencil() {
    m_iStencilState = 0;
    m_bPipelineDirty = true;
}

// renderer settings

void SDLGPUInterface::setClipping(bool enabled) {
    if(enabled) {
        if(m_clipRectStack.size() < 1) enabled = false;
    }
    m_bScissorEnabled = enabled;
    // scissor state is captured per-draw in recordDraw();
}

void SDLGPUInterface::setAlphaTesting(bool /*enabled*/) {
    // TODO (?): handle in shader
}

void SDLGPUInterface::setAlphaTestFunc(DrawCompareFunc /*alphaFunc*/, float /*ref*/) {
    // TODO (?)
}

void SDLGPUInterface::setBlending(bool enabled) {
    if(this->bBlendingEnabled != enabled) {
        this->bBlendingEnabled = enabled;
        m_bPipelineDirty = true;
    }
    Graphics::setBlending(enabled);
}

void SDLGPUInterface::setBlendMode(DrawBlendMode blendMode) {
    if(this->currentBlendMode != blendMode) {
        this->currentBlendMode = blendMode;
        m_bPipelineDirty = true;
    }
    Graphics::setBlendMode(blendMode);
}

void SDLGPUInterface::setDepthBuffer(bool enabled) {
    if(m_bDepthTestEnabled != enabled || m_bDepthWriteEnabled != enabled) {
        m_bDepthTestEnabled = enabled;
        m_bDepthWriteEnabled = enabled;
        m_bPipelineDirty = true;
    }
}

void SDLGPUInterface::setColorWriting(bool r, bool g, bool b, bool a) {
    if(m_bColorWriteR != r || m_bColorWriteG != g || m_bColorWriteB != b || m_bColorWriteA != a) {
        m_bColorWriteR = r;
        m_bColorWriteG = g;
        m_bColorWriteB = b;
        m_bColorWriteA = a;
        m_bPipelineDirty = true;
    }
}

void SDLGPUInterface::setColorInversion(bool enabled) {
    if(m_bColorInversion == enabled) return;

    m_bColorInversion = enabled;
    setTexturing(m_bTexturingEnabled, true /* force */);  // re-apply with new inversion state
}

void SDLGPUInterface::setCulling(bool enabled) {
    if(m_bCullingEnabled != enabled) {
        m_bCullingEnabled = enabled;
        m_bPipelineDirty = true;
    }
}

void SDLGPUInterface::setVSync(bool enabled) {
    m_bVSync = enabled;
    if(!m_device || !m_window || !m_bSupportsSDRComposition) return;

    if(enabled) {
        if(!SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                          SDL_GPU_PRESENTMODE_VSYNC))
            debugLog("SDLGPUInterface: couldn't set vsync present mode: {}", SDL_GetError());
        return;
    }

    // prefer immediate, fall back to mailbox
    if(m_bSupportsImmediate) {
        if(!SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                          SDL_GPU_PRESENTMODE_IMMEDIATE))
            debugLog("SDLGPUInterface: couldn't set immediate present mode: {}", SDL_GetError());
    } else if(m_bSupportsMailbox) {
        if(!SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                          SDL_GPU_PRESENTMODE_MAILBOX))
            debugLog("SDLGPUInterface: couldn't set mailbox present mode: {}", SDL_GetError());
    }
}

void SDLGPUInterface::setAntialiasing(bool /*enabled*/) {
    // not sure how to implement this exactly, but
    // MSAA is active whenever pipeline+target sample counts match and are >1
}

void SDLGPUInterface::setWireframe(bool enabled) {
    if(m_bWireframe != enabled) {
        m_bWireframe = enabled;
        m_bPipelineDirty = true;
    }
}

// renderer actions

void SDLGPUInterface::flush() {
    flushDrawCommands();
    // re-add initial boundary so subsequent draws have a render pass
    if(m_cmdBuf) addRenderPassBoundary();
}

std::vector<u8> SDLGPUInterface::getScreenshot(bool withAlpha) {
    if(!m_device || !m_backbuffer || !m_cmdBuf) return {};

    const u32 w = m_backbufferWidth;
    const u32 h = m_backbufferHeight;
    const u32 bpp = 4;
    const u32 bufSize = w * h * bpp;

    // create download transfer buffer
    SDL_GPUTransferBufferCreateInfo tbInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = bufSize,
        .props = 0,
    };
    auto *tb = SDL_CreateGPUTransferBuffer(m_device, &tbInfo);
    if(!tb) return {};

    // called after EndRenderPass, so we can go straight into the copy pass
    auto *copyPass = SDL_BeginGPUCopyPass(m_cmdBuf);
    if(copyPass) {
        SDL_GPUTextureRegion src{};
        src.texture = m_backbuffer;
        src.w = w;
        src.h = h;
        src.d = 1;

        SDL_GPUTextureTransferInfo dst{};
        dst.transfer_buffer = tb;
        dst.offset = 0;

        SDL_DownloadFromGPUTexture(copyPass, &src, &dst);
        SDL_EndGPUCopyPass(copyPass);
    }

    // submit and wait for the download to complete (also presents the frame)
    auto *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(m_cmdBuf);
    if(fence) {
        SDL_WaitForGPUFences(m_device, true, &fence, 1);
        SDL_ReleaseGPUFence(m_device, fence);
    }
    m_cmdBuf = nullptr;  // endScene checks this to avoid double-submit

    // read pixels
    std::vector<u8> result;
    result.reserve(static_cast<size_t>(w) * h * (withAlpha ? 4 : 3));

    void *mapped = SDL_MapGPUTransferBuffer(m_device, tb, false);
    if(mapped) {
        const u8 *pixels = static_cast<const u8 *>(mapped);
        const bool isBGRA = (DEFAULT_TEXTURE_FORMAT == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                             DEFAULT_TEXTURE_FORMAT == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB);
        for(u32 i = 0; i < w * h; i++) {
            if(isBGRA) {
                result.push_back(pixels[i * 4 + 2]);  // R from B position
                result.push_back(pixels[i * 4 + 1]);  // G
                result.push_back(pixels[i * 4 + 0]);  // B from R position
            } else {
                result.push_back(pixels[i * 4 + 0]);
                result.push_back(pixels[i * 4 + 1]);
                result.push_back(pixels[i * 4 + 2]);
            }
            if(withAlpha) result.push_back(pixels[i * 4 + 3]);
        }
        SDL_UnmapGPUTransferBuffer(m_device, tb);
    }

    SDL_ReleaseGPUTransferBuffer(m_device, tb);

    return result;
}

// render target support

void SDLGPUInterface::pushRenderTarget(SDL_GPUTexture *colorTex, SDL_GPUTexture *depthTex, bool doClear, Color clearCol,
                                       SDL_GPUTexture *resolveTex, SDLGPUSampleCount sampleCount) {
    if(!m_cmdBuf) return;

    // save current state
    m_renderTargetStack.push_back(m_curRTState);
    auto &cur = m_curRTState;

    if(cur.sampleCount != sampleCount) m_bPipelineDirty = true;

    cur.colorTarget = colorTex;
    cur.depthTarget = depthTex;
    cur.resolveTarget = resolveTex;
    cur.sampleCount = sampleCount;

    // set up clear flags for the new RT
    cur.pendingClearColor = doClear;
    cur.pendingClearDepth = doClear;
    cur.pendingClearStencil = false;
    cur.clearColor = clearCol;

    // record boundary
    addRenderPassBoundary();
}

void SDLGPUInterface::popRenderTarget() {
    if(!m_cmdBuf || m_renderTargetStack.empty()) return;

    // restore previous state
    auto prev = m_renderTargetStack.back();
    m_renderTargetStack.pop_back();
    if(m_curRTState.sampleCount != prev.sampleCount) m_bPipelineDirty = true;

    m_curRTState = prev;

    // record boundary
    addRenderPassBoundary();
}

const char *SDLGPUInterface::getName() const { return m_rendererName.c_str(); }

UString SDLGPUInterface::getVendor() {
    if(!m_devProps) return "?";
    return SDL_GetStringProperty(m_devProps, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, "?");
}

UString SDLGPUInterface::getModel() {
    if(!m_devProps) return "?";
    return SDL_GetStringProperty(m_devProps, SDL_PROP_GPU_DEVICE_NAME_STRING, "?");
}

UString SDLGPUInterface::getVersion() {
    if(!m_devProps) return "?";
    return SDL_GetStringProperty(m_devProps, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "?");
}

int SDLGPUInterface::getVRAMTotal() { return 0; }

int SDLGPUInterface::getVRAMRemaining() { return 0; }

// callbacks

void SDLGPUInterface::onFramecountNumChanged(float maxFramesInFlight) {
    if(!m_device) return;

    const int maxFrames = std::clamp(static_cast<int>(maxFramesInFlight), 1, 3);
    if(maxFrames == m_iMaxFrameLatency) return;

    if(!SDL_SetGPUAllowedFramesInFlight(m_device, maxFrames)) {
        debugLog("SDLGPUInterface: Failed to set max frames in flight to {}: {}", maxFrames, SDL_GetError());
        cv::r_sync_max_frames.setValue(m_iMaxFrameLatency, false);
    } else {
        m_iMaxFrameLatency = maxFrames;
    }
}

void SDLGPUInterface::onResolutionChange(vec2 newResolution) {
    m_viewport.size = newResolution;

    const u32 w = (u32)newResolution.x;
    const u32 h = (u32)newResolution.y;
    if(w == 0 || h == 0 || (w == m_backbufferWidth && h == m_backbufferHeight)) return;

    if(m_backbuffer) SDL_ReleaseGPUTexture(m_device, m_backbuffer);
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = (SDL_GPUTextureFormat)DEFAULT_TEXTURE_FORMAT;
    tci.width = w;
    tci.height = h;
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    tci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    m_backbuffer = SDL_CreateGPUTexture(m_device, &tci);
    if(!m_backbuffer) {
        debugLog("SDLGPUInterface: Failed to create backbuffer: {}", SDL_GetError());
        m_backbufferWidth = 0;
        m_backbufferHeight = 0;
        return;
    }
    m_backbufferWidth = w;
    m_backbufferHeight = h;
}

void SDLGPUInterface::onRestored() { onResolutionChange(m_viewport.size); }

// transforms

void SDLGPUInterface::onTransformUpdate() {
    // will be updated in draw() or when necessary
}

void SDLGPUInterface::setTexturing(bool enabled, bool force) {
    if(!force && enabled == m_bTexturingEnabled) return;

    m_bTexturingEnabled = enabled;
    m_defaultShader->setUniform4f("misc", enabled ? 1.f : 0.f, m_bColorInversion ? 1.f : 0.f, 0.f, 0.f);
    if(enabled) {
        m_defaultShader->setUniform4f("col", m_color.Rf(), m_color.Gf(), m_color.Bf(), m_color.Af());
    }
}

// shader switching

void SDLGPUInterface::setActiveShader(SDLGPUShader *shader) {
    if(m_activeShader != shader) {
        m_activeShader = shader;
        m_bPipelineDirty = true;
    }
}

void SDLGPUInterface::initSmoothClipShader() {
    if(m_smoothClipShader) return;

    m_smoothClipShader.reset(static_cast<SDLGPUShader *>(
        createShaderFromSource(std::string(reinterpret_cast<const char *>(SDLGPU_smoothclip_vsh),
                                           static_cast<size_t>(SDLGPU_smoothclip_vsh_size())),
                               std::string(reinterpret_cast<const char *>(SDLGPU_smoothclip_fsh),
                                           static_cast<size_t>(SDLGPU_smoothclip_fsh_size())))));

    if(m_smoothClipShader) {
        m_smoothClipShader->loadAsync();
        m_smoothClipShader->load();
    }
}

// factory

Image *SDLGPUInterface::createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) {
    return new SDLGPUImage(std::move(filePath), mipmapped, keepInSystemMemory);
}

Image *SDLGPUInterface::createImage(i32 width, i32 height, bool mipmapped, bool keepInSystemMemory) {
    return new SDLGPUImage(width, height, mipmapped, keepInSystemMemory);
}

RenderTarget *SDLGPUInterface::createRenderTarget(int x, int y, int width, int height, MultisampleType msType) {
    return new SDLGPURenderTarget(x, y, width, height, msType);
}

Shader *SDLGPUInterface::createShaderFromFile(std::string vertexShaderFilePath, std::string fragmentShaderFilePath) {
    return new SDLGPUShader(vertexShaderFilePath, fragmentShaderFilePath,
                            false);  // NOTE: not currently implemented (all shaders are included as binary data)
}

Shader *SDLGPUInterface::createShaderFromSource(std::string vertexShader, std::string fragmentShader) {
    return new SDLGPUShader(vertexShader, fragmentShader);
}

VertexArrayObject *SDLGPUInterface::createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                                            bool keepInSystemMemory) {
    return new SDLGPUVertexArrayObject(primitive, usage, keepInSystemMemory);
}

// util

SDLGPUPrimitiveType SDLGPUInterface::primitiveToSDLGPUPrimitive(DrawPrimitive prim) {
    SDLGPUPrimitiveType gpuPrimitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    switch(prim) {
        case DrawPrimitive::LINES:
            gpuPrimitive = SDL_GPU_PRIMITIVETYPE_LINELIST;
            break;
        case DrawPrimitive::LINE_STRIP:
            gpuPrimitive = SDL_GPU_PRIMITIVETYPE_LINESTRIP;
            break;
        case DrawPrimitive::TRIANGLE_STRIP:
            gpuPrimitive = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
            break;
        // TODO: SDL_GPU_PRIMITIVETYPE_POINTLIST ?
        default:
            break;
    }

    return gpuPrimitive;
}

#endif
