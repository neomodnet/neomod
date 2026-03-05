//================ Copyright (c) 2025, WH, All rights reserved. =================//
//
// Purpose:		raw opengl es 3.2 graphics interface
//
// $NoKeywords: $gles32i
//===============================================================================//

#include "OpenGLES32Interface.h"

#ifdef MCENGINE_FEATURE_GLES32

#include "Camera.h"
#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"
#include "FixedSizeArray.h"

#include "Font.h"
#include "OpenGLES32Shader.h"
#include "OpenGLES32VertexArrayObject.h"
#include "OpenGLImage.h"
#include "OpenGLHeaders.h"
#include "OpenGLRenderTarget.h"
#include "OpenGLStateCache.h"

#include "SDLGLInterface.h"

#include "binary_embed.h"

OpenGLES32Interface::OpenGLES32Interface() : ModernGraphicsShared(), m_vResolution(engine->getScreenSize()) {
    // renderer
    m_bInScene = false;

    m_shaderTexturedGeneric = nullptr;
    m_iShaderTexturedGenericPrevType = 0;
    m_iShaderTexturedGenericAttribPosition = 0;
    m_iShaderTexturedGenericAttribUV = 1;
    m_iShaderTexturedGenericAttribCol = 2;
    m_iVBOVertices = 0;
    m_iVBOTexcoords = 0;
    m_iVBOTexcolors = 0;

    // persistent vars
    m_color = 0xffffffff;
    m_bAntiAliasing = true;

    // enable
    glEnable(GL_BLEND);

    // disable
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);

    // this should probably be GL_FRAMEBUFFER_SRGB_EXT or something but
    // this is only here to work around bugs on windows anyways
#ifndef MCENGINE_PLATFORM_WASM
    glDisable(GL_FRAMEBUFFER_SRGB);
#endif

    // blending
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    // culling
    glFrontFace(GL_CCW);

    // debugging
#ifndef MCENGINE_PLATFORM_WASM
    // GLES 3.2 has debug functions as core, but WebGL doesn't support them
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(SDLGLInterface::glDebugCB, nullptr);
#endif

    // setWireframe(true);

    constexpr auto texturedGenericV = R"(#version 100

attribute vec3 position;
attribute vec2 uv;
attribute vec4 vcolor;
attribute vec3 normal;

varying vec2 texcoords;
varying vec4 texcolor;
varying vec3 texnormal;

uniform float type;  // 0=no texture, 1=texture, 2=texture+color, 3=texture+normal
uniform mat4 mvp;
uniform mat3 normalMatrix;

void main() {
	texcoords = uv;
	texcolor = vcolor;
	texnormal = normal;

	// apply normal matrix if we're using normals
	if (type >= 3.0) {
		texnormal = normalMatrix * normal;
	}

	gl_Position = mvp * vec4(position, 1.0);
}
)";

    constexpr auto texturedGenericP = R"(#version 100
precision highp float;

varying vec2 texcoords;
varying vec4 texcolor;
varying vec3 texnormal;

uniform float type;  // 0=no texture, 1=texture, 2=texture+color, 3=texture+normal
uniform vec4 col;
uniform sampler2D tex;
uniform vec3 lightDir;  // normalized direction to light source
uniform float inv;

