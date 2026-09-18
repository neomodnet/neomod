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

class OpenGLES32Shader final : public Shader {
    NOCOPY_NOMOVE(OpenGLES32Shader)
   public:
    OpenGLES32Shader(const std::string &shader, bool source);
    OpenGLES32Shader(const std::string &vertexShader, const std::string &fragmentShader, bool source);  // DEPRECATED
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
    bool compile(const std::string &vertexShader, const std::string &fragmentShader, bool source);
    int createShaderFromString(const std::string &shaderSource, int shaderType);
    int createShaderFromFile(const std::string &fileName, int shaderType);
    int getAndCacheUniformLocation(std::string_view name);

    std::string m_sVsh, m_sFsh;

    int m_iVertexShader;
    int m_iFragmentShader;
    int m_iProgram;

    int m_iProgramBackup;

    Hash::unstable_stringmap<int> m_uniformLocationCache;
};

#endif

#endif
