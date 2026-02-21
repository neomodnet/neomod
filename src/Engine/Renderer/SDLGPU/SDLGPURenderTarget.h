//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		SDL_gpu implementation of RenderTarget / render to texture
//
// $NoKeywords: $sdlgpurt
//===============================================================================//

#pragma once
#ifndef SDLGPURENDERTARGET_H
#define SDLGPURENDERTARGET_H
#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU
#include "RenderTarget.h"

typedef struct SDL_GPUTexture SDL_GPUTexture;
typedef struct SDL_GPUSampler SDL_GPUSampler;
typedef struct SDL_GPUDevice SDL_GPUDevice;

// can't forward declare unsized enums, these correspond to the SDL_ prefixed enums of the same name
using SDLGPUSampleCount = u8;

class SDLGPUInterface;

class SDLGPURenderTarget final : public RenderTarget {
    NOCOPY_NOMOVE(SDLGPURenderTarget)
   private:
    friend SDLGPUInterface;
    SDLGPURenderTarget(SDLGPUInterface *gpu, SDL_GPUDevice *device, int x, int y, int width, int height,
                       MultisampleType multiSampleType = MultisampleType::X0);

   public:
    SDLGPURenderTarget() = delete;
    ~SDLGPURenderTarget() override { destroy(); }

    void draw(int x, int y) override;
    void draw(int x, int y, int width, int height) override;
    void drawRect(int x, int y, int width, int height) override;

    void enable() override;
    void disable() override;

    void bind(unsigned int textureUnit = 0) override;
    void unbind() override;

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    SDLGPUInterface *m_gpu;
    SDL_GPUDevice *m_device;

    SDL_GPUTexture *m_colorTexture{nullptr};
    SDL_GPUTexture *m_msaaTexture{nullptr};  // multisampled color texture (only when MSAA)
    SDL_GPUTexture *m_depthTexture{nullptr};
    SDL_GPUSampler *m_sampler{nullptr};
    SDLGPUSampleCount m_sampleCount{0};  // SDL_GPU_SAMPLECOUNT_1 == 0

    mutable SDL_GPUTexture *m_prevTexture{nullptr};
    mutable SDL_GPUSampler *m_prevSampler{nullptr};
};

#endif

#endif
