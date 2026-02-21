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
typedef struct SDL_GPUTransferBuffer SDL_GPUTransferBuffer;
typedef struct SDL_GPUDevice SDL_GPUDevice;

class SDLGPUInterface;

class SDLGPUVertexArrayObject final : public VertexArrayObject {
    NOCOPY_NOMOVE(SDLGPUVertexArrayObject)
   private:
    friend SDLGPUInterface;
    SDLGPUVertexArrayObject(SDLGPUInterface *gpu, SDL_GPUDevice *device, DrawPrimitive primitive = DrawPrimitive::TRIANGLES,
                            DrawUsageType usage = DrawUsageType::STATIC, bool keepInSystemMemory = false);

   public:
    SDLGPUVertexArrayObject() = delete;
    ~SDLGPUVertexArrayObject() override;

    void draw() override;

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    SDLGPUInterface *m_gpu;
    SDL_GPUDevice *m_device;

    std::vector<SDLGPUSimpleVertex> m_convertedVertices;
    SDL_GPUBuffer *m_vertexBuffer{nullptr};
    SDL_GPUTransferBuffer *m_transferBuffer{nullptr};
    DrawPrimitive m_convertedPrimitive;
};

#endif

#endif
