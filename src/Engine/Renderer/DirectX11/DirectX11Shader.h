//================ Copyright (c) 2017, PG, All rights reserved. =================//
//
// Purpose:		DirectX HLSL implementation of Shader
//
// $NoKeywords: $dxshader
//===============================================================================//

#pragma once

#ifndef DIRECTX11SHADER_H
#define DIRECTX11SHADER_H
#include "config.h"

#ifdef MCENGINE_FEATURE_DIRECTX11

#include "Shader.h"

#include "Hashing.h"

#include <cstdint>
#include <string_view>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
MC_DO_PRAGMA(GCC diagnostic ignored "-Wpragmas")
MC_DO_PRAGMA(GCC diagnostic ignored "-Wextern-c-compat")
MC_DO_PRAGMA(GCC diagnostic push)
#endif

#include "d3d11.h"

#if defined(__GNUC__) || defined(__clang__)
MC_DO_PRAGMA(GCC diagnostic pop)
#endif

namespace dynutils {
using lib_obj = struct lib_obj;
}

// calling convention handling
#ifdef _MSC_VER
#define D3D_CALL WINAPI
#else
#if defined(__x86_64__) || defined(__arm64__) || defined(__aarch64__)
#define D3D_CALL __attribute__((ms_abi))
#else
#define D3D_CALL __attribute__((__stdcall__)) __attribute__((__force_align_arg_pointer__))
#endif
#endif

// d3d blob interface

// function pointer type for D3DCompile with proper calling convention
typedef HRESULT(D3D_CALL D3DCompile_t)(LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName,
                                       const D3D_SHADER_MACRO *pDefines, ID3DInclude *pInclude, LPCSTR pEntrypoint,
                                       LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob **ppCode,
                                       ID3DBlob **ppErrorMsgs);

// function pointer type for D3DReflect
typedef HRESULT(D3D_CALL D3DReflect_t)(LPCVOID pSrcData, SIZE_T SrcDataSize, REFIID pInterface, void **ppReflector);

class DirectX11Shader final : public Shader {
    NOCOPY_NOMOVE(DirectX11Shader);

   public:
    DirectX11Shader(std::string_view vertexShader, std::string_view fragmentShader);
    ~DirectX11Shader() override { destroy(); }

    void enable() override;
    void disable() override;

    // ILLEGAL:
    void onJustBeforeDraw();
    inline unsigned long getStatsNumConstantBufferUploadsPerFrame() const {
        return this->iStatsNumConstantBufferUploadsPerFrameCounter;
    }
    inline unsigned long getStatsNumConstantBufferUploadsPerFrameEngineFrameCount() const {
        return this->iStatsNumConstantBufferUploadsPerFrameCounterEngineFrameCount;
    }

    static bool loadLibs();

   private:
    enum class Stage : uint8_t { VERTEX, PIXEL };

    // one D3D constant buffer, reflected from the compiled shader. The canonical GLSL puts the vertex
    // uniform block at set=1 and the fragment block at set=3, both binding=0; spirv-cross maps these to a
    // register(b0) cbuffer per stage, so vertex and pixel cbuffers are bound separately (VS/PS slots).
    struct CONSTANT_BUFFER {
        Stage stage{Stage::VERTEX};
        UINT registerSlot{0};
        std::vector<float> cpuData;  // CPU-side mirror; byteWidth/sizeof(float) floats
        ID3D11Buffer *buffer{nullptr};
    };

    struct CACHE_ENTRY {
        int bufferIndex{-1};          // into this->constantBuffers
        unsigned int offsetBytes{0};  // byte offset of the uniform within that buffer
        unsigned int sizeBytes{0};    // byte size of the uniform
    };

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

    bool compile(std::string_view vertexShader, std::string_view fragmentShader);
    // reflect a compiled blob's constant buffers into this->constantBuffers + uniformLocationCache (+ create the
    // GPU buffers and record them in vs/psBuffers by register slot). returns false on a hard error.
    bool reflectConstantBuffers(ID3DBlob *blob, Stage stage);

    void writeUniform(std::string_view name, UniformType type, const void *const data, unsigned int dataSize) override;

   private:
    std::string_view sVsh, sFsh;

    ID3D11VertexShader *vs{nullptr};
    ID3D11PixelShader *ps{nullptr};
    ID3D11InputLayout *inputLayout{nullptr};

    // every constant buffer across both stages; vs/psBuffers index those buffers by register slot for binding
    std::vector<CONSTANT_BUFFER> constantBuffers;
    std::vector<ID3D11Buffer *> vsBuffers;
    std::vector<ID3D11Buffer *> psBuffers;
    bool bConstantBuffersUpToDate{false};
    bool bStateBackedUp{false};

    DirectX11Shader *prevShader{nullptr};
    ID3D11VertexShader *prevVS{nullptr};
    ID3D11PixelShader *prevPS{nullptr};
    ID3D11InputLayout *prevInputLayout{nullptr};
    std::vector<ID3D11Buffer *> prevVSConstantBuffers;
    std::vector<ID3D11Buffer *> prevPSConstantBuffers;

    Hash::unstable_stringmap<CACHE_ENTRY> uniformLocationCache;

    // stats
    unsigned long iStatsNumConstantBufferUploadsPerFrameCounter{0};
    unsigned long iStatsNumConstantBufferUploadsPerFrameCounterEngineFrameCount{0};

    // dynloading
    static dynutils::lib_obj *s_d3dCompilerHandle;
    static D3DCompile_t *s_d3dCompileFunc;
    static D3DReflect_t *s_d3dReflectFunc;
};

#endif

#endif
