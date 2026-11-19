//================ Copyright (c) 2022, PG, All rights reserved. =================//
//
// Purpose:		DirectX baking support for vao
//
// $NoKeywords: $dxvao
//===============================================================================//
#include "config.h"

#ifdef MCENGINE_FEATURE_DIRECTX11

#include "DirectX11VertexArrayObject.h"

#include "Engine.h"
#include "Logging.h"

#include "DirectX11Interface.h"
#include "ModernGraphicsShared.h"

#include <algorithm>
#include <span>
#include <vector>

DirectX11VertexArrayObject::DirectX11VertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                                       bool keepInSystemMemory)
    : VertexArrayObject(primitive, usage, keepInSystemMemory), convertedPrimitive(primitive) {}

void DirectX11VertexArrayObject::init() {
    if(!this->isAsyncReady() || this->vertices.size() < 2) return;

    auto* device = static_cast<DirectX11Interface*>(g)->getDevice();
    auto* context = static_cast<DirectX11Interface*>(g)->getDeviceContext();

    if(this->isReady()) {
        const D3D11_USAGE usage = (D3D11_USAGE)usageToDirectX(this->usage);

        // TODO: somehow merge this with the partialUpdateColorIndices, annoying
        // TODO: also support converted meshes, extremely annoying. currently this will crash for converted meshes!

        // update vertex buffer

        if(this->partialUpdateVertexIndices.size() > 0) {
            for(size_t i = 0; i < this->partialUpdateVertexIndices.size(); i++) {
                const int offsetIndex = this->partialUpdateVertexIndices[i];

                this->convertedVertices[offsetIndex].pos = this->vertices[offsetIndex];

                // group by continuous chunks to reduce calls
                int numContinuousIndices = 1;
                while((i + 1) < this->partialUpdateVertexIndices.size()) {
                    if((this->partialUpdateVertexIndices[i + 1] - this->partialUpdateVertexIndices[i]) == 1) {
                        numContinuousIndices++;
                        i++;

                        this->convertedVertices[this->partialUpdateVertexIndices[i]].pos =
                            this->vertices[this->partialUpdateVertexIndices[i]];
                    } else
                        break;
                }

                if(usage == D3D11_USAGE_DEFAULT) {
                    D3D11_BOX box;
                    {
                        box.left = sizeof(DirectX11Interface::SimpleVertex) * offsetIndex;
                        box.right = box.left + (sizeof(DirectX11Interface::SimpleVertex) * numContinuousIndices);
                        box.top = 0;
                        box.bottom = 1;
                        box.front = 0;
                        box.back = 1;
                    }
                    context->UpdateSubresource(this->vertexBuffer, 0, &box, &this->convertedVertices[offsetIndex], 0,
                                               0);
                } else if(usage == D3D11_USAGE_DYNAMIC) {
                    D3D11_MAPPED_SUBRESOURCE mappedResource{};
                    if(SUCCEEDED(context->Map(this->vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
                        memcpy(mappedResource.pData, &this->convertedVertices[0],
                               sizeof(DirectX11Interface::SimpleVertex) * this->convertedVertices.size());
                        context->Unmap(this->vertexBuffer, 0);
                    }
                }
            }
            this->partialUpdateVertexIndices.clear();
            this->partialUpdateColorIndices.clear();
        }

        // TODO: update color buffer
    }

    if(this->vertexBuffer != nullptr && (!this->bKeepInSystemMemory || this->isReady()))
        return;  // only fully load if we are not already loaded

    // TODO: optimize this piece of shit

    {
        std::vector<vec3> convVertices;
        std::vector<vec2> convTexcoords;
        std::vector<Color> convColors;
        this->convertedPrimitive = ModernGraphicsShared::convertToTriangles(
            this->primitive, this->vertices, this->texcoords, this->colors, convVertices, convTexcoords, convColors);
        const bool converted = this->convertedPrimitive != this->primitive;

        const std::span<const vec3> finalVertices = converted ? convVertices : std::span<const vec3>{this->vertices};
        const std::span<const vec2> finalTexcoords = converted ? convTexcoords : std::span<const vec2>{this->texcoords};
        const std::span<const Color> finalColors = converted ? convColors : std::span<const Color>{this->colors};

        // build directx vertices
        this->convertedVertices.resize(finalVertices.size());

        this->iNumVertices =
            this->convertedVertices.size();  // NOTE: overwrite this->iNumVertices for potential conversions

        const bool hasColors = !finalColors.empty();
        const size_t maxColorIndex = hasColors ? finalColors.size() - 1 : 0;
        const bool hasTexCoords0 = (finalTexcoords.size() == this->convertedVertices.size());

        for(size_t i = 0; i < finalVertices.size(); i++) {
            this->convertedVertices[i].pos = finalVertices[i];

            if(hasColors) {
                const Color clr = finalColors[std::min(i, maxColorIndex)];
                this->convertedVertices[i].col = vec4(clr.Rf(), clr.Gf(), clr.Bf(), clr.Af());
            } else {
                this->convertedVertices[i].col = vec4(1.0f, 1.0f, 1.0f, 1.0f);
            }

            // TODO: multitexturing
            if(hasTexCoords0) this->convertedVertices[i].tex = finalTexcoords[i];
        }
    }

    // create buffer
    const D3D11_USAGE usage = (D3D11_USAGE)usageToDirectX(this->usage);
    D3D11_BUFFER_DESC bufferDesc{};
    {
        bufferDesc.Usage = usage;
        bufferDesc.ByteWidth = sizeof(DirectX11Interface::SimpleVertex) * this->convertedVertices.size();
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bufferDesc.CPUAccessFlags = (bufferDesc.Usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0);
        bufferDesc.MiscFlags = 0;
        bufferDesc.StructureByteStride = 0;
    }
    D3D11_SUBRESOURCE_DATA dataForImmutable{};
    {
        dataForImmutable.pSysMem = &this->convertedVertices[0];
        dataForImmutable.SysMemPitch = 0;       // (unused for vertices)
        dataForImmutable.SysMemSlicePitch = 0;  // (unused for vertices)
    }
    if(FAILED(device->CreateBuffer(&bufferDesc,
                                   (bufferDesc.Usage == D3D11_USAGE_IMMUTABLE ? &dataForImmutable : nullptr),
                                   &this->vertexBuffer)))  // NOTE: immutable is uploaded to gpu right here
    {
        debugLog("DirectX Error: Couldn't CreateBuffer({})", (int)this->convertedVertices.size());
        return;
    }

    // upload everything to gpu
    if(usage == D3D11_USAGE_DEFAULT) {
        D3D11_BOX box;
        {
            box.left = sizeof(DirectX11Interface::SimpleVertex) * 0;
            box.right = box.left + (sizeof(DirectX11Interface::SimpleVertex) * this->convertedVertices.size());
            box.top = 0;
            box.bottom = 1;
            box.front = 0;
            box.back = 1;
        }
        context->UpdateSubresource(this->vertexBuffer, 0, &box, &this->convertedVertices[0], 0, 0);
    } else if(usage == D3D11_USAGE_DYNAMIC) {
        D3D11_MAPPED_SUBRESOURCE mappedResource{};
        if(SUCCEEDED(context->Map(this->vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
            memcpy(mappedResource.pData, &this->convertedVertices[0],
                   sizeof(DirectX11Interface::SimpleVertex) * this->convertedVertices.size());
            context->Unmap(this->vertexBuffer, 0);
        } else {
            debugLog("DirectX Error: Couldn't Map({}) vertexbuffer", (int)this->convertedVertices.size());
            return;
        }
    }

    // free memory
    if(!this->bKeepInSystemMemory) {
        this->clear();
        // clear() re-derives iNumVertices from the unconverted source vertices; restore the converted count
        // so draw()'s range math matches the GPU buffer (fans/quads are baked as triangle lists)
        this->iNumVertices = this->convertedVertices.size();
        this->convertedVertices.clear();
        this->convertedVertices.shrink_to_fit();
    }

    this->setReady(true);
}

void DirectX11VertexArrayObject::initAsync() { this->setAsyncReady(true); }

void DirectX11VertexArrayObject::destroy() {
    VertexArrayObject::destroy();

    if(this->vertexBuffer != nullptr) {
        this->vertexBuffer->Release();
        this->vertexBuffer = nullptr;
    }

    this->convertedVertices.clear();
}

void DirectX11VertexArrayObject::draw() {
    if(!this->isReady()) {
        debugLog("WARNING: called, but was not ready!");
        return;
    }

    const auto [start, end] = this->getDrawRange();

    if(start >= end) return;

    auto* context = static_cast<DirectX11Interface*>(g)->getDeviceContext();

    // draw it
    {
        const UINT stride = sizeof(DirectX11Interface::SimpleVertex);
        const UINT offset = 0;

        context->IASetVertexBuffers(0, 1, &this->vertexBuffer, &stride, &offset);
        context->IASetPrimitiveTopology((D3D_PRIMITIVE_TOPOLOGY)primitiveToDirectX(this->convertedPrimitive));
        context->Draw(end - start, start);
    }
}

int DirectX11VertexArrayObject::primitiveToDirectX(DrawPrimitive primitive) {
    switch(primitive) {
        case DrawPrimitive::LINES:
            return D3D_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case DrawPrimitive::LINE_STRIP:
        case DrawPrimitive::LINE_LOOP:  // NOTE: no native line loop, close vertex in data
            return D3D_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case DrawPrimitive::TRIANGLES:
            return D3D_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case DrawPrimitive::TRIANGLE_FAN:  // NOTE: not available! -------------------
            return D3D_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case DrawPrimitive::TRIANGLE_STRIP:
            return D3D_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case DrawPrimitive::QUADS:  // NOTE: not available! -------------------
            return D3D_PRIMITIVE_TOPOLOGY::D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }

    return D3D_PRIMITIVE_TOPOLOGY::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

int DirectX11VertexArrayObject::usageToDirectX(DrawUsageType usage) {
    switch(usage) {
        case DrawUsageType::STATIC:
            return D3D11_USAGE_IMMUTABLE;
        // NOTE: this fallthrough is intentional.
        // no performance benefits found so far with DYNAMIC, since D3D11_MAP_WRITE_NO_OVERWRITE has very limited use cases
        case DrawUsageType::DYNAMIC:
        case DrawUsageType::STREAM:
            return D3D11_USAGE_DEFAULT;
    }

    return D3D11_USAGE_IMMUTABLE;
}

#endif
