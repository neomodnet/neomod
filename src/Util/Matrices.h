///////////////////////////////////////////////////////////////////////////////
// Matrices.h
// ==========
// NxN Matrix Math classes using GLM under the hood with optimizations
//
// API compatible with the original McEngine Matrices.h (by Song Ho Ahn)
// (some of the Matrix4 operations are copied to maintain the original semantics)
//
// The elements of the matrix are stored as column major order.
// | 0 2 |    | 0 3 6 |    |  0  4  8 12 |
// | 1 3 |    | 1 4 7 |    |  1  5  9 13 |
//            | 2 5 8 |    |  2  6 10 14 |
//                         |  3  7 11 15 |
///////////////////////////////////////////////////////////////////////////////

#pragma once
#ifndef MATH_MATRICES_H
#define MATH_MATRICES_H

#include "Vectors.h"

#include "fmt/ostream.h"
#include <cmath>
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/mat2x2.hpp"
#include "glm/mat3x3.hpp"
#include "glm/mat4x4.hpp"
#include <iomanip>
#include <iostream>

inline constexpr float MATRIX_FLOAT_EPSILON = 0.00001f;

///////////////////////////////////////////////////////////////////////////
// 2x2 matrix wrapper around glm::mat2
///////////////////////////////////////////////////////////////////////////
struct Matrix2 : public glm::mat2 {
    using glm::mat2::mat2;
    using glm::mat2::operator=;

    // constructors
    constexpr Matrix2() : glm::mat2(1.0f) {}  // identity matrix
    constexpr Matrix2(const glm::mat2& other) : glm::mat2(other) {}

    constexpr Matrix2(const float src[4]) : glm::mat2(src[0], src[1], src[2], src[3]) {}  // column-major order

    void set(const float src[4]) {
        static_cast<glm::mat2&>(*this)[0][0] = src[0];
        static_cast<glm::mat2&>(*this)[0][1] = src[1];
        static_cast<glm::mat2&>(*this)[1][0] = src[2];
        static_cast<glm::mat2&>(*this)[1][1] = src[3];
    }

    void set(float m0, float m1, float m2, float m3) {
        static_cast<glm::mat2&>(*this)[0][0] = m0;
        static_cast<glm::mat2&>(*this)[0][1] = m1;
        static_cast<glm::mat2&>(*this)[1][0] = m2;
        static_cast<glm::mat2&>(*this)[1][1] = m3;
    }

    void setRow(int index, const float row[2]) {
        static_cast<glm::mat2&>(*this)[0][index] = row[0];
        static_cast<glm::mat2&>(*this)[1][index] = row[1];
    }

    void setRow(int index, const vec2& v) {
        static_cast<glm::mat2&>(*this)[0][index] = v.x;
        static_cast<glm::mat2&>(*this)[1][index] = v.y;
    }

    void setColumn(int index, const float col[2]) {
        static_cast<glm::mat2&>(*this)[index][0] = col[0];
        static_cast<glm::mat2&>(*this)[index][1] = col[1];
    }

    void setColumn(int index, const vec2& v) {
        static_cast<glm::mat2&>(*this)[index][0] = v.x;
        static_cast<glm::mat2&>(*this)[index][1] = v.y;
    }

    // accessors
    [[nodiscard]] const float* get() const { return &static_cast<const glm::mat2&>(*this)[0][0]; }
    [[nodiscard]] const glm::mat2& getGLM() const { return *this; }
    [[nodiscard]] glm::mat2& getGLM() { return *this; }
    [[nodiscard]] float getDeterminant() const { return glm::determinant(*this); }

    Matrix2& identity() {
        *this = glm::mat2(1.0f);
        return *this;
    }

    Matrix2& transpose() {
        *this = glm::transpose(*this);
        return *this;
    }

    Matrix2& invert() {
        float det = getDeterminant();
        if(std::fabs(det) <= MATRIX_FLOAT_EPSILON) {
            return identity();
        }
        *this = glm::inverse(*this);
        return *this;
    }

    // operators - many are inherited from glm::mat2, but we can override for API consistency
    Matrix2 operator+(const Matrix2& rhs) const {
        return {static_cast<const glm::mat2&>(*this) + static_cast<const glm::mat2&>(rhs)};
    }

