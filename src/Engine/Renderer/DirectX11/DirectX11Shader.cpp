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
#include "SString.h"

#include "DirectX11Interface.h"

#include "d3dcompiler.h"

#define MCENGINE_D3D11_SHADER_MAX_NUM_CONSTANT_BUFFERS 9

DirectX11Shader::DirectX11Shader(std::string vertexShader, std::string fragmentShader, [[maybe_unused]] bool source)
    : Shader(), sVsh(std::move(vertexShader)), sFsh(std::move(fragmentShader)) {
    for(int i = 0; i < MCENGINE_D3D11_SHADER_MAX_NUM_CONSTANT_BUFFERS; i++) {
        this->prevConstantBuffers.push_back(nullptr);
    }
}

void DirectX11Shader::init() {
    static const auto parseError = []<typename... Args>(const fmt::format_string<Args...> &fmt,
                                                        Args &&...args) -> void {
        engine->showMessageError("DirectX11Shader Error", fmt::format(fmt, std::forward<Args>(args)...));
    };

    const SHADER_PARSE_RESULT parsedVertexShader =
        parseShaderFromString("DirectX11Interface::VertexShader", this->sVsh);
    const SHADER_PARSE_RESULT parsedPixelShader = parseShaderFromString("DirectX11Interface::PixelShader", this->sFsh);

    bool valid = (parsedVertexShader.descs.size() > 0);
    {
        // parse lines
        std::vector<INPUT_DESC_LINE> inputDescLines;
        std::vector<BIND_DESC_LINE> bindDescLines;
        for(size_t i = 0; i < parsedVertexShader.descs.size(); i++) {
            const std::string &desc = parsedVertexShader.descs[i];
            const auto tokens = SString::split(desc, "::");

            logIfCV(debug_shaders, "descs[{}] = {:s}", (int)i, desc);

            if(tokens.size() > 4) {
                const auto descType = tokens[0];

                if(cv::debug_shaders.getBool()) {
                    for(size_t t = 0; t < tokens.size(); t++) {
                        debugLog("descs[{}][{}] = {:s}", (int)i, (int)t, tokens[t]);
                    }
                }

                if(descType == "D3D11_INPUT_ELEMENT_DESC") {
                    const auto inputType = tokens[1];      // e.g. VS_INPUT
                    const auto inputDataType = tokens[2];  // e.g. POSITION or COLOR0 or TEXCOORD0 etc.
                    const auto inputFormat = tokens
                        [3];  // e.g. DXGI_FORMAT_R32G32B32_FLOAT or DXGI_FORMAT_R32G32B32A32_FLOAT or DXGI_FORMAT_R32G32_FLOAT etc.
                    const auto inputClassification = tokens[4];  // e.g. D3D11_INPUT_PER_VERTEX_DATA

                    INPUT_DESC_LINE inputDescLine;
                    {
                        inputDescLine.type = inputType;
                        {
                            inputDescLine.dataType = inputDataType;

                            // NOTE: remove integer from end of datatype string (e.g. "COLOR0", "TEXCOORD0" etc., since this is implied by the order and only
                            // necessary in actual shader code as CreateInputLayout() would fail otherwise)
                            if(!inputDescLine.dataType.empty() &&
                               "0123456789"sv.contains(inputDescLine.dataType.back())) {
                                inputDescLine.dataType.erase(inputDescLine.dataType.length() - 1, 1);
                            }
                        }
                        {
                            if(inputFormat == "DXGI_FORMAT_R32_FLOAT") {
                                inputDescLine.dxgiFormat = DXGI_FORMAT_R32_FLOAT;
                                inputDescLine.dxgiFormatBytes = 1 * 4;
                            } else if(inputFormat == "DXGI_FORMAT_R32G32_FLOAT") {
                                inputDescLine.dxgiFormat = DXGI_FORMAT_R32G32_FLOAT;
                                inputDescLine.dxgiFormatBytes = 2 * 4;
                            } else if(inputFormat == "DXGI_FORMAT_R32G32B32_FLOAT") {
                                inputDescLine.dxgiFormat = DXGI_FORMAT_R32G32B32_FLOAT;
                                inputDescLine.dxgiFormatBytes = 3 * 4;
                            } else if(inputFormat == "DXGI_FORMAT_R32G32B32A32_FLOAT") {
                                inputDescLine.dxgiFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
                                inputDescLine.dxgiFormatBytes = 4 * 4;
                            } else {
                                valid = false;
                                parseError(R"(Invalid/Unsupported inputFormat "{}")", inputFormat);
                                break;
                            }
                        }
                        {
                            if(inputClassification == "D3D11_INPUT_PER_VERTEX_DATA")
                                inputDescLine.classification = D3D11_INPUT_PER_VERTEX_DATA;
                            else {
                                valid = false;
                                parseError(R"(Invalid/Unsupported inputClassification "{}")", inputClassification);
                                break;
                            }
                        }
                    }
                    inputDescLines.push_back(inputDescLine);
                } else if(descType == "D3D11_BUFFER_DESC") {
                    const auto bufferBindType = tokens[1];      // e.g. D3D11_BIND_CONSTANT_BUFFER
                    const auto bufferName = tokens[2];          // e.g. ModelViewProjectionConstantBuffer
                    const auto bufferVariableName = tokens[3];  // e.g. mvp or col or misc etc.
                    const auto bufferVariableType = tokens[4];  // e.g. float4x4 or float4 etc.

                    BIND_DESC_LINE bindDescLine;
                    {
                        bindDescLine.type =
                            descType;  // NOTE: not bufferBindType! since we want to be able to support more than just D3D11_BUFFER_DESC
                        {
                            if(bufferBindType == "D3D11_BIND_CONSTANT_BUFFER")
                                bindDescLine.bindFlag = D3D11_BIND_CONSTANT_BUFFER;
                            else {
                                valid = false;
                                parseError(R"(Invalid/Unsupported bufferBindType "{}")", bufferBindType);
                                break;
                            }
                        }
                        bindDescLine.name = bufferName;
                        bindDescLine.variableName = bufferVariableName;
                        {
                            bindDescLine.variableType = bufferVariableType;
                            if(bufferVariableType == "float")
                                bindDescLine.variableBytes = 1 * 4;
                            else if(bufferVariableType == "int")
                                bindDescLine.variableBytes = 1 * 4;
                            else if(bufferVariableType == "float2")
                                bindDescLine.variableBytes = 2 * 4;
                            else if(bufferVariableType == "float3")
                                bindDescLine.variableBytes = 3 * 4;
                            else if(bufferVariableType == "float4")
                                bindDescLine.variableBytes = 4 * 4;
                            else if(bufferVariableType == "float4x4")
                                bindDescLine.variableBytes = 4 * 4 * 4;
                            else {
                                valid = false;
                                parseError(R"(Invalid/Unsupported bufferVariableType "{}")", bufferVariableType);
                                break;
                            }
                        }
                    }
                    bindDescLines.push_back(bindDescLine);
                } else {
                    valid = false;
                    parseError(R"(Invalid/Unsupported descType "{}")", descType);
                    break;
                }
            } else {
                valid = false;
                parseError(R"(Invalid desc "{}")", desc);
                break;
            }
        }

        // build m_inputDescs + m_bindDescs
        {
            // compound by INPUT_DESC_LINE::type
            for(size_t i = 0; i < inputDescLines.size(); i++) {
                const INPUT_DESC_LINE &inputDescLine = inputDescLines[i];

                bool alreadyExists = false;
                for(INPUT_DESC &inputDesc : this->inputDescs) {
                    if(inputDesc.type == inputDescLine.type) {
                        alreadyExists = true;
                        {
                            inputDesc.lines.push_back(inputDescLine);
                        }
                        break;
                    }
                }
                if(!alreadyExists) {
                    INPUT_DESC inputDesc;
                    {
                        inputDesc.type = inputDescLine.type;
                    }
                    this->inputDescs.push_back(inputDesc);

                    // (repeat to avoid code duplication)
                    i--;
                    continue;
                }
            }

            // compound by BIND_DESC_LINE::name
            for(size_t i = 0; i < bindDescLines.size(); i++) {
                const BIND_DESC_LINE &bindDescLine = bindDescLines[i];

                bool alreadyExists = false;
                for(BIND_DESC &bindDesc : this->bindDescs) {
                    if(bindDesc.name == bindDescLine.name) {
                        alreadyExists = true;
                        {
                            bindDesc.lines.push_back(bindDescLine);
                            bindDesc.floats.resize(bindDesc.floats.size() +
                                                   (bindDescLine.variableBytes / sizeof(float)));

                            if(cv::debug_shaders.getBool())
                                debugLog("bindDesc[{:s}].floats.size() = {}", bindDescLine.name.c_str(),
                                         (int)bindDesc.floats.size());
                        }
                        break;
                    }
                }

                if(!alreadyExists) {
                    BIND_DESC bindDesc;
                    {
                        bindDesc.name = bindDescLine.name;
                    }
                    this->bindDescs.push_back(bindDesc);

                    // (repeat to avoid code duplication)
                    i--;
                    continue;
                }
            }

            // error checking
            if(this->inputDescs.size() < 1) {
                valid = false;
                parseError("Missing at least one D3D11_INPUT_ELEMENT_DESC instance");
            } else if(this->bindDescs.size() < 1) {
                // (there could theoretically be a shader without any buffers bound, so this is not an error)
            }

            if(cv::debug_shaders.getBool()) {
                for(size_t i = 0; i < this->inputDescs.size(); i++) {
                    debugLog(R"(inputDescs[{}] = "{:s}", has {} line(s))", (int)i, this->inputDescs[i].type.c_str(),
                             (int)this->inputDescs[i].lines.size());
                }

                for(size_t i = 0; i < this->bindDescs.size(); i++) {
                    debugLog(R"(bindDescs[{}] = "{:s}", has {} lines(s))", (int)i, this->bindDescs[i].name.c_str(),
                             (int)this->bindDescs[i].lines.size());
                }
            }
        }
    }

    this->setReady(compile((valid ? parsedVertexShader.source : ""), (valid ? parsedPixelShader.source : "")));
}

