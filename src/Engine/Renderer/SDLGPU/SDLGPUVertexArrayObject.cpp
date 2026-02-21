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
        if(!this->partialUpdateVertexIndices.empty()) {
            for(auto idx : this->partialUpdateVertexIndices) {
                m_convertedVertices[idx].pos = this->vertices[idx];
            }

            // re-upload
            void *mapped = SDL_MapGPUTransferBuffer(m_device, m_transferBuffer, true);
            if(mapped) {
                std::memcpy(mapped, m_convertedVertices.data(),
                            sizeof(SDLGPUSimpleVertex) * m_convertedVertices.size());
                SDL_UnmapGPUTransferBuffer(m_device, m_transferBuffer);
            }

            auto *cmdBuf = SDL_AcquireGPUCommandBuffer(m_device);
            if(cmdBuf) {
                auto *copyPass = SDL_BeginGPUCopyPass(cmdBuf);
                if(copyPass) {
                    SDL_GPUTransferBufferLocation src{.transfer_buffer = m_transferBuffer, .offset = 0};
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

            this->partialUpdateVertexIndices.clear();
            this->partialUpdateColorIndices.clear();
        }
        return;
    }

    if(m_vertexBuffer != nullptr && !this->bKeepInSystemMemory) return;

    // convert vertices (quads/fans → triangles)
    m_convertedVertices.clear();

    // maybe TODO: handle normals (not currently used in app code)
    std::vector<vec3> finalVertices = this->vertices;
    std::vector<vec2> finalTexcoords = this->texcoords;
    std::vector<vec4> colors;
    std::vector<vec4> finalColors;

    for(auto clr : this->colors) {
        const vec4 color = vec4(clr.Rf(), clr.Gf(), clr.Bf(), clr.Af());
        colors.push_back(color);
        finalColors.push_back(color);
    }
    const size_t maxColorIndex = (!finalColors.empty() ? finalColors.size() - 1 : 0);

    if(this->primitive == DrawPrimitive::QUADS) {
        finalVertices.clear();
        finalTexcoords.clear();
        finalColors.clear();
        m_convertedPrimitive = DrawPrimitive::TRIANGLES;

        if(this->vertices.size() > 3) {
            for(size_t i = 0; i < this->vertices.size(); i += 4) {
                finalVertices.push_back(this->vertices[i + 0]);
                finalVertices.push_back(this->vertices[i + 1]);
                finalVertices.push_back(this->vertices[i + 2]);

                if(!this->texcoords.empty()) {
                    finalTexcoords.push_back(this->texcoords[i + 0]);
                    finalTexcoords.push_back(this->texcoords[i + 1]);
                    finalTexcoords.push_back(this->texcoords[i + 2]);
                }

                if(!colors.empty()) {
                    finalColors.push_back(colors[std::clamp<size_t>(i + 0, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 1, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 2, 0, maxColorIndex)]);
                }

                finalVertices.push_back(this->vertices[i + 0]);
                finalVertices.push_back(this->vertices[i + 2]);
                finalVertices.push_back(this->vertices[i + 3]);

                if(!this->texcoords.empty()) {
                    finalTexcoords.push_back(this->texcoords[i + 0]);
                    finalTexcoords.push_back(this->texcoords[i + 2]);
                    finalTexcoords.push_back(this->texcoords[i + 3]);
                }

                if(!colors.empty()) {
                    finalColors.push_back(colors[std::clamp<size_t>(i + 0, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 2, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 3, 0, maxColorIndex)]);
                }
            }
        }
    } else if(this->primitive == DrawPrimitive::TRIANGLE_FAN) {
        finalVertices.clear();
        finalTexcoords.clear();
        finalColors.clear();
        m_convertedPrimitive = DrawPrimitive::TRIANGLES;

        if(this->vertices.size() > 2) {
            for(size_t i = 2; i < this->vertices.size(); i++) {
                finalVertices.push_back(this->vertices[0]);
                finalVertices.push_back(this->vertices[i]);
                finalVertices.push_back(this->vertices[i - 1]);

                if(!this->texcoords.empty()) {
                    finalTexcoords.push_back(this->texcoords[0]);
                    finalTexcoords.push_back(this->texcoords[i]);
                    finalTexcoords.push_back(this->texcoords[i - 1]);
                }

                if(!colors.empty()) {
                    finalColors.push_back(colors[std::clamp<size_t>(0, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i - 1, 0, maxColorIndex)]);
                }
            }
        }
    }

    // build SDLGPUSimpleVertex array
    m_convertedVertices.resize(finalVertices.size());
    this->iNumVertices = m_convertedVertices.size();

    const bool hasColors = !finalColors.empty();
    const bool hasTexCoords = (finalTexcoords.size() == m_convertedVertices.size());

    for(size_t i = 0; i < finalVertices.size(); i++) {
        m_convertedVertices[i].pos = finalVertices[i];

        if(hasColors)
            m_convertedVertices[i].col = finalColors[std::clamp<size_t>(i, 0, maxColorIndex)];
        else
            m_convertedVertices[i].col = vec4(1.f, 1.f, 1.f, 1.f);

        if(hasTexCoords)
            m_convertedVertices[i].tex = finalTexcoords[i];
        else
            m_convertedVertices[i].tex = vec2(0.f, 0.f);
    }

    const u32 bufSize = static_cast<u32>(sizeof(SDLGPUSimpleVertex) * m_convertedVertices.size());

    // create GPU buffer
    if(!m_vertexBuffer) {
        SDL_GPUBufferCreateInfo bufInfo{};
        bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufInfo.size = bufSize;

        m_vertexBuffer = SDL_CreateGPUBuffer(m_device, &bufInfo);
        if(!m_vertexBuffer) {
            debugLog("SDLGPUVertexArrayObject Error: Couldn't CreateGPUBuffer({})", m_convertedVertices.size());
            return;
        }
    }

    // create transfer buffer
    if(!m_transferBuffer) {
        SDL_GPUTransferBufferCreateInfo tbInfo{};
        tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbInfo.size = bufSize;

        m_transferBuffer = SDL_CreateGPUTransferBuffer(m_device, &tbInfo);
        if(!m_transferBuffer) return;
    }

    // upload data
    void *mapped = SDL_MapGPUTransferBuffer(m_device, m_transferBuffer, false);
    if(mapped) {
        std::memcpy(mapped, m_convertedVertices.data(), bufSize);
        SDL_UnmapGPUTransferBuffer(m_device, m_transferBuffer);
    }

    auto *cmdBuf = SDL_AcquireGPUCommandBuffer(m_device);
    if(cmdBuf) {
        auto *copyPass = SDL_BeginGPUCopyPass(cmdBuf);
        if(copyPass) {
            SDL_GPUTransferBufferLocation src{.transfer_buffer = m_transferBuffer, .offset = 0};
            SDL_GPUBufferRegion dst{.buffer = m_vertexBuffer, .offset = 0, .size = bufSize};
            SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
            SDL_EndGPUCopyPass(copyPass);
        }
        SDL_SubmitGPUCommandBuffer(cmdBuf);
    }

    // free system memory
    if(!this->bKeepInSystemMemory) {
        this->clear();
        m_convertedVertices.clear();
    }

    this->setReady(true);
}

void SDLGPUVertexArrayObject::initAsync() { this->setAsyncReady(true); }

void SDLGPUVertexArrayObject::destroy() {
    VertexArrayObject::destroy();

    if(m_transferBuffer) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_transferBuffer);
        m_transferBuffer = nullptr;
    }
    if(m_vertexBuffer) {
        SDL_ReleaseGPUBuffer(m_device, m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }

    m_convertedVertices.clear();
}

void SDLGPUVertexArrayObject::draw() {
    if(!m_gpu || !m_device || !this->isReady()) return;

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