    Matrix2 operator-(const Matrix2& rhs) const {
        return {static_cast<const glm::mat2&>(*this) - static_cast<const glm::mat2&>(rhs)};
    }

    Matrix2& operator+=(const Matrix2& rhs) {
        static_cast<glm::mat2&>(*this) += static_cast<const glm::mat2&>(rhs);
        return *this;
    }

    Matrix2& operator-=(const Matrix2& rhs) {
        static_cast<glm::mat2&>(*this) -= static_cast<const glm::mat2&>(rhs);
        return *this;
    }

    vec2 operator*(const vec2& rhs) const {
        glm::vec2 result = static_cast<const glm::mat2&>(*this) * static_cast<const glm::vec2&>(rhs);
        return {result};
    }

    Matrix2 operator*(const Matrix2& rhs) const {
        return {static_cast<const glm::mat2&>(*this) * static_cast<const glm::mat2&>(rhs)};
    }

    Matrix2& operator*=(const Matrix2& rhs) {
        static_cast<glm::mat2&>(*this) *= static_cast<const glm::mat2&>(rhs);
        return *this;
    }

    // comparison operators are inherited from glm::mat2

    // array access is inherited from glm::mat2 via operator[]
    float operator[](int index) const { return (&static_cast<const glm::mat2&>(*this)[0][0])[index]; }
    float& operator[](int index) { return (&static_cast<glm::mat2&>(*this)[0][0])[index]; }

    friend Matrix2 operator-(const Matrix2& m);
    friend Matrix2 operator*(float scalar, const Matrix2& m);
    friend vec2 operator*(const vec2& vec, const Matrix2& m);
    friend std::ostream& operator<<(std::ostream& os, const Matrix2& m);
};

inline Matrix2 operator-(const Matrix2& m) { return {-static_cast<const glm::mat2&>(m)}; }

inline Matrix2 operator*(float s, const Matrix2& m) { return {s * static_cast<const glm::mat2&>(m)}; }

inline vec2 operator*(const vec2& v, const Matrix2& m) {
    glm::vec2 result = static_cast<const glm::vec2&>(v) * static_cast<const glm::mat2&>(m);
    return {result};
}

inline std::ostream& operator<<(std::ostream& os, const Matrix2& m) {
    os << std::fixed << std::setprecision(5);
    os << "[" << std::setw(10) << m[0] << " " << std::setw(10) << m[2] << "]\n"
       << "[" << std::setw(10) << m[1] << " " << std::setw(10) << m[3] << "]\n";
    os << std::resetiosflags(std::ios_base::fixed | std::ios_base::floatfield);
    return os;
}

///////////////////////////////////////////////////////////////////////////
// 3x3 matrix wrapper around glm::mat3
///////////////////////////////////////////////////////////////////////////
struct Matrix3 : public glm::mat3 {
    using glm::mat3::mat3;
    using glm::mat3::operator=;

    // constructors
    constexpr Matrix3() : glm::mat3(1.0f) {}  // identity matrix
    constexpr Matrix3(const glm::mat3& other) : glm::mat3(other) {}

