#pragma once
// Copyright (c) 2012, PG, All rights reserved.
#include "Resource.h"
#include "Matrices.h"

#include <vector>
#include <cstring>

using std::string_view_literals::operator""sv;

class Shader : public Resource {
    NOCOPY_NOMOVE(Shader)
   public:
    Shader() : Resource(SHADER) {}
    ~Shader() override = default;

    virtual void enable() = 0;
    virtual void disable() = 0;

    virtual void setUniform1f(std::string_view name, float value) = 0;
    virtual void setUniform1fv(std::string_view name, int count, const float *const values) = 0;
    virtual void setUniform1i(std::string_view name, int value) = 0;
    virtual void setUniform2f(std::string_view name, float x, float y) = 0;
    virtual void setUniform2fv(std::string_view name, int count, const float *const vectors) = 0;
    virtual void setUniform3f(std::string_view name, float x, float y, float z) = 0;
    virtual void setUniform3fv(std::string_view name, int count, const float *const vectors) = 0;
    virtual void setUniform4f(std::string_view name, float x, float y, float z, float w) = 0;
    virtual void setUniformMatrix4fv(std::string_view name, const Matrix4 &matrix) = 0;
    virtual void setUniformMatrix4fv(std::string_view name, const float *const v) = 0;

    // to avoid redundantly setting MVP matrix
    // just a small optimization for a very common uniform
    // cache seems to have a ~21% hit rate in practice
    inline void setMVP(const Matrix4 &mvp) {
        if(std::memcmp((void *)m_lastMVP.get(), (void *)mvp.get(), sizeof(float) * 16) == 0) return;
        setUniformMatrix4fv("mvp"sv, mvp);
    }

    Shader *asShader() final { return this; }
    [[nodiscard]] const Shader *asShader() const final { return this; }

   protected:
    void init() override = 0;
    void initAsync() override = 0;
    void destroy() override = 0;

    struct SHADER_PARSE_RESULT {
        std::string source;
        std::vector<std::string> descs;
    };

    SHADER_PARSE_RESULT parseShaderFromString(const std::string &graphicsInterfaceAndShaderTypePrefix,
                                              const std::string &shaderSource);

   private:
    // clang-format off
    static inline constexpr float initCachedMVP[16]{
        -1.f, -1.f, -1.f, -1.f,
        -1.f, -1.f, -1.f, -1.f,
        -1.f, -1.f, -1.f, -1.f,
        -1.f, -1.f, -1.f, -1.f
    };
    // clang-format on
    Matrix4 m_lastMVP{&initCachedMVP[0]};
};