void main() {
	// base color calculation
	vec4 baseColor;

	if (type < 0.5) { // no texture
		baseColor = col;
	} else if (type < 1.5) { // texture with uniform color
		baseColor = texture2D(tex, texcoords) * col;
	} else if (type < 2.5) { // vertex color only
		baseColor = texcolor;
	} else if (type < 3.5) { // normal mapping
		baseColor = mix(col, mix(texture2D(tex, texcoords) * col, texcolor, clamp(type - 1.0, 0.0, 1.0)), clamp(type, 0.0, 1.0));

		// lighting effects for normal mapping
		if (length(texnormal) > 0.01) {
			vec3 N = normalize(texnormal);
			float diffuse = max(dot(N, lightDir), 0.3); // ambient component
			baseColor.rgb *= diffuse;
		}
	} else {
		// texture with vertex color (for text rendering)
		baseColor = texture2D(tex, texcoords) * texcolor;
	}
	if (inv > 0.5) {
		baseColor.rgb = vec3(1.0) - baseColor.rgb;
	}

	gl_FragColor = baseColor;
}
)";
    m_shaderTexturedGeneric = (OpenGLES32Shader *)createShaderFromSource(texturedGenericV, texturedGenericP);
    m_shaderTexturedGeneric->load();

    glGenBuffers(1, &m_iVBOVertices);
    glGenBuffers(1, &m_iVBOTexcoords);
    glGenBuffers(1, &m_iVBOTexcolors);

    m_iShaderTexturedGenericAttribPosition = m_shaderTexturedGeneric->getAttribLocation("position");
    m_iShaderTexturedGenericAttribUV = m_shaderTexturedGeneric->getAttribLocation("uv");
    m_iShaderTexturedGenericAttribCol = m_shaderTexturedGeneric->getAttribLocation("vcolor");

    // TODO: handle cases where more than 16384 elements are in an unbaked vao
    glBindBuffer(GL_ARRAY_BUFFER, m_iVBOVertices);
    glVertexAttribPointer(m_iShaderTexturedGenericAttribPosition, 3, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);
    glBufferData(GL_ARRAY_BUFFER, 16384 * sizeof(vec3), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(m_iShaderTexturedGenericAttribPosition);

    glBindBuffer(GL_ARRAY_BUFFER, m_iVBOTexcoords);
    glVertexAttribPointer(m_iShaderTexturedGenericAttribUV, 2, GL_FLOAT, GL_FALSE, 0, (GLvoid *)0);
    glBufferData(GL_ARRAY_BUFFER, 16384 * sizeof(vec2), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(m_iShaderTexturedGenericAttribUV);

    glBindBuffer(GL_ARRAY_BUFFER, m_iVBOTexcolors);
    glVertexAttribPointer(m_iShaderTexturedGenericAttribCol, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (GLvoid *)0);
    glBufferData(GL_ARRAY_BUFFER, 16384 * sizeof(vec4), nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(m_iShaderTexturedGenericAttribCol);

    // init
    m_shaderTexturedGeneric->setUniform1f("inv", 0.f);

    // initialize the state cache
    GLStateCache::initialize();
}

OpenGLES32Interface::~OpenGLES32Interface() {
    SAFE_DELETE(m_shaderTexturedGeneric);

    if(m_iVBOVertices != 0) glDeleteBuffers(1, &m_iVBOVertices);
    if(m_iVBOTexcoords != 0) glDeleteBuffers(1, &m_iVBOTexcoords);
    if(m_iVBOTexcolors != 0) glDeleteBuffers(1, &m_iVBOTexcolors);

    m_iVBOVertices = 0;
    m_iVBOTexcoords = 0;
    m_iVBOTexcolors = 0;
}

void OpenGLES32Interface::beginScene() {
    m_bInScene = true;

    // enable default shader (must happen before any uniform calls)
    m_shaderTexturedGeneric->enable();

    Matrix4 defaultProjectionMatrix = Camera::buildMatrixOrtho2D(0, m_vResolution.x, m_vResolution.y, 0, -1.0f, 1.0f);

    // push main transforms
    pushTransform();
    setProjectionMatrix(defaultProjectionMatrix);
    translate(cv::r_globaloffset_x.getFloat(), cv::r_globaloffset_y.getFloat());

    // and apply them
    updateTransform();

    // set clear color and clear
    // glClearColor(1, 1, 1, 1);
    // glClearColor(0.9568f, 0.9686f, 0.9882f, 1);
    glClearColor(0, 0, 0, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // display any errors of previous frames
    handleGLErrors();
}

void OpenGLES32Interface::endScene() {
    popTransform();

    checkStackLeaks();

    this->processPendingScreenshot();

    if(m_clipRectStack.size() > 0) {
        engine->showMessageErrorFatal("ClipRect Stack Leak", "Make sure all push*() have a pop*()!");
        engine->shutdown();
    }

    m_bInScene = false;
}

void OpenGLES32Interface::clearDepthBuffer() { glClear(GL_DEPTH_BUFFER_BIT); }

void OpenGLES32Interface::setColor(Color color) {
    if(color == m_color) return;

    if(m_shaderTexturedGeneric->isActive()) {
        m_color = color;
        m_shaderTexturedGeneric->setUniform4f("col", color.Rf(), color.Gf(), color.Bf(),
                                              color.Af());  // float components of color
    }
}

void OpenGLES32Interface::setAlpha(float alpha) { setColor(rgba(m_color.Rf(), m_color.Gf(), m_color.Bf(), alpha)); }

void OpenGLES32Interface::drawImage(const Image *image, AnchorPoint anchor, float edgeSoftness, McRect clipRect) {
    // skip entirely transparent images or if the current transparency is disabled
    if(image == nullptr || !image->isGPUReady() || m_color.A() == 0) {
        if(image && cv::r_debug_drawimage.getBool()) {
            const vec2 size = image->getSize();
            const vec2 pos = getAnchoredOrigin(anchor, size);
            this->setColor(0xbbff00ff);
            Graphics::drawRectf(pos.x, pos.y, size.x, size.y);
        }
        return;
    }

    const bool clipRectSpecified = vec::length(clipRect.getSize()) != 0;
    bool smoothedEdges = edgeSoftness > 0.0f;

    // initialize shader on first use
    if(smoothedEdges) {
        if(!this->smoothClipShader) {
            this->initSmoothClipShader();
        }
        smoothedEdges = this->smoothClipShader->isReady();
    }

    const bool fallbackClip = clipRectSpecified && !smoothedEdges;

    if(fallbackClip) {
        pushClipRect(clipRect);
    }

    this->updateTransform();

    const vec2 size = image->getSize();
    const vec2 pos = getAnchoredOrigin(anchor, size);
    const auto [x, y, width, height] = std::tuple{pos.x, pos.y, size.x, size.y};

    if(smoothedEdges && !clipRectSpecified) {
        // set a default clip rect as the exact image size if one wasn't explicitly passed, but we still want smoothing
        clipRect = McRect{pos, size};
    }

    if(smoothedEdges) {
        // compensate for viewport changed by rendertargets
        // flip Y for engine<->opengl coordinate origin
        const auto &viewport{GLStateCache::getCurrentViewport()};
        float clipMinX{clipRect.getX() + viewport[0]},                                           //
            clipMinY{viewport[3] - (clipRect.getY() - viewport[1] - 1 + clipRect.getHeight())},  //
            clipMaxX{clipMinX + clipRect.getWidth()},                                            //
            clipMaxY{clipMinY + clipRect.getHeight()};                                           //

        this->smoothClipShader->enable();
        this->smoothClipShader->setUniform2f("rect_min", clipMinX, clipMinY);
        this->smoothClipShader->setUniform2f("rect_max", clipMaxX, clipMaxY);
        this->smoothClipShader->setUniform1f("edge_softness", edgeSoftness);
        this->smoothClipShader->setUniform4f("col", m_color.Rf(), m_color.Gf(), m_color.Bf(), m_color.Af());

        // set mvp for the shader
        this->smoothClipShader->setUniformMatrix4fv("mvp", this->MP);
    }

    static VertexArrayObject vao(DrawPrimitive::TRIANGLE_STRIP);
    vao.clear();
    {
        vao.addVertex(x, y);
        vao.addTexcoord(0, 0);
        vao.addVertex(x, y + height);
        vao.addTexcoord(0, 1);
        vao.addVertex(x + width, y);
        vao.addTexcoord(1, 0);
        vao.addVertex(x + width, y + height);
        vao.addTexcoord(1, 1);
    }
    image->bind();
    {
        drawVAO(&vao);
    }
    image->unbind();

    if(smoothedEdges) {
        this->smoothClipShader->disable();
    } else if(fallbackClip) {
        popClipRect();
    }

    if(cv::r_debug_drawimage.getBool()) {
        this->setColor(0xbbff00ff);
        this->drawRect(x, y, width - 1, height - 1);
    }
}

void OpenGLES32Interface::drawString(McFont *font, const UString &text, std::optional<TextShadow> shadow) {
    if(font == nullptr || text.length() < 1 || !font->isReady()) return;

    updateTransform();

    font->drawString(text, shadow);
}

void OpenGLES32Interface::drawVAO(VertexArrayObject *vao) {
    if(vao == nullptr) return;

    updateTransform();

    // if baked, then we can directly draw the buffer
    if(vao->isReady()) {
        auto *glvao = static_cast<OpenGLES32VertexArrayObject *>(vao);

        // configure shader
        if(m_shaderTexturedGeneric->isActive()) {
            // both texcoords and colors (e.g., text rendering)
            if(glvao->getNumTexcoords0() > 0 && glvao->getNumColors() > 0) {
                if(m_iShaderTexturedGenericPrevType != 4) {
                    m_shaderTexturedGeneric->setUniform1f("type", 4.0f);
                    m_iShaderTexturedGenericPrevType = 4;
                }
            }
            // texcoords
            else if(glvao->getNumTexcoords0() > 0) {
                if(m_iShaderTexturedGenericPrevType != 1) {
                    m_shaderTexturedGeneric->setUniform1f("type", 1.0f);
                    m_iShaderTexturedGenericPrevType = 1;
                }
            }
            // colors
            else if(glvao->getNumColors() > 0) {
                if(m_iShaderTexturedGenericPrevType != 2) {
                    m_shaderTexturedGeneric->setUniform1f("type", 2.0f);
                    m_iShaderTexturedGenericPrevType = 2;
                }
            }
            // neither
            else if(m_iShaderTexturedGenericPrevType != 0) {
                m_shaderTexturedGeneric->setUniform1f("type", 0.0f);
                m_iShaderTexturedGenericPrevType = 0;
            }
        }

        // draw
        glvao->draw();
        return;
    }

    const std::vector<vec3> &vertices = vao->getVertices();
    [[maybe_unused]] const std::vector<vec3> &normals = vao->getNormals();
    const std::vector<vec2> &texcoords = vao->getTexcoords();
    const std::vector<Color> &vcolors = vao->getColors();

    if(vertices.size() < 2) return;

    // TODO: separate draw for non-quads, update quad draws to triangle draws to avoid rewrite overhead here

    // no support for quads, because fuck you
    // rewrite all quads into triangles
    std::vector<vec3> finalVertices = vertices;
    std::vector<vec2> finalTexcoords = texcoords;
    std::vector<Color> colors;
    std::vector<Color> finalColors;

    if(!vcolors.empty()) {
        // check if any color needs conversion (only R and B are swapped)
        bool needsConversion = false;
        MC_UNROLL
        for(auto color : vcolors) {
            if(color.R() != color.B()) {
                needsConversion = true;
                break;
            }
        }
        if(needsConversion) {
            colors.reserve(vcolors.size());
            MC_UNROLL
            for(auto color : vcolors) {
                colors.push_back(abgr(color));
            }
        } else {
            colors = vcolors;
        }
        finalColors = colors;
    }

    int maxColorIndex = colors.size() - 1;

    DrawPrimitive primitive = vao->getPrimitive();
    if(primitive == DrawPrimitive::QUADS) {
        finalVertices.clear();
        finalTexcoords.clear();
        finalColors.clear();
        primitive = DrawPrimitive::TRIANGLES;

        if(vertices.size() > 3) {
            for(size_t i = 0; i < vertices.size(); i += 4) {
                finalVertices.push_back(vertices[i + 0]);
                finalVertices.push_back(vertices[i + 1]);
                finalVertices.push_back(vertices[i + 2]);

                if(!texcoords.empty()) {
                    finalTexcoords.push_back(texcoords[i + 0]);
                    finalTexcoords.push_back(texcoords[i + 1]);
                    finalTexcoords.push_back(texcoords[i + 2]);
                }

                if(colors.size() > 0) {
                    finalColors.push_back(colors[std::clamp<size_t>(i + 0, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 1, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 2, 0, maxColorIndex)]);
                }

                finalVertices.push_back(vertices[i + 0]);
                finalVertices.push_back(vertices[i + 2]);
                finalVertices.push_back(vertices[i + 3]);

                if(!texcoords.empty()) {
                    finalTexcoords.push_back(texcoords[i + 0]);
                    finalTexcoords.push_back(texcoords[i + 2]);
                    finalTexcoords.push_back(texcoords[i + 3]);
                }

                if(colors.size() > 0) {
                    finalColors.push_back(colors[std::clamp<size_t>(i + 0, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 2, 0, maxColorIndex)]);
                    finalColors.push_back(colors[std::clamp<size_t>(i + 3, 0, maxColorIndex)]);
                }
            }
        }
    }

    const bool orphanBuffers = cv::r_gles_orphan_buffers.getBool();

    // upload vertices to gpu
    if(finalVertices.size() > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m_iVBOVertices);
        if(orphanBuffers)
            glBufferData(GL_ARRAY_BUFFER, 16384 * sizeof(vec3), nullptr, GL_STREAM_DRAW);  // orphan the buffer
        glBufferSubData(GL_ARRAY_BUFFER, 0, finalVertices.size() * sizeof(vec3), &(finalVertices[0]));
    }

    // upload texcoords to gpu
    if(finalTexcoords.size() > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m_iVBOTexcoords);
        if(orphanBuffers)
            glBufferData(GL_ARRAY_BUFFER, 16384 * sizeof(vec2), nullptr, GL_STREAM_DRAW);  // orphan the buffer
        glBufferSubData(GL_ARRAY_BUFFER, 0, finalTexcoords.size() * sizeof(vec2), &(finalTexcoords[0]));
    }

    // upload vertex colors to gpu
    if(finalColors.size() > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, m_iVBOTexcolors);
        if(orphanBuffers)
            glBufferData(GL_ARRAY_BUFFER, 16384 * sizeof(vec4), nullptr, GL_STREAM_DRAW);  // orphan the buffer
        glBufferSubData(GL_ARRAY_BUFFER, 0, finalColors.size() * sizeof(Color), &(finalColors[0]));
    }

    // configure shader
    if(m_shaderTexturedGeneric->isActive()) {
        // texcoords and colors (e.g., text rendering)
        if(finalTexcoords.size() > 0 && finalColors.size() > 0) {
            if(m_iShaderTexturedGenericPrevType != 4) {
                m_shaderTexturedGeneric->setUniform1f("type", 4.0f);
                m_iShaderTexturedGenericPrevType = 4;
            }
        }
        // texcoords
        else if(finalTexcoords.size() > 0) {
            if(m_iShaderTexturedGenericPrevType != 1) {
                m_shaderTexturedGeneric->setUniform1f("type", 1.0f);
                m_iShaderTexturedGenericPrevType = 1;
            }
        }
        // colors
        else if(finalColors.size() > 0) {
            if(m_iShaderTexturedGenericPrevType != 2) {
                m_shaderTexturedGeneric->setUniform1f("type", 2.0f);
                m_iShaderTexturedGenericPrevType = 2;
            }
        }
        // neither
        else if(m_iShaderTexturedGenericPrevType != 0) {
            m_shaderTexturedGeneric->setUniform1f("type", 0.0f);
            m_iShaderTexturedGenericPrevType = 0;
        }
    }

    // draw it
    glDrawArrays(SDLGLInterface::primitiveToOpenGLMap[primitive], 0, finalVertices.size());
}

