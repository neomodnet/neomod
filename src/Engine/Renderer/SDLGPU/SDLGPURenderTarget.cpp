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
#include "Logging.h"
#include "VertexArrayObject.h"

#include "SDLGPUInterface.h"

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
            SDL_ReleaseGPUTexture(m_device, m_colorTexture);
            m_colorTexture = nullptr;
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
        if(m_msaaTexture) {
            SDL_ReleaseGPUTexture(m_device, m_msaaTexture);
            m_msaaTexture = nullptr;
        }
        SDL_ReleaseGPUTexture(m_device, m_colorTexture);
        m_colorTexture = nullptr;
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

    if(m_sampler) {
        SDL_ReleaseGPUSampler(m_device, m_sampler);
        m_sampler = nullptr;
    }
    if(m_depthTexture) {
        SDL_ReleaseGPUTexture(m_device, m_depthTexture);
        m_depthTexture = nullptr;
    }
    if(m_msaaTexture) {
        SDL_ReleaseGPUTexture(m_device, m_msaaTexture);
        m_msaaTexture = nullptr;
    }
    if(m_colorTexture) {
        SDL_ReleaseGPUTexture(m_device, m_colorTexture);
        m_colorTexture = nullptr;
    }
}

void SDLGPURenderTarget::draw(int x, int y) {
    if(!this->isReady()) return;

    bind();
    {
        m_gpu->setColor(this->color);
        m_gpu->drawQuad(x, y, (int)this->getSize().x, (int)this->getSize().y);
    }
    unbind();
}

void SDLGPURenderTarget::draw(int x, int y, int width, int height) {
    if(!this->isReady()) return;

    bind();
    {
        m_gpu->setColor(this->color);
        m_gpu->drawQuad(x, y, width, height);
    }
    unbind();
}

void SDLGPURenderTarget::drawRect(int x, int y, int width, int height) {
    if(!this->isReady()) return;

    const float texCoordWidth0 = (float)x / this->getSize().x;
    const float texCoordWidth1 = (float)(x + width) / this->getSize().x;
    const float texCoordHeight1 = (float)y / this->getSize().y;
    const float texCoordHeight0 = (float)(y + height) / this->getSize().y;

    bind();
    {
        m_gpu->setColor(this->color);

        static VertexArrayObject vao;
        vao.clear();

        vao.addTexcoord(texCoordWidth0, texCoordHeight1);
        vao.addVertex(x, y);

        vao.addTexcoord(texCoordWidth0, texCoordHeight0);
        vao.addVertex(x, y + height);

        vao.addTexcoord(texCoordWidth1, texCoordHeight0);
        vao.addVertex(x + width, y + height);

        vao.addTexcoord(texCoordWidth1, texCoordHeight0);
        vao.addVertex(x + width, y + height);

        vao.addTexcoord(texCoordWidth1, texCoordHeight1);
        vao.addVertex(x + width, y);

        vao.addTexcoord(texCoordWidth0, texCoordHeight1);
        vao.addVertex(x, y);

        m_gpu->drawVAO(&vao);
    }
    unbind();
}

void SDLGPURenderTarget::enable() {
    if(!m_gpu || !m_device || !this->isReady()) return;

    Color clearCol = this->clearColor;
    if(cv::debug_rt.getBool()) clearCol = argb(0.5f, 0.0f, 0.5f, 0.0f);

    // when MSAA: render into m_msaaTexture, resolve into m_colorTexture
    // when not MSAA: render directly into m_colorTexture
    SDL_GPUTexture *renderTex = m_msaaTexture ? m_msaaTexture : m_colorTexture;
    SDL_GPUTexture *resolveTex = m_msaaTexture ? m_colorTexture : nullptr;

    // NOTE: we currently always implicitly use SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM for the format
    m_gpu->pushRenderTarget(renderTex, m_depthTexture, this->bClearColorOnDraw, clearCol, resolveTex, m_sampleCount);
}

void SDLGPURenderTarget::disable() {
    if(!m_gpu || !m_device || !this->isReady()) return;

    m_gpu->popRenderTarget();
}

void SDLGPURenderTarget::bind(unsigned int /*textureUnit*/) {
    if(!m_gpu || !m_device || !this->isReady()) return;

    // backup current
    m_prevTexture = m_gpu->getBoundTexture();
    m_prevSampler = m_gpu->getBoundSampler();

    // bind this RT's color texture for sampling
    m_gpu->setBoundTexture(m_colorTexture);
    m_gpu->setBoundSampler(m_sampler);
    m_gpu->setTexturing(true);
}

void SDLGPURenderTarget::unbind() {
    if(!m_gpu || !m_device || !this->isReady()) return;

    m_gpu->setBoundTexture(m_prevTexture);
    m_gpu->setBoundSampler(m_prevSampler);
}

#endif
