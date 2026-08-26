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
#include "Matrices.h"

#include "ctre.hpp"

#include <charconv>
#include <cstring>

SDLGPUShader::SDLGPUShader(SDLGPUInterface *gpu, SDL_GPUDevice *device, std::string vertexShaderPack,
                           std::string fragmentShaderPack, [[maybe_unused]] bool source)
    : Shader(),
      m_gpu(gpu),
      m_device(device),
      m_sVsh(std::move(vertexShaderPack)),
      m_sFsh(std::move(fragmentShaderPack)) {}

void SDLGPUShader::init() {
    if(!m_device || !m_gpu) {  // should not happen
        debugLog("SDLGPUShader: no GPU/device context");
        this->setReady(false);
        return;
    }

    // parse vertex shader pack
    std::string vshGlsl;
    std::vector<u8> vshBinary;
    SDL_GPUShaderFormat vshFormat;  // NOLINT(cppcoreguidelines-init-variables)
    if(!parseShaderPack(m_device, reinterpret_cast<const u8 *>(m_sVsh.data()), m_sVsh.size(), &vshGlsl, vshBinary,
                        vshFormat)) {
        debugLog("SDLGPUShader: failed to parse vertex shader pack");
        this->setReady(false);
        return;
    }

    // parse fragment shader pack
    std::string fshGlsl;
    std::vector<u8> fshBinary;
    SDL_GPUShaderFormat fshFormat;  // NOLINT(cppcoreguidelines-init-variables)
    if(!parseShaderPack(m_device, reinterpret_cast<const u8 *>(m_sFsh.data()), m_sFsh.size(), &fshGlsl, fshBinary,
                        fshFormat)) {
        debugLog("SDLGPUShader: failed to parse fragment shader pack");
        this->setReady(false);
        return;
    }

    {
        // parse uniform blocks from both GLSL sources
        std::vector<UniformBlock> tmpUniformBlocks = parseUniformBlocks(vshGlsl);
        Mc::ranges::append(tmpUniformBlocks, parseUniformBlocks(fshGlsl));
        m_uniformBlocks = std::move(tmpUniformBlocks);
    }

    if(m_uniformBlocks.empty()) {
        debugLog("SDLGPUShader WARNING: parsed no uniform blocks from shaders!");
    }

    // count samplers and uniform buffers from GLSL decorations
    u32 vertexNumSamplers = 0;
    u32 vertexNumUniformBuffers = 0;
    u32 fragmentNumSamplers = 0;
    u32 fragmentNumUniformBuffers = 0;

    // count samplers from GLSL source
    {
        static constexpr ctll::fixed_string samplerPat{R"(uniform\s+sampler\w+\s+)"};
        for([[maybe_unused]] auto _ : ctre::search_all<samplerPat>(vshGlsl)) vertexNumSamplers++;
        for([[maybe_unused]] auto _ : ctre::search_all<samplerPat>(fshGlsl)) fragmentNumSamplers++;
    }

    // count uniform buffers from blocks
    for(auto &block : m_uniformBlocks) {
        if(block.set == 1) {
            vertexNumUniformBuffers++;
        } else {
            fragmentNumUniformBuffers++;
        }
    }

    // create GPU shader objects
    // MSL reserves "main" for shader entry points; our MSL sources use "main0"
    const char *vshEntry = (vshFormat == SDL_GPU_SHADERFORMAT_MSL) ? "main0" : "main";
    const char *fshEntry = (fshFormat == SDL_GPU_SHADERFORMAT_MSL) ? "main0" : "main";

    SDL_GPUShaderCreateInfo vertInfo{
        .code_size = vshBinary.size(),
        .code = vshBinary.data(),
        .entrypoint = vshEntry,
        .format = vshFormat,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = vertexNumSamplers,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = vertexNumUniformBuffers,
        .props = 0,
    };

    SDL_GPUShaderCreateInfo fragInfo{
        .code_size = fshBinary.size(),
        .code = fshBinary.data(),
        .entrypoint = fshEntry,
        .format = fshFormat,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = fragmentNumSamplers,
        .num_storage_textures = 0,
        .num_storage_buffers = 0,
        .num_uniform_buffers = fragmentNumUniformBuffers,
        .props = 0,
    };

    m_gpuVertexShader = SDL_CreateGPUShader(m_device, &vertInfo);
    m_gpuFragmentShader = SDL_CreateGPUShader(m_device, &fragInfo);

    if(!m_gpuVertexShader || !m_gpuFragmentShader) {
        debugLog("SDLGPUShader: failed to create GPU shaders: {}", SDL_GetError());
        this->setReady(false);
        return;
    }

    this->setReady(true);
}