void OpenGLES32Interface::setClipRect(McRect clipRect) {
    if(cv::r_debug_disable_cliprect.getBool()) return;
    // if (m_bIs3DScene) return; // TODO

    // rendertargets change the current viewport
    const auto &viewport{GLStateCache::getCurrentViewport()};

    // debugLog("viewport = {}, {}, {}, {}", viewport[0], viewport[1], viewport[2], viewport[3]);

    glEnable(GL_SCISSOR_TEST);
    glScissor((int)clipRect.getX() + viewport[0],
              viewport[3] - ((int)clipRect.getY() - viewport[1] - 1 + (int)clipRect.getHeight()),
              (int)clipRect.getWidth(), (int)clipRect.getHeight());

    // debugLog("scissor = {}, {}, {}, {}", (int)clipRect.getX()+viewport[0],
}

void OpenGLES32Interface::pushClipRect(McRect clipRect) {
    if(m_clipRectStack.size() > 0)
        m_clipRectStack.push_back(m_clipRectStack.back().intersect(clipRect));
    else
        m_clipRectStack.push_back(clipRect);

    setClipRect(m_clipRectStack.back());
}

void OpenGLES32Interface::popClipRect() {
    m_clipRectStack.pop_back();

    if(m_clipRectStack.size() > 0)
        setClipRect(m_clipRectStack.back());
    else
        setClipping(false);
}

