//================ Copyright (c) 2025, WH, All rights reserved. =================//
//
// Purpose:		OpenGLES 3.2 GLSL implementation of Shader
//
// $NoKeywords: $gles32shader
//===============================================================================//

#pragma once
#ifndef OPENGLES32SHADER_H
#define OPENGLES32SHADER_H

#include "config.h"

#ifdef MCENGINE_FEATURE_GLES32

#include "Shader.h"
#include "Hashing.h"

#include <string_view>

class OpenGLES32Shader final : public Shader {
    NOCOPY_NOMOVE(OpenGLES32Shader)
   public:
    OpenGLES32Shader(std::string_view vertexShader, std::string_view fragmentShader);
    ~OpenGLES32Shader() override { destroy(); }

    void enable() override;
    void disable() override;

    int getAttribLocation(std::string_view name);

    // ILLEGAL:
    bool isActive();

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

    void writeUniform(std::string_view name, UniformType type, const void *const data, unsigned int dataSize) override;

   private:
    bool compile(std::string_view vertexShader, std::string_view fragmentShader);
    int createShaderFromString(std::string_view shaderSource, int shaderType);
    int getAndCacheUniformLocation(std::string_view name);

    std::string_view m_sVsh, m_sFsh;

    int m_iVertexShader;
    int m_iFragmentShader;
    int m_iProgram;

    int m_iProgramBackup;

    Hash::unstable_stringmap<int> m_uniformLocationCache;
};

#endif

#endif
