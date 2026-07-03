// Copyright (c) 2016, PG, All rights reserved.
#include "config.h"

#ifdef MCENGINE_FEATURE_OPENGL
#include "OpenGLInterface.h"

#include "Camera.h"
#include "Matrices.h"
#include "ConVar.h"
#include "Engine.h"

#include "Font.h"
#include "OpenGLHeaders.h"
#include "OpenGLImage.h"
#include "OpenGLRenderTarget.h"
#include "OpenGLShader.h"
#include "OpenGLVertexArrayObject.h"
#include "Logging.h"

#include "SDLGLInterface.h"
#include "OpenGLStateCache.h"
#include "Graphics_private.h"

#include "binary_embed.h"

#include <cstddef>

OpenGLInterface::OpenGLInterface(void *window)
    : SDLGLInterface(static_cast<SDL_Window *>(window)),
      vResolution(engine->getScreenSize())  // initial viewport size = window size
{}

OpenGLInterface::~OpenGLInterface() = default;

bool OpenGLInterface::init() {
    if(!SDLGLInterface::init()) return false;

    // quality
    glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_NICEST);
    glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
    glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST);

    // enable
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glEnable(GL_COLOR_MATERIAL);

    // disable
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FRAMEBUFFER_SRGB);

    // shading
    glShadeModel(GL_SMOOTH);

    // blending
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);

    // culling
    glFrontFace(GL_CCW);

    // debugging
    auto glDebugFunc = glDebugMessageCallback ? glDebugMessageCallback
                                              : (glDebugMessageCallbackARB ? glDebugMessageCallbackARB : nullptr);
    if(glDebugFunc) {
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
        glDebugFunc(SDLGLInterface::glDebugCB, nullptr);
    }

    // initialize the state cache
    GLStateCache::initialize();
    return true;
}

