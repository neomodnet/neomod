//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		SDL_gpu baking support for vao
//
// $NoKeywords: $sdlgpuvao
//===============================================================================//

#pragma once
#ifndef SDLGPUVERTEXARRAYOBJECT_H
#define SDLGPUVERTEXARRAYOBJECT_H

#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU

#include "VertexArrayObject.h"

struct SDLGPUSimpleVertex;

typedef struct SDL_GPUBuffer SDL_GPUBuffer;
typedef struct SDL_GPUDevice SDL_GPUDevice;

class SDLGPUInterface;

class SDLGPUVertexArrayObject final : public VertexArrayObject {
    NOCOPY_NOMOVE(SDLGPUVertexArrayObject)
   private:
    friend SDLGPUInterface;
    SDLGPUVertexArrayObject(SDLGPUInterface *gpu, SDL_GPUDevice *device,
                            DrawPrimitive primitive = DrawPrimitive{2} /* TRIANGLES */,
                            DrawUsageType usage = DrawUsageType{0} /* STATIC */, bool keepInSystemMemory = false);

   public:
    SDLGPUVertexArrayObject() = delete;
    ~SDLGPUVertexArrayObject() override;

    void draw() override;

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    // pushes setVertex()/setColor() modifications of an already baked vao back to the gpu
    void uploadPartialUpdates();

    SDLGPUInterface *m_gpu;
    SDL_GPUDevice *m_device;

    std::vector<SDLGPUSimpleVertex> m_convertedVertices;

    // half open range of converted vertices to re-upload in one copy
    struct DirtyRun {
        u32 start;
        u32 end;
    };

    // scratch buffers for partial updates, kept alive to avoid reallocating them every update
    std::vector<u32> m_dirtyIndices;
    std::vector<DirtyRun> m_dirtyRuns;

    SDL_GPUBuffer *m_vertexBuffer{nullptr};
    u32 m_vertexBufferSize{0};
    DrawPrimitive m_convertedPrimitive;
};

#endif

#endif
