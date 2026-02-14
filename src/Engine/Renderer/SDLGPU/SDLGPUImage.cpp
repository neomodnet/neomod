//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		SDL_gpu implementation of Image
//
// $NoKeywords: $sdlgpuimg
//===============================================================================//
#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU

#include <SDL3/SDL_gpu.h>

#include "SDLGPUImage.h"

#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"

#include "SDLGPUInterface.h"

#include <cstring>

namespace {
[[nodiscard]] constexpr bool operator==(const SDL_GPUTransferBufferCreateInfo &a,
                                        const SDL_GPUTransferBufferCreateInfo &b) noexcept {
    // clang-format off
    return a.usage == b.usage &&
           a.size == b.size &&
           a.props == b.props;
    // clang-format on
}

[[nodiscard]] constexpr bool operator==(const SDL_GPUSamplerCreateInfo &a, const SDL_GPUSamplerCreateInfo &b) noexcept {
    // clang-format off
    return a.min_filter == b.min_filter &&
           a.mag_filter == b.mag_filter &&
           a.mipmap_mode == b.mipmap_mode &&
           a.address_mode_u == b.address_mode_u &&
           a.address_mode_v == b.address_mode_v &&
           a.address_mode_w == b.address_mode_w &&
           a.mip_lod_bias == b.mip_lod_bias &&
           a.max_anisotropy == b.max_anisotropy &&
           a.compare_op == b.compare_op &&
           a.min_lod == b.min_lod &&
           a.max_lod == b.max_lod &&
           a.enable_anisotropy == b.enable_anisotropy &&
           a.enable_compare == b.enable_compare &&
           a.props == b.props;
    // clang-format on
}
}  // namespace

SDLGPUImage::SDLGPUImage(std::string filepath, bool mipmapped, bool keepInSystemMemory)
    : Image(std::move(filepath), mipmapped, keepInSystemMemory),
      m_lastSamplerCreateInfo(std::make_unique<SDL_GPUSamplerCreateInfo>()),
      m_lastTransferBufferCreateInfo(std::make_unique<SDL_GPUTransferBufferCreateInfo>()) {}

SDLGPUImage::SDLGPUImage(int width, int height, bool mipmapped, bool keepInSystemMemory)
    : Image(width, height, mipmapped, keepInSystemMemory),
      m_lastSamplerCreateInfo(std::make_unique<SDL_GPUSamplerCreateInfo>()),
      m_lastTransferBufferCreateInfo(std::make_unique<SDL_GPUTransferBufferCreateInfo>()) {}

SDLGPUImage::~SDLGPUImage() {
    this->destroy();

    auto *device = static_cast<SDLGPUInterface *>(g.get())->getDevice();

    if(m_uploadFence) {
        SDL_WaitForGPUFences(device, false, &m_uploadFence, 1);
        SDL_ReleaseGPUFence(device, m_uploadFence);
        m_uploadFence = nullptr;
    }

    if(m_transferBuf) SDL_ReleaseGPUTransferBuffer(device, m_transferBuf);
    if(m_sampler) SDL_ReleaseGPUSampler(device, m_sampler);
    if(m_texture) SDL_ReleaseGPUTexture(device, m_texture);

    this->rawImage.clear();
}

void SDLGPUImage::init() {
    if(!this->isAsyncReady()) return;
    auto *device = static_cast<SDLGPUInterface *>(g.get())->getDevice();

    // wait for async GPU upload to finish
    if(m_uploadFence) {
        SDL_WaitForGPUFences(device, false, &m_uploadFence, 1);
        SDL_ReleaseGPUFence(device, m_uploadFence);
        m_uploadFence = nullptr;
    }
    this->resetDirtyRegion();

    // free raw image
    if(!this->bKeepInSystemMemory) this->rawImage.clear();

    this->setReady(m_texture != nullptr && m_sampler != nullptr);
}

