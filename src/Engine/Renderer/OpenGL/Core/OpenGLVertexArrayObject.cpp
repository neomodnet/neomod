// Copyright (c) 2017, PG, All rights reserved.
#include "config.h"

#ifdef MCENGINE_FEATURE_OPENGL
#include "OpenGLVertexArrayObject.h"

#include "SDLGLInterface.h"
#include "OpenGLHeaders.h"
#include "OpenGLStateCache.h"

#include "Engine.h"
#include "ConVar.h"
#include "Logging.h"

OpenGLVertexArrayObject::OpenGLVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage, bool keepInSystemMemory)
    : VertexArrayObject(primitive, usage, keepInSystemMemory) {
}

void OpenGLVertexArrayObject::init() {
    if(!(this->isAsyncReady()) || this->vertices.size() < 2) return;

    // handle partial reloads

    if(this->isReady()) {
        // update vertex buffer
        if(this->partialUpdateVertexIndices.size() > 0) {
            GLStateCache::bindArrayBuffer(this->iVertexBuffer);
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
            GLStateCache::bindArrayBuffer(this->iColorBuffer);
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

    if(this->iVertexBuffer != 0 && (!this->bKeepInSystemMemory || this->isReady()))
        return;  // only fully load if we are not already loaded

    // handle full loads

    unsigned int vertexAttribArrayIndexCounter = 0;
    if(cv::r_opengl_legacy_vao_use_vertex_array.getBool()) {
        // build and bind vertex array
        glGenVertexArrays(1, &this->iVertexArray);
        glBindVertexArray(this->iVertexArray);
    }

    // build and fill vertex buffer
    {
        glGenBuffers(1, &this->iVertexBuffer);
        GLStateCache::bindArrayBuffer(this->iVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * this->vertices.size(), &(this->vertices[0]),
                     SDLGLInterface::usageToOpenGLMap[this->usage]);

        if(cv::r_opengl_legacy_vao_use_vertex_array.getBool()) {
            glEnableVertexAttribArray(vertexAttribArrayIndexCounter);
            glVertexAttribPointer(vertexAttribArrayIndexCounter, 3, GL_FLOAT, GL_FALSE, 0, (char*)nullptr);
            vertexAttribArrayIndexCounter++;
        } else {
            // NOTE: this state will persist engine-wide forever
            GLStateCache::enableClientState(GL_VERTEX_ARRAY);
        }
    }

    // build and fill texcoord buffer
    if(this->texcoords.size() > 0) {
        this->iNumTexcoords = this->texcoords.size();

        glGenBuffers(1, &this->iTexcoordBuffer);
        GLStateCache::bindArrayBuffer(this->iTexcoordBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vec2) * this->texcoords.size(), &(this->texcoords[0]),
                     SDLGLInterface::usageToOpenGLMap[this->usage]);

        if(cv::r_opengl_legacy_vao_use_vertex_array.getBool()) {
            if(this->iNumTexcoords > 0) {
                glEnableVertexAttribArray(vertexAttribArrayIndexCounter);
                glVertexAttribPointer(vertexAttribArrayIndexCounter, 2, GL_FLOAT, GL_FALSE, 0, (char*)nullptr);
                vertexAttribArrayIndexCounter++;
            }
        }
    }

    // build and fill color buffer
    if(this->colors.size() > 0) {
        this->iNumColors = this->colors.size();

        for(auto& color : this->colors) {
            color = abgr(color);
        }

        glGenBuffers(1, &this->iColorBuffer);
        GLStateCache::bindArrayBuffer(this->iColorBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Color) * this->colors.size(), &(this->colors[0]),
                     SDLGLInterface::usageToOpenGLMap[this->usage]);

        if(cv::r_opengl_legacy_vao_use_vertex_array.getBool()) {
            if(this->iNumColors > 0) {
                glEnableVertexAttribArray(vertexAttribArrayIndexCounter);
                glVertexAttribPointer(vertexAttribArrayIndexCounter, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (char*)nullptr);
                vertexAttribArrayIndexCounter++;
            }
        }
    }

    // build and fill normal buffer
    if(this->normals.size() > 0) {
        this->iNumNormals = this->normals.size();

        glGenBuffers(1, &this->iNormalBuffer);
        GLStateCache::bindArrayBuffer(this->iNormalBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vec3) * this->normals.size(), &(this->normals[0]),
                     SDLGLInterface::usageToOpenGLMap[this->usage]);

        if(cv::r_opengl_legacy_vao_use_vertex_array.getBool()) {
            if(this->iNumNormals > 0) {
                glEnableVertexAttribArray(vertexAttribArrayIndexCounter);
                glVertexAttribPointer(vertexAttribArrayIndexCounter, 3, GL_FLOAT, GL_FALSE, 0, (char*)nullptr);
                vertexAttribArrayIndexCounter++;
            }
        }
    }

    if(cv::r_opengl_legacy_vao_use_vertex_array.getBool()) {
        glBindVertexArray(0);
    }

    // free memory
    if(!this->bKeepInSystemMemory) this->clear();

    this->setReady(true);
}

