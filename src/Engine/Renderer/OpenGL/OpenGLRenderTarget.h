#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#ifndef OPENGLRENDERTARGET_H
#define OPENGLRENDERTARGET_H

#include "RenderTarget.h"

#if defined(MCENGINE_FEATURE_OPENGL) || defined(MCENGINE_FEATURE_GLES32)

#include <array>

class OpenGLRenderTarget final : public RenderTarget {
    NOCOPY_NOMOVE(OpenGLRenderTarget)
   public:
    OpenGLRenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType = MultisampleType{0})
        : RenderTarget(x, y, width, height, multiSampleType) {}
    ~OpenGLRenderTarget() override { destroy(); }

    void enable(bool clear) override;
    void disable() override;

    void bind(unsigned int textureUnit = 0) override;
    void unbind() override;

    // ILLEGAL:
    void blitResolveFrameBufferIntoFrameBuffer(OpenGLRenderTarget *rt);
    void blitFrameBufferIntoFrameBuffer(OpenGLRenderTarget *rt);
    [[nodiscard]] inline unsigned int getFrameBuffer() const { return this->iFrameBuffer; }
    [[nodiscard]] inline unsigned int getRenderTexture() const { return this->iRenderTexture; }
    [[nodiscard]] inline unsigned int getResolveFrameBuffer() const { return this->iResolveFrameBuffer; }
    [[nodiscard]] inline unsigned int getResolveTexture() const { return this->iResolveTexture; }

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    unsigned int iFrameBuffer{0};
    unsigned int iRenderTexture{0};
    unsigned int iDepthBuffer{0};
    unsigned int iResolveFrameBuffer{0};
    unsigned int iResolveTexture{0};

    unsigned int iFrameBufferBackup{0};
    unsigned int iTextureUnitBackup{0};
    std::array<int, 4> iViewportBackup{};

    static int iMaxMultiSamples;
    static int iHaveGLInvalidateFramebuffer;
};

#endif

#endif
