#pragma once
// Copyright (c) 2016, PG, All rights reserved.
#ifndef OPENGLSHADER_H
#define OPENGLSHADER_H

#include "config.h"

#ifdef MCENGINE_FEATURE_OPENGL

#include "Shader.h"

#include "Hashing.h"

class OpenGLShader final : public Shader {
    NOCOPY_NOMOVE(OpenGLShader)
   public:
    OpenGLShader(std::string vertexShader, std::string fragmentShader, bool source);
    ~OpenGLShader() override { destroy(); }

    void enable() override;
    void disable() override;

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

    void writeUniform(std::string_view name, UniformType type, const void *const data, unsigned int dataSize) override;

   private:
    bool compile(const std::string &vertexShader, const std::string &fragmentShader, bool source);
    int createShaderFromString(const std::string &shaderSource, int shaderType);
    int createShaderFromFile(const std::string &fileName, int shaderType);

    int getAttribLocation(std::string_view name);
    int getAndCacheUniformLocation(std::string_view name);

    std::string sVsh;
    std::string sFsh;

    bool bSource;
    unsigned int iVertexShader;
    unsigned int iFragmentShader;
    unsigned int iProgram;

    unsigned int iProgramBackup;
    OpenGLShader *activeShaderBackup{nullptr};

    Hash::unstable_stringmap<int> uniformLocationCache;
};

#endif

#endif
