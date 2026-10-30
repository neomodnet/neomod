#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#ifndef OPENGLSHADER_H
#define OPENGLSHADER_H

#include "config.h"

#ifdef MCENGINE_FEATURE_OPENGL

#include "Shader.h"

#include "Hashing.h"

#include <string_view>

class OpenGLShader final : public Shader {
    NOCOPY_NOMOVE(OpenGLShader)
   public:
    OpenGLShader(std::string_view vertexShader, std::string_view fragmentShader);
    ~OpenGLShader() override { destroy(); }

    void enable() override;
    void disable() override;

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

    void writeUniform(std::string_view name, UniformType type, const void *const data, unsigned int dataSize) override;

   private:
    bool compile(std::string_view vertexShader, std::string_view fragmentShader);
    int createShaderFromString(std::string_view shaderSource, int shaderType);

    int getAttribLocation(std::string_view name);
    int getAndCacheUniformLocation(std::string_view name);

    std::string_view sVsh;
    std::string_view sFsh;

    unsigned int iVertexShader;
    unsigned int iFragmentShader;
    unsigned int iProgram;

    unsigned int iProgramBackup;
    OpenGLShader *activeShaderBackup{nullptr};

    Hash::unstable_stringmap<int> uniformLocationCache;
};

#endif

#endif
