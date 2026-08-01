//================ Copyright (c) 2017, PG, All rights reserved. =================//
//
// Purpose:		DirectX HLSL implementation of Shader
//
// $NoKeywords: $dxshader
//===============================================================================//

// TODO: prime full cache on load anyway
// TODO: individually remember m_bConstantBuffersUpToDate per constant buffer
#include "config.h"

#ifdef MCENGINE_FEATURE_DIRECTX11

#include "DirectX11Shader.h"

#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"
#include "Matrices.h"

#include "DirectX11Interface.h"

#include <algorithm>
#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
MC_DO_PRAGMA(GCC diagnostic ignored "-Wpragmas")
MC_DO_PRAGMA(GCC diagnostic ignored "-Wextern-c-compat")
MC_DO_PRAGMA(GCC diagnostic push)
#endif

#include "d3dcompiler.h"
#include "d3d11shader.h"  // POD reflection desc structs/enums (D3D11_SHADER_DESC, D3D11_SIGNATURE_PARAMETER_DESC, ...)

#if defined(__GNUC__) || defined(__clang__)
MC_DO_PRAGMA(GCC diagnostic pop)
#endif

namespace {
// d3d blob interface
typedef struct D3D_Blob D3D_Blob;
typedef struct D3D_BlobVtbl {
    HRESULT(D3D_CALL *QueryInterface)(D3D_Blob *This, REFIID riid, void **ppvObject);
    ULONG(D3D_CALL *AddRef)(D3D_Blob *This);
    ULONG(D3D_CALL *Release)(D3D_Blob *This);
    void *(D3D_CALL *GetBufferPointer)(D3D_Blob *This);
    SIZE_T(D3D_CALL *GetBufferSize)(D3D_Blob *This);
} D3D_BlobVtbl;

struct D3D_Blob {
    const D3D_BlobVtbl *lpVtbl;
};

// NOTE: ID3D11ShaderReflectionConstantBuffer/Variable do NOT inherit IUnknown (their vtables start at GetDesc).
typedef struct D3D_RVariable D3D_RVariable;
typedef struct D3D_RVariableVtbl {
    HRESULT(D3D_CALL *GetDesc)(D3D_RVariable *This, D3D11_SHADER_VARIABLE_DESC *desc);
} D3D_RVariableVtbl;
struct D3D_RVariable {
    const D3D_RVariableVtbl *lpVtbl;
};

typedef struct D3D_RConstantBuffer D3D_RConstantBuffer;
typedef struct D3D_RConstantBufferVtbl {
    HRESULT(D3D_CALL *GetDesc)(D3D_RConstantBuffer *This, D3D11_SHADER_BUFFER_DESC *desc);
    D3D_RVariable *(D3D_CALL *GetVariableByIndex)(D3D_RConstantBuffer *This, UINT index);
} D3D_RConstantBufferVtbl;
struct D3D_RConstantBuffer {
    const D3D_RConstantBufferVtbl *lpVtbl;
};

typedef struct D3D_Reflection D3D_Reflection;
typedef struct D3D_ReflectionVtbl {
    HRESULT(D3D_CALL *QueryInterface)(D3D_Reflection *This, REFIID riid, void **ppvObject);
    ULONG(D3D_CALL *AddRef)(D3D_Reflection *This);
    ULONG(D3D_CALL *Release)(D3D_Reflection *This);
    HRESULT(D3D_CALL *GetDesc)(D3D_Reflection *This, D3D11_SHADER_DESC *desc);
    D3D_RConstantBuffer *(D3D_CALL *GetConstantBufferByIndex)(D3D_Reflection *This, UINT index);
    D3D_RConstantBuffer *(D3D_CALL *GetConstantBufferByName)(D3D_Reflection *This, LPCSTR name);
    HRESULT(D3D_CALL *GetResourceBindingDesc)(D3D_Reflection *This, UINT index, D3D11_SHADER_INPUT_BIND_DESC *desc);
    HRESULT(D3D_CALL *GetInputParameterDesc)(D3D_Reflection *This, UINT index, D3D11_SIGNATURE_PARAMETER_DESC *desc);
} D3D_ReflectionVtbl;
struct D3D_Reflection {
    const D3D_ReflectionVtbl *lpVtbl;
};

// IID_ID3D11ShaderReflection (the D3D11 reflection interface; d3dcompiler_47 / vkd3d-utils). Hardcoded so we
// don't pull in a DEFINE_GUID/-ldxguid link dependency from d3d11shader.h.
static constexpr GUID s_IID_ID3D11ShaderReflection{
    0x8d536ca1, 0x0cca, 0x4956, {0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84}};

// wrapper functions for dx blob ops
void *getBlobBufferPointer(ID3DBlob *blob) {
    auto *d3dblob = (D3D_Blob *)blob;
    return d3dblob->lpVtbl->GetBufferPointer(d3dblob);
}

SIZE_T getBlobBufferSize(ID3DBlob *blob) {
    auto *d3dblob = (D3D_Blob *)blob;
    return d3dblob->lpVtbl->GetBufferSize(d3dblob);
}

void releaseBlob(ID3DBlob *blob) {
    if(blob != nullptr) {
        auto *d3dblob = (D3D_Blob *)blob;
        d3dblob->lpVtbl->Release(d3dblob);
    }
}

}  // namespace

