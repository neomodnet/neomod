// Copyright (c) 2016, PG, All rights reserved.
#include "config.h"

#ifdef MCENGINE_FEATURE_OPENGL
#include "OpenGLShader.h"

#include "OpenGLHeaders.h"
#include "OpenGLStateCache.h"

#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"
#include "Matrices.h"
#include "File.h"

#include <fstream>

static inline void glDeleteObject_wrapper(GLuint obj) {
    if(glIsProgram(obj))
        glDeleteProgram(obj);
    else
        glDeleteShader(obj);
}

static inline void glGetObjectParameteriv_wrapper(GLuint obj, GLenum pname, GLint *params) {
    if(glIsProgram(obj))
        glGetProgramiv(obj, pname, params);
    else
        glGetShaderiv(obj, pname, params);
}

#define MCglDeleteObject glDeleteObject_wrapper
#define MCglGetObjectParameteriv glGetObjectParameteriv_wrapper

#define MCglCreateProgramObject glCreateProgram
#define MCglCreateShaderObject glCreateShader
#define MCglAttachObject glAttachShader
#define MCglUseProgramObject glUseProgram

#define MCglShaderSource glShaderSource
#define MCglCompileShader glCompileShader
#define MCglLinkProgram glLinkProgram
#define MCglValidateProgram glValidateProgram
#define MCglGetUniformLocation glGetUniformLocation
#define MCglGetAttribLocation glGetAttribLocation
#define MCglGetInfoLog glGetShaderInfoLog

#define MCglUniform1f glUniform1f
#define MCglUniform1fv glUniform1fv
#define MCglUniform1i glUniform1i
#define MCglUniform2f glUniform2f
#define MCglUniform2fv glUniform2fv
#define MCglUniform3f glUniform3f
#define MCglUniform3fv glUniform3fv
#define MCglUniform4f glUniform4f
#define MCglUniformMatrix4fv glUniformMatrix4fv

OpenGLShader::OpenGLShader(std::string vertexShader, std::string fragmentShader, bool source)
    : Shader(), sVsh(std::move(vertexShader)), sFsh(std::move(fragmentShader)), bSource(source) {
    this->iProgram = 0;
    this->iVertexShader = 0;
    this->iFragmentShader = 0;

    this->iProgramBackup = 0;
}

void OpenGLShader::init() {
    this->setReady(this->compile(this->sVsh, this->sFsh, this->bSource));
    if(!this->isReady()) {
        debugLog("name: {:s}\nVSH:\n{:s}\nFSH:\n{:s}", this->sName, this->sFsh, this->sVsh);
    }
}

void OpenGLShader::initAsync() { this->setAsyncReady(true); }

void OpenGLShader::destroy() {
    if(this->iProgram != 0) MCglDeleteObject(this->iProgram);
    if(this->iFragmentShader != 0) MCglDeleteObject(this->iFragmentShader);
    if(this->iVertexShader != 0) MCglDeleteObject(this->iVertexShader);

    this->iProgram = 0;
    this->iFragmentShader = 0;
    this->iVertexShader = 0;

    this->iProgramBackup = 0;

    this->uniformLocationCache.clear();
}

void OpenGLShader::enable() {
    if(unlikely(!this->isReady())) return;

    unsigned int currentProgram = GLStateCache::getCurrentProgram();
    if(currentProgram == this->iProgram) return;  // already active

    this->iProgramBackup = currentProgram;
    MCglUseProgramObject(this->iProgram);
    GLStateCache::setCurrentProgram(this->iProgram);
}

void OpenGLShader::disable() {
    if(unlikely(!this->isReady())) return;

    MCglUseProgramObject(this->iProgramBackup);

    // update cache
    GLStateCache::setCurrentProgram(this->iProgramBackup);
}

void OpenGLShader::writeUniform(std::string_view name, UniformType type, const void *const data, u32 dataSize) {
    if(unlikely(!this->isReady())) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }

    switch(type) {
        using enum Shader::UniformType;
        case UNI_1F:
            MCglUniform1f(id, *static_cast<const float *>(data));
            break;
        case UNI_1FV:
            MCglUniform1fv(id, static_cast<int>(dataSize / sizeof(float)), static_cast<const float *>(data));
            break;
        case UNI_1I:
            MCglUniform1i(id, *static_cast<const int *>(data));
            break;
        case UNI_2F:
            MCglUniform2f(id, static_cast<const float *>(data)[0], static_cast<const float *>(data)[1]);
            break;
        case UNI_2FV:
            MCglUniform2fv(id, static_cast<int>(dataSize / sizeof(float) / 2), static_cast<const float *>(data));
            break;
        case UNI_3F:
            MCglUniform3f(id, static_cast<const float *>(data)[0], static_cast<const float *>(data)[1],
                          static_cast<const float *>(data)[2]);
            break;
        case UNI_3FV:
            MCglUniform3fv(id, static_cast<int>(dataSize / sizeof(float) / 3), static_cast<const float *>(data));
            break;
        case UNI_4F:
            MCglUniform4f(id, static_cast<const float *>(data)[0], static_cast<const float *>(data)[1],
                          static_cast<const float *>(data)[2], static_cast<const float *>(data)[3]);
            break;
        case UNI_MATRIX4FV:
            MCglUniformMatrix4fv(id, 1, GL_FALSE, static_cast<const float *>(data));
            break;
        default:
            debugLog("OpenGLShader ERROR: unhandled type {} name {}", (u32)type, name);
            break;
    }
}

