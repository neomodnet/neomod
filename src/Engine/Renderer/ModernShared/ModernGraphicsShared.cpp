// Copyright (c) 2026, WH, All rights reserved.
#include "config.h"
#include "Algorithms.h"

#if (defined(MCENGINE_FEATURE_GLES32) || defined(MCENGINE_FEATURE_DIRECTX11) || defined(MCENGINE_FEATURE_SDLGPU))

// TODO: share more code

#include "ModernGraphicsShared.h"
#include "ConVar.h"
#include "Font.h"
#include "Image.h"
#include "Shader.h"
#include "VertexArrayObject.h"

#include "Graphics_private.h"

#include <cmath>
#include <tuple>

namespace {
// temp vao buffers
static CONSTINIT VertexArrayObject lineVAO{DrawPrimitive::LINES};
static CONSTINIT VertexArrayObject triStripVAO{DrawPrimitive::TRIANGLE_STRIP};
static CONSTINIT VertexArrayObject triFanVAO{DrawPrimitive::TRIANGLE_FAN};
static CONSTINIT VertexArrayObject lineStripVAO{DrawPrimitive::LINE_STRIP};

// emit outer/inner vertex pairs for a triangle strip outline
inline void addArcStripVertices(VertexArrayObject &vao, float cx, float cy, float rInner, float rOuter,
                                float startAngle, float endAngle) {
    const int numSteps = Mc::max(1, (int)((endAngle - startAngle) / 0.05f));
    const float step = (endAngle - startAngle) / (float)numSteps;

    // rotate (c,s) by step each iteration
    const float cs = std::cos(step), ss = std::sin(step);
    float c = std::cos(startAngle), s = std::sin(startAngle);
    for(int i = 0; i < numSteps; i++) {
        vao.addVertex(rOuter * c + cx, rOuter * s + cy);
        vao.addVertex(rInner * c + cx, rInner * s + cy);
        const float nc = c * cs - s * ss;
        s = s * cs + c * ss;
        c = nc;
    }
    // final pair at exact endAngle
    c = std::cos(endAngle);
    s = std::sin(endAngle);
    vao.addVertex(rOuter * c + cx, rOuter * s + cy);
    vao.addVertex(rInner * c + cx, rInner * s + cy);
}
}  // namespace

ModernGraphicsShared::~ModernGraphicsShared() = default;

DrawPrimitive ModernGraphicsShared::convertToTriangles(DrawPrimitive primitive, std::span<const vec3> vertices,
                                                       std::span<const vec2> texcoords, std::span<const Color> colors,
                                                       std::vector<vec3> &outVertices, std::vector<vec2> &outTexcoords,
                                                       std::vector<Color> &outColors) {
    outVertices.clear();
    outTexcoords.clear();
    outColors.clear();

    // partial texcoords can't follow the vertices around, so they are dropped entirely.
    // missing colors repeat the last one
    const bool hasTexcoords = texcoords.size() >= vertices.size();
    const size_t maxColorIdx = colors.empty() ? 0 : colors.size() - 1;
    const auto emit = [&](size_t i) {
        outVertices.push_back(vertices[i]);
        if(hasTexcoords) outTexcoords.push_back(texcoords[i]);
        if(!colors.empty()) outColors.push_back(colors[Mc::min(i, maxColorIdx)]);
    };

    switch(primitive) {
        case DrawPrimitive::QUADS: {
            // a trailing partial quad is dropped
            const size_t numQuadVerts = vertices.size() / 4 * 4;
            outVertices.reserve(numQuadVerts / 4 * 6);
            for(size_t i = 0; i < numQuadVerts; i += 4) {
                for(const size_t corner : {0, 1, 2, 0, 2, 3}) emit(i + corner);
            }
            return DrawPrimitive::TRIANGLES;
        }
        case DrawPrimitive::TRIANGLE_FAN:
            // hub, rim i, rim i - 1
            if(vertices.size() > 2) outVertices.reserve((vertices.size() - 2) * 3);
            for(size_t i = 2; i < vertices.size(); i++) {
                emit(0);
                emit(i);
                emit(i - 1);
            }
            return DrawPrimitive::TRIANGLES;
        default:
            return primitive;
    }
}

void ModernGraphicsShared::addArcVertices(VertexArrayObject &vao, float cx, float cy, float radius, float startAngle,
                                          float endAngle) {
    constexpr float step = 0.05f;
    const int numSteps = (int)((endAngle - startAngle) / step);

    // rotate (c,s) by step each iteration
    const float cs = std::cos(step), ss = std::sin(step);
    float c = std::cos(startAngle), s = std::sin(startAngle);
    for(int i = 0; i <= numSteps; i++) {
        vao.addVertex(radius * c + cx, radius * s + cy);
        const float nc = c * cs - s * ss;
        s = s * cs + c * ss;
        c = nc;
    }
    vao.addVertex(radius * std::cos(endAngle) + cx, radius * std::sin(endAngle) + cy);
}

// color
void ModernGraphicsShared::setColor(Color color) { m_data->color = color; }

