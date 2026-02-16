//================ Copyright (c) 2026, WH, All rights reserved. =================//
//
// Purpose:		SDL_gpu shader pack implementation of Shader
//
// $NoKeywords: $sdlgpushader
//===============================================================================//

#include "config.h"

#ifdef MCENGINE_FEATURE_SDLGPU

#include <SDL3/SDL_gpu.h>

#include "SDLGPUShader.h"

#include "SDLGPUInterface.h"

#include "ConVar.h"
#include "Engine.h"
#include "Graphics.h"
#include "Logging.h"
#include "ContainerRanges.h"

#include "ctre.hpp"

#include <charconv>
#include <cstring>

SDLGPUShader::SDLGPUShader(std::string vertexShaderPack, std::string fragmentShaderPack, [[maybe_unused]] bool source)
    : Shader(), m_sVsh(std::move(vertexShaderPack)), m_sFsh(std::move(fragmentShaderPack)) {
    auto *gpu = static_cast<SDLGPUInterface *>(g.get());
    if(!gpu || !(m_device = gpu->getDevice())) {
        debugLog("SDLGPUShader: no GPU device");
        return;
    }
}

void SDLGPUShader::init() {
    if(!m_device) {  // should not happen
        auto *gpu = static_cast<SDLGPUInterface *>(g.get());
        if(!gpu || !(m_device = gpu->getDevice())) {
            debugLog("SDLGPUShader: no GPU device");
            this->setReady(false);
            return;
        }
    }

    // parse vertex shader pack
    std::string vshGlsl;
    std::vector<u8> vshBinary;
    SDL_GPUShaderFormat vshFormat;
    if(!parseShaderPack(m_device, reinterpret_cast<const u8 *>(m_sVsh.data()), m_sVsh.size(), &vshGlsl, vshBinary,
                        vshFormat)) {
        debugLog("SDLGPUShader: failed to parse vertex shader pack");
        this->setReady(false);
        return;
    }

    // parse fragment shader pack
    std::string fshGlsl;
    std::vector<u8> fshBinary;
    SDL_GPUShaderFormat fshFormat;
    if(!parseShaderPack(m_device, reinterpret_cast<const u8 *>(m_sFsh.data()), m_sFsh.size(), &fshGlsl, fshBinary,
                        fshFormat)) {
        debugLog("SDLGPUShader: failed to parse fragment shader pack");
        this->setReady(false);
        return;
    }

    {
        // parse uniform blocks from both GLSL sources
        Mc::append_range(m_uniformBlocks, parseUniformBlocks(vshGlsl));
        Mc::append_range(m_uniformBlocks, parseUniformBlocks(fshGlsl));
    }

    if(m_uniformBlocks.empty()) {
        debugLog("SDLGPUShader WARNING: parsed no uniform blocks from shaders!");
    }

    // count samplers and uniform buffers from GLSL decorations
    m_vertexNumSamplers = 0;
    m_vertexNumUniformBuffers = 0;
    m_fragmentNumSamplers = 0;
    m_fragmentNumUniformBuffers = 0;

    // count samplers from GLSL source
    {
        static constexpr ctll::fixed_string samplerPat{R"(uniform\s+sampler\w+\s+)"};
        for([[maybe_unused]] auto _ : ctre::search_all<samplerPat>(vshGlsl)) m_vertexNumSamplers++;
        for([[maybe_unused]] auto _ : ctre::search_all<samplerPat>(fshGlsl)) m_fragmentNumSamplers++;
    }

    // count uniform buffers from blocks
    for(auto &block : m_uniformBlocks) {
        if(block.set == 1) {
            m_vertexNumUniformBuffers++;
        } else {
            m_fragmentNumUniformBuffers++;
        }
    }

    // create GPU shader objects
    SDL_GPUShaderCreateInfo vertInfo{
        .code_size = vshBinary.size(),
        .code = vshBinary.data(),
        .entrypoint = "main",
        .format = vshFormat,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = m_vertexNumSamplers,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = m_vertexNumUniformBuffers,
        .props = 0,
    };

    SDL_GPUShaderCreateInfo fragInfo{
        .code_size = fshBinary.size(),
        .code = fshBinary.data(),
        .entrypoint = "main",
        .format = fshFormat,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = m_fragmentNumSamplers,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = m_fragmentNumUniformBuffers,
        .props = 0,
    };

    m_gpuVertexShader = SDL_CreateGPUShader(m_device, &vertInfo);
    m_gpuFragmentShader = SDL_CreateGPUShader(m_device, &fragInfo);

    if(!m_gpuVertexShader || !m_gpuFragmentShader) {
        debugLog("SDLGPUShader: failed to create GPU shaders: {}", SDL_GetError());
        this->setReady(false);
        return;
    }

    // free source data now that shaders are compiled
    m_sVsh.clear();
    m_sFsh.clear();

    this->setReady(true);
}

void SDLGPUShader::initAsync() { this->setAsyncReady(true); }