void SDLGPUShader::initAsync() { this->setAsyncReady(true); }

void SDLGPUShader::destroy() {
    if(!m_gpuVertexShader && !m_gpuFragmentShader) return;

    if(m_gpu) m_gpu->clearActiveShader(this);

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
    if(unlikely(!m_gpu || !m_device || !this->isReady())) return;

    // backup
    SDLGPUShader *currentShader = m_gpu->getActiveShader();

    if(currentShader == this) {
        engine->showMessageErrorFatal("Programmer Error", "Tried to enable() the same shader twice!");
        engine->shutdown();
    }

    m_lastActiveShader = currentShader;

    m_gpu->setActiveShader(this);
}

void SDLGPUShader::disable() {
    if(unlikely(!m_gpu || !m_device || !this->isReady())) return;

    // restore backup
    assert(m_lastActiveShader);
    m_gpu->setActiveShader(m_lastActiveShader);
}

// uniform setters

void SDLGPUShader::writeUniform(std::string_view name, [[maybe_unused]] UniformType type, const void *const data,
                                u32 dataSize) {
    if(unlikely(!m_gpu || !m_device || !this->isReady())) return;

    u8 *uniformVarDataPtr;  // NOLINT(cppcoreguidelines-init-variables)
    u32 varSize;            // NOLINT(cppcoreguidelines-init-variables)
    if(auto it = m_uniformCache.find(name); it != m_uniformCache.end()) {
        uniformVarDataPtr = it->second.dataPtr;
        varSize = it->second.varSize;
    } else {
        bool found = false;
        for(auto &block : m_uniformBlocks) {
            for(auto &var : block.vars) {
                if(var.name == name) {
                    uniformVarDataPtr = block.buffer.data() + var.offset;
                    varSize = var.size;
                    m_uniformCache.emplace(std::string{name}, UniformCacheEntry{uniformVarDataPtr, varSize});
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

    std::memcpy(uniformVarDataPtr, data, std::min(dataSize, varSize));
}

// shader pack parsing

bool SDLGPUShader::parseShaderPack(SDL_GPUDevice *device, const u8 *data, size_t dataSize, std::string *glslOut,
                                   std::vector<u8> &binaryOut, SDL_GPUShaderFormat &formatOut) {
    if(dataSize < 12 || std::memcmp(data, "SGSH", 4) != 0) return false;

    u32 version;  // NOLINT(cppcoreguidelines-init-variables)
    std::memcpy(&version, data + 4, 4);
    if(version != 1) return false;

    u32 numSections;  // NOLINT(cppcoreguidelines-init-variables)
    std::memcpy(&numSections, data + 8, 4);
    if(dataSize < 12 + numSections * 12) return false;

    std::vector<ShaderPackSection> sections(numSections);
    for(uSz i = 0; i < numSections; i++) {
        const u8 *entry = data + 12 + i * 12;
        std::memcpy(&sections[i].format, entry, 4);
        std::memcpy(&sections[i].offset, entry + 4, 4);
        std::memcpy(&sections[i].size, entry + 8, 4);
    }

    // extract GLSL source (optional, caller may not need it)
    if(glslOut) {
        bool foundGlsl = false;
        for(const auto &sec : sections) {
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
    for(const auto &prio : formatPriority) {
        if(!(supportedFormats & prio.gpuFmt)) continue;
        for(const auto &sec : sections) {
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
namespace {
u32 type_size(std::string_view typeName) {
    if(typeName == "float" || typeName == "int" || typeName == "uint" || typeName == "bool") return 4;
    if(typeName == "vec2" || typeName == "ivec2" || typeName == "uvec2") return 8;
    if(typeName == "vec3" || typeName == "ivec3" || typeName == "uvec3") return 12;
    if(typeName == "vec4" || typeName == "ivec4" || typeName == "uvec4") return 16;
    if(typeName == "mat4") return 64;
    if(typeName == "mat3") return 48;  // 3 * vec4 in std140
    if(typeName == "mat2") return 32;  // 2 * vec4 in std140
    return 4;
}

u32 type_alignment(std::string_view typeName) {
    if(typeName == "float" || typeName == "int" || typeName == "uint" || typeName == "bool") return 4;
    if(typeName == "vec2" || typeName == "ivec2" || typeName == "uvec2") return 8;
    if(typeName == "vec3" || typeName == "ivec3" || typeName == "uvec3") return 16;
    if(typeName == "vec4" || typeName == "ivec4" || typeName == "uvec4") return 16;
    if(typeName == "mat4" || typeName == "mat3" || typeName == "mat2") return 16;
    return 4;
}

u32 compute_std140_offset(u32 currentOffset, std::string_view typeName) {
    u32 align = type_alignment(typeName);
    return (currentOffset + align - 1) & ~(align - 1);
}
}  // namespace

std::vector<SDLGPUShader::UniformBlock> SDLGPUShader::parseUniformBlocks(const std::string &glsl) {
    std::vector<UniformBlock> ret;

    // find: layout(...set=N, binding=M...) uniform BlockName { ... }
    static constexpr ctll::fixed_string blockPat{
        R"(layout\s*\([^)]*set\s*=\s*(\d+)\s*,\s*binding\s*=\s*(\d+)[^)]*\)\s*uniform\s+(\w+)\s*\{([^}]*)\})"};
    static constexpr ctll::fixed_string varPat{R"((\w+)\s+(\w+)\s*;)"};

    for(auto blockMatch : ctre::search_all<blockPat>(glsl)) {
        auto setStr = blockMatch.get<1>().to_view();
        auto bindStr = blockMatch.get<2>().to_view();

        // strip // comments from block body before parsing variables
        std::string blockBody{blockMatch.get<4>().to_view()};
        for(auto pos = blockBody.find("//"); pos != std::string::npos; pos = blockBody.find("//", pos)) {
            auto eol = blockBody.find('\n', pos);
            blockBody.erase(pos, (eol != std::string::npos ? eol : blockBody.size()) - pos);
        }

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
            u32 alignedOffset = compute_std140_offset(totalOffset, typeName);
            totalOffset = alignedOffset + type_size(typeName);
        }
        totalOffset = (totalOffset + 15) & ~15u;  // std140 struct alignment
        block.buffer = FixedSizeArray<u8>{totalOffset, {/*zero*/}};

        // second pass: record non-padding variables with their offsets
        u32 currentOffset = 0;
        std::vector<UniformVar> tmpVars;
        for(auto varMatch : ctre::search_all<varPat>(blockBody)) {
            auto typeName = varMatch.get<1>().to_view();
            auto varName = varMatch.get<2>().to_view();

            u32 alignedOffset = compute_std140_offset(currentOffset, typeName);
            u32 size = type_size(typeName);
            currentOffset = alignedOffset + size;

            if(varName.starts_with("_pad"sv)) continue;
            tmpVars.emplace_back(std::string{varName}, alignedOffset, size);
        }
        block.vars = std::move(tmpVars);

        ret.push_back(std::move(block));
    }

    return ret;
}

#endif