void ModernGraphicsShared::setAlpha(float alpha) { m_data->color.setA(alpha); }

// 2d primitive drawing
void ModernGraphicsShared::drawPixels(int /*x*/, int /*y*/, int /*width*/, int /*height*/, DrawPixelsType /*type*/,
                                      const void * /*pixels*/) {
    // TODO: implement
}

void ModernGraphicsShared::drawPixel(int x, int y) {
    // TODO: this isn't really good...?
    drawLinef((float)x, (float)y, (float)x + 1.f, (float)y);
}

void ModernGraphicsShared::drawLinef(float x1, float y1, float x2, float y2) {
    this->updateTransform();
    this->setTexturing(false);

    {
        lineVAO.clear();
        lineVAO.addVertex(x1, y1);
        lineVAO.addVertex(x2, y2);
    }
    this->drawVAO(&lineVAO);
}

void ModernGraphicsShared::drawRectf(const RectOptions &opts) {
    if(opts.cornerRadius <= 0.f) return Graphics::drawRectf(opts);

    this->updateTransform();
    this->setTexturing(false);

    // draw as a filled triangle strip to avoid line rasterization differences
    // NOTE: GL_LINE_LOOP looks a lot cleaner than what seems to be possible with a LINE_STRIP in DX11/SDL_gpu,
    // so GLES3.2 overrides this case
    const float r = opts.cornerRadius;
    const float x2 = opts.x + opts.width, y2 = opts.y + opts.height;
    const float hw = opts.lineThickness / 2.f;
    const float rInner = r - hw, rOuter = r + hw;
    {
        triStripVAO.clear();
        addArcStripVertices(triStripVAO, opts.x + r, opts.y + r, rInner, rOuter, PI_F, 1.5f * PI_F);
        addArcStripVertices(triStripVAO, x2 - r, opts.y + r, rInner, rOuter, 1.5f * PI_F, 2.f * PI_F);
        addArcStripVertices(triStripVAO, x2 - r, y2 - r, rInner, rOuter, 0.f, 0.5f * PI_F);
        addArcStripVertices(triStripVAO, opts.x + r, y2 - r, rInner, rOuter, 0.5f * PI_F, PI_F);
        // close back to first vertex pair
        triStripVAO.addVertex(opts.x - hw, opts.y + r);
        triStripVAO.addVertex(opts.x + hw, opts.y + r);
    }
    this->drawVAO(&triStripVAO);
}

void ModernGraphicsShared::fillRectf(const FillRectOptions &opts) {
    this->updateTransform();
    this->setTexturing(false);

    if(opts.cornerRadius > 0.f) {
        const float r = opts.cornerRadius;
        const float x2 = opts.x + opts.width, y2 = opts.y + opts.height;
        {
            triFanVAO.clear();
            triFanVAO.addVertex(opts.x + opts.width * 0.5f, opts.y + opts.height * 0.5f);  // center (fan hub)

            addArcVertices(triFanVAO, opts.x + r, opts.y + r, r, PI_F, 1.5f * PI_F);
            addArcVertices(triFanVAO, x2 - r, opts.y + r, r, 1.5f * PI_F, 2.f * PI_F);
            addArcVertices(triFanVAO, x2 - r, y2 - r, r, 0.f, 0.5f * PI_F);
            addArcVertices(triFanVAO, opts.x + r, y2 - r, r, 0.5f * PI_F, PI_F);
            // close the fan back to the first perimeter vertex
            triFanVAO.addVertex(opts.x, opts.y + r);
        }
        this->drawVAO(&triFanVAO);
    } else {
        {
            triStripVAO.clear();
            triStripVAO.addVertex(opts.x, opts.y);
            triStripVAO.addVertex(opts.x, opts.y + opts.height);
            triStripVAO.addVertex(opts.x + opts.width, opts.y);
            triStripVAO.addVertex(opts.x + opts.width, opts.y + opts.height);
        }
        this->drawVAO(&triStripVAO);
    }
}

void ModernGraphicsShared::drawArcf(float cx, float cy, float radius, float startAngle, float endAngle) {
    this->updateTransform();
    this->setTexturing(false);

    {
        lineStripVAO.clear();
        addArcVertices(lineStripVAO, cx, cy, radius, startAngle, endAngle);
    }
    this->drawVAO(&lineStripVAO);
}

void ModernGraphicsShared::fillGradient(int x, int y, int width, int height, Color topLeftColor, Color topRightColor,
                                        Color bottomLeftColor, Color bottomRightColor) {
    this->updateTransform();
    this->setTexturing(false);  // disable texturing

    {
        triStripVAO.clear();
        triStripVAO.addVertex((float)x, (float)y);
        triStripVAO.addColor(topLeftColor);
        triStripVAO.addVertex((float)x, (float)y + (float)height);
        triStripVAO.addColor(bottomLeftColor);
        triStripVAO.addVertex((float)x + (float)width, (float)y);
        triStripVAO.addColor(topRightColor);
        triStripVAO.addVertex((float)x + (float)width, (float)y + (float)height);
        triStripVAO.addColor(bottomRightColor);
    }
    this->drawVAO(&triStripVAO);
}