void OpenGLES32Interface::pushViewport() {
    this->viewportStack.push_back(GLStateCache::getCurrentViewport());
    this->resolutionStack.push_back(m_vResolution);
}

void OpenGLES32Interface::setViewport(int x, int y, int width, int height) {
    m_vResolution = vec2(width, height);
    GLStateCache::setViewport(x, y, width, height);
}

void OpenGLES32Interface::popViewport() {
    if(this->viewportStack.empty() || this->resolutionStack.empty()) {
        debugLog("WARNING: viewport stack underflow!");
        return;
    }

    m_vResolution = this->resolutionStack.back();
    this->resolutionStack.pop_back();

    GLStateCache::setViewport(this->viewportStack.back());
    this->viewportStack.pop_back();
}

void OpenGLES32Interface::pushStencil() {
    // init and clear
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);

    // set mask
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 1, 1);
    glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
}

void OpenGLES32Interface::fillStencil(bool inside) {
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_NOTEQUAL, inside ? 0 : 1, 1);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void OpenGLES32Interface::popStencil() { glDisable(GL_STENCIL_TEST); }

void OpenGLES32Interface::setClipping(bool enabled) {
    if(enabled) {
        if(m_clipRectStack.size() > 0) glEnable(GL_SCISSOR_TEST);
    } else
        glDisable(GL_SCISSOR_TEST);
}