void OpenGLInterface::beginScene() {
    SDLGLInterface::beginScene();
    this->bInScene = true;

    Matrix4 defaultProjectionMatrix =
        Camera::buildMatrixOrtho2D(0, this->vResolution.x, this->vResolution.y, 0, -1.0f, 1.0f);

    // push main transforms
    pushTransform();
    setProjectionMatrix(defaultProjectionMatrix);
    if(cv::r_globaloffset_x.getFloat() != cv::r_globaloffset_x.getDefaultFloat() ||
       cv::r_globaloffset_y.getFloat() != cv::r_globaloffset_y.getDefaultFloat())
        translate(cv::r_globaloffset_x.getFloat(), cv::r_globaloffset_y.getFloat());

    // and apply them
    updateTransform();

    // set clear color and clear
    // glClearColor(1, 1, 1, 1);
    // glClearColor(0.9568f, 0.9686f, 0.9882f, 1);
    glClearColor(0, 0, 0, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void OpenGLInterface::endScene() {
    popTransform();

    if constexpr(Env::cfg(BUILD::DEBUG)) {
        checkStackLeaks();

        if(this->clipRectStack.size() > 0) {
            engine->showMessageErrorFatal("ClipRect Stack Leak", "Make sure all push*() have a pop*()!");
            engine->shutdown();
        }
    }

    this->processPendingScreenshot();

    this->bInScene = false;
    SDLGLInterface::endScene();
}

void OpenGLInterface::clearDepthBuffer() { glClear(GL_DEPTH_BUFFER_BIT); }

void OpenGLInterface::setColor(Color color) {
    if(color == m_data->color) return;

    m_data->color = color;

    glColor4ub(m_data->color.r, m_data->color.g, m_data->color.b, m_data->color.a);
}

void OpenGLInterface::setAlpha(float alpha) {
    if(m_data->color.a == Colors::to_byte(alpha)) return;
    Color newColor = m_data->color;
    this->setColor(newColor.setA(alpha));
}

void OpenGLInterface::drawPixels(int x, int y, int width, int height, DrawPixelsType type, const void *pixels) {
    glRasterPos2i(x, y + height);  // '+height' because of opengl bottom left origin, but engine top left origin
    glDrawPixels(width, height, GL_RGBA, (type == DrawPixelsType::UBYTE ? GL_UNSIGNED_BYTE : GL_FLOAT), pixels);
}

void OpenGLInterface::drawPixel(int x, int y) {
    updateTransform();

    glDisable(GL_TEXTURE_2D);

    glBegin(GL_POINTS);
    {
        glVertex2i(x, y);
    }
    glEnd();
}

void OpenGLInterface::drawLinef(float x1, float y1, float x2, float y2) {
    updateTransform();

    glDisable(GL_TEXTURE_2D);

    glBegin(GL_LINES);
    {
        glVertex2f(x1, y1);
        glVertex2f(x2, y2);
    }
    glEnd();
}

namespace {
void emitArcVertices(float cx, float cy, float radius, float startAngle, float endAngle) {
    constexpr double step = 0.05;
    const int numSteps = (int)((endAngle - startAngle) / step);

    // rotate (c,s) by step each iteration
    const double cs = std::cos(step), ss = std::sin(step);
    double c = std::cos((double)startAngle), s = std::sin((double)startAngle);
    for(int i = 0; i <= numSteps; i++) {
        glVertex2d(radius * c + cx, radius * s + cy);
        const double nc = c * cs - s * ss;
        s = s * cs + c * ss;
        c = nc;
    }
    glVertex2d(radius * std::cos((double)endAngle) + cx, radius * std::sin((double)endAngle) + cy);
}

// emit outer/inner vertex pairs for a triangle strip outline
inline void emitArcStripVertices(float cx, float cy, float rInner, float rOuter, float startAngle, float endAngle) {
    const int numSteps = std::max(1, (int)((endAngle - startAngle) / 0.05f));
    const float step = (endAngle - startAngle) / (float)numSteps;

    // rotate (c,s) by step each iteration
    const float cs = std::cos(step), ss = std::sin(step);
    float c = std::cos(startAngle), s = std::sin(startAngle);
    for(int i = 0; i < numSteps; i++) {
        glVertex2f(rOuter * c + cx, rOuter * s + cy);
        glVertex2f(rInner * c + cx, rInner * s + cy);
        const float nc = c * cs - s * ss;
        s = s * cs + c * ss;
        c = nc;
    }
    // final pair at exact endAngle
    c = std::cos(endAngle);
    s = std::sin(endAngle);
    glVertex2f(rOuter * c + cx, rOuter * s + cy);
    glVertex2f(rInner * c + cx, rInner * s + cy);
}
}  // namespace

void OpenGLInterface::drawRectf(const RectOptions &opts) {
    updateTransform();

    glDisable(GL_TEXTURE_2D);

    // set line width if specified
    if(opts.lineThickness != 1.0f) {
        glLineWidth(opts.lineThickness);
    }

    if(opts.cornerRadius > 0.f) {
        const float r = opts.cornerRadius;

        if(opts.lineThickness > 1.f) {
            // use triangle strip for thick lines to avoid join gaps
            const float hw = opts.lineThickness / 2.f;
            const float rInner = r - hw, rOuter = r + hw;
            const float x2 = opts.x + opts.width, y2 = opts.y + opts.height;
            glBegin(GL_TRIANGLE_STRIP);
            {
                emitArcStripVertices(opts.x + r, opts.y + r, rInner, rOuter, PI_F, 1.5f * PI_F);
                emitArcStripVertices(x2 - r, opts.y + r, rInner, rOuter, 1.5f * PI_F, 2.f * PI_F);
                emitArcStripVertices(x2 - r, y2 - r, rInner, rOuter, 0.f, 0.5f * PI_F);
                emitArcStripVertices(opts.x + r, y2 - r, rInner, rOuter, 0.5f * PI_F, PI_F);
                // close back to first vertex pair
                glVertex2f(opts.x - hw, opts.y + r);
                glVertex2f(opts.x + hw, opts.y + r);
            }
            glEnd();
        } else {
            glBegin(GL_LINE_LOOP);
            {
                emitArcVertices(opts.x + r, opts.y + r, r, PI_F, 1.5f * PI_F);
                emitArcVertices(opts.x + opts.width - r, opts.y + r, r, 1.5f * PI_F, 2.f * PI_F);
                emitArcVertices(opts.x + opts.width - r, opts.y + opts.height - r, r, 0.f, 0.5f * PI_F);
                emitArcVertices(opts.x + r, opts.y + opts.height - r, r, 0.5f * PI_F, PI_F);
            }
            glEnd();
        }

        if(opts.lineThickness != 1.0f) {
            glLineWidth(1.0f);
        }
        return;
    }

    glBegin(opts.withColor ? GL_LINES : GL_LINE_LOOP);
    if(opts.withColor) {
        setColor(opts.top);
        glVertex2f(opts.x, opts.y);
        glVertex2f(opts.x + opts.width, opts.y);

        setColor(opts.left);
        glVertex2f(opts.x, opts.y + opts.height);
        glVertex2f(opts.x, opts.y);

        setColor(opts.bottom);
        glVertex2f(opts.x + opts.width, opts.y + opts.height);
        glVertex2f(opts.x, opts.y + opts.height);

        setColor(opts.right);
        glVertex2f(opts.x + opts.width, opts.y);
        glVertex2f(opts.x + opts.width, opts.y + opts.height);
    } else {
        glVertex2f(opts.x, opts.y);
        glVertex2f(opts.x + opts.width, opts.y);
        glVertex2f(opts.x + opts.width, opts.y + opts.height);
        glVertex2f(opts.x, opts.y + opts.height);
    }
    glEnd();

    // restore line width
    if(opts.lineThickness != 1.0f) {
        glLineWidth(1.0f);
    }
}

void OpenGLInterface::fillRectf(const FillRectOptions &opts) {
    updateTransform();

    glDisable(GL_TEXTURE_2D);

    if(opts.cornerRadius > 0.f) {
        const float r = opts.cornerRadius;

        glBegin(GL_POLYGON);
        {
            emitArcVertices(opts.x + r, opts.y + r, r, PI_F, 1.5f * PI_F);
            emitArcVertices(opts.x + opts.width - r, opts.y + r, r, 1.5f * PI_F, 2.f * PI_F);
            emitArcVertices(opts.x + opts.width - r, opts.y + opts.height - r, r, 0.f, 0.5f * PI_F);
            emitArcVertices(opts.x + r, opts.y + opts.height - r, r, 0.5f * PI_F, PI_F);
        }
        glEnd();
    } else {
        glBegin(GL_QUADS);
        {
            glVertex2f(opts.x, opts.y);
            glVertex2f(opts.x, opts.y + opts.height);
            glVertex2f(opts.x + opts.width, opts.y + opts.height);
            glVertex2f(opts.x + opts.width, opts.y);
        }
        glEnd();
    }
}

void OpenGLInterface::drawArcf(float cx, float cy, float radius, float startAngle, float endAngle) {
    updateTransform();

    glDisable(GL_TEXTURE_2D);

    glBegin(GL_LINE_STRIP);
    {
        emitArcVertices(cx, cy, radius, startAngle, endAngle);
    }
    glEnd();
}

void OpenGLInterface::fillGradient(int x, int y, int width, int height, Color topLeftColor, Color topRightColor,
                                   Color bottomLeftColor, Color bottomRightColor) {
    updateTransform();

    glDisable(GL_TEXTURE_2D);

    glBegin(GL_QUADS);
    {
        setColor(topLeftColor);
        glVertex2i(x, y);

        setColor(topRightColor);
        glVertex2i((x + width), y);

        setColor(bottomRightColor);
        glVertex2i((x + width), (y + height));

        setColor(bottomLeftColor);
        glVertex2i(x, (y + height));
    }
    glEnd();
}

void OpenGLInterface::drawQuad(int x, int y, int width, int height) {
    updateTransform();

    const int left = x;
    const int right = (x + width);

    const int top = (y + height);
    const int bottom = y;

    glBegin(GL_QUADS);
    {
        glTexCoord2i(0, 0);
        glVertex2i(left, bottom);

        glTexCoord2i(0, 1);
        glVertex2i(left, top);

        glTexCoord2i(1, 1);
        glVertex2i(right, top);

        glTexCoord2i(1, 0);
        glVertex2i(right, bottom);
    }
    glEnd();
}

void OpenGLInterface::drawQuad(vec2 topLeft, vec2 topRight, vec2 bottomRight, vec2 bottomLeft, Color topLeftColor,
                               Color topRightColor, Color bottomRightColor, Color bottomLeftColor) {
    updateTransform();

    glBegin(GL_QUADS);
    {
        setColor(topLeftColor);
        glTexCoord2f(0.f, 0.f);
        glVertex2f(topLeft.x, topLeft.y);

        setColor(bottomLeftColor);
        glTexCoord2f(0.f, 1.f);
        glVertex2f(bottomLeft.x, bottomLeft.y);

        setColor(bottomRightColor);
        glTexCoord2f(1.f, 1.f);
        glVertex2f(bottomRight.x, bottomRight.y);

        setColor(topRightColor);
        glTexCoord2f(1.f, 0.f);
        glVertex2f(topRight.x, topRight.y);
    }
    glEnd();
}

void OpenGLInterface::drawImage(const Image *image, AnchorPoint anchor, float edgeSoftness, McRect clipRect) {
    // skip entirely transparent images
    if(image == nullptr || !image->isGPUReady()) {
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
        //this->smoothClipShader->setUniform1i("texture", 0);
    }

    image->bind();
    {
        glBegin(GL_QUADS);
        {
            glTexCoord2f(0, 0);
            glVertex2f(x, y);

            glTexCoord2f(0, 1);
            glVertex2f(x, (y + height));

            glTexCoord2f(1, 1);
            glVertex2f((x + width), (y + height));

            glTexCoord2f(1, 0);
            glVertex2f((x + width), y);
        }
        glEnd();
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

void OpenGLInterface::drawString(McFont *font, std::string_view text, std::optional<TextFX> effects) {
    if(font == nullptr || text.length() < 1 || !font->isReady()) return;

    updateTransform();

    if(cv::r_debug_flush_drawstring.getBool()) {
        glFinish();
        glFlush();
        glFinish();
        glFlush();
    }

    font->drawString(text, effects);
}

void OpenGLInterface::drawVAO(VertexArrayObject *vao) {
    if(vao == nullptr) return;

    updateTransform();

    // HACKHACK: disable texturing for special primitives, also for untextured vaos
    if(vao->getPrimitive() == DrawPrimitive::LINES || vao->getPrimitive() == DrawPrimitive::LINE_STRIP ||
       vao->getPrimitive() == DrawPrimitive::LINE_LOOP || !vao->hasTexcoords())
        glDisable(GL_TEXTURE_2D);

    // if baked, then we can directly draw the buffer
    if(vao->isReady()) {
        vao->draw();
        return;
    }

    // fetch data references
    const auto vertices = vao->getVertices();
    const auto texcoords = vao->getTexcoords();
    const auto normals = vao->getNormals();
    const auto colors = vao->getColors();

    const bool hasTexcoords0 = vao->hasTexcoords() && !texcoords.empty();
    const bool hasNormals = !normals.empty();
    const bool hasColors = !colors.empty();

    size_t drawCount = vertices.size();

    // only texture unit 0 handled atm (all that's actually used anyways)
    if(hasTexcoords0) {
        drawCount = std::min(drawCount, texcoords.size());
    }
    if(hasNormals) {
        drawCount = std::min(drawCount, normals.size());
    }
    if(hasColors) {
        drawCount = std::min(drawCount, colors.size());
    }

    if(drawCount == 0) {
        return;
    }

    // unbind any previous VBO/VAO to ensure client-side arrays are used correctly
    GLStateCache::bindArrayBuffer(0);
    if(cv::r_opengl_legacy_vao_use_vertex_array.getBool()) {
        glBindVertexArray(0);
    }

    // enable and set vertex array (always present)
    GLStateCache::enableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vertices.data());

    // handle texcoords (only unit 0)
    if(hasTexcoords0) {
        GLStateCache::enableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, texcoords.data());
    } else {
        GLStateCache::disableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    // handle colors
    static std::vector<Color> swapped_colors;
    if(hasColors) {
        // check if any color needs conversion (only R and B are swapped)
        bool needsConversion = false;
        MC_UNROLL
        for(size_t i = 0; i < drawCount; ++i) {
            if(colors[i].r != colors[i].b) {
                needsConversion = true;
                break;
            }
        }
        // (create temporary swapped buffer for correct RGBA byte order)
        // TODO: just store them properly in the first place
        GLStateCache::enableClientState(GL_COLOR_ARRAY);
        if(needsConversion) {
            swapped_colors.clear();
            swapped_colors.reserve(drawCount);
            for(size_t i = 0; i < drawCount; ++i) {
                swapped_colors.push_back(abgr(colors[i]));
            }
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, swapped_colors.data());
        } else {
            glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors.data());
        }
    } else {
        GLStateCache::disableClientState(GL_COLOR_ARRAY);
    }

    // handle normals
    if(hasNormals) {
        GLStateCache::enableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, 0, normals.data());
    } else {
        GLStateCache::disableClientState(GL_NORMAL_ARRAY);
    }

    // draw using client-side arrays
    glDrawArrays(SDLGLInterface::primitiveToOpenGLMap[vao->getPrimitive()], 0, static_cast<GLint>(drawCount));
}