#define MCENGINE_D3D11_SHADER_MAX_NUM_CONSTANT_BUFFERS 9

DirectX11Shader::DirectX11Shader(std::string vertexShader, std::string fragmentShader, [[maybe_unused]] bool source)
    : Shader(), sVsh(std::move(vertexShader)), sFsh(std::move(fragmentShader)) {
    this->prevVSConstantBuffers.resize(MCENGINE_D3D11_SHADER_MAX_NUM_CONSTANT_BUFFERS, nullptr);
    this->prevPSConstantBuffers.resize(MCENGINE_D3D11_SHADER_MAX_NUM_CONSTANT_BUFFERS, nullptr);
}

void DirectX11Shader::init() {
    // the generated HLSL carries no metadata; D3DReflect (in compile()) discovers the input layout + cbuffers
    this->setReady(compile(this->sVsh, this->sFsh));
}

void DirectX11Shader::initAsync() { this->setAsyncReady(true); }

void DirectX11Shader::destroy() {
    for(auto &cb : this->constantBuffers) {
        if(cb.buffer != nullptr) {
            cb.buffer->Release();
            cb.buffer = nullptr;
        }
    }
    this->constantBuffers.clear();
    this->vsBuffers.clear();
    this->psBuffers.clear();

    if(this->inputLayout != nullptr) {
        this->inputLayout->Release();
        this->inputLayout = nullptr;
    }

    if(this->vs != nullptr) {
        this->vs->Release();
        this->vs = nullptr;
    }

    if(this->ps != nullptr) {
        this->ps->Release();
        this->ps = nullptr;
    }

    this->uniformLocationCache.clear();
    this->bConstantBuffersUpToDate = false;
}

void DirectX11Shader::enable() {
    auto *dx11 = static_cast<DirectX11Interface *>(g);
    if(!this->isReady() || dx11->getActiveShader() == this) return;

    auto *context = dx11->getDeviceContext();

    // backup
    // HACKHACK: slow af
    {
        this->prevShader = dx11->getActiveShader();

        context->IAGetInputLayout(&this->prevInputLayout);
        context->VSGetShader(&this->prevVS, nullptr, nullptr);
        context->PSGetShader(&this->prevPS, nullptr, nullptr);
        context->VSGetConstantBuffers(0, (UINT)this->prevVSConstantBuffers.size(), &this->prevVSConstantBuffers[0]);
        context->PSGetConstantBuffers(0, (UINT)this->prevPSConstantBuffers.size(), &this->prevPSConstantBuffers[0]);

        this->bStateBackedUp = true;  // mark that we actually backed up state
    }

    context->IASetInputLayout(this->inputLayout);
    context->VSSetShader(this->vs, nullptr, 0);
    context->PSSetShader(this->ps, nullptr, 0);

    if(!this->vsBuffers.empty()) context->VSSetConstantBuffers(0, (UINT)this->vsBuffers.size(), &this->vsBuffers[0]);
    if(!this->psBuffers.empty()) context->PSSetConstantBuffers(0, (UINT)this->psBuffers.size(), &this->psBuffers[0]);

    dx11->setActiveShader(this);

    // the newly activated shader may not have the current MVP if no transform
    // change occurred since it was last active. setMVP's internal cache makes
    // this essentially free (memcmp skip) when the matrix hasn't changed.
    this->setMVP(g->getMVP());
}

