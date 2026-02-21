//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		SDL_gpu shader pack implementation of Shader
//
// $NoKeywords: $sdlgpushader
//===============================================================================//

#pragma once

#ifndef SDLGPUSHADER_H
#define SDLGPUSHADER_H
#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU

#include "Shader.h"

#include "Hashing.h"
#include "types.h"

#include <string>
#include <vector>

typedef struct SDL_GPUCommandBuffer SDL_GPUCommandBuffer;
typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_GPUShader SDL_GPUShader;

typedef u32 SDL_GPUShaderFormat;

class SDLGPUInterface;

class SDLGPUShader final : public Shader {
    NOCOPY_NOMOVE(SDLGPUShader);

   private:
    friend SDLGPUInterface;
    SDLGPUShader(SDLGPUInterface *gpu, SDL_GPUDevice *device, std::string vertexShaderPack,
                 std::string fragmentShaderPack, bool source = true);

   public:
    SDLGPUShader() = delete;
    ~SDLGPUShader() override { destroy(); }

    void enable() override;
    void disable() override;

    void setUniform1f(std::string_view name, float value) override;
    void setUniform1fv(std::string_view name, int count, const float *const values) override;
    void setUniform1i(std::string_view name, int value) override;
    void setUniform2f(std::string_view name, float x, float y) override;
    void setUniform2fv(std::string_view name, int count, const float *const vectors) override;
    void setUniform3f(std::string_view name, float x, float y, float z) override;
    void setUniform3fv(std::string_view name, int count, const float *const vectors) override;
    void setUniform4f(std::string_view name, float x, float y, float z, float w) override;
    void setUniformMatrix4fv(std::string_view name, const Matrix4 &matrix) override;
    void setUniformMatrix4fv(std::string_view name, const float *const v) override;

    [[nodiscard]] SDL_GPUShader *getVertexShader() const { return m_gpuVertexShader; }
    [[nodiscard]] SDL_GPUShader *getFragmentShader() const { return m_gpuFragmentShader; }

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

   private:
    // shader pack section format tags
    static constexpr u32 FMT_GLSL = 0;
    static constexpr u32 FMT_SPIRV = 1;
    static constexpr u32 FMT_DXIL = 2;
    static constexpr u32 FMT_MSL = 3;

    struct ShaderPackSection {
        u32 format;
        u32 offset;
        u32 size;
    };

   public:
    struct UniformVar {
        std::string name;
        u32 offset;  // byte offset within the uniform block buffer
        u32 size;    // size in bytes
    };

    struct UniformBlock {
        std::vector<UniformVar> vars;
        std::vector<u8> buffer;  // cpu-side data
        u32 set;
        u32 binding;
    };

    // access uniform blocks for snapshotting into deferred draw commands
    [[nodiscard]] const std::vector<UniformBlock> &getUniformBlocks() const { return m_uniformBlocks; }

   private:
    // parse a .shdpk shader pack, extracting GLSL source and the best-matching binary for the device
    static bool parseShaderPack(SDL_GPUDevice *device, const u8 *data, size_t dataSize, std::string *glslOut,
                                std::vector<u8> &binaryOut, SDL_GPUShaderFormat &formatOut);

    static std::vector<UniformBlock> parseUniformBlocks(const std::string &glsl);

    static u32 computeStd140Offset(u32 currentOffset, std::string_view typeName);
    static u32 typeSize(std::string_view typeName);
    static u32 typeAlignment(std::string_view typeName);

    void writeUniform(std::string_view name, const void *data, u32 dataSize);

    SDLGPUInterface *m_gpu;
    SDL_GPUDevice *m_device;

    std::string m_sVsh;
    std::string m_sFsh;

    SDLGPUShader *m_lastActiveShader{nullptr};  // for restore, to allow nested shaders to restore last enabled shader
    SDL_GPUShader *m_gpuVertexShader{nullptr};
    SDL_GPUShader *m_gpuFragmentShader{nullptr};

    u32 m_vertexNumSamplers{0};
    u32 m_vertexNumUniformBuffers{0};
    u32 m_fragmentNumSamplers{0};
    u32 m_fragmentNumUniformBuffers{0};

    // uniform blocks parsed from GLSL (fragment stage only for custom shaders)
    // index 0 = vertex uniforms (set=1), rest = fragment uniform blocks
    std::vector<UniformBlock> m_uniformBlocks;

    // fast name → (block index, var index) lookup
    Hash::unstable_stringmap<std::pair<size_t, size_t>> m_uniformCache;
};

#endif

#endif
