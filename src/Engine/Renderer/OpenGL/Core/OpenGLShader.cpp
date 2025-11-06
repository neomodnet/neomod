// Copyright (c) 2016, PG, All rights reserved.
#include "config.h"

#ifdef MCENGINE_FEATURE_OPENGL
#include "OpenGLShader.h"

#include "OpenGLHeaders.h"
#include "OpenGLStateCache.h"

#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"

#include <fstream>

OpenGLShader::OpenGLShader(std::string vertexShader, std::string fragmentShader, bool source)
    : Shader(), sVsh(std::move(vertexShader)), sFsh(std::move(fragmentShader)), bSource(source) {
    this->iProgram = 0;
    this->iVertexShader = 0;
    this->iFragmentShader = 0;

    this->iProgramBackup = 0;
}

void OpenGLShader::init() { this->setReady(this->compile(this->sVsh, this->sFsh, this->bSource)); }

void OpenGLShader::initAsync() { this->setAsyncReady(true); }

void OpenGLShader::destroy() {
    if(this->iProgram != 0) glDeleteProgram(this->iProgram);
    if(this->iFragmentShader != 0) glDeleteShader(this->iFragmentShader);
    if(this->iVertexShader != 0) glDeleteShader(this->iVertexShader);

    this->iProgram = 0;
    this->iFragmentShader = 0;
    this->iVertexShader = 0;

    this->iProgramBackup = 0;

    this->uniformLocationCache.clear();
}

void OpenGLShader::enable() {
    if(!this->isReady()) return;

    auto currentProgram = GLStateCache::getCurrentProgram();
    if(currentProgram == this->iProgram) return;  // already active

    this->iProgramBackup = currentProgram;
    glUseProgram(this->iProgram);
    GLStateCache::setCurrentProgram(this->iProgram);
}

void OpenGLShader::disable() {
    if(!this->isReady()) return;

    glUseProgram(this->iProgramBackup);

    // update cache
    GLStateCache::setCurrentProgram(this->iProgramBackup);
}

void OpenGLShader::setUniform1f(std::string_view name, float value) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniform1fARB(id, value);
}

void OpenGLShader::setUniform1fv(std::string_view name, int count, const float *const values) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniform1fvARB(id, count, values);
}

void OpenGLShader::setUniform1i(std::string_view name, int value) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniform1iARB(id, value);
}

void OpenGLShader::setUniform2f(std::string_view name, float value1, float value2) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniform2fARB(id, value1, value2);
}

void OpenGLShader::setUniform2fv(std::string_view name, int count, const float *const vectors) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniform2fv(id, count, &vectors[0]);
}

void OpenGLShader::setUniform3f(std::string_view name, float x, float y, float z) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniform3fARB(id, x, y, z);
}

void OpenGLShader::setUniform3fv(std::string_view name, int count, const float *const vectors) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniform3fv(id, count, &vectors[0]);
}

void OpenGLShader::setUniform4f(std::string_view name, float x, float y, float z, float w) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniform4fARB(id, x, y, z, w);
}

void OpenGLShader::setUniformMatrix4fv(std::string_view name, const Matrix4 &matrix) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniformMatrix4fv(id, 1, GL_FALSE, matrix.get());
}

void OpenGLShader::setUniformMatrix4fv(std::string_view name, const float *const v) {
    if(!this->isReady()) return;

    const int id = getAndCacheUniformLocation(name);
    if(id == -1) {
        logIfCV(debug_shaders, "OpenGLShader Warning: Can't find uniform {:s}", name);
        return;
    }
    glUniformMatrix4fv(id, 1, GL_FALSE, v);
}

int OpenGLShader::getAttribLocation(std::string_view name) {
    if(!this->isReady() || name.empty()) return -1;

    return glGetAttribLocation(this->iProgram, name.data());
}

int OpenGLShader::getAndCacheUniformLocation(std::string_view name) {
    if(!this->isReady() || name.empty()) return -1;

    const auto cachedValue = this->uniformLocationCache.find(name);
    const bool cached = (cachedValue != this->uniformLocationCache.end());

    const int id = (cached ? cachedValue->second : glGetUniformLocation(this->iProgram, name.data()));
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

    if(this->iVertexShader == 0 || this->iFragmentShader == 0) {
        engine->showMessageError("OpenGLShader Error", "Couldn't createShader()");
        return false;
    }

    // create program
    this->iProgram = glCreateProgram();
    if(this->iProgram == 0) {
        engine->showMessageError("OpenGLShader Error", "Couldn't glCreateProgram()");
        return false;
    }

    // attach
    glAttachShader(this->iProgram, this->iVertexShader);
    glAttachShader(this->iProgram, this->iFragmentShader);

    // link
    glLinkProgram(this->iProgram);

    int returnValue = GL_TRUE;
    glGetProgramiv(this->iProgram, GL_OBJECT_LINK_STATUS_ARB, &returnValue);
    if(returnValue == GL_FALSE) {
        engine->showMessageError("OpenGLShader Error", "Couldn't glLinkProgram()");
        return false;
    }

    // validate
    glValidateProgram(this->iProgram);
    returnValue = GL_TRUE;
    glGetProgramiv(this->iProgram, GL_OBJECT_VALIDATE_STATUS_ARB, &returnValue);
    if(returnValue == GL_FALSE) {
        engine->showMessageError("OpenGLShader Error", "Couldn't glValidateProgram()");
        return false;
    }

    return true;
}

int OpenGLShader::createShaderFromString(std::string shaderSource, int shaderType) {
    const auto shader = glCreateShader(shaderType);
    if(shader == 0) {
        engine->showMessageError("OpenGLShader Error", "Couldn't glCreateShader()");
        return 0;
    }

    size_t pos = shaderSource.find("{RUNTIME_VERSION}"sv);
    if(pos != std::string::npos) {
        shaderSource.replace(pos, "{RUNTIME_VERSION}"sv.length(), "110");
    }

    // compile shader
    const char *shaderSourceChar = shaderSource.c_str();
    glShaderSource(shader, 1, &shaderSourceChar, nullptr);
    glCompileShader(shader);

    int returnValue = GL_TRUE;
    glGetProgramiv(shader, GL_OBJECT_COMPILE_STATUS_ARB, &returnValue);

    if(returnValue == GL_FALSE) {
        debugLog("------------------OpenGLShader Compile Error------------------");

        glGetProgramiv(shader, GL_OBJECT_INFO_LOG_LENGTH_ARB, &returnValue);

        if(returnValue > 0) {
            char *errorLog = new char[returnValue];
            glGetShaderInfoLog(shader, returnValue, &returnValue, errorLog);
            logRaw("{}", errorLog);
            delete[] errorLog;
        }

        debugLog("--------------------------------------------------------------");

        engine->showMessageError("OpenGLShader Error", "Couldn't glShaderSource() or glCompileShader()");
        return 0;
    }

    return static_cast<int>(shader);
}

int OpenGLShader::createShaderFromFile(const std::string &fileName, int shaderType) {
    // load file
    std::ifstream inFile(fileName);
    if(!inFile) {
        engine->showMessageError("OpenGLShader Error", fileName.c_str());
        return 0;
    }
    std::string line;
    std::string shaderSource;
    // int linecount = 0;
    while(inFile.good()) {
        std::getline(inFile, line);
        shaderSource += line + "\n\0";
        // linecount++;
    }
    shaderSource += "\n\0";
    inFile.close();

    return createShaderFromString(shaderSource.c_str(), shaderType);
}

#endif