void DirectX11Shader::disable() {
    auto *dx11 = static_cast<DirectX11Interface *>(g);
    if(!this->isReady() || dx11->getActiveShader() != this || !this->bStateBackedUp) return;

    auto *context = dx11->getDeviceContext();

    // restore
    // HACKHACK: slow af
    {
        UINT numPrevVS = 0;
        for(auto &prevConstantBuffer : this->prevVSConstantBuffers) {
            if(prevConstantBuffer != nullptr)
                numPrevVS++;
            else
                break;
        }
        UINT numPrevPS = 0;
        for(auto &prevConstantBuffer : this->prevPSConstantBuffers) {
            if(prevConstantBuffer != nullptr)
                numPrevPS++;
            else
                break;
        }

        context->IASetInputLayout(this->prevInputLayout);
        context->VSSetShader(this->prevVS, nullptr, 0);
        context->PSSetShader(this->prevPS, nullptr, 0);
        if(numPrevVS > 0) context->VSSetConstantBuffers(0, numPrevVS, &this->prevVSConstantBuffers[0]);
        if(numPrevPS > 0) context->PSSetConstantBuffers(0, numPrevPS, &this->prevPSConstantBuffers[0]);

        // refcount
        {
            if(this->prevInputLayout != nullptr) {
                this->prevInputLayout->Release();
                this->prevInputLayout = nullptr;
            }

            if(this->prevVS != nullptr) {
                this->prevVS->Release();
                this->prevVS = nullptr;
            }

            if(this->prevPS != nullptr) {
                this->prevPS->Release();
                this->prevPS = nullptr;
            }

            for(auto &buffer : this->prevVSConstantBuffers) {
                if(buffer != nullptr) {
                    buffer->Release();
                    buffer = nullptr;
                }
            }
            for(auto &buffer : this->prevPSConstantBuffers) {
                if(buffer != nullptr) {
                    buffer->Release();
                    buffer = nullptr;
                }
            }
        }

        dx11->setActiveShader(this->prevShader);
        this->bStateBackedUp = false;  // clear the flag after restore
    }
}

void DirectX11Shader::writeUniform(std::string_view name, [[maybe_unused]] UniformType type, const void *const src,
                                   unsigned int numBytes) {
    if(!this->isReady()) return;

    const auto &cachedValue = this->uniformLocationCache.find(name);
    if(cachedValue == this->uniformLocationCache.end()) {
        logIfCV(debug_shaders, "DirectX11Shader Warning: Can't find uniform {:s}", name);
        return;
    }

    const CACHE_ENTRY &cacheEntry = cachedValue->second;
    CONSTANT_BUFFER &cb = this->constantBuffers[cacheEntry.bufferIndex];

    // raw copy: spirv-cross emits row_major matrices + mul(vec, mat), matching the GLSL/SPIR-V convention
    // (same as engine)
    const unsigned int n = std::min(numBytes, cacheEntry.sizeBytes);
    float *dst = &cb.cpuData[cacheEntry.offsetBytes / sizeof(float)];
    if(memcmp(dst, src, n) != 0)  // NOTE: ignore redundant updates
    {
        memcpy(dst, src, n);

        // NOTE: uniforms will be lazy updated later in onJustBeforeDraw() below
        // NOTE: this way we concatenate multiple uniform updates into one single gpu memory transfer
        this->bConstantBuffersUpToDate = false;
    }
}

