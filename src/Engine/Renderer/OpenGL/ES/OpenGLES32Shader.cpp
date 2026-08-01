//================ Copyright (c) 2025, WH, All rights reserved. =================//
//
// Purpose:		OpenGLES32 GLSL implementation of Shader
//
// $NoKeywords: $gles32shader
//===============================================================================//

#include "OpenGLES32Shader.h"

#ifdef MCENGINE_FEATURE_GLES32

#include "Engine.h"
#include "ConVar.h"
#include "Logging.h"
#include "Matrices.h"

#include "OpenGLHeaders.h"
#include "OpenGLES32Interface.h"
#include "OpenGLStateCache.h"

OpenGLES32Shader::OpenGLES32Shader(const std::string &shader, [[maybe_unused]] bool source) : Shader() {
    SHADER_PARSE_RESULT parsedVertexShader = parseShaderFromString("OpenGLES32Interface::VertexShader", shader);
    SHADER_PARSE_RESULT parsedFragmentShader = parseShaderFromString("OpenGLES32Interface::FragmentShader", shader);

    m_sVsh = parsedVertexShader.source;
    m_sFsh = parsedFragmentShader.source;

    m_iProgram = 0;
    m_iVertexShader = 0;
    m_iFragmentShader = 0;

    m_iProgramBackup = 0;
}

OpenGLES32Shader::OpenGLES32Shader(const std::string &vertexShader, const std::string &fragmentShader,
                                   [[maybe_unused]] bool source)
    : Shader() {
    m_sVsh = vertexShader;
    m_sFsh = fragmentShader;

    m_iProgram = 0;
    m_iVertexShader = 0;
    m_iFragmentShader = 0;

    m_iProgramBackup = 0;
}

void OpenGLES32Shader::init() { this->setReady(this->compile(m_sVsh, m_sFsh, true)); }

void OpenGLES32Shader::initAsync() { this->setAsyncReady(true); }

void OpenGLES32Shader::destroy() {
    auto *gles32 = static_cast<OpenGLES32Interface *>(g);
    if(gles32 != nullptr) gles32->unregisterShader(this);

    if(m_iProgram != 0) glDeleteProgram(m_iProgram);
    if(m_iFragmentShader != 0) glDeleteShader(m_iFragmentShader);
    if(m_iVertexShader != 0) glDeleteShader(m_iVertexShader);

    m_iProgram = 0;
    m_iFragmentShader = 0;
    m_iVertexShader = 0;

    m_iProgramBackup = 0;
    m_uniformLocationCache.clear();
}

void OpenGLES32Shader::enable() {
    if(unlikely(!this->isReady())) return;

    int currentProgram = static_cast<int>(GLStateCache::getCurrentProgram());
    if(currentProgram == m_iProgram)  // already active
        return;

    // use the state cache instead of querying gl directly
    m_iProgramBackup = static_cast<int>(GLStateCache::getCurrentProgram());
    glUseProgram(m_iProgram);

    // update cache
    GLStateCache::setCurrentProgram(m_iProgram);

    // the newly activated shader may not have the current MVP if no transform
    // change occurred since it was last active. setMVP's internal cache makes
    // this essentially free (memcmp skip) when the matrix hasn't changed.
    this->setMVP(g->getMVP());
}

void OpenGLES32Shader::disable() {
    if(unlikely(!this->isReady())) return;

    glUseProgram(m_iProgramBackup);  // restore

    // update cache
    GLStateCache::setCurrentProgram(m_iProgramBackup);
}

int OpenGLES32Shader::getAndCacheUniformLocation(std::string_view name) {
    if(unlikely(!this->isReady()) || name.empty()) return -1;

    const auto cachedValue = m_uniformLocationCache.find(name);
    const bool cached = (cachedValue != m_uniformLocationCache.end());

    const int id = (cached ? cachedValue->second : glGetUniformLocation(m_iProgram, std::string{name}.c_str()));
    if(!cached && id != -1) m_uniformLocationCache.emplace(name, id);

    return id;
}

void OpenGLES32Shader::writeUniform(std::string_view name, UniformType type, const void *const data, u32 dataSize) {
    if(unlikely(!this->isReady())) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLES32Shader Warning: Can't find uniform {:s}", name);
        return;
    }
    switch(type) {
        using enum Shader::UniformType;
        case UNI_1F:
            glUniform1f(id, *static_cast<const float *>(data));
            break;
        case UNI_1FV:
            glUniform1fv(id, static_cast<int>(dataSize / sizeof(float)), static_cast<const float *>(data));
            break;
        case UNI_1I:
            glUniform1i(id, *static_cast<const int *>(data));
            break;
        case UNI_2F:
            glUniform2f(id, static_cast<const float *>(data)[0], static_cast<const float *>(data)[1]);
            break;
        case UNI_2FV:
            glUniform2fv(id, static_cast<int>(dataSize / sizeof(float) / 2), static_cast<const float *>(data));
            break;
        case UNI_3F:
            glUniform3f(id, static_cast<const float *>(data)[0], static_cast<const float *>(data)[1],
                        static_cast<const float *>(data)[2]);
            break;
        case UNI_3FV:
            glUniform3fv(id, static_cast<int>(dataSize / sizeof(float) / 3), static_cast<const float *>(data));
            break;
        case UNI_4F:
            glUniform4f(id, static_cast<const float *>(data)[0], static_cast<const float *>(data)[1],
                        static_cast<const float *>(data)[2], static_cast<const float *>(data)[3]);
            break;
        case UNI_MATRIX4FV:
            glUniformMatrix4fv(id, 1, GL_FALSE, static_cast<const float *>(data));
            break;
        default:
            debugLog("OpenGLES32Shader ERROR: unhandled type {} name {}", (u32)type, name);
            break;
    }
}