void OpenGLInterface::setClipRect(McRect clipRect) {
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

void OpenGLInterface::pushClipRect(McRect clipRect) {
    if(this->clipRectStack.size() > 0)
        this->clipRectStack.push_back(this->clipRectStack.back().intersect(clipRect));
    else
        this->clipRectStack.push_back(clipRect);

    setClipRect(this->clipRectStack.back());
}

void OpenGLInterface::popClipRect() {
    this->clipRectStack.pop_back();

    if(this->clipRectStack.size() > 0)
        setClipRect(this->clipRectStack.back());
    else
        setClipping(false);
}

void OpenGLInterface::pushViewport() {
    m_data->viewportStack.push_back(GLStateCache::getCurrentViewport());
    m_data->resolutionStack.push_back(this->vResolution);
}

void OpenGLInterface::setViewport(int x, int y, int width, int height) {
    this->vResolution = vec2(width, height);
    GLStateCache::setViewport(x, y, width, height);
}

void OpenGLInterface::popViewport() {
    if(m_data->viewportStack.empty() || m_data->resolutionStack.empty()) {
        debugLog("WARNING: viewport stack underflow!");
        return;
    }

    this->vResolution = m_data->resolutionStack.back();
    m_data->resolutionStack.pop_back();

    GLStateCache::setViewport(m_data->viewportStack.back());
    m_data->viewportStack.pop_back();
}

void OpenGLInterface::setClipping(bool enabled) {
    if(enabled) {
        if(this->clipRectStack.size() > 0) glEnable(GL_SCISSOR_TEST);
    } else
        glDisable(GL_SCISSOR_TEST);
}

void OpenGLInterface::setAlphaTesting(bool enabled) {
    if(enabled)
        glEnable(GL_ALPHA_TEST);
    else
        glDisable(GL_ALPHA_TEST);
}

void OpenGLInterface::setAlphaTestFunc(DrawCompareFunc alphaFunc, float ref) {
    glAlphaFunc(SDLGLInterface::compareFuncToOpenGLMap[alphaFunc], ref);
}

void OpenGLInterface::setColorWriting(bool r, bool g, bool b, bool a) { glColorMask(r, g, b, a); }

void OpenGLInterface::setColorInversion(bool enabled) {
    if(enabled) {
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(GL_COPY_INVERTED);
    } else {
        glDisable(GL_COLOR_LOGIC_OP);
    }
}

void OpenGLInterface::setAntialiasing(bool aa) {
    this->bAntiAliasing = aa;
    if(aa)
        glEnable(GL_MULTISAMPLE);
    else
        glDisable(GL_MULTISAMPLE);
}

void OpenGLInterface::setWireframe(bool enabled) {
    if(enabled)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void OpenGLInterface::flush() { glFlush(); }

std::vector<u8> OpenGLInterface::getScreenshot(bool withAlpha) {
    std::vector<u8> result;
    i32 width = this->vResolution.x;
    i32 height = this->vResolution.y;

    // sanity check
    if(width > 65535 || height > 65535 || width < 1 || height < 1) {
        engine->showMessageError("Renderer Error", "getScreenshot() called with invalid arguments!");
        return result;
    }

    const u8 numChannels = withAlpha ? 4 : 3;
    const u32 numElements = width * height * numChannels;
    const GLenum glFormat = withAlpha ? GL_RGBA : GL_RGB;

    // take screenshot
    result.resize(numElements);
    glFinish();
    for(sSz y = 0; y < height; y++)  // flip it while reading
    {
        glReadPixels(0, (height - (y + 1)), width, 1, glFormat, GL_UNSIGNED_BYTE, &(result[y * width * numChannels]));
    }

    // fix alpha channel (set all pixels to fully opaque)
    // TODO: allow proper transparent screenshots
    if(withAlpha) {
        for(u32 i = 3; i < numElements; i += numChannels) {
            result[i] = 255;
        }
    }

    // copy to vector
    return result;
}

void OpenGLInterface::onResolutionChange(vec2 newResolution) {
    // rebuild viewport
    this->vResolution = newResolution;
    GLStateCache::setViewport(0, 0, this->vResolution.x, this->vResolution.y);

    // special case: custom rendertarget resolution rendering, update active projection matrix immediately
    if(this->bInScene) {
        m_data->projectionTransformStack.back() =
            Camera::buildMatrixOrtho2D(0, this->vResolution.x, this->vResolution.y, 0, -1.0f, 1.0f);
        m_data->bTransformUpToDate = false;
    }
}

Image *OpenGLInterface::createImage(std::string filePath, bool mipmapped, bool keepInSystemMemory) {
    return new OpenGLImage(filePath, mipmapped, keepInSystemMemory);
}

Image *OpenGLInterface::createImage(i32 width, i32 height, bool mipmapped, bool keepInSystemMemory) {
    return new OpenGLImage(width, height, mipmapped, keepInSystemMemory);
}

RenderTarget *OpenGLInterface::createRenderTarget(int x, int y, int width, int height,
                                                  MultisampleType multiSampleType) {
    return new OpenGLRenderTarget(x, y, width, height, multiSampleType);
}

Shader *OpenGLInterface::createShaderFromFile(std::string vertexShaderFilePath, std::string fragmentShaderFilePath) {
    return new OpenGLShader(vertexShaderFilePath, fragmentShaderFilePath, false);
}

Shader *OpenGLInterface::createShaderFromSource(std::string vertexShader, std::string fragmentShader) {
    return new OpenGLShader(vertexShader, fragmentShader, true);
}

VertexArrayObject *OpenGLInterface::createVertexArrayObject(DrawPrimitive primitive, DrawUsageType usage,
                                                            bool keepInSystemMemory) {
    return new OpenGLVertexArrayObject(primitive, usage, keepInSystemMemory);
}

void OpenGLInterface::onTransformUpdate() {
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m_data->projectionMatrix.get());

    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m_data->worldMatrix.get());
}

void OpenGLInterface::initSmoothClipShader() {
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