#ifndef MCENGINE_PLATFORM_WASM
void OpenGLES32Interface::setAlphaTesting(bool enabled) {
    if(enabled)
        glEnable(GL_ALPHA_TEST);
    else
        glDisable(GL_ALPHA_TEST);
}

void OpenGLES32Interface::setAlphaTestFunc(DrawCompareFunc alphaFunc, float ref) {
    glAlphaFunc(SDLGLInterface::compareFuncToOpenGLMap[alphaFunc], ref);
}

void OpenGLES32Interface::setAntialiasing(bool aa) {
    m_bAntiAliasing = aa;
    if(aa)
        glEnable(GL_MULTISAMPLE);
    else
        glDisable(GL_MULTISAMPLE);
}
#else
// WebGL doesn't support alpha testing or multisampling, provide stubs
void OpenGLES32Interface::setAlphaTesting(bool /*enabled*/) {}
void OpenGLES32Interface::setAlphaTestFunc(DrawCompareFunc /*alphaFunc*/, float /*ref*/) {}
void OpenGLES32Interface::setAntialiasing(bool /*aa*/) {}
#endif

void OpenGLES32Interface::setBlending(bool enabled) {
    Graphics::setBlending(enabled);

    if(enabled)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
}

void OpenGLES32Interface::setBlendMode(DrawBlendMode blendMode) {
    Graphics::setBlendMode(blendMode);

    switch(blendMode) {
        case DrawBlendMode::ALPHA:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case DrawBlendMode::ADDITIVE:
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case DrawBlendMode::PREMUL_ALPHA:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case DrawBlendMode::PREMUL_COLOR:
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }
}

