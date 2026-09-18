#pragma once
// Copyright (c) 2012, PG, All rights reserved.
#include "Resource.h"

#include <vector>
#include <cstring>
#include <array>

struct Matrix4;

class Shader : public Resource {
    NOCOPY_NOMOVE(Shader)
   public:
    Shader() : Resource(SHADER) {}
    ~Shader() override = default;

    virtual void enable() = 0;
    virtual void disable() = 0;

    void setUniform1f(std::string_view name, float value);
    void setUniform1fv(std::string_view name, int count, const float *const values);
    void setUniform1i(std::string_view name, int value);
    void setUniform2f(std::string_view name, float x, float y);
    void setUniform2fv(std::string_view name, int count, const float *const vectors);
    void setUniform3f(std::string_view name, float x, float y, float z);
    void setUniform3fv(std::string_view name, int count, const float *const vectors);
    void setUniform4f(std::string_view name, float x, float y, float z, float w);
    void setUniformMatrix4fv(std::string_view name, const Matrix4 &matrix);
    void setUniformMatrix4fv(std::string_view name, const float *const v);

    // to avoid redundantly setting MVP matrix
    // just a small optimization for a very common uniform
    // cache seems to have a ~21% hit rate in practice
    void setMVP(const Matrix4 &mvp);

    Shader *asShader() final { return this; }
    [[nodiscard]] const Shader *asShader() const final { return this; }

   protected:
    void init() override = 0;
    void initAsync() override = 0;
    void destroy() override = 0;

    enum UniformType : unsigned char {
        UNI_1F,
        UNI_1FV,
        UNI_1I,
        UNI_2F,
        UNI_2FV,
        UNI_3F,
        UNI_3FV,
        UNI_4F,
        UNI_MATRIX4FV,
    };

    virtual void writeUniform(std::string_view name, UniformType type, const void *const data, unsigned int dataSize) = 0;

   private:
    // clang-format off
    static inline constexpr std::array<float, 16> initCachedMVP{
        -1.f, -1.f, -1.f, -1.f,
        -1.f, -1.f, -1.f, -1.f,
        -1.f, -1.f, -1.f, -1.f,
        -1.f, -1.f, -1.f, -1.f
    };
    // clang-format on
    std::array<float, 16> m_lastMVP{initCachedMVP};
};