void DirectX11Shader::onJustBeforeDraw() {
    if(!this->isReady()) return;

    // lazy update uniforms
    if(!this->bConstantBuffersUpToDate) {
        auto *dx11 = static_cast<DirectX11Interface *>(g);

        for(auto &cb : this->constantBuffers) {
            if(cb.buffer == nullptr || cb.cpuData.empty()) continue;

            // lock
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            if(FAILED(dx11->getDeviceContext()->Map(cb.buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
                debugLog("ERROR: failed to Map()!");
                continue;
            }

            // write
            memcpy(mappedResource.pData, &cb.cpuData[0], cb.cpuData.size() * sizeof(float));

            // unlock
            dx11->getDeviceContext()->Unmap(cb.buffer, 0);

            // stats
            {
                if(engine->getFrameCount() == this->iStatsNumConstantBufferUploadsPerFrameCounterEngineFrameCount)
                    this->iStatsNumConstantBufferUploadsPerFrameCounter++;
                else {
                    this->iStatsNumConstantBufferUploadsPerFrameCounterEngineFrameCount = engine->getFrameCount();
                    this->iStatsNumConstantBufferUploadsPerFrameCounter = 1;
                }
            }
        }

        this->bConstantBuffersUpToDate = true;
    }
}

#include "dynutils.h"

dynutils::lib_obj *DirectX11Shader::s_d3dCompilerHandle{nullptr};
D3DCompile_t *DirectX11Shader::s_d3dCompileFunc{nullptr};
D3DReflect_t *DirectX11Shader::s_d3dReflectFunc{nullptr};

bool DirectX11Shader::loadLibs() {
    if(s_d3dCompileFunc != nullptr && s_d3dReflectFunc != nullptr) return true;  // already initialized

#ifdef MCENGINE_PLATFORM_LINUX
    setenv("DXVK_WSI_DRIVER", "SDL3", 1);
#endif

    // try to load vkd3d-utils for linux, d3dcompiler for windows
    constexpr const char *lib_name = Env::cfg(OS::LINUX) ? "libvkd3d-utils.so.1" : "d3dcompiler_";
    unsigned int major, minor;

    if constexpr(Env::cfg(OS::LINUX)) {
        s_d3dCompilerHandle = dynutils::load_lib(lib_name);
    } else {
        // try any d3dcompiler version from 43 to 47, any should be fine (?)
        for(auto version : std::array<std::pair<std::string_view, unsigned int>, 5>{
                {{"47"sv, 47}, {"46"sv, 46}, {"45"sv, 45}, {"44"sv, 44}, {"43"sv, 43}}}) {
            std::string full_lib = lib_name;
            full_lib += version.first;
            major = version.second;

            s_d3dCompilerHandle = dynutils::load_lib(full_lib.c_str());
            if(s_d3dCompilerHandle) break;
        }
    }

    if(s_d3dCompilerHandle == nullptr) {
        debugLog("DirectX11Shader: Failed to load {}: {:s}", lib_name, dynutils::get_error());
        return false;
    }

    // get D3DCompile function pointer
    s_d3dCompileFunc = load_func<D3DCompile_t>(s_d3dCompilerHandle, "D3DCompile");
    if(s_d3dCompileFunc == nullptr) {
        debugLog("DirectX11Shader: Failed to find D3DCompile function: {:s}", dynutils::get_error());
        unload_lib(s_d3dCompilerHandle);
        s_d3dCompilerHandle = nullptr;
        return false;
    }

    // get D3DReflect function pointer (same library; used to discover input layouts + cbuffer layouts)
    s_d3dReflectFunc = load_func<D3DReflect_t>(s_d3dCompilerHandle, "D3DReflect");
    if(s_d3dReflectFunc == nullptr) {
        debugLog("DirectX11Shader: Failed to find D3DReflect function: {:s}", dynutils::get_error());
        unload_lib(s_d3dCompilerHandle);
        s_d3dCompilerHandle = nullptr;
        s_d3dCompileFunc = nullptr;
        return false;
    }

    // check vkd3d version compatibility
    if constexpr(Env::cfg(OS::LINUX)) {
        auto pvkd3d_shader_get_version =
            load_func<const char *(unsigned int *, unsigned int *)>(s_d3dCompilerHandle, "vkd3d_shader_get_version");
        if(pvkd3d_shader_get_version) {
            pvkd3d_shader_get_version(&major, &minor);
            if(!((major > 1) || (major == 1 && minor >= 10))) {
                debugLog("DirectX11Shader: vkd3d version {}.{} is too old, need >= 1.10", major, minor);
                return false;
            }
            debugLog("DirectX11Shader: Using vkd3d version {}.{}", major, minor);
        }
    } else {
        debugLog("DirectX11Shader: Using d3dcompiler_{}.dll", major);
    }

    return true;
}

bool DirectX11Shader::reflectConstantBuffers(ID3DBlob *blob, Stage stage) {
    auto *dx11 = static_cast<DirectX11Interface *>(g);

    D3D_Reflection *refl = nullptr;
    if(FAILED(s_d3dReflectFunc(getBlobBufferPointer(blob), getBlobBufferSize(blob), s_IID_ID3D11ShaderReflection,
                               reinterpret_cast<void **>(&refl))) ||
       refl == nullptr) {
        engine->showMessageError("DirectX11Shader Error", "D3DReflect() failed!");
        return false;
    }

    D3D11_SHADER_DESC shaderDesc;
    refl->lpVtbl->GetDesc(refl, &shaderDesc);

    std::vector<ID3D11Buffer *> &bindArray = (stage == Stage::VERTEX) ? this->vsBuffers : this->psBuffers;

    for(UINT b = 0; b < shaderDesc.ConstantBuffers; b++) {
        D3D_RConstantBuffer *cbRefl = refl->lpVtbl->GetConstantBufferByIndex(refl, b);
        D3D11_SHADER_BUFFER_DESC bufDesc;
        if(cbRefl == nullptr || FAILED(cbRefl->lpVtbl->GetDesc(cbRefl, &bufDesc))) continue;

        // resolve the register slot from the resource binding that matches this cbuffer's name
        UINT registerSlot = 0;
        for(UINT r = 0; r < shaderDesc.BoundResources; r++) {
            D3D11_SHADER_INPUT_BIND_DESC ibd;
            if(FAILED(refl->lpVtbl->GetResourceBindingDesc(refl, r, &ibd))) continue;
            if(ibd.Type == D3D_SIT_CBUFFER && ibd.Name != nullptr && bufDesc.Name != nullptr &&
               std::strcmp(ibd.Name, bufDesc.Name) == 0) {
                registerSlot = ibd.BindPoint;
                break;
            }
        }

        CONSTANT_BUFFER cb;
        cb.stage = stage;
        cb.registerSlot = registerSlot;
        cb.cpuData.assign(bufDesc.Size / sizeof(float), 0.0f);

        const int bufferIndex = (int)this->constantBuffers.size();

        for(UINT v = 0; v < bufDesc.Variables; v++) {
            D3D_RVariable *varRefl = cbRefl->lpVtbl->GetVariableByIndex(cbRefl, v);
            D3D11_SHADER_VARIABLE_DESC varDesc;
            if(varRefl == nullptr || FAILED(varRefl->lpVtbl->GetDesc(varRefl, &varDesc)) || varDesc.Name == nullptr)
                continue;

            // spirv-cross prefixes anonymous-block members with the block's SPIR-V id (e.g. "_19_mvp");
            // strip that so lookups match the engine's setUniform("mvp") names
            std::string_view varName{varDesc.Name};
            if(varName.size() > 1 && varName[0] == '_') {
                size_t i = 1;
                while(i < varName.size() && varName[i] >= '0' && varName[i] <= '9') i++;
                if(i > 1 && i < varName.size() && varName[i] == '_') varName.remove_prefix(i + 1);
            }

            this->uniformLocationCache[std::string{varName}] =
                CACHE_ENTRY{bufferIndex, varDesc.StartOffset, varDesc.Size};
        }

        // create the GPU buffer (std140 byte widths are already 16-byte multiples)
        D3D11_BUFFER_DESC bufferDesc;
        {
            bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
            bufferDesc.ByteWidth = bufDesc.Size;
            bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            bufferDesc.MiscFlags = 0;
            bufferDesc.StructureByteStride = 0;
        }
        ID3D11Buffer *buffer = nullptr;
        if(FAILED(dx11->getDevice()->CreateBuffer(&bufferDesc, nullptr, &buffer)) || buffer == nullptr) {
            refl->lpVtbl->Release(refl);
            engine->showMessageError(
                "DirectX11Shader Error",
                fmt::format("Couldn't CreateBuffer() for \"{}\"!", bufDesc.Name != nullptr ? bufDesc.Name : "?"));
            return false;
        }

        cb.buffer = buffer;
        this->constantBuffers.push_back(std::move(cb));

        // record the buffer in the per-stage bind array at its register slot
        if(bindArray.size() <= registerSlot) bindArray.resize(registerSlot + 1, nullptr);
        bindArray[registerSlot] = buffer;
    }

    refl->lpVtbl->Release(refl);
    return true;
}

bool DirectX11Shader::compile(const std::string &vertexShader, const std::string &fragmentShader) {
    if(vertexShader.length() < 1 || fragmentShader.length() < 1) return false;

    auto *dx11 = static_cast<DirectX11Interface *>(g);

    const char *vsProfile =
        (dx11->getDevice()->GetFeatureLevel() >= D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0 ? "vs_5_0" : "vs_4_0");
    const char *psProfile =
        (dx11->getDevice()->GetFeatureLevel() >= D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0 ? "ps_5_0" : "ps_4_0");
    // spirv-cross emits the entry point as "main" for both stages
    const char *vsEntryPoint = "main";
    const char *psEntryPoint = "main";

    UINT flags = 0;

    if(cv::debug_shaders.getBool()) flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    ID3DBlob *vs = nullptr;
    ID3DBlob *ps = nullptr;
    ID3DBlob *vsError = nullptr;
    ID3DBlob *psError = nullptr;

    // convert to persistent c strings to avoid lifetime issues
    std::string vsSource = vertexShader.c_str();
    std::string psSource = fragmentShader.c_str();

    HRESULT hr1{S_OK}, hr2{S_OK};

    debugLog("DirectX11Shader: D3DCompile()-ing vertex shader ...");
    hr1 = s_d3dCompileFunc(vsSource.c_str(), vsSource.length(), "VS_SOURCE", nullptr /*defines*/, nullptr, vsEntryPoint,
                           vsProfile, flags, 0, &vs, &vsError);

    debugLog("DirectX11Shader: D3DCompile()-ing pixel shader ...");
    hr2 = s_d3dCompileFunc(psSource.c_str(), psSource.length(), "PS_SOURCE", nullptr /*defines*/, nullptr, psEntryPoint,
                           psProfile, flags, 0, &ps, &psError);

    // check compilation results
    if(FAILED(hr1) || FAILED(hr2) || vs == nullptr || ps == nullptr) {
        if(vsError != nullptr) {
            debugLog("DirectX11Shader Vertex Shader Error: \n{:s}", (const char *)getBlobBufferPointer(vsError));
            releaseBlob(vsError);
            debugLog("");
        }

        if(psError != nullptr) {
            debugLog("DirectX11Shader Pixel Shader Error: \n{:s}", (const char *)getBlobBufferPointer(psError));
            releaseBlob(psError);
            debugLog("");
        }

        // clean up on failure
        releaseBlob(vs);
        releaseBlob(ps);

        engine->showMessageError("DirectX11Shader Error", "Couldn't D3DCompile()! Check the log for details.");
        return false;
    }

    // create vertex shader
    debugLog("DirectX11Shader: CreateVertexShader({}) ...", getBlobBufferSize(vs));
    hr1 = dx11->getDevice()->CreateVertexShader(getBlobBufferPointer(vs), getBlobBufferSize(vs), nullptr, &this->vs);

    // create pixel shader
    debugLog("DirectX11Shader: CreatePixelShader({}) ...", getBlobBufferSize(ps));
    hr2 = dx11->getDevice()->CreatePixelShader(getBlobBufferPointer(ps), getBlobBufferSize(ps), nullptr, &this->ps);

    if(FAILED(hr1) || FAILED(hr2)) {
        releaseBlob(vs);
        releaseBlob(ps);

        engine->showMessageError("DirectX11Shader Error", "Couldn't CreateVertexShader()/CreatePixelShader()!");
        return false;
    }

    // build the input layout from vertex-shader reflection
    {
        D3D_Reflection *vsRefl = nullptr;
        if(FAILED(s_d3dReflectFunc(getBlobBufferPointer(vs), getBlobBufferSize(vs), s_IID_ID3D11ShaderReflection,
                                   reinterpret_cast<void **>(&vsRefl))) ||
           vsRefl == nullptr) {
            releaseBlob(vs);
            releaseBlob(ps);
            engine->showMessageError("DirectX11Shader Error", "D3DReflect() failed for the input layout!");
            return false;
        }

        D3D11_SHADER_DESC sd;
        vsRefl->lpVtbl->GetDesc(vsRefl, &sd);

        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        std::vector<std::string> semanticNames;  // owns the SemanticName strings the descs point at
        elements.reserve(sd.InputParameters);
        semanticNames.reserve(sd.InputParameters);

        UINT alignedByteOffset = 0;
        bool ok = true;
        for(UINT i = 0; i < sd.InputParameters; i++) {
            D3D11_SIGNATURE_PARAMETER_DESC p;
            if(FAILED(vsRefl->lpVtbl->GetInputParameterDesc(vsRefl, i, &p))) {
                ok = false;
                break;
            }

            // component count from the write mask, DXGI format from (count, component type)
            UINT comps = 0;
            for(unsigned int m = p.Mask; m != 0u; m >>= 1u) comps += (m & 1u);

            DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
            if(p.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32) {
                switch(comps) {
                    case 1:
                        dxgiFormat = DXGI_FORMAT_R32_FLOAT;
                        break;
                    case 2:
                        dxgiFormat = DXGI_FORMAT_R32G32_FLOAT;
                        break;
                    case 3:
                        dxgiFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                        break;
                    case 4:
                        dxgiFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
                        break;
                    default:
                        break;
                }
            }
            if(dxgiFormat == DXGI_FORMAT_UNKNOWN) {
                engine->showMessageError(
                    "DirectX11Shader Error",
                    fmt::format(R"(Unsupported vertex input "{}" (mask {}, componentType {}))",
                                p.SemanticName != nullptr ? p.SemanticName : "?", (int)p.Mask, (int)p.ComponentType));
                ok = false;
                break;
            }

            semanticNames.emplace_back(p.SemanticName != nullptr ? p.SemanticName : "");

            D3D11_INPUT_ELEMENT_DESC element;
            {
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = p.SemanticIndex;
                element.Format = dxgiFormat;
                element.InputSlot = 0;
                element.AlignedByteOffset = alignedByteOffset;
                element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
                element.InstanceDataStepRate = 0;
            }
            elements.push_back(element);

            alignedByteOffset += comps * 4;
        }

        vsRefl->lpVtbl->Release(vsRefl);

        if(!ok || elements.empty()) {
            releaseBlob(vs);
            releaseBlob(ps);
            if(ok && elements.empty())
                engine->showMessageError("DirectX11Shader Error", "Vertex shader has no input parameters!");
            return false;
        }

        hr1 = dx11->getDevice()->CreateInputLayout(&elements[0], (UINT)elements.size(), getBlobBufferPointer(vs),
                                                   getBlobBufferSize(vs), &this->inputLayout);
    }

    if(FAILED(hr1)) {
        releaseBlob(vs);
        releaseBlob(ps);
        engine->showMessageError("DirectX11Shader Error", "Couldn't CreateInputLayout()!");
        return false;
    }

    // reflect the per-stage constant buffers (vertex set=1 / fragment set=3 -> register(b0) each)
    const bool reflected = reflectConstantBuffers(vs, Stage::VERTEX) && reflectConstantBuffers(ps, Stage::PIXEL);

    releaseBlob(vs);
    releaseBlob(ps);

    return reflected;
}

#endif