void OpenGLES32Interface::setDepthBuffer(bool enabled) {
    if(enabled)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);
}

void OpenGLES32Interface::setColorInversion(bool enabled) {
    if(m_bColorInversion == enabled) return;
    m_bColorInversion = enabled;

    if(m_shaderTexturedGeneric->isActive()) {
        m_shaderTexturedGeneric->setUniform1f("inv", enabled ? 1.0f : 0.0f);
    }
}

void OpenGLES32Interface::setCulling(bool culling) {
    if(culling)
        glEnable(GL_CULL_FACE);
    else
        glDisable(GL_CULL_FACE);
}

void OpenGLES32Interface::setWireframe(bool _) {
    // TODO
}

void OpenGLES32Interface::flush() { glFlush(); }

std::vector<u8> OpenGLES32Interface::getScreenshot(bool withAlpha) {
    std::vector<u8> result;
    i32 width = m_vResolution.x;
    i32 height = m_vResolution.y;

    // sanity check
    if(width > 65535 || height > 65535 || width < 1 || height < 1) {
        engine->showMessageError("Renderer Error", "getScreenshot() called with invalid arguments!");
        return result;
    }

    const u8 numChannels = withAlpha ? 4 : 3;
    const u32 numElements = width * height * numChannels;
    const GLenum glFormat = withAlpha ? GL_RGBA : GL_RGB;

    // buffer to read into
    FixedSizeArray<u8> tempBuffer(numElements);

    // prep framebuffer for reading
    GLint currentFramebuffer;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFramebuffer);
    GLint previousAlignment;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFinish();

    // read
    GLenum error = GL_NO_ERROR;
    glReadPixels(0, 0, width, height, glFormat, GL_UNSIGNED_BYTE, tempBuffer.data());

    error = glGetError();
    if(error != GL_NO_ERROR) {
        engine->showMessageError("Screenshot Error", fmt::format("glReadPixels failed with error code: {}", error));

        glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
        glBindFramebuffer(GL_FRAMEBUFFER, currentFramebuffer);
        return result;
    }

    // restore state
    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
    glBindFramebuffer(GL_FRAMEBUFFER, currentFramebuffer);

    result.resize(numElements);

    // discard alpha component and flip it (Engine coordinates vs GL coordinates)
    for(i32 y = 0; y < height; ++y) {
        const i32 srcRow = height - y - 1;
        const i32 dstRow = y;

        size_t curSrc{0}, curDest{0};
        for(i32 x = 0; x < width; ++x) {
            curSrc = (srcRow * width + x) * numChannels;
            curDest = (dstRow * width + x) * numChannels;

            result[curDest + 0] = tempBuffer[curSrc + 0];  // R
            result[curDest + 1] = tempBuffer[curSrc + 1];  // G
            result[curDest + 2] = tempBuffer[curSrc + 2];  // B
            // set alpha channel to opaque
            // TODO: allow screenshots with actual alpha
            if(withAlpha) {
                result[curDest + 3] = 255;
            }
        }
    }

    return result;
}

