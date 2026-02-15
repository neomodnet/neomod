// Copyright (c) 2016, PG, All rights reserved.
#include "OpenGLRenderTarget.h"

#if defined(MCENGINE_FEATURE_OPENGL) || defined(MCENGINE_FEATURE_GLES32)

#include "OpenGLHeaders.h"

#include "OpenGLStateCache.h"
#include "ConVar.h"
#include "Engine.h"
#include "VertexArrayObject.h"
#include "Logging.h"

int OpenGLRenderTarget::iMaxMultiSamples{-1};
int OpenGLRenderTarget::iHaveGLInvalidateFramebuffer{Env::cfg(OS::WASM) ? 1 : -1};

void OpenGLRenderTarget::init() {
#if !defined(MCENGINE_PLATFORM_WASM)
    if(iHaveGLInvalidateFramebuffer == -1) {
        iHaveGLInvalidateFramebuffer = !!glInvalidateFramebuffer;
    }
#endif

    debugLog("Building RenderTarget ({}x{}) ...", (int)this->getSize().x, (int)this->getSize().y);

    this->iFrameBuffer = 0;
    this->iRenderTexture = 0;
    this->iDepthBuffer = 0;
    this->iResolveTexture = 0;
    this->iResolveFrameBuffer = 0;

    int numMultiSamples = 2;

    if(iMaxMultiSamples == -1) {
#ifdef MCENGINE_FEATURE_GLES32
        // GLES 3.2 has glTexStorage2DMultisample and glRenderbufferStorageMultisample as core
        glGetIntegerv(GL_MAX_SAMPLES, &iMaxMultiSamples);
#else
        if((!glTexImage2DMultisample && !glTexStorage2DMultisample) || !glRenderbufferStorageMultisample) {
            iMaxMultiSamples = 0;
        } else {
            glGetIntegerv(GL_MAX_SAMPLES, &iMaxMultiSamples);
        }
#endif
    }

    if(iMaxMultiSamples == 0) {
        this->multiSampleType = MultisampleType::X0;
    }

    switch(this->multiSampleType) {
        case MultisampleType::X0:
            break;
        case MultisampleType::X2:
            numMultiSamples = 2;
            break;
        case MultisampleType::X4:
            numMultiSamples = 4;
            break;
        case MultisampleType::X8:
            numMultiSamples = 8;
            break;
        case MultisampleType::X16:
            numMultiSamples = 16;
            break;
    }

    if(iMaxMultiSamples < numMultiSamples) {
        numMultiSamples = iMaxMultiSamples;
    }

    // create framebuffer
    glGenFramebuffers(1, &this->iFrameBuffer);
    GLStateCache::bindFramebuffer(this->iFrameBuffer);
    if(this->iFrameBuffer == 0) {
        engine->showMessageError("RenderTarget Error", "Couldn't glGenFramebuffers() or glBindFramebuffer()!");
        return;
    }

    // create depth buffer
    glGenRenderbuffers(1, &this->iDepthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, this->iDepthBuffer);
    if(this->iDepthBuffer == 0) {
        engine->showMessageError("RenderTarget Error", "Couldn't glGenRenderBuffers() or glBindRenderBuffer()!");
        return;
    }

    // fill depth buffer
    if(isMultiSampled()) {
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, numMultiSamples, GL_DEPTH_COMPONENT24, (int)this->getSize().x,
                                         (int)this->getSize().y);
    } else {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (int)this->getSize().x, (int)this->getSize().y);
    }

    // set depth buffer as depth attachment on the framebuffer
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, this->iDepthBuffer);

    // create texture
    glGenTextures(1, &this->iRenderTexture);

    if(isMultiSampled()) {
        glBindTexture(isMultiSampled() ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D, this->iRenderTexture);
    } else {
        glBindTexture(GL_TEXTURE_2D, this->iRenderTexture);
    }

    if(this->iRenderTexture == 0) {
        engine->showMessageError("RenderTarget Error", "Couldn't glGenTextures() or glBindTexture()!");
        return;
    }

    // fill texture
    if(isMultiSampled()) {
#ifdef MCENGINE_FEATURE_OPENGL
        glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, numMultiSamples, GL_RGBA8, (int)this->getSize().x,
                                (int)this->getSize().y, true);  // use fixed sample locations
