//================ Copyright (c) 2012, PG, All rights reserved. =================//
//
// Purpose:		shader wrapper
//
// $NoKeywords: $shader
//===============================================================================//

#include "Shader.h"
#include "SString.h"
#include "Engine.h"
#include "UString.h"

#include <sstream>

Shader::SHADER_PARSE_RESULT Shader::parseShaderFromString(const std::string &graphicsInterfaceAndShaderTypePrefix,
                                                          const std::string &shaderSource) {
    SHADER_PARSE_RESULT result;

    // e.g. ###OpenGLInterface::VertexShader##########################################################################################
    const std::string_view shaderPrefix = "###";
    // e.g. ##D3D11_BUFFER_DESC::D3D11_BIND_CONSTANT_BUFFER::ModelViewProjectionConstantBuffer::mvp::float4x4
    const std::string_view descPrefix = "##";

    std::istringstream ss(shaderSource);

    bool foundGraphicsInterfaceAndShaderTypePrefixAtLeastOnce = false;
    bool foundGraphicsInterfaceAndShaderTypePrefix = false;
    std::string curLine;

    while(!!std::getline(ss, curLine)) {
        SString::trim_inplace(curLine);  // remove CRLF
        const bool isShaderPrefixLine = (curLine.contains(shaderPrefix));

        if(isShaderPrefixLine) {
            if(!foundGraphicsInterfaceAndShaderTypePrefix) {
                if(curLine.find(graphicsInterfaceAndShaderTypePrefix) == shaderPrefix.length()) {
                    foundGraphicsInterfaceAndShaderTypePrefix = true;
                    foundGraphicsInterfaceAndShaderTypePrefixAtLeastOnce = true;
                }
            } else
                foundGraphicsInterfaceAndShaderTypePrefix = false;
        } else if(foundGraphicsInterfaceAndShaderTypePrefix) {
            const bool isDescPrefixLine = (curLine.starts_with(descPrefix));

            if(!isDescPrefixLine) {
                if(!result.source.empty()) result.source.push_back('\n');
                result.source.append(curLine);
            } else {
                result.descs.push_back(curLine.substr(descPrefix.length()));
            }
        }
    }

    if(!foundGraphicsInterfaceAndShaderTypePrefixAtLeastOnce)
        engine->showMessageError(
            "Shader Error", fmt::format("Missing \"{}\" in shader {}", graphicsInterfaceAndShaderTypePrefix.c_str(),
                                        shaderSource.c_str()));

    return result;
}