int OpenGLShader::getAttribLocation(std::string_view name) {
    if(unlikely(!this->isReady()) || name.empty()) return -1;

    return MCglGetAttribLocation(this->iProgram, std::string{name}.c_str());
}

int OpenGLShader::getAndCacheUniformLocation(std::string_view name) {
    if(unlikely(!this->isReady()) || name.empty()) return -1;

    const auto cachedValue = this->uniformLocationCache.find(name);
    const bool cached = (cachedValue != this->uniformLocationCache.end());

    const int id = (cached ? cachedValue->second : MCglGetUniformLocation(this->iProgram, std::string{name}.c_str()));
    if(!cached && id != -1) this->uniformLocationCache.emplace(name, id);

    return id;
}

bool OpenGLShader::compile(const std::string &vertexShader, const std::string &fragmentShader, bool source) {
    // load & compile shaders
    debugLog("Compiling {:s} ...", (source ? "vertex source" : vertexShader));
    this->iVertexShader = source ? createShaderFromString(vertexShader, GL_VERTEX_SHADER_ARB)
                                 : createShaderFromFile(vertexShader, GL_VERTEX_SHADER_ARB);
    debugLog("Compiling {:s} ...", (source ? "fragment source" : fragmentShader));
    this->iFragmentShader = source ? createShaderFromString(fragmentShader, GL_FRAGMENT_SHADER_ARB)
                                   : createShaderFromFile(fragmentShader, GL_FRAGMENT_SHADER_ARB);

    const bool showDebugPopup = cv::debug_shaders.getBool() || Env::cfg(BUILD::DEBUG);
    if(this->iVertexShader == 0 || this->iFragmentShader == 0) {
        if(showDebugPopup) {
            engine->showMessageError("OpenGLShader Error", "Couldn't createShader()");
        } else {
            debugLog("Couldn't createShader()");
        }
        return false;
    }

    // create program
    this->iProgram = MCglCreateProgramObject();
    if(this->iProgram == 0) {
        if(showDebugPopup) {
            engine->showMessageError("OpenGLShader Error", "Couldn't glCreateProgram()");
        } else {
            debugLog("Couldn't glCreateProgram()");
        }
        return false;
    }

    // attach
    MCglAttachObject(this->iProgram, this->iVertexShader);
    MCglAttachObject(this->iProgram, this->iFragmentShader);

    // link
    MCglLinkProgram(this->iProgram);

    int returnValue = GL_TRUE;
    MCglGetObjectParameteriv(this->iProgram, GL_OBJECT_LINK_STATUS_ARB, &returnValue);
    if(returnValue == GL_FALSE) {
        if(showDebugPopup) {
            engine->showMessageError("OpenGLShader Error", "Couldn't glLinkProgram()");
        } else {
            debugLog("Couldn't glLinkProgram()");
        }
        return false;
    }

    return true;
}

int OpenGLShader::createShaderFromString(std::string shaderSource, int shaderType) {
    const auto shader = MCglCreateShaderObject(shaderType);

    if(shader == 0) {
        engine->showMessageError("OpenGLShader Error", "Couldn't glCreateShaderObjectARB()");
        return 0;
    }

    if(size_t pos = shaderSource.find("{RUNTIME_VERSION}"sv); pos != std::string::npos) {
        shaderSource.replace(pos, "{RUNTIME_VERSION}"sv.length(), "110");
    }

    // compile shader
    const char *shaderSourceChar = shaderSource.c_str();
    MCglShaderSource(shader, 1, &shaderSourceChar, nullptr);
    MCglCompileShader(shader);

    int returnValue = GL_TRUE;
    MCglGetObjectParameteriv(shader, GL_OBJECT_COMPILE_STATUS_ARB, &returnValue);

    if(returnValue == GL_FALSE) {
        debugLog("------------------OpenGLShader Compile Error------------------");

        MCglGetObjectParameteriv(shader, GL_OBJECT_INFO_LOG_LENGTH_ARB, &returnValue);

        if(returnValue > 0) {
            char *errorLog = new char[returnValue];
            MCglGetInfoLog(shader, returnValue, &returnValue, errorLog);
            logRaw("{}", errorLog);
            delete[] errorLog;
        }

        debugLog("--------------------------------------------------------------");

        engine->showMessageError("OpenGLShader Error", "Couldn't glShaderSourceARB() or glCompileShaderARB()");
        return 0;
    }

    return static_cast<int>(shader);
}

int OpenGLShader::createShaderFromFile(const std::string &fileName, int shaderType) {
    // load file
    File inFile(fileName);
    if(!inFile.canRead()) {
        engine->showMessageError("OpenGLShader Error", fileName.c_str());
        return 0;
    }
    return createShaderFromString(inFile.readToString(), shaderType);
}

#endif
