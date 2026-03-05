//================ Copyright (c) 2025, WH, All rights reserved. =================//
//
// Purpose:		raw opengl es 3.2 graphics interface
//
// $NoKeywords: $gles32i
//===============================================================================//

#pragma once
#ifndef OPENGLES32INTERFACE_H
#define OPENGLES32INTERFACE_H

#include "config.h"

#ifdef MCENGINE_FEATURE_GLES32

#include "ModernGraphicsShared.h"

class OpenGLES32Shader;

class OpenGLES32Interface : public ModernGraphicsShared {
    NOCOPY_NOMOVE(OpenGLES32Interface)
    friend class OpenGLES32Shader;

   public:
    OpenGLES32Interface();
    ~OpenGLES32Interface() override;

    // scene
    void beginScene() override;
    void endScene() override;

    // depth buffer
    void clearDepthBuffer() final;

    // color
    void setColor(Color color) final;
    void setAlpha(float alpha) final;

    // 2d primitive drawing (implemented in ModernGraphicsShared)

    // 2d resource drawing
    void drawImage(const Image *image, AnchorPoint anchor = AnchorPoint::CENTER, float edgeSoftness = 0.0f,
                   McRect clipRect = {}) final;
    void drawString(McFont *font, const UString &text, std::optional<TextShadow> shadow = std::nullopt) final;

    // 3d type drawing
    void drawVAO(VertexArrayObject *vao) final;

    // DEPRECATED: 2d clipping
    void setClipRect(McRect clipRect) final;
    void pushClipRect(McRect clipRect) final;
    void popClipRect() final;

    // viewport modification
    void pushViewport() final;
    void setViewport(int x, int y, int width, int height) final;
    void popViewport() final;

    // stencil
    void pushStencil() final;
    void fillStencil(bool inside) final;
    void popStencil() final;

    // renderer settings
    void setAlphaTesting(bool enabled) final;
    void setAlphaTestFunc(DrawCompareFunc alphaFunc, float ref) final;
    void setAntialiasing(bool aa) final;
    void setClipping(bool enabled) final;
    void setBlending(bool enabled) final;
    void setBlendMode(DrawBlendMode blendMode) final;
    void setDepthBuffer(bool enabled) final;

    void setCulling(bool culling) final;
    void setWireframe(bool _) final;

    // TODO
    void setColorWriting(bool /*r*/, bool /*g*/, bool /*b*/, bool /*a*/) final {}
    void setColorInversion(bool /*enabled*/) final;

    // renderer actions
    void flush() final;

    // renderer info
    [[nodiscard]] vec2 getResolution() const final { return m_vResolution; }
    inline const char *getName() const override { return "OpenGL ES"; }

    // callbacks
    void onResolutionChange(vec2 newResolution) final;

    // factory
    Image *createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) final;
    Image *createImage(int width, int height, bool mipmapped, bool keepInSystemMemory) final;
    RenderTarget *createRenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType) final;
    Shader *createShaderFromFile(std::string vertexShaderFilePath,
                                 std::string fragmentShaderFilePath) final;                      // DEPRECATED
    Shader *createShaderFromSource(std::string vertexShader, std::string fragmentShader) final;  // DEPRECATED
    VertexArrayObject *createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                               bool keepInSystemMemory) final;

    // matrices & transforms
    [[nodiscard]] inline int getShaderGenericAttribPosition() const { return m_iShaderTexturedGenericAttribPosition; }
    [[nodiscard]] inline int getShaderGenericAttribUV() const { return m_iShaderTexturedGenericAttribUV; }
    [[nodiscard]] inline int getShaderGenericAttribCol() const { return m_iShaderTexturedGenericAttribCol; }

    [[nodiscard]] inline unsigned int getVBOVertices() const { return m_iVBOVertices; }
    [[nodiscard]] inline unsigned int getVBOTexcoords() const { return m_iVBOTexcoords; }
    [[nodiscard]] inline unsigned int getVBOTexcolors() const { return m_iVBOTexcolors; }

   protected:
    void onTransformUpdate() final;
    std::vector<u8> getScreenshot(bool withAlpha = false) final;

    void setTexturing(bool /*enabled*/, bool /*force*/) override { /*unused here*/ }

   private:
    void handleGLErrors();

    void registerShader(OpenGLES32Shader *shader);
    void unregisterShader(OpenGLES32Shader *shader);
    void updateAllShaderTransforms();

    std::unique_ptr<Shader> smoothClipShader{nullptr};
    void initSmoothClipShader();

    // renderer
    vec2 m_vResolution;

    OpenGLES32Shader *m_shaderTexturedGeneric;
    std::vector<OpenGLES32Shader *> m_registeredShaders;
    int m_iShaderTexturedGenericPrevType;
    int m_iShaderTexturedGenericAttribPosition;
    int m_iShaderTexturedGenericAttribUV;
    int m_iShaderTexturedGenericAttribCol;
    unsigned int m_iVBOVertices;
    unsigned int m_iVBOTexcoords;
    unsigned int m_iVBOTexcolors;

    // persistent vars
    Color m_color;
    bool m_bAntiAliasing;
    bool m_bInScene;
    bool m_bColorInversion{false};

    // clipping
    std::vector<McRect> m_clipRectStack;
};

#else
class OpenGLES32Interface {};
#endif

#endif