void DirectX11Shader::initAsync() { this->setAsyncReady(true); }

void DirectX11Shader::destroy() {
    for(auto &buffer : this->constantBuffers) {
        if(buffer != nullptr) {
            buffer->Release();
            buffer = nullptr;
        }
    }
    this->constantBuffers.clear();

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

    this->inputDescs.clear();
    this->bindDescs.clear();

    this->uniformLocationCache.clear();
}

void DirectX11Shader::enable() {
    auto *dx11 = static_cast<DirectX11Interface *>(g.get());
    if(!this->isReady() || dx11->getActiveShader() == this) return;

    auto *context = dx11->getDeviceContext();

    // backup
    // HACKHACK: slow af
    {
        this->prevShader = dx11->getActiveShader();

        context->IAGetInputLayout(&this->prevInputLayout);
        context->VSGetShader(&this->prevVS, nullptr, nullptr);
        context->PSGetShader(&this->prevPS, nullptr, nullptr);
        context->VSGetConstantBuffers(0, (UINT)this->prevConstantBuffers.size(), &this->prevConstantBuffers[0]);

        this->bStateBackedUp = true;  // mark that we actually backed up state
    }

    context->IASetInputLayout(this->inputLayout);
    context->VSSetShader(this->vs, nullptr, 0);
    context->PSSetShader(this->ps, nullptr, 0);

    context->VSSetConstantBuffers(0, (UINT)this->constantBuffers.size(), &this->constantBuffers[0]);

    dx11->setActiveShader(this);
}

