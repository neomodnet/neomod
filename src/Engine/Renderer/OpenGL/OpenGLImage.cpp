// Copyright (c) 2016, PG, All rights reserved.
#include "OpenGLImage.h"

#if defined(MCENGINE_FEATURE_OPENGL) || defined(MCENGINE_FEATURE_GLES32)

#include <utility>

#include "Engine.h"
#include "ConVar.h"
#include "File.h"
#include "Logging.h"

#include "OpenGLHeaders.h"

OpenGLImage::~OpenGLImage() {
    this->destroy();
    this->deleteGL();
    this->rawImage.clear();
}

void OpenGLImage::init() {
    // only load if not:
    // 0. entirely transparent
    // 1. already uploaded to gpu, and we didn't keep the image in system memory
    // 2. failed to async load
    if(this->bLoadedImageEntirelyTransparent) {
        this->setReady(true);
        this->setAsyncReady(true);
        return;
    }
    if((this->GLTexture != 0 && !this->bKeepInSystemMemory) || !(this->isAsyncReady())) {
        if(cv::debug_image.getBool()) {
            debugLog(
                "we are already loaded, bReady: {} createdImage: {} GLTexture: {} bKeepInSystemMemory: {} bAsyncReady: "
                "{}",
                this->isReady(), this->bCreatedImage, this->GLTexture, this->bKeepInSystemMemory, this->isAsyncReady());
        }
        return;
    }

    logIfCV(debug_image, "loading {}", this->sFilePath.empty() ? this->sName : this->sFilePath);

    // rawImage cannot be empty here, if it is, we're screwed
    assert(this->totalBytes() != 0);

    // create texture object
    const bool glTextureWasEmpty = this->GLTexture == 0;
    if(glTextureWasEmpty) {
        // FFP compatibility (part 1)
        if constexpr(Env::cfg(REND::GL)) {
            glEnable(GL_TEXTURE_2D);
        }

        // create texture and bind
        glGenTextures(1, &this->GLTexture);
        glBindTexture(GL_TEXTURE_2D, this->GLTexture);

        // set texture filtering mode (mipmapping is disabled by default)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, this->bMipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // texture wrapping, defaults to clamp
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // upload to gpu
    {
        if(glTextureWasEmpty) {
            // first upload: must use glTexImage2D to allocate texture storage
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->rawImage.getX(), this->rawImage.getY(), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, this->rawImage.get());
        } else {
            // rebind
            glBindTexture(GL_TEXTURE_2D, this->GLTexture);

            // reupload: use glTexSubImage2D with dirty rects to avoid full reupload
            auto dirtyRects = this->getDirtyRects();
            const bool fullImage = dirtyRects.size() == 1 && dirtyRects[0].getWidth() == this->iWidth &&
                                   dirtyRects[0].getHeight() == this->iHeight;

            if(fullImage) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, this->rawImage.getX(), this->rawImage.getY(), GL_RGBA,
                                GL_UNSIGNED_BYTE, this->rawImage.get());
            } else {
                glPixelStorei(GL_UNPACK_ROW_LENGTH, this->rawImage.getX());
                for(const auto &rect : dirtyRects) {
                    const u8 *src =
                        this->rawImage.get() +
                        ((i64)rect.getMinY() * this->rawImage.getX() + rect.getMinX()) * Image::NUM_CHANNELS;
                    glTexSubImage2D(GL_TEXTURE_2D, 0, rect.getMinX(), rect.getMinY(), rect.getWidth(), rect.getHeight(),
                                    GL_RGBA, GL_UNSIGNED_BYTE, src);
                }
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
        }

        this->resetDirtyRegion();

        if(this->bMipmapped) {
            // cap mipmap levels at 32px minimum dimension to avoid excessive generation cost
            // we're not going to care about huge images looking good when downscaled to webpage icon size
            const int maxDim = std::max(this->rawImage.getX(), this->rawImage.getY());
            const int maxLevel = std::max(0, (int)std::floor(std::log2(maxDim)) - 5);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    // free from RAM (it's now in VRAM)
    if(!this->bKeepInSystemMemory) {
        this->rawImage.clear();
    }

    this->setReady(true);

    if(this->filterMode != TextureFilterMode::LINEAR) {
        setFilterMode(this->filterMode);
    }

    if(this->wrapMode != TextureWrapMode::CLAMP) {
        setWrapMode(this->wrapMode);
    }
}

void OpenGLImage::initAsync() {
    if(this->GLTexture != 0) {
        this->setAsyncReady(true);
        return;  // only load if we are not already loaded
    }

    if(!this->bCreatedImage) {
        logIfCV(debug_rm, "Resource Manager: Loading {:s}", this->sFilePath.c_str());

        this->setAsyncReady(loadRawImage());
    } else {
        // created image is always async ready
        this->setAsyncReady(true);
    }
}

void OpenGLImage::destroy() {
    // don't delete the texture if we're keeping it in memory, for reloads
    if(!this->bKeepInSystemMemory) {
        this->deleteGL();
        this->rawImage.clear();
    }
}

void OpenGLImage::deleteGL() {
#ifdef MCENGINE_FEATURE_GLES32
    constexpr bool hasGLFuncs = true;
#else
    const bool hasGLFuncs = glDeleteTextures != nullptr && glIsTexture != nullptr;
#endif
    if(this->GLTexture != 0 && hasGLFuncs) {
        if(!glIsTexture(this->GLTexture)) {
            debugLog("WARNING: tried to glDeleteTexture on {} ({:p}), which is not a valid GL texture!", this->sName,
                     static_cast<const void *>(&this->GLTexture));
        } else {
            glDeleteTextures(1, &this->GLTexture);
        }
    }
    this->GLTexture = 0;
}

void OpenGLImage::bind(unsigned int textureUnit) const {
    if(!this->isGPUReady()) return;

    this->iTextureUnitBackup = textureUnit;

    // switch texture units before enabling+binding
    glActiveTexture(GL_TEXTURE0 + textureUnit);

    // set texture
    glBindTexture(GL_TEXTURE_2D, this->GLTexture);

    // FFP compatibility (part 2)
    if constexpr(Env::cfg(REND::GL)) {
        glEnable(GL_TEXTURE_2D);
    }
}

void OpenGLImage::unbind() const {
    if(!this->isGPUReady() || !cv::r_gl_image_unbind.getBool()) return;

    // restore texture unit (just in case) and set to no texture
    glActiveTexture(GL_TEXTURE0 + this->iTextureUnitBackup);
    glBindTexture(GL_TEXTURE_2D, 0);

    // restore default texture unit
    if(this->iTextureUnitBackup != 0) glActiveTexture(GL_TEXTURE0);
}

void OpenGLImage::setFilterMode(TextureFilterMode filterMode) {
    Image::setFilterMode(filterMode);
    if(!this->isGPUReady()) return;

    bind();
    {
        switch(filterMode) {
            case TextureFilterMode::NONE:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                break;
            case TextureFilterMode::LINEAR:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                break;
            case TextureFilterMode::MIPMAP:
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                break;
        }
    }
    unbind();
}

void OpenGLImage::setWrapMode(TextureWrapMode wrapMode) {
    Image::setWrapMode(wrapMode);
    if(!this->isGPUReady()) return;

    bind();
    {
        switch(wrapMode) {
            case TextureWrapMode::CLAMP:  // NOTE: there is also GL_CLAMP, which works a bit differently
                                          // concerning the border color
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                break;
            case TextureWrapMode::REPEAT:
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                break;
        }
    }
    unbind();
}

void OpenGLImage::handleGLErrors() {
    // no
}

#endif