#else                                                           // GL ES
        glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, numMultiSamples, GL_RGBA8, (int)this->getSize().x,
                                  (int)this->getSize().y, true);
#endif
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (int)this->getSize().x, (int)this->getSize().y, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);

        // set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);              // no mipmapping atm
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);  // disable texture wrap
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // set render texture as color attachment0 on the framebuffer
    if(isMultiSampled()) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, this->iRenderTexture,
                               0);
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->iRenderTexture, 0);
    }

    // if multisampled, create resolve framebuffer/texture
    if(isMultiSampled()) {
        if(this->iResolveFrameBuffer == 0) {
            // create resolve framebuffer
            glGenFramebuffers(1, &this->iResolveFrameBuffer);
            GLStateCache::bindFramebuffer(this->iResolveFrameBuffer);

            if(this->iResolveFrameBuffer == 0) {
                engine->showMessageError("RenderTarget Error",
                                         "Couldn't glGenFramebuffers() or glBindFramebuffer() multisampled!");
                return;
            }

            // create resolve texture
            glGenTextures(1, &this->iResolveTexture);
            glBindTexture(GL_TEXTURE_2D, this->iResolveTexture);
            if(this->iResolveTexture == 0) {
                engine->showMessageError("RenderTarget Error",
                                         "Couldn't glGenTextures() or glBindTexture() multisampled!");
                return;
            }

            // set texture parameters
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);  // no mips

            // fill texture
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (int)this->getSize().x, (int)this->getSize().y, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);

            // set resolve texture as color attachment0 on the resolve framebuffer
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, this->iResolveTexture, 0);
        }
    }

    // put this behind a flag because glCheckFramebufferStatus causes unnecessary command queue syncs
    if(cv::debug_opengl.getBool()) {
        // error checking
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if(status != GL_FRAMEBUFFER_COMPLETE) {
            engine->showMessageError(
                "RenderTarget Error",
                fmt::format("!GL_FRAMEBUFFER_COMPLETE, size = ({}x{}), multisampled = {}, status = {}",
                            (int)this->getSize().x, (int)this->getSize().y, (int)isMultiSampled(), status));
            return;
        }
    }

    // reset bound texture and framebuffer
    GLStateCache::bindFramebuffer(0);

    this->setReady(true);
}

void OpenGLRenderTarget::initAsync() { this->setAsyncReady(true); }

void OpenGLRenderTarget::destroy() {
    if(this->iResolveTexture != 0) glDeleteTextures(1, &this->iResolveTexture);
    if(this->iResolveFrameBuffer != 0) glDeleteFramebuffers(1, &this->iResolveFrameBuffer);
    if(this->iRenderTexture != 0) glDeleteTextures(1, &this->iRenderTexture);
    if(this->iDepthBuffer != 0) glDeleteRenderbuffers(1, &this->iDepthBuffer);
    if(this->iFrameBuffer != 0) glDeleteFramebuffers(1, &this->iFrameBuffer);

    this->iFrameBuffer = 0;
    this->iRenderTexture = 0;
    this->iDepthBuffer = 0;
    this->iResolveTexture = 0;
    this->iResolveFrameBuffer = 0;
}

void OpenGLRenderTarget::enable() {
    if(!this->isReady()) return;

    // use the state cache instead of querying OpenGL directly
    this->iFrameBufferBackup = GLStateCache::getCurrentFramebuffer();
    GLStateCache::bindFramebuffer(this->iFrameBuffer);

    this->iViewportBackup = GLStateCache::getCurrentViewport();

    // set new viewport
    std::array<int, 4> newViewport{
        /*x*/ static_cast<int>(-this->getPos().x),
        /*y*/ static_cast<int>((this->getPos().y - g->getResolution().y) + this->getSize().y),
        /*w*/ static_cast<int>(g->getResolution().x),
        /*h*/ static_cast<int>(g->getResolution().y)};

    // update cache
    GLStateCache::setViewport(newViewport);

    if(iHaveGLInvalidateFramebuffer) {
        if(this->bClearColorOnDraw && this->bClearDepthOnDraw) {
            constexpr GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
            glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
        } else if(this->bClearColorOnDraw) {
            constexpr GLenum attachment = GL_COLOR_ATTACHMENT0;
            glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &attachment);
        } else if(this->bClearDepthOnDraw) {
            constexpr GLenum attachment = GL_DEPTH_ATTACHMENT;
            glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, &attachment);
        }
    }

    // clear
    if(cv::debug_rt.getBool())
        glClearColor(0.0f, 0.5f, 0.0f, 0.5f);
    else
        glClearColor(this->clearColor.Rf(), this->clearColor.Gf(), this->clearColor.Bf(), this->clearColor.Af());

    if(this->bClearColorOnDraw || this->bClearDepthOnDraw)
        glClear((this->bClearColorOnDraw ? GL_COLOR_BUFFER_BIT : 0) |
                (this->bClearDepthOnDraw ? GL_DEPTH_BUFFER_BIT : 0));
}