void DirectX11Shader::disable() {
    auto *dx11 = static_cast<DirectX11Interface *>(g.get());
    if(!this->isReady() || dx11->getActiveShader() != this || !this->bStateBackedUp) return;

    auto *context = dx11->getDeviceContext();

    // restore
    // HACKHACK: slow af
    {
        UINT numPrevConstantBuffers = 0;
        for(auto &prevConstantBuffer : this->prevConstantBuffers) {
            if(prevConstantBuffer != nullptr)
                numPrevConstantBuffers++;
            else
                break;
        }

        context->IASetInputLayout(this->prevInputLayout);
        context->VSSetShader(this->prevVS, nullptr, 0);
        context->PSSetShader(this->prevPS, nullptr, 0);
        context->VSSetConstantBuffers(0, numPrevConstantBuffers, &this->prevConstantBuffers[0]);

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

            for(auto &buffer : this->prevConstantBuffers) {
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

void DirectX11Shader::setUniform1f(std::string_view name, float value) { setUniform(name, &value, sizeof(float) * 1); }

void DirectX11Shader::setUniform1fv(std::string_view name, int count, const float *const values) {
    setUniform(name, values, sizeof(float) * 1 * count);
}

void DirectX11Shader::setUniform1i(std::string_view name, int value) { setUniform(name, &value, sizeof(int32_t) * 1); }

void DirectX11Shader::setUniform2f(std::string_view name, float x, float y) {
    vec2 f(x, y);
    setUniform(name, &f[0], sizeof(float) * 2);
}

void DirectX11Shader::setUniform2fv(std::string_view name, int count, const float *const vectors) {
    setUniform(name, vectors, sizeof(float) * 2 * count);
}

void DirectX11Shader::setUniform3f(std::string_view name, float x, float y, float z) {
    vec3 f(x, y, z);
    setUniform(name, &f[0], sizeof(float) * 3);
}

void DirectX11Shader::setUniform3fv(std::string_view name, int count, const float *const vectors) {
    setUniform(name, vectors, sizeof(float) * 3 * count);
}

void DirectX11Shader::setUniform4f(std::string_view name, float x, float y, float z, float w) {
    vec4 f(x, y, z, w);
    setUniform(name, &f[0], sizeof(float) * 4);
}

void DirectX11Shader::setUniformMatrix4fv(std::string_view name, const Matrix4 &matrix) {
    Matrix4 transposed{matrix};  // HACKHACK
    transposed.transpose();
    setUniformMatrix4fv(name, transposed.get());
}

void DirectX11Shader::setUniformMatrix4fv(std::string_view name, const float *const v) {
    setUniform(name, v, sizeof(float) * 4 * 4);
}

void DirectX11Shader::setUniform(std::string_view name, const void *const src, size_t numBytes) {
    if(!this->isReady()) return;

    const CACHE_ENTRY cacheEntry = getAndCacheUniformLocation(name);
    if(cacheEntry.bindIndex > -1) {
        BIND_DESC &bindDesc = this->bindDescs[cacheEntry.bindIndex];

        if(memcmp(src, &bindDesc.floats[cacheEntry.offsetBytes / sizeof(float)], numBytes) !=
           0)  // NOTE: ignore redundant updates
        {
            memcpy(&bindDesc.floats[cacheEntry.offsetBytes / sizeof(float)], src, numBytes);

            // NOTE: uniforms will be lazy updated later in onJustBeforeDraw() below
            // NOTE: this way we concatenate multiple uniform updates into one single gpu memory transfer
            this->bConstantBuffersUpToDate = false;
        }
    }
}

void DirectX11Shader::onJustBeforeDraw() {
    if(!this->isReady()) return;

    // lazy update uniforms
    if(!this->bConstantBuffersUpToDate) {
        auto *dx11 = static_cast<DirectX11Interface *>(g.get());

        for(size_t i = 0; i < this->constantBuffers.size(); i++) {
            ID3D11Buffer *constantBuffer = this->constantBuffers[i];
            BIND_DESC &bindDesc = this->bindDescs[i];

            // lock
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            if(FAILED(dx11->getDeviceContext()->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource))) {
                debugLog("ERROR: failed to Map()!");
                continue;
            }

            // write
            memcpy(mappedResource.pData, &bindDesc.floats[0], bindDesc.floats.size() * sizeof(float));

            // unlock
            dx11->getDeviceContext()->Unmap(constantBuffer, 0);

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

const DirectX11Shader::CACHE_ENTRY DirectX11Shader::getAndCacheUniformLocation(std::string_view name) {
    if(!this->isReady()) return invalidCacheEntry;

    const auto &cachedValue = this->uniformLocationCache.find(name);
    const bool isCached = (cachedValue != this->uniformLocationCache.end());

    if(isCached)
        return cachedValue->second;
    else {
        CACHE_ENTRY newCacheEntry;

        for(size_t i = 0; i < this->bindDescs.size(); i++) {
            const BIND_DESC &bindDesc = this->bindDescs[i];
            int offsetBytesCounter = 0;
            for(const auto &bindDescLine : bindDesc.lines) {
                if(bindDescLine.variableName == name) {
                    newCacheEntry.bindIndex = (int)i;
                    newCacheEntry.offsetBytes = offsetBytesCounter;
                    break;
                } else
                    offsetBytesCounter += bindDescLine.variableBytes;
            }
        }

        if(newCacheEntry.bindIndex > -1 && newCacheEntry.offsetBytes > -1) {
            this->uniformLocationCache[std::string{name}] = newCacheEntry;
            return newCacheEntry;
        } else if(cv::debug_shaders.getBool())
            debugLog("DirectX11Shader Warning: Can't find uniform {:s}", name);
    }

    return invalidCacheEntry;
}

#include "dynutils.h"

dynutils::lib_obj *DirectX11Shader::s_d3dCompilerHandle{nullptr};
D3DCompile_t *DirectX11Shader::s_d3dCompileFunc{nullptr};

bool DirectX11Shader::loadLibs() {
    if(s_d3dCompileFunc != nullptr) return true;  // already initialized

#ifdef MCENGINE_PLATFORM_LINUX
    setenv("DXVK_WSI_DRIVER", "SDL3", 1);
#endif

    // try to load vkd3d-utils for linux, d3dcompiler for windows
    constexpr const char *lib_name = Env::cfg(OS::LINUX) ? "libvkd3d-utils.so.1" : "d3dcompiler_";
    unsigned int major, minor;

    if constexpr(Env::cfg(OS::LINUX)) {
        s_d3dCompilerHandle = dynutils::load_lib(lib_name);
    } else {
        // try any d3dcompiler version from 43 to 47, any should be fine
        for(auto version : std::array<std::pair<std::string_view, unsigned int>, 5>{
                {{"43"sv, 43}, {"44"sv, 44}, {"45"sv, 45}, {"46"sv, 46}, {"47"sv, 47}}}) {
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

void *DirectX11Shader::getBlobBufferPointer(ID3DBlob *blob) {
    auto *d3dblob = (D3D_Blob *)blob;
    return d3dblob->lpVtbl->GetBufferPointer(d3dblob);
}

SIZE_T DirectX11Shader::getBlobBufferSize(ID3DBlob *blob) {
    auto *d3dblob = (D3D_Blob *)blob;
    return d3dblob->lpVtbl->GetBufferSize(d3dblob);
}

void DirectX11Shader::releaseBlob(ID3DBlob *blob) {
    if(blob != nullptr) {
        auto *d3dblob = (D3D_Blob *)blob;
        d3dblob->lpVtbl->Release(d3dblob);
    }
}

bool DirectX11Shader::compile(const std::string &vertexShader, const std::string &fragmentShader) {
    if(vertexShader.length() < 1 || fragmentShader.length() < 1) return false;

    auto *dx11 = static_cast<DirectX11Interface *>(g.get());

    const char *vsProfile =
        (dx11->getDevice()->GetFeatureLevel() >= D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0 ? "vs_5_0" : "vs_4_0");
    const char *psProfile =
        (dx11->getDevice()->GetFeatureLevel() >= D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_11_0 ? "ps_5_0" : "ps_4_0");
    const char *vsEntryPoint = "vsmain";
    const char *psEntryPoint = "psmain";

    UINT flags = 0;

    if(cv::debug_shaders.getBool()) flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    // const D3D_SHADER_MACRO defines[] = {
    //     {"EXAMPLE_DEFINE", "1" },
    //     {nullptr,             nullptr}  // sentinel
    // };

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
    releaseBlob(ps);

    if(FAILED(hr1) || FAILED(hr2)) {
        releaseBlob(vs);

        engine->showMessageError("DirectX11Shader Error", "Couldn't CreateVertexShader()/CreatePixelShader()!");
        return false;
    }

    // create the input layout (rest of the function remains the same)
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    {
        const INPUT_DESC &inputDesc = this->inputDescs[0];
        UINT alignedByteOffsetCounter = 0;
        for(const INPUT_DESC_LINE &inputDescLine : inputDesc.lines) {
            D3D11_INPUT_ELEMENT_DESC element;
            {
                element.SemanticName = inputDescLine.dataType.c_str();
                element.SemanticIndex = 0;
                element.Format = inputDescLine.dxgiFormat;
                element.InputSlot = 0;
                element.AlignedByteOffset = alignedByteOffsetCounter;
                element.InputSlotClass = inputDescLine.classification;
                element.InstanceDataStepRate = 0;

                if(cv::debug_shaders.getBool())
                    debugLog("{:s}, {:d}, {:d}, {:d}, {:d}, {:d}, {:d}", element.SemanticName,
                             (int)element.SemanticIndex, (int)element.Format, (int)element.InputSlot,
                             (int)element.AlignedByteOffset, (int)element.InputSlotClass,
                             (int)element.InstanceDataStepRate);
            }
            elements.push_back(element);

            alignedByteOffsetCounter += inputDescLine.dxgiFormatBytes;
        }
    }

    hr1 = dx11->getDevice()->CreateInputLayout(&elements[0], elements.size(), getBlobBufferPointer(vs),
                                               getBlobBufferSize(vs), &this->inputLayout);
    releaseBlob(vs);

    if(FAILED(hr1)) {
        engine->showMessageError("DirectX11Shader Error", "Couldn't CreateInputLayout()!");
        return false;
    }

    // create binds/buffers (rest remains the same)
    for(const BIND_DESC &bindDesc : this->bindDescs) {
        const std::string &descType = bindDesc.lines[0].type;

        if(descType == "D3D11_BUFFER_DESC") {
            const D3D11_BIND_FLAG bindFlag = bindDesc.lines[0].bindFlag;
            const std::string &name = bindDesc.lines[0].name;

            UINT byteWidth = 0;
            for(const BIND_DESC_LINE &bindDescLine : bindDesc.lines) {
                byteWidth += bindDescLine.variableBytes;
            }

            if(byteWidth % 16 != 0) {
                engine->showMessageError("DirectX11Shader Error",
                                         fmt::format("Invalid byteWidth {} for \"{}\" (must be a multiple of 16)",
                                                     (int)byteWidth, name.c_str()));
                return false;
            }

            D3D11_BUFFER_DESC bufferDesc;
            {
                bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
                bufferDesc.ByteWidth = byteWidth;
                bufferDesc.BindFlags = bindFlag;
                bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                bufferDesc.MiscFlags = 0;
                bufferDesc.StructureByteStride = 0;
            }
            ID3D11Buffer *buffer = nullptr;
            hr1 = dx11->getDevice()->CreateBuffer(&bufferDesc, nullptr, &buffer);
            if(FAILED(hr1) || buffer == nullptr) {
                engine->showMessageError("DirectX11Shader Error", fmt::format("Couldn't CreateBuffer({}, {:x}, {:x})!",
                                                                              hr1, hr1, MAKE_DXGI_HRESULT(hr1)));
                return false;
            }

            this->constantBuffers.push_back(buffer);
        } else {
            engine->showMessageError("DirectX11Shader Error", fmt::format("Invalid descType \"{}\"", descType.c_str()));
            return false;
        }
    }

    return true;
}

#endif