void ModernGraphicsShared::drawQuad(int x, int y, int width, int height) {
    this->updateTransform();
    this->setTexturing(true);  // enable texturing

    {
        triStripVAO.clear();
        triStripVAO.addVertex((float)x, (float)y);
        triStripVAO.addTexcoord(0, 0);
        triStripVAO.addVertex((float)x, (float)y + (float)height);
        triStripVAO.addTexcoord(0, 1);
        triStripVAO.addVertex((float)x + (float)width, (float)y);
        triStripVAO.addTexcoord(1, 0);
        triStripVAO.addVertex((float)x + (float)width, (float)y + (float)height);
        triStripVAO.addTexcoord(1, 1);
    }
    this->drawVAO(&triStripVAO);
}

void ModernGraphicsShared::drawQuad(vec2 topLeft, vec2 topRight, vec2 bottomRight, vec2 bottomLeft, Color topLeftColor,
                                    Color topRightColor, Color bottomRightColor, Color bottomLeftColor) {
    this->updateTransform();
    this->setTexturing(false);  // disable texturing

    {
        triStripVAO.clear();
        triStripVAO.addVertex(topLeft.x, topLeft.y);
        triStripVAO.addColor(topLeftColor);
        triStripVAO.addVertex(bottomLeft.x, bottomLeft.y);
        triStripVAO.addColor(bottomLeftColor);
        triStripVAO.addVertex(topRight.x, topRight.y);
        triStripVAO.addColor(topRightColor);
        triStripVAO.addVertex(bottomRight.x, bottomRight.y);
        triStripVAO.addColor(bottomRightColor);
    }
    this->drawVAO(&triStripVAO);
}

void ModernGraphicsShared::drawImage(const Image *image, AnchorPoint anchor, float edgeSoftness, McRect clipRect) {
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
            this->smoothClipShader.reset(this->createSmoothClipShader());
            if(this->smoothClipShader) {
                this->smoothClipShader->loadAsync();
                this->smoothClipShader->load();
            }
        }
        smoothedEdges = this->smoothClipShader && this->smoothClipShader->isReady();
    }

    const bool fallbackClip = clipRectSpecified && !smoothedEdges;
    if(fallbackClip) this->pushClipRect(clipRect);

    this->updateTransform();
    this->setTexturing(true);

    const vec2 size = image->getSize();
    const vec2 pos = getAnchoredOrigin(anchor, size);
    const auto [x, y, width, height] = std::tuple{pos.x, pos.y, size.x, size.y};

    if(smoothedEdges) {
        // set a default clip rect as the exact image size if one wasn't explicitly passed, but we still want smoothing
        if(!clipRectSpecified) clipRect = McRect{pos, size};

        const vec2 clipMin = this->getSmoothClipMin(clipRect);
        const vec2 clipMax = clipMin + clipRect.getSize();

        this->smoothClipShader->enable();
        this->smoothClipShader->setUniform2f("rect_min", clipMin.x, clipMin.y);
        this->smoothClipShader->setUniform2f("rect_max", clipMax.x, clipMax.y);
        this->smoothClipShader->setUniform1f("edge_softness", edgeSoftness);
        this->smoothClipShader->setUniform4f("col", m_data->color.Rf(), m_data->color.Gf(), m_data->color.Bf(),
                                             m_data->color.Af());
        this->smoothClipShader->setMVP(m_data->MP);
    }

    triStripVAO.clear();
    {
        triStripVAO.addVertex(x, y);
        triStripVAO.addTexcoord(0, 0);
        triStripVAO.addVertex(x, y + height);
        triStripVAO.addTexcoord(0, 1);
        triStripVAO.addVertex(x + width, y);
        triStripVAO.addTexcoord(1, 0);
        triStripVAO.addVertex(x + width, y + height);
        triStripVAO.addTexcoord(1, 1);
    }

    image->bind();
    {
        this->drawVAO(&triStripVAO);
    }
    image->unbind();

    if(smoothedEdges) {
        this->smoothClipShader->disable();
    } else if(fallbackClip) {
        this->popClipRect();
    }

    if(cv::r_debug_drawimage.getBool()) {
        this->setColor(0xbbff00ff);
        Graphics::drawRectf(x, y, width, height);
    }
}

void ModernGraphicsShared::drawString(McFont *font, std::string_view text, std::optional<TextFX> effects) {
    if(font == nullptr || text.length() < 1 || !font->isReady()) return;

    this->updateTransform();
    this->setTexturing(true);

    font->drawString(text, effects);
}

void ModernGraphicsShared::pushClipRect(McRect clipRect) {
    if(this->clipRectStack.size() > 0)
        this->clipRectStack.push_back(this->clipRectStack.back().intersect(clipRect));
    else
        this->clipRectStack.push_back(clipRect);

    this->setClipRect(this->clipRectStack.back());
}

void ModernGraphicsShared::popClipRect() {
    this->clipRectStack.pop_back();

    if(this->clipRectStack.size() > 0)
        this->setClipRect(this->clipRectStack.back());
    else
        this->setClipping(false);
}

#endif
