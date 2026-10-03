//================ Copyright (c) 2017, PG, All rights reserved. =================//
//
// Purpose:		DirectX implementation of RenderTarget / render to texture
//
// $NoKeywords: $drt
//===============================================================================//

#pragma once
#ifndef DIRECTX11RENDERTARGET_H
#define DIRECTX11RENDERTARGET_H
#include "config.h"

#ifdef MCENGINE_FEATURE_DIRECTX11
#include "RenderTarget.h"

#if defined(__GNUC__) || defined(__clang__)
MC_DO_PRAGMA(GCC diagnostic ignored "-Wpragmas")
MC_DO_PRAGMA(GCC diagnostic ignored "-Wextern-c-compat")
MC_DO_PRAGMA(GCC diagnostic push)
#endif

#include "d3d11.h"

#if defined(__GNUC__) || defined(__clang__)
MC_DO_PRAGMA(GCC diagnostic pop)
#endif

class DirectX11Interface;

class DirectX11RenderTarget final : public RenderTarget {
    NOCOPY_NOMOVE(DirectX11RenderTarget)
   public:
    DirectX11RenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType = MultisampleType{0});
    ~DirectX11RenderTarget() override { destroy(); }

    void enable(bool clear) override;
    void disable() override;

    void bind(unsigned int textureUnit = 0) override;
    void unbind() override;

    // ILLEGAL:
    [[nodiscard]] inline ID3D11Texture2D *getRenderTexture() const { return this->renderTexture; }

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    ID3D11Texture2D *renderTexture;
    ID3D11Texture2D *depthStencilTexture;
    ID3D11RenderTargetView *renderTargetView;
    ID3D11DepthStencilView *depthStencilView;
    ID3D11ShaderResourceView *shaderResourceView;

    ID3D11RenderTargetView *prevRenderTargetView;
    ID3D11DepthStencilView *prevDepthStencilView;

    unsigned int iTextureUnitBackup;
    ID3D11ShaderResourceView *prevShaderResourceView;
};

#endif

#endif
