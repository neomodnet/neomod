//================ Copyright (c) 2017, PG, All rights reserved. =================//
//
// Purpose:		DirectX implementation of Image
//
// $NoKeywords: $dximg
//===============================================================================//
#include "config.h"

#ifdef MCENGINE_FEATURE_DIRECTX11
#include "DirectX11Image.h"

#include "Engine.h"
#include "ConVar.h"
#include "Logging.h"

#include "DirectX11Interface.h"

DirectX11Image::DirectX11Image(std::string filepath, bool mipmapped, bool keepInSystemMemory)
    : Image(std::move(filepath), mipmapped, keepInSystemMemory), samplerDesc() {
    this->texture = nullptr;
    this->shaderResourceView = nullptr;
    this->samplerState = nullptr;

    this->iTextureUnitBackup = 0;
    this->prevShaderResourceView = nullptr;

    this->bShared = false;
}

DirectX11Image::DirectX11Image(int width, int height, bool mipmapped, bool keepInSystemMemory)
    : Image(width, height, mipmapped, keepInSystemMemory), samplerDesc() {
    this->texture = nullptr;
    this->shaderResourceView = nullptr;
    this->samplerState = nullptr;

    this->iTextureUnitBackup = 0;
    this->prevShaderResourceView = nullptr;

    this->bShared = false;
}

DirectX11Image::~DirectX11Image() {
    this->destroy();
    this->deleteDX();

    if(this->samplerState != nullptr) {
        this->samplerState->Release();
        this->samplerState = nullptr;
    }

    this->rawImage.clear();
}