void OpenGLES32Interface::onResolutionChange(vec2 newResolution) {
    // rebuild viewport
    m_vResolution = newResolution;

    // update state cache with the new viewport
    GLStateCache::setViewport(0, 0, m_vResolution.x, m_vResolution.y);

    // special case: custom rendertarget resolution rendering, update active projection matrix immediately
    if(m_bInScene) {
        this->projectionTransformStack.back() =
            Camera::buildMatrixOrtho2D(0, m_vResolution.x, m_vResolution.y, 0, -1.0f, 1.0f);
        this->bTransformUpToDate = false;
    }
}

Image *OpenGLES32Interface::createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) {
    return new OpenGLImage(filePath, mipmapped, keepInSystemMemory);
}

Image *OpenGLES32Interface::createImage(int width, int height, bool mipmapped, bool keepInSystemMemory) {
    return new OpenGLImage(width, height, mipmapped, keepInSystemMemory);
}

RenderTarget *OpenGLES32Interface::createRenderTarget(int x, int y, int width, int height,
                                                      MultisampleType multiSampleType) {
    return new OpenGLRenderTarget(x, y, width, height, multiSampleType);
}

Shader *OpenGLES32Interface::createShaderFromFile(std::string vertexShaderFilePath,
                                                  std::string fragmentShaderFilePath) {
    auto *shader = new OpenGLES32Shader(vertexShaderFilePath, fragmentShaderFilePath, false);
    registerShader(shader);
    return shader;
}

