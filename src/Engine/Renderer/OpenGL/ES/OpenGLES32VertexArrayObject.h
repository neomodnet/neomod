//================ Copyright (c) 2025, WH, All rights reserved. =================//
//
// Purpose:		OpenGLES 3.2 baking support for vao
//
// $NoKeywords: $gles32vao
//===============================================================================//

#pragma once
#ifndef OPENGLES32VERTEXARRAYOBJECT_H
#define OPENGLES32VERTEXARRAYOBJECT_H

#include "config.h"

#ifdef MCENGINE_FEATURE_GLES32
#include "VertexArrayObject.h"

class OpenGLES32VertexArrayObject final : public VertexArrayObject {
    NOCOPY_NOMOVE(OpenGLES32VertexArrayObject)
   public:
    friend class OpenGLES32Interface;
    OpenGLES32VertexArrayObject(DrawPrimitive primitive = DrawPrimitive::TRIANGLES,
                                DrawUsageType usage = DrawUsageType::STATIC, bool keepInSystemMemory = false);
    ~OpenGLES32VertexArrayObject() override { destroy(); }

    void draw() override;

    [[nodiscard]] inline unsigned int getNumTexcoords0() const { return m_iNumTexcoords; }
    [[nodiscard]] inline unsigned int getNumColors() const { return m_iNumColors; }
    [[nodiscard]] inline unsigned int getNumNormals() const { return m_iNumNormals; }

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    // buffer objects
    unsigned int m_iVertexBuffer{0};
    unsigned int m_iTexcoordBuffer{0};
    unsigned int m_iColorBuffer{0};
    unsigned int m_iNormalBuffer{0};

    // counts
    unsigned int m_iNumTexcoords{0};
    unsigned int m_iNumColors{0};
    unsigned int m_iNumNormals{0};
};

#endif

#endif
