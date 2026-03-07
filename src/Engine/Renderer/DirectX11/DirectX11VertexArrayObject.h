//================ Copyright (c) 2022, PG, All rights reserved. =================//
//
// Purpose:		DirectX baking support for vao
//
// $NoKeywords: $dxvao
//===============================================================================//

#pragma once
#ifndef DIRECTX11VERTEXARRAYOBJECT_H
#define DIRECTX11VERTEXARRAYOBJECT_H

#include "config.h"

#ifdef MCENGINE_FEATURE_DIRECTX11

#include "VertexArrayObject.h"
#include "DirectX11Interface.h"
#include "CDynArray.h"

class DirectX11VertexArrayObject final : public VertexArrayObject {
    NOCOPY_NOMOVE(DirectX11VertexArrayObject)
   public:
    DirectX11VertexArrayObject(DrawPrimitive primitive = DrawPrimitive::TRIANGLES,
                               DrawUsageType usage = DrawUsageType::STATIC,
                               bool keepInSystemMemory = false);
    ~DirectX11VertexArrayObject() override { destroy(); }

    void draw() override;

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    static int primitiveToDirectX(DrawPrimitive primitive);
    static int usageToDirectX(DrawUsageType usage);

    Mc::CDynArray<DirectX11Interface::SimpleVertex> convertedVertices;

    ID3D11Buffer *vertexBuffer{};

    DrawPrimitive convertedPrimitive;
};

#endif

#endif