    constexpr Matrix3(const float src[9])
        : glm::mat3(src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7], src[8]) {}

    void set(const float src[9]) {
        for(int col = 0; col < 3; ++col)
            for(int row = 0; row < 3; ++row) static_cast<glm::mat3&>(*this)[col][row] = src[col * 3 + row];
    }

    void set(float m0, float m1, float m2, float m3, float m4, float m5, float m6, float m7, float m8) {
        static_cast<glm::mat3&>(*this)[0][0] = m0;
        static_cast<glm::mat3&>(*this)[0][1] = m1;
        static_cast<glm::mat3&>(*this)[0][2] = m2;
        static_cast<glm::mat3&>(*this)[1][0] = m3;
        static_cast<glm::mat3&>(*this)[1][1] = m4;
        static_cast<glm::mat3&>(*this)[1][2] = m5;
        static_cast<glm::mat3&>(*this)[2][0] = m6;
        static_cast<glm::mat3&>(*this)[2][1] = m7;
        static_cast<glm::mat3&>(*this)[2][2] = m8;
    }

    void setRow(int index, const float row[3]) {
        static_cast<glm::mat3&>(*this)[0][index] = row[0];
        static_cast<glm::mat3&>(*this)[1][index] = row[1];
        static_cast<glm::mat3&>(*this)[2][index] = row[2];
    }

    void setRow(int index, const vec3& v) {
        static_cast<glm::mat3&>(*this)[0][index] = v.x;
        static_cast<glm::mat3&>(*this)[1][index] = v.y;
        static_cast<glm::mat3&>(*this)[2][index] = v.z;
    }

    void setColumn(int index, const float col[3]) {
        static_cast<glm::mat3&>(*this)[index][0] = col[0];
        static_cast<glm::mat3&>(*this)[index][1] = col[1];
        static_cast<glm::mat3&>(*this)[index][2] = col[2];
    }

    void setColumn(int index, const vec3& v) {
        static_cast<glm::mat3&>(*this)[index][0] = v.x;
        static_cast<glm::mat3&>(*this)[index][1] = v.y;
        static_cast<glm::mat3&>(*this)[index][2] = v.z;
    }

    [[nodiscard]] const float* get() const { return &static_cast<const glm::mat3&>(*this)[0][0]; }
    [[nodiscard]] const glm::mat3& getGLM() const { return *this; }
    [[nodiscard]] glm::mat3& getGLM() { return *this; }
    [[nodiscard]] float getDeterminant() const { return glm::determinant(*this); }

    Matrix3& identity() {
        *this = glm::mat3(1.0f);
        return *this;
    }

    Matrix3& transpose() {
        *this = glm::transpose(*this);
        return *this;
    }

    Matrix3& invert() {
        float det = getDeterminant();
        if(std::fabs(det) <= MATRIX_FLOAT_EPSILON) {
            return identity();
        }
        *this = glm::inverse(*this);
        return *this;
    }

    // operators
    Matrix3 operator+(const Matrix3& rhs) const {
        return {static_cast<const glm::mat3&>(*this) + static_cast<const glm::mat3&>(rhs)};
    }

    Matrix3 operator-(const Matrix3& rhs) const {
        return {static_cast<const glm::mat3&>(*this) - static_cast<const glm::mat3&>(rhs)};
    }

    Matrix3& operator+=(const Matrix3& rhs) {
        static_cast<glm::mat3&>(*this) += static_cast<const glm::mat3&>(rhs);
        return *this;
    }

    Matrix3& operator-=(const Matrix3& rhs) {
        static_cast<glm::mat3&>(*this) -= static_cast<const glm::mat3&>(rhs);
        return *this;
    }

    vec3 operator*(const vec3& rhs) const {
        glm::vec3 result = static_cast<const glm::mat3&>(*this) * static_cast<const glm::vec3&>(rhs);
        return {result};
    }

    Matrix3 operator*(const Matrix3& rhs) const {
        return {static_cast<const glm::mat3&>(*this) * static_cast<const glm::mat3&>(rhs)};
    }

    Matrix3& operator*=(const Matrix3& rhs) {
        static_cast<glm::mat3&>(*this) *= static_cast<const glm::mat3&>(rhs);
        return *this;
    }

    float operator[](int index) const { return (&static_cast<const glm::mat3&>(*this)[0][0])[index]; }
    float& operator[](int index) { return (&static_cast<glm::mat3&>(*this)[0][0])[index]; }

    friend Matrix3 operator-(const Matrix3& m);
    friend Matrix3 operator*(float scalar, const Matrix3& m);
    friend vec3 operator*(const vec3& vec, const Matrix3& m);
    friend std::ostream& operator<<(std::ostream& os, const Matrix3& m);
};

inline Matrix3 operator-(const Matrix3& m) { return {-static_cast<const glm::mat3&>(m)}; }

inline Matrix3 operator*(float s, const Matrix3& m) { return {s * static_cast<const glm::mat3&>(m)}; }

inline vec3 operator*(const vec3& v, const Matrix3& m) {
    glm::vec3 result = static_cast<const glm::vec3&>(v) * static_cast<const glm::mat3&>(m);
    return {result};
}

