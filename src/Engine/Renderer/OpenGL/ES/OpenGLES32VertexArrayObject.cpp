//================ Copyright (c) 2025, WH, All rights reserved. =================//
//
// Purpose:		OpenGLES 3.2 baking support for vao
//
// $NoKeywords: $gles32vao
//===============================================================================//

#include "OpenGLES32VertexArrayObject.h"

#ifdef MCENGINE_FEATURE_GLES32

#include "Engine.h"
#include "OpenGLES32Interface.h"
#include "OpenGLStateCache.h"
#include "OpenGLHeaders.h"
#include "Logging.h"

#include "SDLGLInterface.h"

OpenGLES32VertexArrayObject::OpenGLES32VertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                                         bool keepInSystemMemory)
    : VertexArrayObject(primitive, usage, keepInSystemMemory) {
}

void OpenGLES32VertexArrayObject::init() {
    if(!(this->isAsyncReady()) || this->vertices.size() < 2) return;

    // handle partial reloads
    if(this->isReady()) {
        // update vertex buffer
        if(this->partialUpdateVertexIndices.size() > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, m_iVertexBuffer);
            for(size_t i = 0; i < this->partialUpdateVertexIndices.size(); i++) {
                const int offsetIndex = this->partialUpdateVertexIndices[i];

                // group by continuous chunks to reduce calls
                int numContinuousIndices = 1;
                while((i + 1) < this->partialUpdateVertexIndices.size()) {
                    if((this->partialUpdateVertexIndices[i + 1] - this->partialUpdateVertexIndices[i]) == 1) {
                        numContinuousIndices++;
                        i++;
                    } else
                        break;
                }

                glBufferSubData(GL_ARRAY_BUFFER, sizeof(vec3) * offsetIndex, sizeof(vec3) * numContinuousIndices,
                                &(this->vertices[offsetIndex]));
            }
            this->partialUpdateVertexIndices.clear();
        }

        // update color buffer
        if(this->partialUpdateColorIndices.size() > 0) {
            glBindBuffer(GL_ARRAY_BUFFER, m_iColorBuffer);
            for(size_t i = 0; i < this->partialUpdateColorIndices.size(); i++) {
                const int offsetIndex = this->partialUpdateColorIndices[i];

                this->colors[offsetIndex] = abgr(this->colors[offsetIndex]);

                // group by continuous chunks to reduce calls
                int numContinuousIndices = 1;
                while((i + 1) < this->partialUpdateColorIndices.size()) {
                    if((this->partialUpdateColorIndices[i + 1] - this->partialUpdateColorIndices[i]) == 1) {
                        numContinuousIndices++;
                        i++;

                        this->colors[this->partialUpdateColorIndices[i]] =
                            abgr(this->colors[this->partialUpdateColorIndices[i]]);
                    } else
                        break;
                }

                glBufferSubData(GL_ARRAY_BUFFER, sizeof(Color) * offsetIndex, sizeof(Color) * numContinuousIndices,
                                &(this->colors[offsetIndex]));
            }
            this->partialUpdateColorIndices.clear();
        }
    }

    if(m_iVertexBuffer != 0 && (!this->bKeepInSystemMemory || this->isReady()))
        return;  // only fully load if we are not already loaded

    // handle full loads

    // build and fill vertex buffer
    glGenBuffers(1, &m_iVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_iVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * this->vertices.size(), &(this->vertices[0]),
                 SDLGLInterface::usageToOpenGLMap[this->usage]);

    // build and fill texcoord buffer
    if(this->texcoords.size() > 0) {
        m_iNumTexcoords = this->texcoords.size();

        glGenBuffers(1, &m_iTexcoordBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_iTexcoordBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vec2) * this->texcoords.size(), &(this->texcoords[0]),
                     SDLGLInterface::usageToOpenGLMap[this->usage]);
    }

    // build and fill color buffer
    if(this->colors.size() > 0) {
        m_iNumColors = this->colors.size();

        // convert ARGB to ABGR for OpenGL
        for(auto & color : this->colors) {
            color = abgr(color);
        }

        glGenBuffers(1, &m_iColorBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_iColorBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Color) * this->colors.size(), &(this->colors[0]),
                     SDLGLInterface::usageToOpenGLMap[this->usage]);
    }

    // build and fill normal buffer
    if(this->normals.size() > 0) {
        m_iNumNormals = this->normals.size();

        glGenBuffers(1, &m_iNormalBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, m_iNormalBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * this->normals.size(), &(this->normals[0]),
                     SDLGLInterface::usageToOpenGLMap[this->usage]);
    }

    // free memory
    if(!this->bKeepInSystemMemory) clear();

    this->setReady(true);
}

void OpenGLES32VertexArrayObject::initAsync() { this->setAsyncReady(true); }

void OpenGLES32VertexArrayObject::destroy() {
    VertexArrayObject::destroy();

    if(m_iVertexBuffer > 0) glDeleteBuffers(1, &m_iVertexBuffer);

    if(m_iTexcoordBuffer > 0) glDeleteBuffers(1, &m_iTexcoordBuffer);

    if(m_iColorBuffer > 0) glDeleteBuffers(1, &m_iColorBuffer);

    if(m_iNormalBuffer > 0) glDeleteBuffers(1, &m_iNormalBuffer);

    m_iVertexBuffer = 0;
    m_iTexcoordBuffer = 0;
    m_iColorBuffer = 0;
    m_iNormalBuffer = 0;
}

void OpenGLES32VertexArrayObject::draw() {
    if(!this->isReady()) {
        debugLog("WARNING: called, but was not ready!");
        return;
    }

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

    if(start > end || std::abs(end - start) == 0) {
        return;
    }

    auto *gles32 = static_cast<OpenGLES32Interface *>(g.get());

    // configure shader state for our vertex attributes
    if(m_iNumColors > 0) {
        glEnableVertexAttribArray(gles32->getShaderGenericAttribCol());
    } else {
        glDisableVertexAttribArray(gles32->getShaderGenericAttribCol());
    }

    // set vertex attribute pointers
    glBindBuffer(GL_ARRAY_BUFFER, m_iVertexBuffer);
    glVertexAttribPointer(gles32->getShaderGenericAttribPosition(), 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    if(m_iNumTexcoords > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m_iTexcoordBuffer);
        glVertexAttribPointer(gles32->getShaderGenericAttribUV(), 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);
    }

    if(m_iNumColors > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m_iColorBuffer);
        glVertexAttribPointer(gles32->getShaderGenericAttribCol(), 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (GLvoid *)0);
    }

    // draw the geometry
    glDrawArrays(SDLGLInterface::primitiveToOpenGLMap[this->primitive], start, end - start);

    // restore default state
    glBindBuffer(GL_ARRAY_BUFFER, gles32->getVBOVertices());
    glVertexAttribPointer(gles32->getShaderGenericAttribPosition(), 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    glBindBuffer(GL_ARRAY_BUFFER, gles32->getVBOTexcoords());
    glVertexAttribPointer(gles32->getShaderGenericAttribUV(), 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);

    // always enable color attrib as the default state
    glBindBuffer(GL_ARRAY_BUFFER, gles32->getVBOTexcolors());
    glVertexAttribPointer(gles32->getShaderGenericAttribCol(), 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (GLvoid *)0);
    glEnableVertexAttribArray(gles32->getShaderGenericAttribCol());
}

#endif
