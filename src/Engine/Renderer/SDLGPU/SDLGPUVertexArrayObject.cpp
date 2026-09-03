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

#include <algorithm>
#include <cstring>
#include <span>

namespace {
// invokes fn(convertedIndex) for every slot of the baked buffer which is fed by the given source vertex.
// quads bake 4 corners into 6 triangle vertices (corners 0 and 2 are duplicated), fans bake into one triangle
// per spoke with source vertex 0 as the hub shared by all of them
template <typename F>
void forEachConvertedSlot(DrawPrimitive srcPrimitive, u32 srcIdx, u32 numSrcVerts, u32 numConvVerts, const F &fn) {
    if(srcIdx >= numSrcVerts) return;

    switch(srcPrimitive) {
        case DrawPrimitive::QUADS: {
            const u32 base = (srcIdx / 4) * 6;
            switch(srcIdx % 4) {
                case 0:
                    fn(base + 0);
                    fn(base + 3);
                    break;
                case 1:
                    fn(base + 1);
                    break;
                case 2:
                    fn(base + 2);
                    fn(base + 4);
                    break;
                default:
                    fn(base + 5);
                    break;
            }
            break;
        }
        case DrawPrimitive::TRIANGLE_FAN:
            if(srcIdx == 0) {
                for(u32 i = 0; i < numConvVerts; i += 3) fn(i);
            } else {
                if(srcIdx >= 2) fn((srcIdx - 2) * 3 + 1);
                if(srcIdx + 1 < numSrcVerts) fn((srcIdx - 1) * 3 + 2);
            }
            break;
        default:
            fn(srcIdx);
            break;
    }
}
}  // namespace

SDLGPUVertexArrayObject::SDLGPUVertexArrayObject(SDLGPUInterface *gpu, SDL_GPUDevice *device, DrawPrimitive primitive,
                                                 DrawUsageType usage, bool keepInSystemMemory)
    : VertexArrayObject(primitive, usage, keepInSystemMemory),
      m_gpu(gpu),
      m_device(device),
      m_convertedPrimitive(primitive) {}
SDLGPUVertexArrayObject::~SDLGPUVertexArrayObject() { destroy(); }