inline std::ostream& operator<<(std::ostream& os, const Matrix3& m) {
    os << std::fixed << std::setprecision(5);
    os << "[" << std::setw(10) << m[0] << " " << std::setw(10) << m[3] << " " << std::setw(10) << m[6] << "]\n"
       << "[" << std::setw(10) << m[1] << " " << std::setw(10) << m[4] << " " << std::setw(10) << m[7] << "]\n"
       << "[" << std::setw(10) << m[2] << " " << std::setw(10) << m[5] << " " << std::setw(10) << m[8] << "]\n";
    os << std::resetiosflags(std::ios_base::fixed | std::ios_base::floatfield);
    return os;
}

///////////////////////////////////////////////////////////////////////////
// 4x4 matrix wrapper around glm::mat4
///////////////////////////////////////////////////////////////////////////
struct Matrix4 : public glm::mat4 {
    using glm::mat4::mat4;
    using glm::mat4::operator=;
    using glm::mat4::operator[];

    // constructors
    constexpr Matrix4() : glm::mat4(1.0f) {}  // identity matrix
    constexpr Matrix4(const glm::mat4& other) : glm::mat4(other) {}

    constexpr Matrix4(const float src[16])
        : glm::mat4(src[0], src[1], src[2], src[3], src[4], src[5], src[6], src[7], src[8], src[9], src[10], src[11],
                    src[12], src[13], src[14], src[15]) {}

    void set(const float src[16]) {
        for(int col = 0; col < 4; ++col)
            for(int row = 0; row < 4; ++row) static_cast<glm::mat4&>(*this)[col][row] = src[col * 4 + row];
    }

    void set(float m00, float m01, float m02, float m03, float m04, float m05, float m06, float m07, float m08,
             float m09, float m10, float m11, float m12, float m13, float m14, float m15) {
        static_cast<glm::mat4&>(*this)[0][0] = m00;
        static_cast<glm::mat4&>(*this)[0][1] = m01;
        static_cast<glm::mat4&>(*this)[0][2] = m02;
        static_cast<glm::mat4&>(*this)[0][3] = m03;
        static_cast<glm::mat4&>(*this)[1][0] = m04;
        static_cast<glm::mat4&>(*this)[1][1] = m05;
        static_cast<glm::mat4&>(*this)[1][2] = m06;
        static_cast<glm::mat4&>(*this)[1][3] = m07;
        static_cast<glm::mat4&>(*this)[2][0] = m08;
        static_cast<glm::mat4&>(*this)[2][1] = m09;
        static_cast<glm::mat4&>(*this)[2][2] = m10;
        static_cast<glm::mat4&>(*this)[2][3] = m11;
        static_cast<glm::mat4&>(*this)[3][0] = m12;
        static_cast<glm::mat4&>(*this)[3][1] = m13;
        static_cast<glm::mat4&>(*this)[3][2] = m14;
        static_cast<glm::mat4&>(*this)[3][3] = m15;
    }

    void setRow(int index, const float row[4]) {
        static_cast<glm::mat4&>(*this)[0][index] = row[0];
        static_cast<glm::mat4&>(*this)[1][index] = row[1];
        static_cast<glm::mat4&>(*this)[2][index] = row[2];
        static_cast<glm::mat4&>(*this)[3][index] = row[3];
    }

    void setRow(int index, const vec4& v) {
        static_cast<glm::mat4&>(*this)[0][index] = v.x;
        static_cast<glm::mat4&>(*this)[1][index] = v.y;
        static_cast<glm::mat4&>(*this)[2][index] = v.z;
        static_cast<glm::mat4&>(*this)[3][index] = v.w;
    }

    void setRow(int index, const vec3& v) {
        static_cast<glm::mat4&>(*this)[0][index] = v.x;
        static_cast<glm::mat4&>(*this)[1][index] = v.y;
        static_cast<glm::mat4&>(*this)[2][index] = v.z;
    }

    void setColumn(int index, const float col[4]) {
        static_cast<glm::mat4&>(*this)[index][0] = col[0];
        static_cast<glm::mat4&>(*this)[index][1] = col[1];
        static_cast<glm::mat4&>(*this)[index][2] = col[2];
        static_cast<glm::mat4&>(*this)[index][3] = col[3];
    }