void SDLGPUImage::uploadPixelData(SDL_GPUDevice *device) {
    const u32 totalBytes = (u32)this->totalBytes();

    // reuse persistent transfer buffer for images kept in system memory
    auto *transferBuf = m_transferBuf;
    {
        SDL_GPUTransferBufferCreateInfo tbInfo{};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = (u32)totalBytes;
        bool infoMismatch = false;
        if(!transferBuf || (infoMismatch = (*m_lastTransferBufferCreateInfo != tbInfo))) {
            if(infoMismatch) {
                SDL_ReleaseGPUTransferBuffer(device, transferBuf);
                transferBuf = nullptr;
            }

            transferBuf = SDL_CreateGPUTransferBuffer(device, &tbInfo);
            if(!transferBuf) return;

            if(this->bKeepInSystemMemory) {
                m_transferBuf = transferBuf;
                *m_lastTransferBufferCreateInfo = tbInfo;
            }
        }
    }

    auto dirtyRects = this->getDirtyRects();

    // map and copy pixel data
    void *mapped = SDL_MapGPUTransferBuffer(device, transferBuf, false);
    if(!mapped) return;

    const bool fullImage = dirtyRects.size() == 1 && (u32)dirtyRects[0].getWidth() == (u32)this->iWidth &&
                           (u32)dirtyRects[0].getHeight() == (u32)this->iHeight;

    // for multi-rect: track packed offset per rect
    std::vector<u32> rectOffsets;

    if(fullImage) {
        std::memcpy(mapped, this->rawImage.get(), totalBytes);
    } else {
        // pack each rect's rows tightly into the transfer buffer
        const sSz srcStride = (sSz)this->iWidth * Image::NUM_CHANNELS;
        u8 *dst = static_cast<u8 *>(mapped);
        const u8 *src = this->rawImage.get();
        u32 offset = 0;

        rectOffsets.reserve(dirtyRects.size());
        for(const auto &rect : dirtyRects) {
            rectOffsets.push_back(offset);
            const sSz rx = (sSz)rect.getMinX();
            const sSz ry = (sSz)rect.getMinY();
            const sSz rw = (sSz)rect.getWidth();
            const sSz rh = (sSz)rect.getHeight();
            const sSz rowBytes = rw * Image::NUM_CHANNELS;

            for(sSz y = 0; y < rh; y++) {
                std::memcpy(dst + offset, src + (ry + y) * srcStride + rx * Image::NUM_CHANNELS, rowBytes);
                offset += (u32)rowBytes;
            }
        }
    }

    SDL_UnmapGPUTransferBuffer(device, transferBuf);

    // upload via copy pass, then generate mipmaps
    auto *cmdBuf = SDL_AcquireGPUCommandBuffer(device);
    if(cmdBuf) {
        auto *copyPass = SDL_BeginGPUCopyPass(cmdBuf);
        if(copyPass) {
            if(fullImage) {
                SDL_GPUTextureTransferInfo src{};
                src.transfer_buffer = transferBuf;

                SDL_GPUTextureRegion dst{};
                dst.texture = m_texture;
                dst.w = (u32)this->iWidth;
                dst.h = (u32)this->iHeight;
                dst.d = 1;

                SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
            } else {
                for(size_t i = 0; i < dirtyRects.size(); i++) {
                    const auto &rect = dirtyRects[i];

                    SDL_GPUTextureTransferInfo src{};
                    src.transfer_buffer = transferBuf;
                    src.offset = rectOffsets[i];
                    src.pixels_per_row = (u32)rect.getWidth();

                    SDL_GPUTextureRegion dst{};
                    dst.texture = m_texture;
                    dst.x = (u32)rect.getMinX();
                    dst.y = (u32)rect.getMinY();
                    dst.w = (u32)rect.getWidth();
                    dst.h = (u32)rect.getHeight();
                    dst.d = 1;

                    SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
                }
            }
            SDL_EndGPUCopyPass(copyPass);
        }

        if(this->bMipmapped) {
            SDL_GenerateMipmapsForGPUTexture(cmdBuf, m_texture);
        }

        m_uploadFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
    }

    if(!this->bKeepInSystemMemory) SDL_ReleaseGPUTransferBuffer(device, transferBuf);
}