Shader *OpenGLES32Interface::createShaderFromSource(std::string vertexShader, std::string fragmentShader) {
    auto *shader = new OpenGLES32Shader(vertexShader, fragmentShader, true);
    registerShader(shader);
    return shader;
}

VertexArrayObject *OpenGLES32Interface::createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                                                bool keepInSystemMemory) {
    return new OpenGLES32VertexArrayObject(primitive, usage, keepInSystemMemory);
}

void OpenGLES32Interface::onTransformUpdate() {
    // always update default shader
    if(m_shaderTexturedGeneric) m_shaderTexturedGeneric->setUniformMatrix4fv("mvp", this->MP);

    // update all registered shaders, including the default one
    updateAllShaderTransforms();
}

void OpenGLES32Interface::handleGLErrors() {
    // int error = glGetError();
    // if (error != 0)
    // 	debugLog("OpenGL Error: {} on frame {}\n", error, engine->getFrameCount());
}

void OpenGLES32Interface::registerShader(OpenGLES32Shader *shader) {
    // check if already registered
    for(size_t i = 0; i < m_registeredShaders.size(); i++) {
        if(m_registeredShaders[i] == shader) return;
    }

    m_registeredShaders.push_back(shader);
}

void OpenGLES32Interface::unregisterShader(OpenGLES32Shader *shader) {
    // remove from registry if found
    for(size_t i = 0; i < m_registeredShaders.size(); i++) {
        if(m_registeredShaders[i] == shader) {
            m_registeredShaders.erase(m_registeredShaders.begin() + i);
            return;
        }
    }
}

void OpenGLES32Interface::updateAllShaderTransforms() {
    for(size_t i = 0; i < m_registeredShaders.size(); i++) {
        if(m_registeredShaders[i]->isActive()) {
            m_registeredShaders[i]->setUniformMatrix4fv("mvp", this->MP);
        }
    }
}

void OpenGLES32Interface::initSmoothClipShader() {
    if(this->smoothClipShader != nullptr) return;

    this->smoothClipShader.reset(
        this->createShaderFromSource(std::string(reinterpret_cast<const char *>(&GL_smoothclip_vsh[0]),
                                                 reinterpret_cast<const char *>(&GL_smoothclip_vsh_end[0])),
                                     std::string(reinterpret_cast<const char *>(&GL_smoothclip_fsh[0]),
                                                 reinterpret_cast<const char *>(&GL_smoothclip_fsh_end[0]))));

    if(this->smoothClipShader != nullptr) {
        this->smoothClipShader->loadAsync();
        this->smoothClipShader->load();
    }
}

#endif