void SDLGPUVertexArrayObject::init() {
    if(!m_gpu || !m_device || !this->isAsyncReady() || this->vertices.size() < 2) return;

    // if already ready, only push the modified regions back to the gpu
    if(this->isReady()) {
        uploadPartialUpdates();
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
            // a trailing partial quad can't be baked, so don't read past the last complete one
            const size_t numQuadVerts = numQuads * 4;
            convVertices.reserve(numQuads * 6);

            // partial texcoords can't be expanded alongside the quads, so ignore them entirely (as the default path does)
            const bool hasTCs = this->texcoords.size() >= this->vertices.size();
            if(hasTCs) convTexcoords.reserve(numQuads * 6);

            const size_t maxColorIdx = this->colors.empty() ? 0 : this->colors.size() - 1;
            if(!this->colors.empty()) convColors.reserve(numQuads * 6);

            for(size_t i = 0; i < numQuadVerts; i += 4) {
                convVertices.push_back(this->vertices[i + 0]);
                convVertices.push_back(this->vertices[i + 1]);
                convVertices.push_back(this->vertices[i + 2]);
                convVertices.push_back(this->vertices[i + 0]);
                convVertices.push_back(this->vertices[i + 2]);
                convVertices.push_back(this->vertices[i + 3]);

                if(hasTCs) {
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

            const bool hasTCs = this->texcoords.size() >= this->vertices.size();
            if(hasTCs) convTexcoords.reserve(numTris * 3);

            const size_t maxColorIdx = this->colors.empty() ? 0 : this->colors.size() - 1;
            if(!this->colors.empty()) convColors.reserve(numTris * 3);

            for(size_t i = 2; i < this->vertices.size(); i++) {
                convVertices.push_back(this->vertices[0]);
                convVertices.push_back(this->vertices[i]);
                convVertices.push_back(this->vertices[i - 1]);

                if(hasTCs) {
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

    // create GPU buffer (a previous bake may have left a smaller one behind)
    if(m_vertexBuffer && m_vertexBufferSize < bufSize) {
        SDL_ReleaseGPUBuffer(m_device, m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }
    if(!m_vertexBuffer) {
        SDL_GPUBufferCreateInfo bufInfo{};
        bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bufInfo.size = bufSize;

        m_vertexBuffer = SDL_CreateGPUBuffer(m_device, &bufInfo);
        if(!m_vertexBuffer) {
            debugLog("SDLGPUVertexArrayObject Error: Couldn't CreateGPUBuffer({})", numVerts);
            return;
        }
        m_vertexBufferSize = bufSize;
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

        if(void *mapped = SDL_MapGPUTransferBuffer(m_device, transferBuffer, false)) {
            std::memcpy(mapped, m_convertedVertices.data(), bufSize);
            SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);
        }
    } else {
        if(void *mapped = SDL_MapGPUTransferBuffer(m_device, transferBuffer, false)) {
            interleave(static_cast<SDLGPUSimpleVertex *>(mapped));
            SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);
        }
    }

    SDL_GPUFence *fence = nullptr;
    if(auto *cmdBuf = SDL_AcquireGPUCommandBuffer(m_device)) {
        if(auto *copyPass = SDL_BeginGPUCopyPass(cmdBuf)) {
            SDL_GPUTransferBufferLocation src{.transfer_buffer = transferBuffer, .offset = 0};
            SDL_GPUBufferRegion dst{.buffer = m_vertexBuffer, .offset = 0, .size = bufSize};
            SDL_UploadToGPUBuffer(copyPass, &src, &dst, this->bKeepInSystemMemory);
            SDL_EndGPUCopyPass(copyPass);
        }
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
    }

    m_gpu->releaseUploadTransferBuffer(transferBuffer, pooledSize, fence);

    // free system memory
    if(!this->bKeepInSystemMemory) {
        this->clear();
        // clear() re-derives iNumVertices from the unconverted source vertices; restore the converted count
        // so draw()'s range math matches the uploaded buffer (fans/quads are baked as triangle lists)
        this->iNumVertices = numVerts;
        m_convertedVertices.clear();
        m_convertedVertices.shrink_to_fit();
    } else {
        // anything set before the first bake is already part of the upload above
        this->partialUpdateVertexIndices.clear();
        this->partialUpdateColorIndices.clear();
    }

    this->setReady(true);
}

void SDLGPUVertexArrayObject::uploadPartialUpdates() {
    const u32 numConv = static_cast<u32>(m_convertedVertices.size());
    if(numConv < 1 || !m_vertexBuffer) {
        this->partialUpdateVertexIndices.clear();
        this->partialUpdateColorIndices.clear();
        return;
    }

    // the baked buffer is authoritative for how many source vertices actually made it in, the source arrays
    // may have grown (or have a trailing partial primitive) which the buffer has no room for
    u32 numBakedSrc = numConv;
    if(this->primitive == DrawPrimitive::QUADS)
        numBakedSrc = (numConv / 6) * 4;
    else if(this->primitive == DrawPrimitive::TRIANGLE_FAN)
        numBakedSrc = (numConv / 3) + 2;

    m_dirtyIndices.clear();

    for(const int idx : this->partialUpdateVertexIndices) {
        const auto srcIdx = static_cast<u32>(idx);
        if(srcIdx >= this->vertices.size()) continue;

        const vec3 pos = this->vertices[srcIdx];
        forEachConvertedSlot(this->primitive, srcIdx, numBakedSrc, numConv, [&](u32 convIdx) {
            m_convertedVertices[convIdx].pos = pos;
            m_dirtyIndices.push_back(convIdx);
        });
    }

    if(!this->colors.empty()) {
        const auto maxColorIdx = static_cast<u32>(this->colors.size() - 1);
        for(const int idx : this->partialUpdateColorIndices) {
            const auto colorIdx = static_cast<u32>(idx);
            if(colorIdx > maxColorIdx) continue;

            const Color c = this->colors[colorIdx];
            const vec4 col(c.Rf(), c.Gf(), c.Bf(), c.Af());

            // baking clamps the color index, so the last color also feeds every vertex past the end of the array
            const u32 srcEnd = (colorIdx == maxColorIdx) ? numBakedSrc : colorIdx + 1;
            for(u32 srcIdx = colorIdx; srcIdx < srcEnd; srcIdx++) {
                forEachConvertedSlot(this->primitive, srcIdx, numBakedSrc, numConv, [&](u32 convIdx) {
                    m_convertedVertices[convIdx].col = col;
                    m_dirtyIndices.push_back(convIdx);
                });
            }
        }
    }

    this->partialUpdateVertexIndices.clear();
    this->partialUpdateColorIndices.clear();

    if(m_dirtyIndices.empty()) return;

    std::ranges::sort(m_dirtyIndices);
    const auto duplicates = std::ranges::unique(m_dirtyIndices);
    m_dirtyIndices.erase(duplicates.begin(), duplicates.end());

    // bridging a small gap drags a few untouched vertices along with the run, which is far cheaper
    // than the extra copy command it saves
    static constexpr u32 RUN_MERGE_GAP = 8;
    // past this many scattered runs a single full upload wins
    static constexpr size_t MAX_RUNS = 32;

    m_dirtyRuns.clear();
    u32 numDirtyVerts = 0;
    for(const u32 convIdx : m_dirtyIndices) {
        if(!m_dirtyRuns.empty() && convIdx <= m_dirtyRuns.back().end + RUN_MERGE_GAP) {
            numDirtyVerts += (convIdx + 1) - m_dirtyRuns.back().end;
            m_dirtyRuns.back().end = convIdx + 1;
        } else {
            m_dirtyRuns.push_back({.start = convIdx, .end = convIdx + 1});
            numDirtyVerts++;
        }
    }

    constexpr u32 stride = sizeof(SDLGPUSimpleVertex);
    const bool fullUpload = (m_dirtyRuns.size() > MAX_RUNS) || (numDirtyVerts * 2 >= numConv);
    const u32 uploadBytes = (fullUpload ? numConv : numDirtyVerts) * stride;

    u32 pooledSize = 0;
    SDL_GPUTransferBuffer *transferBuffer = m_gpu->acquireUploadTransferBuffer(uploadBytes, pooledSize);
    if(!transferBuffer) {
        engine->showMessageError("SDLGPUVertexArrayObject ERROR", "Failed to acquire vertex upload transfer buffer!");
        return;
    }

    void *mapped = SDL_MapGPUTransferBuffer(m_device, transferBuffer, false);
    if(!mapped) {
        m_gpu->releaseUploadTransferBuffer(transferBuffer, pooledSize);
        return;
    }

    if(fullUpload) {
        std::memcpy(mapped, m_convertedVertices.data(), uploadBytes);
    } else {
        // pack the runs back to back, the copy commands below pick them apart again
        auto *dst = static_cast<SDLGPUSimpleVertex *>(mapped);
        for(const auto &run : m_dirtyRuns) {
            const u32 count = run.end - run.start;
            std::memcpy(dst, &m_convertedVertices[run.start], static_cast<size_t>(count) * stride);
            dst += count;
        }
    }
    SDL_UnmapGPUTransferBuffer(m_device, transferBuffer);

    SDL_GPUFence *fence = nullptr;
    if(auto *cmdBuf = SDL_AcquireGPUCommandBuffer(m_device)) {
        if(auto *copyPass = SDL_BeginGPUCopyPass(cmdBuf)) {
            if(fullUpload) {
                SDL_GPUTransferBufferLocation src{.transfer_buffer = transferBuffer, .offset = 0};
                SDL_GPUBufferRegion dst{.buffer = m_vertexBuffer, .offset = 0, .size = uploadBytes};
                SDL_UploadToGPUBuffer(copyPass, &src, &dst, true);
            } else {
                u32 srcOffset = 0;
                for(const auto &run : m_dirtyRuns) {
                    const u32 runBytes = (run.end - run.start) * stride;
                    SDL_GPUTransferBufferLocation src{.transfer_buffer = transferBuffer, .offset = srcOffset};
                    SDL_GPUBufferRegion dst{.buffer = m_vertexBuffer, .offset = run.start * stride, .size = runBytes};
                    // NOTE: must not cycle here, that hands us a fresh allocation and discards everything
                    // outside of the run
                    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
                    srcOffset += runBytes;
                }
            }
            SDL_EndGPUCopyPass(copyPass);
        }
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
    }

    m_gpu->releaseUploadTransferBuffer(transferBuffer, pooledSize, fence);
}

void SDLGPUVertexArrayObject::initAsync() { this->setAsyncReady(true); }

void SDLGPUVertexArrayObject::destroy() {
    VertexArrayObject::destroy();

    if(m_vertexBuffer) {
        SDL_ReleaseGPUBuffer(m_device, m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }
    m_vertexBufferSize = 0;

    m_convertedVertices.clear();
    m_dirtyIndices.clear();
    m_dirtyRuns.clear();
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