    void setColumn(int index, const vec4& v) {
        static_cast<glm::mat4&>(*this)[index][0] = v.x;
        static_cast<glm::mat4&>(*this)[index][1] = v.y;
        static_cast<glm::mat4&>(*this)[index][2] = v.z;
        static_cast<glm::mat4&>(*this)[index][3] = v.w;
    }

    void setColumn(int index, const vec3& v) {
        static_cast<glm::mat4&>(*this)[index][0] = v.x;
        static_cast<glm::mat4&>(*this)[index][1] = v.y;
        static_cast<glm::mat4&>(*this)[index][2] = v.z;
    }

    [[nodiscard]] const float* get() const { return &static_cast<const glm::mat4&>(*this)[0][0]; }
    [[nodiscard]] const glm::mat4& getGLM() const { return *this; }
    [[nodiscard]] glm::mat4& getGLM() { return *this; }

    [[nodiscard]] float getDeterminant() const { return glm::determinant(*this); }

    Matrix4& identity() {
        *this = glm::mat4(1.0f);
        return *this;
    }

    Matrix4& transpose() {
        *this = glm::transpose(*this);
        return *this;
    }

    Matrix4& invert() {
        float det = getDeterminant();
        if(std::fabs(det) <= MATRIX_FLOAT_EPSILON) {
            return identity();
        }
        *this = glm::inverse(*this);
        return *this;
    }

    Matrix4& invertEuclidean() {
        // R^-1 = R^T (rotation is orthogonal)
        // T' = -R^T * T

        // extract rotation part (3x3 upper-left)
        auto rotation = glm::mat3(*this);
        glm::mat3 rotationT = glm::transpose(rotation);

        // extract translation (last column)
        auto translation = glm::vec3((*this)[3]);

        // new translation = -R^T * T
        glm::vec3 newTranslation = -rotationT * translation;

        // build the inverse matrix
        *this = glm::mat4(rotationT);
        static_cast<glm::mat4&>(*this)[3] = glm::vec4(newTranslation, 1.0f);

        return *this;
    }

    Matrix4& invertAffine() { return invert(); }
    Matrix4& invertProjective() { return invert(); }
    Matrix4& invertGeneral() { return invert(); }

    // transform matrix operations
    Matrix4& translate(float x, float y, float z) {
        float* m = &static_cast<glm::mat4&>(*this)[0][0];

        m[0] += m[3] * x;
        m[4] += m[7] * x;
        m[8] += m[11] * x;
        m[12] += m[15] * x;
        m[1] += m[3] * y;
        m[5] += m[7] * y;
        m[9] += m[11] * y;
        m[13] += m[15] * y;
        m[2] += m[3] * z;
        m[6] += m[7] * z;
        m[10] += m[11] * z;
        m[14] += m[15] * z;

        return *this;
    }

    Matrix4& translate(const vec3& v) { return translate(v.x, v.y, v.z); }

    Matrix4& rotate(float angle, const vec3& axis) { return rotate(angle, axis.x, axis.y, axis.z); }

    Matrix4& rotate(float angle, float x, float y, float z) {
        float c = cosf(glm::radians(angle));  // cosine
        float s = sinf(glm::radians(angle));  // sine
        float c1 = 1.0f - c;                  // 1 - c

        float* m = &static_cast<glm::mat4&>(*this)[0][0];
        float m0 = m[0], m4 = m[4], m8 = m[8], m12 = m[12], m1 = m[1], m5 = m[5], m9 = m[9], m13 = m[13], m2 = m[2],
              m6 = m[6], m10 = m[10], m14 = m[14];

        // build rotation matrix components
        float r0 = x * x * c1 + c;
        float r1 = x * y * c1 + z * s;
        float r2 = x * z * c1 - y * s;
        float r4 = x * y * c1 - z * s;
        float r5 = y * y * c1 + c;
        float r6 = y * z * c1 + x * s;
        float r8 = x * z * c1 + y * s;
        float r9 = y * z * c1 - x * s;
        float r10 = z * z * c1 + c;

        // multiply rotation matrix
        m[0] = r0 * m0 + r4 * m1 + r8 * m2;
        m[1] = r1 * m0 + r5 * m1 + r9 * m2;
        m[2] = r2 * m0 + r6 * m1 + r10 * m2;
        m[4] = r0 * m4 + r4 * m5 + r8 * m6;
        m[5] = r1 * m4 + r5 * m5 + r9 * m6;
        m[6] = r2 * m4 + r6 * m5 + r10 * m6;
        m[8] = r0 * m8 + r4 * m9 + r8 * m10;
        m[9] = r1 * m8 + r5 * m9 + r9 * m10;
        m[10] = r2 * m8 + r6 * m9 + r10 * m10;
        m[12] = r0 * m12 + r4 * m13 + r8 * m14;
        m[13] = r1 * m12 + r5 * m13 + r9 * m14;
        m[14] = r2 * m12 + r6 * m13 + r10 * m14;

        return *this;
    }