void OpenGLRenderTarget::disable() {
    if(!this->isReady()) return;

    // if multisampled, blit content for multisampling into resolve texture
    if(isMultiSampled()) {
        // HACKHACK: force disable antialiasing
        g->setAntialiasing(false);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, this->iFrameBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, this->iResolveFrameBuffer);

        // for multisampled, the sizes MUST be the same! you can't blit from multisampled into non-multisampled or different size
        glBlitFramebuffer(0, 0, (int)this->getSize().x, (int)this->getSize().y, 0, 0, (int)this->getSize().x,
                          (int)this->getSize().y, GL_COLOR_BUFFER_BIT, GL_LINEAR);

        if(iHaveGLInvalidateFramebuffer) {
            // invalidate multisampled framebuffer, we don't need it anymore
            constexpr GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
            glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 2, attachments);
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    // restore viewport
    GLStateCache::setViewport(this->iViewportBackup);

    // restore framebuffer
    GLStateCache::bindFramebuffer(this->iFrameBufferBackup);
}

void OpenGLRenderTarget::bind(unsigned int textureUnit) {
    if(!this->isReady()) return;

    this->iTextureUnitBackup = textureUnit;

    // switch texture units before enabling+binding
    glActiveTexture(GL_TEXTURE0 + textureUnit);

    // set texture
    if(isMultiSampled()) {
        glBindTexture(GL_TEXTURE_2D, this->iResolveTexture);
    } else {
        glBindTexture(GL_TEXTURE_2D, this->iRenderTexture);
    }
    // needed for legacy FFP renderer support (OpenGLInterface)
    if constexpr(Env::cfg(REND::GL)) glEnable(GL_TEXTURE_2D);
}

void OpenGLRenderTarget::unbind() {
    if(!this->isReady() || !cv::r_gl_rt_unbind.getBool()) return;

    // restore texture unit (just in case) and set to no texture
    glActiveTexture(GL_TEXTURE0 + this->iTextureUnitBackup);
    glBindTexture(GL_TEXTURE_2D, 0);

    // restore default texture unit
    if(this->iTextureUnitBackup != 0) glActiveTexture(GL_TEXTURE0);
}

void OpenGLRenderTarget::blitResolveFrameBufferIntoFrameBuffer(OpenGLRenderTarget *rt) {
    if(isMultiSampled()) {
        // HACKHACK: force disable antialiasing
        g->setAntialiasing(false);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, this->iResolveFrameBuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rt->getFrameBuffer());

        glBlitFramebuffer(0, 0, (int)this->getSize().x, (int)this->getSize().y, 0, 0, (int)rt->getWidth(),
                          (int)rt->getHeight(), GL_COLOR_BUFFER_BIT, GL_LINEAR);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }
}

void OpenGLRenderTarget::blitFrameBufferIntoFrameBuffer(OpenGLRenderTarget *rt) {
    // HACKHACK: force disable antialiasing
    g->setAntialiasing(false);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, this->iFrameBuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, rt->getFrameBuffer());

    glBlitFramebuffer(0, 0, (int)this->getSize().x, (int)this->getSize().y, 0, 0, (int)rt->getWidth(),
                      (int)rt->getHeight(), GL_COLOR_BUFFER_BIT, GL_LINEAR);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
}

#endif
