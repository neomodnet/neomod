//================ Copyright (c) 2012, PG, All rights reserved. =================//
//
// Purpose:		shader wrapper
//
// $NoKeywords: $shader
//===============================================================================//

#include "Shader.h"
#include "SString.h"
#include "Engine.h"
#include "Matrices.h"

#include <sstream>

void Shader::setUniform1f(std::string_view name, float value) { writeUniform(name, UNI_1F, &value, sizeof(float)); }

void Shader::setUniform1fv(std::string_view name, int count, const float *const values) {
    writeUniform(name, UNI_1FV, values, (u32)(sizeof(float) * count));
}

void Shader::setUniform1i(std::string_view name, int value) { writeUniform(name, UNI_1I, &value, sizeof(int)); }

void Shader::setUniform2f(std::string_view name, float x, float y) {
    float v[2] = {x, y};
    writeUniform(name, UNI_2F, &v[0], sizeof(v));
}

void Shader::setUniform2fv(std::string_view name, int count, const float *const vectors) {
    writeUniform(name, UNI_2FV, vectors, (u32)(sizeof(float) * 2 * count));
}

void Shader::setUniform3f(std::string_view name, float x, float y, float z) {
    float v[3] = {x, y, z};
    writeUniform(name, UNI_3F, &v[0], sizeof(v));
}

void Shader::setUniform3fv(std::string_view name, int count, const float *const vectors) {
    writeUniform(name, UNI_3FV, vectors, (u32)(sizeof(float) * 3 * count));
}

void Shader::setUniform4f(std::string_view name, float x, float y, float z, float w) {
    float v[4] = {x, y, z, w};
    writeUniform(name, UNI_4F, &v[0], sizeof(v));
}

void Shader::setUniformMatrix4fv(std::string_view name, const Matrix4 &matrix) {
    setUniformMatrix4fv(name, matrix.get());
}

void Shader::setUniformMatrix4fv(std::string_view name, const float *const v) {
    writeUniform(name, UNI_MATRIX4FV, v, sizeof(float) * 16);
}

void Shader::setMVP(const Matrix4 &mvp) {
    if(std::memcmp((void *)m_lastMVP.data(), (void *)mvp.get(), sizeof(float) * 16) == 0) return;
    std::memcpy((void *)m_lastMVP.data(), (void *)mvp.get(), sizeof(float) * 16);
    using std::string_view_literals::operator""sv;
    setUniformMatrix4fv("mvp"sv, mvp);
}