void DirectX11Image::init() {
    // only load if not:
    // 0. entirely transparent
    // 1. already uploaded to gpu, and we didn't keep the image in system memory
    // 2. failed to async load
    if(this->bLoadedImageEntirelyTransparent) {
        this->setReady(true);
        this->setAsyncReady(true);
        return;
    }
    if((this->texture != nullptr && !this->bKeepInSystemMemory) || !this->isAsyncReady()) {
        logIfCV(debug_image,
                "we are already loaded, bReady: {} createdImage: {} texture: {:p} bKeepInSystemMemory: {} bAsyncReady: "
                "{}",
                this->isReady(), this->bCreatedImage, fmt::ptr(this->texture), this->bKeepInSystemMemory,
                this->isAsyncReady());
        return;  // only load if we are not already loaded
    }

    HRESULT hr;

    auto* device = static_cast<DirectX11Interface*>(g.get())->getDevice();
    auto* context = static_cast<DirectX11Interface*>(g.get())->getDeviceContext();

    // cap to 32px smallest mipmap (same as OpenGL)
    const UINT maxDim = (UINT)std::max(this->iWidth, this->iHeight);
    const UINT mipLevels = (UINT)std::max(1, (int)std::floor(std::log2(maxDim)) - 4);

    // create texture (with initial data)
    D3D11_TEXTURE2D_DESC textureDesc;
    D3D11_SUBRESOURCE_DATA initData;
    {
        // default desc
        {
            textureDesc.Width = (UINT)this->iWidth;
            textureDesc.Height = (UINT)this->iHeight;
            textureDesc.MipLevels = (this->bMipmapped ? mipLevels : 1);
            textureDesc.ArraySize = 1;
            textureDesc.Format =
                Image::NUM_CHANNELS == 4
                    ? DXGI_FORMAT_R8G8B8A8_UNORM
                    : (Image::NUM_CHANNELS == 3
                           ? DXGI_FORMAT_R8_UNORM
                           : (Image::NUM_CHANNELS == 1 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM));
            textureDesc.SampleDesc.Count = 1;
            textureDesc.SampleDesc.Quality = 0;
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = (this->bMipmapped ? D3D11_BIND_RENDER_TARGET : 0) | D3D11_BIND_SHADER_RESOURCE;
            textureDesc.CPUAccessFlags = 0;
            textureDesc.MiscFlags = (this->bMipmapped ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0) |
                                    (this->bShared ? D3D11_RESOURCE_MISC_SHARED : 0);
        }

        // upload new/overwrite data (not mipmapped) (1/2)
        if(this->texture == nullptr) {
            // initData
            {
                initData.pSysMem = (void*)this->rawImage.get();
                initData.SysMemPitch = static_cast<UINT>(this->iWidth * Image::NUM_CHANNELS * sizeof(unsigned char));
                initData.SysMemSlicePitch = 0;
            }
            hr = device->CreateTexture2D(
                &textureDesc,
                (!this->bMipmapped && this->totalBytes() >= this->iWidth * this->iHeight * Image::NUM_CHANNELS
                     ? &initData
                     : nullptr),
                &this->texture);
            if(FAILED(hr) || this->texture == nullptr) {
                debugLog("DirectX Image Error: Couldn't CreateTexture2D({}, {:x}, {:x}) on file {:s}!", hr, hr,
                         MAKE_DXGI_HRESULT(hr), this->sFilePath);
                engine->showMessageError(
                    "Image Error",
                    fmt::format("DirectX Image error, couldn't CreateTexture2D({}, {:x}, {:x}) on file {}", hr, hr,
                                MAKE_DXGI_HRESULT(hr), this->sFilePath));
                return;
            }
            this->resetDirtyRegion();
        } else {
            // reupload: use UpdateSubresource with dirty rects to avoid full reupload
            auto dirtyRects = this->getDirtyRects();
            const UINT srcRowPitch = static_cast<UINT>(this->iWidth * Image::NUM_CHANNELS);
            const bool fullImage = dirtyRects.size() == 1 && dirtyRects[0].getWidth() == this->iWidth &&
                                   dirtyRects[0].getHeight() == this->iHeight;

            if(fullImage) {
                context->UpdateSubresource(this->texture, 0, nullptr, this->rawImage.get(), srcRowPitch, 0);
            } else {
                for(const auto& rect : dirtyRects) {
                    D3D11_BOX box;
                    box.left = (UINT)rect.getMinX();
                    box.top = (UINT)rect.getMinY();
                    box.right = (UINT)(rect.getMinX() + rect.getWidth());
                    box.bottom = (UINT)(rect.getMinY() + rect.getHeight());
                    box.front = 0;
                    box.back = 1;

                    const u8* src = this->rawImage.get() +
                                    ((i64)rect.getMinY() * this->iWidth + rect.getMinX()) * Image::NUM_CHANNELS;
                    context->UpdateSubresource(this->texture, 0, &box, src, srcRowPitch, 0);
                }
            }

            this->resetDirtyRegion();
        }
    }

    // free memory (not mipmapped) (1/2)
    if(!this->bKeepInSystemMemory && !this->bMipmapped) this->rawImage.clear();

    // create shader resource view
    if(this->shaderResourceView == nullptr) {
        D3D11_SHADER_RESOURCE_VIEW_DESC shaderResourceViewDesc{};
        {
            shaderResourceViewDesc.Format = textureDesc.Format;
            shaderResourceViewDesc.ViewDimension = D3D_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D;
            shaderResourceViewDesc.Texture2D.MipLevels = (this->bMipmapped ? mipLevels : 1);
            shaderResourceViewDesc.Texture2D.MostDetailedMip = 0;
        }
        hr = device->CreateShaderResourceView(this->texture, &shaderResourceViewDesc, &this->shaderResourceView);
        if(FAILED(hr) || this->shaderResourceView == nullptr) {
            this->texture->Release();
            this->texture = nullptr;

            debugLog("DirectX Image Error: Couldn't CreateShaderResourceView({}, {:x}, {:x}) on file {:s}!", hr, hr,
                     MAKE_DXGI_HRESULT(hr), this->sFilePath);
            engine->showMessageError(
                "Image Error",
                fmt::format("DirectX Image error, couldn't CreateShaderResourceView({}, {:x}, {:x}) on file {}", hr, hr,
                            MAKE_DXGI_HRESULT(hr), this->sFilePath));

            return;
        }

        // upload new/overwrite data (mipmapped) (2/2)
        if(this->bMipmapped)
            context->UpdateSubresource(this->texture, 0, nullptr, initData.pSysMem, initData.SysMemPitch,
                                       initData.SysMemPitch * (UINT)this->iHeight);
    }

    // free memory (mipmapped) (2/2)
    if(!this->bKeepInSystemMemory && this->bMipmapped) this->rawImage.clear();

    // create mipmaps
    if(this->bMipmapped) context->GenerateMips(this->shaderResourceView);

    // create sampler
    {
        // default sampler
        if(this->samplerState == nullptr) {
            this->samplerDesc = {};

            this->samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            this->samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            this->samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            this->samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            this->samplerDesc.MinLOD = -D3D11_FLOAT32_MAX;
            this->samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
            this->samplerDesc.MipLODBias =
                0.0f;  // TODO: make this configurable somehow (per texture, but also some kind of global override convar?)
            this->samplerDesc.MaxAnisotropy = 1;  // TODO: anisotropic filtering support (valid range 1 to 16)
            this->samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            this->samplerDesc.BorderColor[0] = 1.0f;
            this->samplerDesc.BorderColor[1] = 1.0f;
            this->samplerDesc.BorderColor[2] = 1.0f;
            this->samplerDesc.BorderColor[3] = 1.0f;
        }

        // customize sampler
        // NOTE: this concatenates into one single actual createOrUpdateSampler() call below because we are not this->bReady yet here on purpose
        {
            if(this->filterMode != TextureFilterMode::LINEAR) setFilterMode(this->filterMode);

            if(this->wrapMode != TextureWrapMode::CLAMP) setWrapMode(this->wrapMode);
        }

        // actually create the (customized) sampler now
        createOrUpdateSampler();
        if(this->samplerState == nullptr) {
            debugLog("DirectX Image Error: Couldn't CreateSamplerState() on file {:s}!", this->sFilePath);
            engine->showMessageError("Image Error",
                                     fmt::format("Couldn't CreateSamplerState() on file {}!", this->sFilePath));
            return;
        }
    }

    this->setReady(true);
}