void SDLGPUShader::destroy() {
    if(!m_gpuVertexShader && !m_gpuFragmentShader) return;

    if(m_device) {
        if(m_gpuVertexShader) SDL_ReleaseGPUShader(m_device, m_gpuVertexShader);
        if(m_gpuFragmentShader) SDL_ReleaseGPUShader(m_device, m_gpuFragmentShader);
    }

    m_gpuVertexShader = nullptr;
    m_gpuFragmentShader = nullptr;
    m_uniformBlocks.clear();
    m_uniformCache.clear();
}

void SDLGPUShader::enable() {
    if(!this->isReady()) return;

    auto *gpu = static_cast<SDLGPUInterface *>(g.get());
    if(!gpu) return;

    // backup
    SDLGPUShader *currentShader = gpu->getActiveShader();

    if(currentShader == this) {
        engine->showMessageErrorFatal(US_("Programmer Error"), US_("Tried to enable() the same shader twice!"));
        engine->shutdown();
    }

    m_lastActiveShader = currentShader;

    gpu->setActiveShader(this);
}

void SDLGPUShader::disable() {
    if(!this->isReady()) return;

    auto *gpu = static_cast<SDLGPUInterface *>(g.get());
    if(!gpu) return;

    // restore backup
    assert(m_lastActiveShader);
    gpu->setActiveShader(m_lastActiveShader);
}

// uniform setters

void SDLGPUShader::writeUniform(std::string_view name, const void *data, u32 dataSize) {
    if(!this->isReady()) return;

    auto it = m_uniformCache.find(name);
    size_t bi, vi;

    if(it != m_uniformCache.end()) {
        bi = it->second.first;
        vi = it->second.second;
    } else {
        bool found = false;
        for(bi = 0; bi < m_uniformBlocks.size(); bi++) {
            auto &block = m_uniformBlocks[bi];
            for(vi = 0; vi < block.vars.size(); vi++) {
                if(block.vars[vi].name == name) {
                    m_uniformCache.emplace(std::string(name), std::pair{bi, vi});
                    found = true;
                    break;
                }
            }
            if(found) break;
        }
        if(!found) {
            logIfCV(debug_shaders, "SDLGPUShader: can't find uniform {:s}", name);
            return;
        }
    }

    auto &var = m_uniformBlocks[bi].vars[vi];
    std::memcpy(m_uniformBlocks[bi].buffer.data() + var.offset, data, std::min(dataSize, var.size));
}

void SDLGPUShader::setUniform1f(std::string_view name, float value) { writeUniform(name, &value, sizeof(float)); }

void SDLGPUShader::setUniform1fv(std::string_view name, int count, const float *const values) {
    writeUniform(name, values, (u32)(sizeof(float) * count));
}

void SDLGPUShader::setUniform1i(std::string_view name, int value) { writeUniform(name, &value, sizeof(int)); }

void SDLGPUShader::setUniform2f(std::string_view name, float x, float y) {
    float v[2] = {x, y};
    writeUniform(name, v, sizeof(v));
}

void SDLGPUShader::setUniform2fv(std::string_view name, int count, const float *const vectors) {
    writeUniform(name, vectors, (u32)(sizeof(float) * 2 * count));
}

void SDLGPUShader::setUniform3f(std::string_view name, float x, float y, float z) {
    float v[3] = {x, y, z};
    writeUniform(name, v, sizeof(v));
}

void SDLGPUShader::setUniform3fv(std::string_view name, int count, const float *const vectors) {
    writeUniform(name, vectors, (u32)(sizeof(float) * 3 * count));
}

void SDLGPUShader::setUniform4f(std::string_view name, float x, float y, float z, float w) {
    float v[4] = {x, y, z, w};
    writeUniform(name, v, sizeof(v));
}

void SDLGPUShader::setUniformMatrix4fv(std::string_view name, const Matrix4 &matrix) {
    setUniformMatrix4fv(name, matrix.get());
}

void SDLGPUShader::setUniformMatrix4fv(std::string_view name, const float *const v) {
    writeUniform(name, v, sizeof(float) * 16);
}

// shader pack parsing

