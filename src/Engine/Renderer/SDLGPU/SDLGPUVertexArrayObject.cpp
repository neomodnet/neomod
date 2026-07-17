//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		SDL_gpu baking support for vao
//
// $NoKeywords: $sdlgpuvao
//===============================================================================//
#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU

#include <SDL3/SDL_gpu.h>

#include "SDLGPUVertexArrayObject.h"
#include "SDLGPUInterface.h"

#include "Engine.h"
#include "Logging.h"

#include <cstring>
#include <span>

SDLGPUVertexArrayObject::SDLGPUVertexArrayObject(SDLGPUInterface *gpu, SDL_GPUDevice *device, DrawPrimitive primitive,
                                                 DrawUsageType usage, bool keepInSystemMemory)
    : VertexArrayObject(primitive, usage, keepInSystemMemory),
      m_gpu(gpu),
      m_device(device),
      m_convertedPrimitive(primitive) {}
SDLGPUVertexArrayObject::~SDLGPUVertexArrayObject() { destroy(); }

void SDLGPUVertexArrayObject::init() {
    if(!m_gpu || !m_device || !this->isAsyncReady() || this->vertices.size() < 2) return;

    // if already ready, handle partial updates by re-uploading
    if(this->isReady()) {
        // TODO: colors?
        if(!this->partialUpdateVertexIndices.empty()) {
            for(auto idx : this->partialUpdateVertexIndices) {
                m_convertedVertices[idx].pos = this->vertices[idx];
            }

            // re-upload
            u32 pooledSize = 0;
            const u32 bufSize = static_cast<u32>(sizeof(SDLGPUSimpleVertex) * m_convertedVertices.size());
            SDL_GPUTransferBuffer *transferBuffer = m_gpu->acquireUploadTransferBuffer(bufSize, pooledSize);
            if(!transferBuffer) {
                engine->showMessageError("SDLGPUVertexArrayObject ERROR",
                                         "Failed to acquire vertex upload transfer buffer!");
                this->partialUpdateVertexIndices.clear();
                this->partialUpdateColorIndices.clear();
                return;
            }
            if(void *mapped = SDL_MapGPUTransferBuffer(m_device, transferBuffer, true)) {
                std::memcpy(mapped, m_convertedVertices.data(),
                            sizeof(SDLGPUSimpleVertex) * m_convertedVertices.size());
                SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);
            }

            auto *cmdBuf = SDL_AcquireGPUCommandBuffer(m_device);
            if(cmdBuf) {
                auto *copyPass = SDL_BeginGPUCopyPass(cmdBuf);
                if(copyPass) {
                    SDL_GPUTransferBufferLocation src{.transfer_buffer = transferBuffer, .offset = 0};
                    SDL_GPUBufferRegion dst{
                        .buffer = m_vertexBuffer,
                        .offset = 0,
                        .size = static_cast<Uint32>(sizeof(SDLGPUSimpleVertex) * m_convertedVertices.size()),
                    };
                    SDL_UploadToGPUBuffer(copyPass, &src, &dst, true);
                    SDL_EndGPUCopyPass(copyPass);
                }
                SDL_SubmitGPUCommandBuffer(cmdBuf);
            }

            m_gpu->releaseUploadTransferBuffer(transferBuffer, pooledSize);

            this->partialUpdateVertexIndices.clear();
            this->partialUpdateColorIndices.clear();
        }
        return;
    }

    if(m_vertexBuffer != nullptr && !this->bKeepInSystemMemory) return;

    // convert primitives (quads/fans → triangles) and set up source data spans.
    // for TRIANGLES, this avoids copying into intermediate vectors
    // entirely, and for !keepInSystemMemory, interleaves directly into the mapped transfer buffer.
    std::vector<vec3> convVertices;
    std::vector<vec2> convTexcoords;
    std::vector<vec4> convColors;

    std::span<const vec3> srcVerts;
    std::span<const vec2> srcTCs;
    std::span<const vec4> srcColors;

    constexpr const auto toVec4 = [] [[gnu::always_inline]] (Color c) { return vec4(c.Rf(), c.Gf(), c.Bf(), c.Af()); };

    if(this->primitive == DrawPrimitive::QUADS) {
        m_convertedPrimitive = DrawPrimitive::TRIANGLES;

        if(this->vertices.size() > 3) {
            const size_t numQuads = this->vertices.size() / 4;
            convVertices.reserve(numQuads * 6);
            if(!this->texcoords.empty()) convTexcoords.reserve(numQuads * 6);

            const size_t maxColorIdx = this->colors.empty() ? 0 : this->colors.size() - 1;
            if(!this->colors.empty()) convColors.reserve(numQuads * 6);

            for(size_t i = 0; i < this->vertices.size(); i += 4) {
                convVertices.push_back(this->vertices[i + 0]);
                convVertices.push_back(this->vertices[i + 1]);
                convVertices.push_back(this->vertices[i + 2]);
                convVertices.push_back(this->vertices[i + 0]);
                convVertices.push_back(this->vertices[i + 2]);
                convVertices.push_back(this->vertices[i + 3]);

                if(!this->texcoords.empty()) {
                    convTexcoords.push_back(this->texcoords[i + 0]);
                    convTexcoords.push_back(this->texcoords[i + 1]);
                    convTexcoords.push_back(this->texcoords[i + 2]);
                    convTexcoords.push_back(this->texcoords[i + 0]);
                    convTexcoords.push_back(this->texcoords[i + 2]);
                    convTexcoords.push_back(this->texcoords[i + 3]);
                }

                if(!this->colors.empty()) {
                    convColors.push_back(toVec4(this->colors[std::min(i + 0, maxColorIdx)]));
                    convColors.push_back(toVec4(this->colors[std::min(i + 1, maxColorIdx)]));
                    convColors.push_back(toVec4(this->colors[std::min(i + 2, maxColorIdx)]));
                    convColors.push_back(toVec4(this->colors[std::min(i + 0, maxColorIdx)]));
                    convColors.push_back(toVec4(this->colors[std::min(i + 2, maxColorIdx)]));
                    convColors.push_back(toVec4(this->colors[std::min(i + 3, maxColorIdx)]));
                }
            }
        }
        srcVerts = convVertices;
        srcTCs = convTexcoords;
        srcColors = convColors;
    } else if(this->primitive == DrawPrimitive::TRIANGLE_FAN) {
        m_convertedPrimitive = DrawPrimitive::TRIANGLES;

        if(this->vertices.size() > 2) {
            const size_t numTris = this->vertices.size() - 2;
            convVertices.reserve(numTris * 3);
            if(!this->texcoords.empty()) convTexcoords.reserve(numTris * 3);

            const size_t maxColorIdx = this->colors.empty() ? 0 : this->colors.size() - 1;
            if(!this->colors.empty()) convColors.reserve(numTris * 3);

            for(size_t i = 2; i < this->vertices.size(); i++) {
                convVertices.push_back(this->vertices[0]);
                convVertices.push_back(this->vertices[i]);
                convVertices.push_back(this->vertices[i - 1]);

                if(!this->texcoords.empty()) {
                    convTexcoords.push_back(this->texcoords[0]);
                    convTexcoords.push_back(this->texcoords[i]);
                    convTexcoords.push_back(this->texcoords[i - 1]);
                }

                if(!this->colors.empty()) {
                    convColors.push_back(toVec4(this->colors[std::min<size_t>(0, maxColorIdx)]));
                    convColors.push_back(toVec4(this->colors[std::min(i, maxColorIdx)]));
                    convColors.push_back(toVec4(this->colors[std::min(i - 1, maxColorIdx)]));
                }
            }
        }
        srcVerts = convVertices;
        srcTCs = convTexcoords;
        srcColors = convColors;
    } else {
        srcVerts = this->vertices;
        srcTCs = this->texcoords;

        if(!this->colors.empty()) {
            const size_t maxColorIdx = this->colors.size() - 1;
            convColors.resize(this->vertices.size());
            for(size_t i = 0; i < this->vertices.size(); i++) {
                convColors[i] = toVec4(this->colors[std::min(i, maxColorIdx)]);
            }
            srcColors = convColors;
        }
    }

    const size_t numVerts = srcVerts.size();
    if(numVerts == 0) return;

    this->iNumVertices = numVerts;
    const u32 bufSize = static_cast<u32>(sizeof(SDLGPUSimpleVertex) * numVerts);

    // create GPU buffer
    if(!m_vertexBuffer) {
        SDL_GPUBufferCreateInfo bufInfo{};
        bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufInfo.size = bufSize;

        m_vertexBuffer = SDL_CreateGPUBuffer(m_device, &bufInfo);
        if(!m_vertexBuffer) {
            debugLog("SDLGPUVertexArrayObject Error: Couldn't CreateGPUBuffer({})", numVerts);
            return;
        }
    }

    // get temp transfer buffer
    u32 pooledSize = 0;
    SDL_GPUTransferBuffer *transferBuffer = m_gpu->acquireUploadTransferBuffer(bufSize, pooledSize);
    if(!transferBuffer) {
        engine->showMessageError("SDLGPUVertexArrayObject ERROR", "Failed to acquire vertex upload transfer buffer!");
        return;
    }

    // interleave source data into SDLGPUSimpleVertex layout
    const bool hasColors = !srcColors.empty();
    const bool hasTCs = (srcTCs.size() == numVerts);
    const size_t maxColorIdx = hasColors ? srcColors.size() - 1 : 0;

    auto interleave = [&](SDLGPUSimpleVertex *dst) {
        if(hasColors && hasTCs) {
            for(size_t i = 0; i < numVerts; i++) {
                dst[i].pos = srcVerts[i];
                dst[i].col = srcColors[std::min(i, maxColorIdx)];
                dst[i].tex = srcTCs[i];
            }
        } else if(hasColors) {
            for(size_t i = 0; i < numVerts; i++) {
                dst[i].pos = srcVerts[i];
                dst[i].col = srcColors[std::min(i, maxColorIdx)];
                dst[i].tex = vec2(0.f, 0.f);
            }
        } else if(hasTCs) {
            for(size_t i = 0; i < numVerts; i++) {
                dst[i].pos = srcVerts[i];
                dst[i].col = vec4(1.f, 1.f, 1.f, 1.f);
                dst[i].tex = srcTCs[i];
            }
        } else {
            for(size_t i = 0; i < numVerts; i++) {
                dst[i].pos = srcVerts[i];
                dst[i].col = vec4(1.f, 1.f, 1.f, 1.f);
                dst[i].tex = vec2(0.f, 0.f);
            }
        }
    };

    if(this->bKeepInSystemMemory) {
        m_convertedVertices.resize(numVerts);
        interleave(m_convertedVertices.data());

        if(void *mapped = SDL_MapGPUTransferBuffer(m_device, transferBuffer, true)) {
            std::memcpy(mapped, m_convertedVertices.data(), bufSize);
            SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);
        }
    } else {
        if(void *mapped = SDL_MapGPUTransferBuffer(m_device, transferBuffer, true)) {
            interleave(static_cast<SDLGPUSimpleVertex *>(mapped));
            SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);
        }
    }

    if(auto *cmdBuf = SDL_AcquireGPUCommandBuffer(m_device)) {
        if(auto *copyPass = SDL_BeginGPUCopyPass(cmdBuf)) {
            SDL_GPUTransferBufferLocation src{.transfer_buffer = transferBuffer, .offset = 0};
            SDL_GPUBufferRegion dst{.buffer = m_vertexBuffer, .offset = 0, .size = bufSize};
            SDL_UploadToGPUBuffer(copyPass, &src, &dst, this->bKeepInSystemMemory);
            SDL_EndGPUCopyPass(copyPass);
        }
        SDL_SubmitGPUCommandBuffer(cmdBuf);
    }

    m_gpu->releaseUploadTransferBuffer(transferBuffer, pooledSize);

    // free system memory
    if(!this->bKeepInSystemMemory) {
        this->clear();
        // clear() re-derives iNumVertices from the unconverted source vertices; restore the converted count
        // so draw()'s range math matches the uploaded buffer (fans/quads are baked as triangle lists)
        this->iNumVertices = numVerts;
        m_convertedVertices.clear();
        m_convertedVertices.shrink_to_fit();
    }

    this->setReady(true);
}

void SDLGPUVertexArrayObject::initAsync() { this->setAsyncReady(true); }

void SDLGPUVertexArrayObject::destroy() {
    VertexArrayObject::destroy();

    if(m_vertexBuffer) {
        SDL_ReleaseGPUBuffer(m_device, m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }

    m_convertedVertices.clear();
}

void SDLGPUVertexArrayObject::draw() {
    if(unlikely(!m_gpu || !m_device || !this->isReady())) return;

    const int start = std::clamp<int>(this->iDrawRangeFromIndex > -1
                                          ? this->iDrawRangeFromIndex
                                          : nearestMultipleUp((int)(this->iNumVertices * this->fDrawPercentFromPercent),
                                                              this->iDrawPercentNearestMultiple),
                                      0, this->iNumVertices);
    const int end = std::clamp<int>(this->iDrawRangeToIndex > -1
                                        ? this->iDrawRangeToIndex
                                        : nearestMultipleDown((int)(this->iNumVertices * this->fDrawPercentToPercent),
                                                              this->iDrawPercentNearestMultiple),
                                    0, this->iNumVertices);

    if(start >= end) return;

    m_gpu->recordBakedDraw(m_vertexBuffer, (u32)start, (u32)(end - start), m_convertedPrimitive);
}

#endif