void SDLGPUImage::initAsync() {
    if(m_texture != nullptr && !this->bKeepInSystemMemory) {
        this->setAsyncReady(true);
        return;
    }

    // load raw image from file if needed
    if(!this->bCreatedImage) {
        logIfCV(debug_rm, "Resource Manager: Loading {:s}", this->sFilePath);
        if(!loadRawImage()) {
            this->setAsyncReady(false);
            return;
        }
    }

    if(this->isInterrupted()) return;

    auto *device = static_cast<SDLGPUInterface *>(g.get())->getDevice();

    // sanity: if we have an existing upload fence, wait on it
    if(m_uploadFence) {
        SDL_WaitForGPUFences(device, false, &m_uploadFence, 1);
        SDL_ReleaseGPUFence(device, m_uploadFence);
        m_uploadFence = nullptr;
    }

    // create sampler (applies current filter/wrap mode)
    if(m_sampler == nullptr) {
        if(this->filterMode != TextureFilterMode::LINEAR) setFilterMode(this->filterMode);
        if(this->wrapMode != TextureWrapMode::CLAMP) setWrapMode(this->wrapMode);
        createOrUpdateSampler();
    }

    if(!m_sampler) {
        debugLog("SDLGPUImage Error: Couldn't CreateGPUSampler() on {}!", this->getDebugIdentifier());
        SDL_ReleaseGPUTexture(device, m_texture);
        m_texture = nullptr;
        this->setAsyncReady(false);
        return;
    }

    // calculate mip levels: cap to 32px smallest mipmap (same as OpenGL/DX11)
    const u32 maxDim = (u32)std::max(this->iWidth, this->iHeight);
    const u32 mipLevels =
        this->bMipmapped ? (u32)std::max(2, (int)std::floor(std::log2(maxDim)) - 4) : 1;  // must be >= 1 if mipmapped

    // create texture (or re-upload to existing)
    if(m_texture == nullptr) {
        SDL_GPUTextureCreateInfo texInfo{};
        texInfo.type = SDL_GPU_TEXTURETYPE_2D;
        texInfo.format = (SDL_GPUTextureFormat)SDLGPUInterface::DEFAULT_TEXTURE_FORMAT;
        texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if(this->bMipmapped) texInfo.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;  // needed for GenerateMipmaps
        texInfo.width = (u32)this->iWidth;
        texInfo.height = (u32)this->iHeight;
        texInfo.layer_count_or_depth = 1;
        texInfo.num_levels = mipLevels;
        texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        m_texture = SDL_CreateGPUTexture(device, &texInfo);
        if(!m_texture) {
            debugLog("SDLGPUImage Error: Couldn't CreateGPUTexture() on file {:s}: {}", this->sFilePath,
                     SDL_GetError());
            this->setAsyncReady(false);
            return;
        }
    }

    // upload pixel data (and generate mipmaps if needed)
    if(this->totalBytes() >= (u64)this->iWidth * this->iHeight * Image::NUM_CHANNELS) {
        this->uploadPixelData(device);
    }

    this->setAsyncReady(true);
}

void SDLGPUImage::destroy() {
    if(!this->bKeepInSystemMemory) {
        auto *device = static_cast<SDLGPUInterface *>(g.get())->getDevice();

        if(m_uploadFence) {
            SDL_WaitForGPUFences(device, false, &m_uploadFence, 1);
            SDL_ReleaseGPUFence(device, m_uploadFence);
            m_uploadFence = nullptr;
        }

        if(m_texture) {
            SDL_ReleaseGPUTexture(device, m_texture);
            m_texture = nullptr;
        }

        this->rawImage.clear();
    }
}

void SDLGPUImage::bind(unsigned int /*textureUnit*/) const {
    if(!this->isReady()) return;

    auto *gpu = static_cast<SDLGPUInterface *>(g.get());

    // backup current
    m_prevTexture = gpu->getBoundTexture();
    m_prevSampler = gpu->getBoundSampler();

    // bind
    gpu->setBoundTexture(m_texture);
    gpu->setBoundSampler(m_sampler);
    gpu->setTexturing(true);
}

void SDLGPUImage::unbind() const {
    if(!this->isReady()) return;

    auto *gpu = static_cast<SDLGPUInterface *>(g.get());
    gpu->setBoundTexture(m_prevTexture);
    gpu->setBoundSampler(m_prevSampler);
}

void SDLGPUImage::setFilterMode(TextureFilterMode newFilterMode) {
    Image::setFilterMode(newFilterMode);
    if(!this->isReady()) return;
    createOrUpdateSampler();
}

void SDLGPUImage::setWrapMode(TextureWrapMode newWrapMode) {
    Image::setWrapMode(newWrapMode);
    if(!this->isReady()) return;
    createOrUpdateSampler();
}

void SDLGPUImage::createOrUpdateSampler() {
    auto *device = static_cast<SDLGPUInterface *>(g.get())->getDevice();
    {
        SDL_GPUSamplerCreateInfo samplerInfo{};

        switch(this->filterMode) {
            case TextureFilterMode::NONE:
                samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
                samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
                samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
                break;
            case TextureFilterMode::LINEAR:
            case TextureFilterMode::MIPMAP:
                samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
                samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
                samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
                break;
        }

        switch(this->wrapMode) {
            case TextureWrapMode::CLAMP:
                samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                break;
            case TextureWrapMode::REPEAT:
                samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
                samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
                samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
                break;
        }

        samplerInfo.max_lod = this->bMipmapped ? 1000.0f : 0.0f;
        if(m_sampler) {
            // nothing to do
            if(*m_lastSamplerCreateInfo == samplerInfo) {
                return;
            }
            SDL_ReleaseGPUSampler(device, m_sampler);
            m_sampler = nullptr;
        }
        *m_lastSamplerCreateInfo = samplerInfo;
    }

    m_sampler = SDL_CreateGPUSampler(device, &*m_lastSamplerCreateInfo);
}

#endif