void DirectX11Image::initAsync() {
    if(this->texture != nullptr) {
        this->setAsyncReady(true);
        return;  // only load if we are not already loaded
    }

    if(!this->bCreatedImage) {
        logIfCV(debug_rm, "Resource Manager: Loading {:s}", this->sFilePath);

        this->setAsyncReady(loadRawImage());
    } else {
        // created image is always async ready
        this->setAsyncReady(true);
    }
}

void DirectX11Image::destroy() {
    // don't delete the texture if we're keeping it in memory, for reloads
    if(!this->bKeepInSystemMemory) {
        this->deleteDX();
        this->rawImage.clear();
    }
}

void DirectX11Image::deleteDX() {
    if(this->shaderResourceView != nullptr) {
        this->shaderResourceView->Release();
        this->shaderResourceView = nullptr;
    }

    if(this->texture != nullptr) {
        this->texture->Release();
        this->texture = nullptr;
    }
}

void DirectX11Image::bind(unsigned int textureUnit) const {
    if(!this->isGPUReady()) return;

    this->iTextureUnitBackup = textureUnit;

    auto* dx11 = static_cast<DirectX11Interface*>(g.get());
    auto* context = dx11->getDeviceContext();
    // backup
    // HACKHACK: slow af
    {
        // release previous backup if unbind() was never called
        if(this->prevShaderResourceView != nullptr) {
            this->prevShaderResourceView->Release();
            this->prevShaderResourceView = nullptr;
        }
        context->PSGetShaderResources(textureUnit, 1, &this->prevShaderResourceView);
    }

    context->PSSetShaderResources(textureUnit, 1, &this->shaderResourceView);
    context->PSSetSamplers(textureUnit, 1, &this->samplerState);

    // HACKHACK: TEMP:
    dx11->setTexturing(true);  // enable texturing
}

void DirectX11Image::unbind() const {
    if(!this->isGPUReady()) return;

    // restore
    // HACKHACK: slow af
    {
        static_cast<DirectX11Interface*>(g.get())->getDeviceContext()->PSSetShaderResources(
            this->iTextureUnitBackup, 1, &this->prevShaderResourceView);

        // refcount
        {
            if(this->prevShaderResourceView != nullptr) {
                this->prevShaderResourceView->Release();
                this->prevShaderResourceView = nullptr;
            }
        }
    }
}

void DirectX11Image::setFilterMode(TextureFilterMode filterMode) {
    Image::setFilterMode(filterMode);

    switch(filterMode) {
        case TextureFilterMode::NONE:
            this->samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            break;
        case TextureFilterMode::LINEAR:
            this->samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            break;
        case TextureFilterMode::MIPMAP:
            this->samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            break;
    }

    // TODO: anisotropic filtering support (this->samplerDesc.Filter = D3D11_FILTER_ANISOTROPIC), needs new FILTER_MODE_ANISOTROPIC and support in other renderers (implies mipmapping)

    if(!this->isGPUReady()) return;

    createOrUpdateSampler();
}

void DirectX11Image::setWrapMode(TextureWrapMode wrapMode) {
    Image::setWrapMode(wrapMode);

    switch(wrapMode) {
        case TextureWrapMode::CLAMP:
            this->samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            this->samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            this->samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            break;
        case TextureWrapMode::REPEAT:
            this->samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
            this->samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
            this->samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            break;
    }

    if(!this->isGPUReady()) return;

    createOrUpdateSampler();
}

void DirectX11Image::createOrUpdateSampler() {
    if(this->samplerState != nullptr) {
        this->samplerState->Release();
        this->samplerState = nullptr;
    }

    static_cast<DirectX11Interface*>(g.get())->getDevice()->CreateSamplerState(&this->samplerDesc, &this->samplerState);
}

#endif
