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

#include "FixedSizeArray.h"
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

    // for SDLGPUInterface
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
        FixedSizeArray<UniformVar> vars;
        FixedSizeArray<u8> buffer;  // cpu-side data
        u32 set;
        u32 binding;
    };

    // access uniform blocks for snapshotting into deferred draw commands
    [[nodiscard]] const FixedSizeArray<UniformBlock> &getUniformBlocks() const { return m_uniformBlocks; }

   protected:
    void writeUniform(std::string_view name, UniformType type, const void *const data, u32 dataSize) override;

   private:
    // parse a .shdpk shader pack, extracting GLSL source and the best-matching binary for the device
    static bool parseShaderPack(SDL_GPUDevice *device, const u8 *data, size_t dataSize, std::string *glslOut,
                                std::vector<u8> &binaryOut, SDL_GPUShaderFormat &formatOut);

    static std::vector<UniformBlock> parseUniformBlocks(const std::string &glsl);

    SDLGPUInterface *m_gpu;
    SDL_GPUDevice *m_device;

    std::string m_sVsh;
    std::string m_sFsh;

    SDLGPUShader *m_lastActiveShader{nullptr};  // for restore, to allow nested shaders to restore last enabled shader
    SDL_GPUShader *m_gpuVertexShader{nullptr};
    SDL_GPUShader *m_gpuFragmentShader{nullptr};

    // uniform blocks parsed from GLSL (fragment stage only for custom shaders)
    // index 0 = vertex uniforms (set=1), rest = fragment uniform blocks
    FixedSizeArray<UniformBlock> m_uniformBlocks;

    // fast name -> uniform {var data pointer, var size} lookup
    struct UniformCacheEntry {
        u8 *dataPtr;
        u32 varSize;
    };
    Hash::unstable_stringmap<UniformCacheEntry> m_uniformCache;
};

#endif

#endif