void OpenGLVertexArrayObject::initAsync() { this->setAsyncReady(true); }

void OpenGLVertexArrayObject::destroy() {
    VertexArrayObject::destroy();

    if(this->iVertexBuffer > 0) glDeleteBuffers(1, &this->iVertexBuffer);
    if(this->iTexcoordBuffer > 0) glDeleteBuffers(1, &this->iTexcoordBuffer);
    if(this->iColorBuffer > 0) glDeleteBuffers(1, &this->iColorBuffer);
    if(this->iNormalBuffer > 0) glDeleteBuffers(1, &this->iNormalBuffer);
    if(this->iVertexArray > 0) glDeleteVertexArrays(1, &this->iVertexArray);

    this->iVertexBuffer = 0;
    this->iTexcoordBuffer = 0;
    this->iColorBuffer = 0;
    this->iNormalBuffer = 0;

    this->iVertexArray = 0;
}

void OpenGLVertexArrayObject::draw() {
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

    if(start > end || std::abs(end - start) == 0) return;

    if(cv::r_opengl_legacy_vao_use_vertex_array.getBool()) {
        // set vao
        glBindVertexArray(this->iVertexArray);

        // render it
        glDrawArrays(SDLGLInterface::primitiveToOpenGLMap[this->primitive], start,
                     end - start);  // (everything is already preconfigured inside the vertexArray)
    } else {
        // set vertices
        GLStateCache::bindArrayBuffer(this->iVertexBuffer);
        glVertexPointer(3, GL_FLOAT, 0, (char*)nullptr);  // set vertex pointer to vertex buffer

        // set texture0
        if(this->iNumTexcoords > 0) {
            GLStateCache::enableClientState(GL_TEXTURE_COORD_ARRAY);
            GLStateCache::bindArrayBuffer(this->iTexcoordBuffer);
            glTexCoordPointer(2, GL_FLOAT, 0, (char*)nullptr);  // set first texcoord pointer to texcoord buffer
        } else
            GLStateCache::disableClientState(GL_TEXTURE_COORD_ARRAY);

        // set colors
        if(this->iNumColors > 0) {
            GLStateCache::enableClientState(GL_COLOR_ARRAY);
            GLStateCache::bindArrayBuffer(this->iColorBuffer);
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, (char*)nullptr);  // set color pointer to color buffer
        } else
            GLStateCache::disableClientState(GL_COLOR_ARRAY);

        // set normals
        if(this->iNumNormals > 0) {
            GLStateCache::enableClientState(GL_NORMAL_ARRAY);
            GLStateCache::bindArrayBuffer(this->iNormalBuffer);
            glNormalPointer(GL_FLOAT, 0, (char*)nullptr);  // set normal pointer to normal buffer
        } else
            GLStateCache::disableClientState(GL_NORMAL_ARRAY);

        // render it
        glDrawArrays(SDLGLInterface::primitiveToOpenGLMap[this->primitive], start, end - start);

        GLStateCache::bindArrayBuffer(this->iNormalBuffer);
    }
}

#endif