    Matrix4& rotateX(float angle) {
        float c = cosf(glm::radians(angle));
        float s = sinf(glm::radians(angle));
        float* m = &static_cast<glm::mat4&>(*this)[0][0];

        float m1 = m[1], m2 = m[2], m5 = m[5], m6 = m[6], m9 = m[9], m10 = m[10], m13 = m[13], m14 = m[14];

        m[1] = m1 * c + m2 * -s;
        m[2] = m1 * s + m2 * c;
        m[5] = m5 * c + m6 * -s;
        m[6] = m5 * s + m6 * c;
        m[9] = m9 * c + m10 * -s;
        m[10] = m9 * s + m10 * c;
        m[13] = m13 * c + m14 * -s;
        m[14] = m13 * s + m14 * c;

        return *this;
    }

    Matrix4& rotateY(float angle) {
        float c = cosf(glm::radians(angle));
        float s = sinf(glm::radians(angle));
        float* m = &static_cast<glm::mat4&>(*this)[0][0];

        float m0 = m[0], m2 = m[2], m4 = m[4], m6 = m[6], m8 = m[8], m10 = m[10], m12 = m[12], m14 = m[14];

        m[0] = m0 * c + m2 * s;
        m[2] = m0 * -s + m2 * c;
        m[4] = m4 * c + m6 * s;
        m[6] = m4 * -s + m6 * c;
        m[8] = m8 * c + m10 * s;
        m[10] = m8 * -s + m10 * c;
        m[12] = m12 * c + m14 * s;
        m[14] = m12 * -s + m14 * c;

        return *this;
    }

    Matrix4& rotateZ(float angle) {
        float c = cosf(glm::radians(angle));
        float s = sinf(glm::radians(angle));
        float* m = &static_cast<glm::mat4&>(*this)[0][0];

        float m0 = m[0], m1 = m[1], m4 = m[4], m5 = m[5], m8 = m[8], m9 = m[9], m12 = m[12], m13 = m[13];

        m[0] = m0 * c + m1 * -s;
        m[1] = m0 * s + m1 * c;
        m[4] = m4 * c + m5 * -s;
        m[5] = m4 * s + m5 * c;
        m[8] = m8 * c + m9 * -s;
        m[9] = m8 * s + m9 * c;
        m[12] = m12 * c + m13 * -s;
        m[13] = m12 * s + m13 * c;

        return *this;
    }

    Matrix4& scale(float s) { return scale(s, s, s); }

    Matrix4& scale(float sx, float sy, float sz) {
        float* m = &static_cast<glm::mat4&>(*this)[0][0];

        m[0] *= sx;
        m[4] *= sx;
        m[8] *= sx;
        m[12] *= sx;
        m[1] *= sy;
        m[5] *= sy;
        m[9] *= sy;
        m[13] *= sy;
        m[2] *= sz;
        m[6] *= sz;
        m[10] *= sz;
        m[14] *= sz;

        return *this;
    }

    // operators
    Matrix4 operator+(const Matrix4& rhs) const {
        return {static_cast<const glm::mat4&>(*this) + static_cast<const glm::mat4&>(rhs)};
    }

    Matrix4 operator-(const Matrix4& rhs) const {
        return {static_cast<const glm::mat4&>(*this) - static_cast<const glm::mat4&>(rhs)};
    }