bool SDLGPUShader::parseShaderPack(SDL_GPUDevice *device, const u8 *data, size_t dataSize, std::string *glslOut,
                                   std::vector<u8> &binaryOut, SDL_GPUShaderFormat &formatOut) {
    if(dataSize < 12 || std::memcmp(data, "SGSH", 4) != 0) return false;

    u32 version;
    std::memcpy(&version, data + 4, 4);
    if(version != 1) return false;

    u32 numSections;
    std::memcpy(&numSections, data + 8, 4);
    if(dataSize < 12 + numSections * 12) return false;

    std::vector<ShaderPackSection> sections(numSections);
    for(u32 i = 0; i < numSections; i++) {
        const u8 *entry = data + 12 + i * 12;
        std::memcpy(&sections[i].format, entry, 4);
        std::memcpy(&sections[i].offset, entry + 4, 4);
        std::memcpy(&sections[i].size, entry + 8, 4);
    }

    // extract GLSL source (optional, caller may not need it)
    if(glslOut) {
        bool foundGlsl = false;
        for(auto &sec : sections) {
            if(sec.format == FMT_GLSL) {
                if(sec.offset + sec.size > dataSize) return false;
                glslOut->assign(reinterpret_cast<const char *>(data) + sec.offset, sec.size);
                foundGlsl = true;
                break;
            }
        }
        if(!foundGlsl) return false;
    }

    // query supported formats and pick best match
    SDL_GPUShaderFormat supportedFormats = SDL_GetGPUShaderFormats(device);

    static constexpr struct {
        u32 packFmt;
        SDL_GPUShaderFormat gpuFmt;
    } formatPriority[] = {
        {FMT_SPIRV, SDL_GPU_SHADERFORMAT_SPIRV},
        {FMT_DXIL, SDL_GPU_SHADERFORMAT_DXIL},
        {FMT_MSL, SDL_GPU_SHADERFORMAT_MSL},
    };

    bool foundBinary = false;
    for(auto &prio : formatPriority) {
        if(!(supportedFormats & prio.gpuFmt)) continue;
        for(auto &sec : sections) {
            if(sec.format == prio.packFmt) {
                if(sec.offset + sec.size > dataSize) return false;
                binaryOut.assign(data + sec.offset, data + sec.offset + sec.size);
                formatOut = prio.gpuFmt;
                foundBinary = true;
                break;
            }
        }
        if(foundBinary) break;
    }

    if(!foundBinary) {
        debugLog("SDLGPUShader: no compatible binary format found in shader pack");
        return false;
    }

    return true;
}

// GLSL uniform block parsing

u32 SDLGPUShader::typeSize(std::string_view typeName) {
    if(typeName == "float" || typeName == "int" || typeName == "uint" || typeName == "bool") return 4;
    if(typeName == "vec2" || typeName == "ivec2" || typeName == "uvec2") return 8;
    if(typeName == "vec3" || typeName == "ivec3" || typeName == "uvec3") return 12;
    if(typeName == "vec4" || typeName == "ivec4" || typeName == "uvec4") return 16;
    if(typeName == "mat4") return 64;
    if(typeName == "mat3") return 48;  // 3 * vec4 in std140
    if(typeName == "mat2") return 32;  // 2 * vec4 in std140
    return 4;
}

u32 SDLGPUShader::typeAlignment(std::string_view typeName) {
    if(typeName == "float" || typeName == "int" || typeName == "uint" || typeName == "bool") return 4;
    if(typeName == "vec2" || typeName == "ivec2" || typeName == "uvec2") return 8;
    if(typeName == "vec3" || typeName == "ivec3" || typeName == "uvec3") return 16;
    if(typeName == "vec4" || typeName == "ivec4" || typeName == "uvec4") return 16;
    if(typeName == "mat4" || typeName == "mat3" || typeName == "mat2") return 16;
    return 4;
}

u32 SDLGPUShader::computeStd140Offset(u32 currentOffset, std::string_view typeName) {
    u32 align = typeAlignment(typeName);
    return (currentOffset + align - 1) & ~(align - 1);
}

std::vector<SDLGPUShader::UniformBlock> SDLGPUShader::parseUniformBlocks(const std::string &glsl) {
    std::vector<UniformBlock> ret;

    // find: layout(...set=N, binding=M...) uniform BlockName { ... }
    static constexpr ctll::fixed_string blockPat{
        R"(layout\s*\([^)]*set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)[^)]*\)\s*uniform\s+(\w+)\s*\{([^}]*)\})"};
    static constexpr ctll::fixed_string varPat{R"((\w+)\s+(\w+)\s*;)"};

    for(auto blockMatch : ctre::search_all<blockPat>(glsl)) {
        auto setStr = blockMatch.get<1>().to_view();
        auto bindStr = blockMatch.get<2>().to_view();
        auto blockBody = blockMatch.get<4>().to_view();

        u32 set = 0, binding = 0;
        std::from_chars(setStr.data(), setStr.data() + setStr.size(), set);
        std::from_chars(bindStr.data(), bindStr.data() + bindStr.size(), binding);

        UniformBlock block;
        block.set = set;
        block.binding = binding;

        // first pass: compute total buffer size (including _pad fields)
        u32 totalOffset = 0;
        for(auto varMatch : ctre::search_all<varPat>(blockBody)) {
            auto typeName = varMatch.get<1>().to_view();
            u32 alignedOffset = computeStd140Offset(totalOffset, typeName);
            totalOffset = alignedOffset + typeSize(typeName);
        }
        totalOffset = (totalOffset + 15) & ~15u;  // std140 struct alignment
        block.buffer.resize(totalOffset, 0);

        // second pass: record non-padding variables with their offsets
        u32 currentOffset = 0;
        for(auto varMatch : ctre::search_all<varPat>(blockBody)) {
            auto typeName = varMatch.get<1>().to_view();
            auto varName = varMatch.get<2>().to_view();

            u32 alignedOffset = computeStd140Offset(currentOffset, typeName);
            u32 size = typeSize(typeName);
            currentOffset = alignedOffset + size;

            if(varName.starts_with("_pad")) continue;
            block.vars.push_back({std::string(varName), alignedOffset, size});
        }

        ret.push_back(std::move(block));
    }

    return ret;
}

#endif
