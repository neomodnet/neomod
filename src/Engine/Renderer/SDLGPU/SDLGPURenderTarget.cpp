//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		SDL_gpu implementation of RenderTarget / render to texture
//
// $NoKeywords: $sdlgpurt
//===============================================================================//
#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU

#include <SDL3/SDL_gpu.h>

#include "SDLGPURenderTarget.h"

#include "ConVar.h"
#include "Engine.h"
#include "Graphics.h"
#include "Logging.h"

#include "SDLGPUInterface.h"

#include <cassert>

SDLGPURenderTarget::SDLGPURenderTarget(SDLGPUInterface *gpu, SDL_GPUDevice *device, int x, int y, int width, int height,
                                       MultisampleType multiSampleType)
    : RenderTarget(x, y, width, height, multiSampleType), m_gpu(gpu), m_device(device) {}

void SDLGPURenderTarget::init() {
    if(!m_gpu || !m_device) {
        debugLog("SDLGPURenderTarget: no GPU/device context");
        this->setReady(false);
        return;
    }
    debugLog("Building RenderTarget ({}x{}) ...", (int)this->getSize().x, (int)this->getSize().y);

    const u32 w = (u32)this->getSize().x;
    const u32 h = (u32)this->getSize().y;

    // map MultisampleType to SDL_GPUSampleCount
    switch(this->getMultiSampleType()) {
        case MultisampleType::X2:
            m_sampleCount = SDL_GPU_SAMPLECOUNT_2;
            break;
        case MultisampleType::X4:
            m_sampleCount = SDL_GPU_SAMPLECOUNT_4;
            break;
        case MultisampleType::X8:
        case MultisampleType::X16:
            m_sampleCount = SDL_GPU_SAMPLECOUNT_8;
            break;  // clamp to 8
        default:
            m_sampleCount = SDL_GPU_SAMPLECOUNT_1;
            break;
    }

    // create color texture (resolve target when MSAA, or direct render target when not)
    // always needs SAMPLER usage since we read from it after rendering
    {
        SDL_GPUTextureCreateInfo colorInfo{};
        colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
        colorInfo.format = (SDL_GPUTextureFormat)SDLGPUInterface::DEFAULT_TEXTURE_FORMAT;
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        colorInfo.width = w;
        colorInfo.height = h;
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = 1;
        colorInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        m_colorTexture = SDL_CreateGPUTexture(m_device, &colorInfo);
    }

    if(!m_colorTexture) {
        debugLog("SDLGPURenderTarget Error: Couldn't create color texture: {}", SDL_GetError());
        return;
    }

    // create MSAA color texture if multisampled
    if(this->isMultiSampled()) {
        SDL_GPUTextureCreateInfo msaaInfo{};
        msaaInfo.type = SDL_GPU_TEXTURETYPE_2D;
        msaaInfo.format = (SDL_GPUTextureFormat)SDLGPUInterface::DEFAULT_TEXTURE_FORMAT;
        msaaInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;  // no SAMPLER - can't sample multisampled textures
        msaaInfo.width = w;
        msaaInfo.height = h;
        msaaInfo.layer_count_or_depth = 1;
        msaaInfo.num_levels = 1;
        msaaInfo.sample_count = (SDL_GPUSampleCount)m_sampleCount;

        m_msaaTexture = SDL_CreateGPUTexture(m_device, &msaaInfo);
        if(!m_msaaTexture) {
            debugLog("SDLGPURenderTarget Error: Couldn't create MSAA texture: {}", SDL_GetError());
            m_gpu->releaseTexture(m_colorTexture);
            return;
        }
    }

    // create depth texture (must match sample count)
    {
        SDL_GPUTextureCreateInfo depthInfo{};
        depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
        depthInfo.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
        depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        depthInfo.width = w;
        depthInfo.height = h;
        depthInfo.layer_count_or_depth = 1;
        depthInfo.num_levels = 1;
        depthInfo.sample_count = this->isMultiSampled() ? (SDL_GPUSampleCount)m_sampleCount : SDL_GPU_SAMPLECOUNT_1;

        m_depthTexture = SDL_CreateGPUTexture(m_device, &depthInfo);
    }

    if(!m_depthTexture) {
        debugLog("SDLGPURenderTarget Error: Couldn't create depth texture: {}", SDL_GetError());
        m_gpu->releaseTexture(m_msaaTexture);
        m_gpu->releaseTexture(m_colorTexture);
        return;
    }

    // create sampler for reading this RT as a texture
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    m_sampler = SDL_CreateGPUSampler(m_device, &samplerInfo);
    if(!m_sampler) {
        debugLog("SDLGPURenderTarget Error: Couldn't create sampler: {}", SDL_GetError());
        return;
    }

    this->setReady(true);
}

void SDLGPURenderTarget::initAsync() { this->setAsyncReady(true); }

void SDLGPURenderTarget::destroy() {
    if(!m_gpu || !m_device) return;

    m_gpu->releaseSampler(m_sampler);
    m_gpu->releaseTexture(m_depthTexture);
    m_gpu->releaseTexture(m_msaaTexture);
    m_gpu->releaseTexture(m_colorTexture);
}

void SDLGPURenderTarget::enable(bool clear) {
    if(unlikely(!m_gpu || !m_device || !this->isReady())) return;

    Color clearCol = this->clearColor;
    if(cv::debug_rt.getBool()) clearCol = argb(0.5f, 0.0f, 0.5f, 0.0f);

    // when MSAA: render into m_msaaTexture, resolve into m_colorTexture
    // when not MSAA: render directly into m_colorTexture
    SDL_GPUTexture *renderTex = m_msaaTexture ? m_msaaTexture : m_colorTexture;
    SDL_GPUTexture *resolveTex = m_msaaTexture ? m_colorTexture : nullptr;

    // NOTE: we currently always implicitly use SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM for the format
    m_gpu->pushRenderTarget(renderTex, m_depthTexture, clear, clearCol, resolveTex, m_sampleCount);
}

void SDLGPURenderTarget::disable() {
    if(unlikely(!m_gpu || !m_device || !this->isReady())) return;

    m_gpu->popRenderTarget();
}

void SDLGPURenderTarget::bind(unsigned int textureUnit) {
    if(unlikely(!m_gpu || !m_device || !this->isReady())) return;
    assert(textureUnit < Graphics::MAX_TEXTURE_UNITS);

    // save current binding for nested bind/unbind support
    m_boundUnit = textureUnit;
    m_prevTexture = m_gpu->getBoundTexture(textureUnit);
    m_prevSampler = m_gpu->getBoundSampler(textureUnit);

    m_gpu->setBoundTexture(textureUnit, m_colorTexture);
    m_gpu->setBoundSampler(textureUnit, m_sampler);
    m_gpu->setTexturing(true);
}

void SDLGPURenderTarget::unbind() {
    if(unlikely(!m_gpu || !m_device || !this->isReady())) return;

    m_gpu->setBoundTexture(m_boundUnit, m_prevTexture);
    m_gpu->setBoundSampler(m_boundUnit, m_prevSampler);
}

#endif