int OpenGLES32Shader::getAttribLocation(std::string_view name) {
    if(unlikely(!this->isReady()) || name.empty()) return -1;

    return glGetAttribLocation(m_iProgram, std::string{name}.c_str());
}

bool OpenGLES32Shader::isActive() {
    int currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    return (this->isReady() && currentProgram == m_iProgram);
}

bool OpenGLES32Shader::compile(const std::string &vertexShader, const std::string &fragmentShader, bool source) {
    // load & compile shaders
    debugLog("Compiling {:s} ...", (source ? "vertex source" : vertexShader));
    m_iVertexShader = source ? createShaderFromString(vertexShader, GL_VERTEX_SHADER)
                             : createShaderFromFile(vertexShader, GL_VERTEX_SHADER);
    debugLog("Compiling {:s} ...", (source ? "fragment source" : fragmentShader));
    m_iFragmentShader = source ? createShaderFromString(fragmentShader, GL_FRAGMENT_SHADER)
                               : createShaderFromFile(fragmentShader, GL_FRAGMENT_SHADER);

    const bool showDebugPopup = cv::debug_shaders.getBool();  // || Env::cfg(BUILD::DEBUG);
    if(m_iVertexShader == 0 || m_iFragmentShader == 0) {
        if(showDebugPopup) {
            engine->showMessageError("OpenGLES32Shader Error", "Couldn't createShader()");
        } else {
            debugLog("couldn't createShader()");
        }
        return false;
    }

    // create program
    m_iProgram = glCreateProgram();
    if(m_iProgram == 0) {
        if(showDebugPopup) {
            engine->showMessageError("OpenGLES32Shader Error", "Couldn't glCreateProgram()");
        } else {
            debugLog("couldn't glCreateProgram()");
        }
        return false;
    }

    // attach
    glAttachShader(m_iProgram, m_iVertexShader);
    glAttachShader(m_iProgram, m_iFragmentShader);

    // force consistent attribute locations so custom shaders share
    // the same layout as the default shader (drawVAO binds vertex
    // data to these fixed locations regardless of which shader is active)
    glBindAttribLocation(m_iProgram, 0, "position");
    glBindAttribLocation(m_iProgram, 1, "uv");
    glBindAttribLocation(m_iProgram, 2, "vcolor");

    // link
    glLinkProgram(m_iProgram);

    GLint ret = GL_FALSE;
    glGetProgramiv(m_iProgram, GL_LINK_STATUS, &ret);
    if(ret == GL_FALSE) {
        if(showDebugPopup) {
            engine->showMessageError("OpenGLES32Shader Error", "Couldn't glLinkProgram()");
        } else {
            debugLog("couldn't glLinkProgram()");
        }
        return false;
    }

    return true;
}

int OpenGLES32Shader::createShaderFromString(std::string shaderSource, int shaderType) {
    const GLint shader = glCreateShader(shaderType);

    if(shader == 0) {
        engine->showMessageError("OpenGLES32Shader Error", "Couldn't glCreateShader()");
        return 0;
    }

    // compile shader
    size_t pos = shaderSource.find("{RUNTIME_VERSION}"sv);
    if(pos != std::string::npos) {
        shaderSource.replace(pos, "{RUNTIME_VERSION}"sv.length(), "100");
    }

    const char *shaderSourceChar = shaderSource.c_str();
    glShaderSource(shader, 1, &shaderSourceChar, nullptr);
    glCompileShader(shader);

    GLint ret = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ret);
    if(ret == GL_FALSE) {
        debugLog("------------------OpenGLES32Shader Compile Error------------------");

        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &ret);
        if(ret > 0) {
            char *errorLog = new char[ret];
            {
                glGetShaderInfoLog(shader, ret, &ret, errorLog);
                logRaw("{}", errorLog);
            }
            delete[] errorLog;
        }

        debugLog("-----------------------------------------------------------------");

        engine->showMessageError("OpenGLES32Shader Error", "Couldn't glShaderSource() or glCompileShader()");
        return 0;
    }

    return shader;
}

int OpenGLES32Shader::createShaderFromFile(const std::string &fileName, int shaderType) {
    // load file
    std::ifstream inFile(fileName.c_str());
    if(!inFile) {
        engine->showMessageError("OpenGLES32Shader Error", fileName);
        return 0;
    }
    std::string line;
    std::string shaderSource;
    while(inFile.good()) {
        std::getline(inFile, line);
        shaderSource += line + "\n\0";
    }
    shaderSource += "\n\0";
    inFile.close();

    std::string shaderSourcePtr = std::string(shaderSource.c_str());

    return createShaderFromString(shaderSourcePtr, shaderType);
}

#endif