    Matrix4& operator+=(const Matrix4& rhs) {
        static_cast<glm::mat4&>(*this) += static_cast<const glm::mat4&>(rhs);
        return *this;
    }

    Matrix4& operator-=(const Matrix4& rhs) {
        static_cast<glm::mat4&>(*this) -= static_cast<const glm::mat4&>(rhs);
        return *this;
    }

    vec4 operator*(const vec4& rhs) const {
        glm::vec4 result = static_cast<const glm::mat4&>(*this) * static_cast<const glm::vec4&>(rhs);
        return {result};
    }

    vec3 operator*(const vec3& rhs) const {
        // treat vec3 as homogeneous coordinate with w=1, but only return x,y,z
        glm::vec4 v(rhs.x, rhs.y, rhs.z, 1.0f);
        glm::vec4 result = static_cast<const glm::mat4&>(*this) * v;
        return {result.x, result.y, result.z};
    }

    Matrix4 operator*(const Matrix4& rhs) const {
        return {static_cast<const glm::mat4&>(*this) * static_cast<const glm::mat4&>(rhs)};
    }

    Matrix4& operator*=(const Matrix4& rhs) {
        static_cast<glm::mat4&>(*this) *= static_cast<const glm::mat4&>(rhs);
        return *this;
    }

    float operator[](int index) const { return (&static_cast<const glm::mat4&>(*this)[0][0])[index]; }
    float& operator[](int index) { return (&static_cast<glm::mat4&>(*this)[0][0])[index]; }

    friend Matrix4 operator-(const Matrix4& m);
    friend Matrix4 operator*(float scalar, const Matrix4& m);
    friend vec3 operator*(const vec3& vec, const Matrix4& m);
    friend vec4 operator*(const vec4& vec, const Matrix4& m);
    friend std::ostream& operator<<(std::ostream& os, const Matrix4& m);
};

inline Matrix4 operator-(const Matrix4& m) { return {-static_cast<const glm::mat4&>(m)}; }

inline Matrix4 operator*(float s, const Matrix4& m) { return {s * static_cast<const glm::mat4&>(m)}; }

inline vec4 operator*(const vec4& v, const Matrix4& m) {
    glm::vec4 result = static_cast<const glm::vec4&>(v) * static_cast<const glm::mat4&>(m);  // row-major mult
    return {result};
}

inline vec3 operator*(const vec3& v, const Matrix4& m) {
    glm::vec4 vec(v.x, v.y, v.z, 1.0f);
    glm::vec4 result = vec * static_cast<const glm::mat4&>(m);  // row-major mult
    return {result.x, result.y, result.z};
}

inline std::ostream& operator<<(std::ostream& os, const Matrix4& m) {
    os << std::fixed << std::setprecision(5);
    os << "[" << std::setw(10) << m[0] << " " << std::setw(10) << m[4] << " " << std::setw(10) << m[8] << " "
       << std::setw(10) << m[12] << "]\n"
       << "[" << std::setw(10) << m[1] << " " << std::setw(10) << m[5] << " " << std::setw(10) << m[9] << " "
       << std::setw(10) << m[13] << "]\n"
       << "[" << std::setw(10) << m[2] << " " << std::setw(10) << m[6] << " " << std::setw(10) << m[10] << " "
       << std::setw(10) << m[14] << "]\n"
       << "[" << std::setw(10) << m[3] << " " << std::setw(10) << m[7] << " " << std::setw(10) << m[11] << " "
       << std::setw(10) << m[15] << "]\n";
    os << std::resetiosflags(std::ios_base::fixed | std::ios_base::floatfield);
    return os;
}

namespace fmt {
template <>
struct formatter<Matrix2> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Matrix2& r, FormatContext& ctx) const {
        return format_to(ctx.out(), "{}"_cf, fmt::streamed(r));
    }
};

template <>
struct formatter<Matrix3> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Matrix3& r, FormatContext& ctx) const {
        return format_to(ctx.out(), "{}"_cf, fmt::streamed(r));
    }
};

template <>
struct formatter<Matrix4> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const Matrix4& r, FormatContext& ctx) const {
        return format_to(ctx.out(), "{}"_cf, fmt::streamed(r));
    }
};
}  // namespace fmt


#endif
