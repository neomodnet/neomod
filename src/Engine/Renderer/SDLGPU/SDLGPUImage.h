//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		SDL_gpu implementation of Image
//
// $NoKeywords: $sdlgpuimg
//===============================================================================//

#pragma once
#ifndef SDLGPUIMAGE_H
#define SDLGPUIMAGE_H
#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU
#include "Image.h"

typedef struct SDL_Window SDL_Window;
typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_GPUSampler SDL_GPUSampler;
typedef struct SDL_GPUTexture SDL_GPUTexture;

struct SDL_GPUSamplerCreateInfo;

class SDLGPUInterface;

class SDLGPUImage final : public Image {
    NOCOPY_NOMOVE(SDLGPUImage)
   private:
    friend SDLGPUInterface;
    SDLGPUImage(SDLGPUInterface *gpu, SDL_GPUDevice *device, std::string filepath, bool mipmapped = false,
                bool keepInSystemMemory = false);
    SDLGPUImage(SDLGPUInterface *gpu, SDL_GPUDevice *device, int width, int height, bool mipmapped = false,
                bool keepInSystemMemory = false);

   public:
    SDLGPUImage() = delete;
    ~SDLGPUImage() override;

    void bind(unsigned int textureUnit = 0) const override;
    void unbind() const override;

    void setFilterMode(TextureFilterMode filterMode) override;
    void setWrapMode(TextureWrapMode wrapMode) override;

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    void createOrUpdateSampler();
    void uploadPixelData();

    SDLGPUInterface *m_gpu;
    SDL_GPUDevice *m_device;

    SDL_GPUTexture *m_texture{nullptr};
    SDL_GPUSampler *m_sampler{nullptr};

    mutable SDL_GPUTexture *m_prevTexture{nullptr};
    mutable SDL_GPUSampler *m_prevSampler{nullptr};

    std::unique_ptr<SDL_GPUSamplerCreateInfo> m_lastSamplerCreateInfo{nullptr};
};

#endif

#endif
