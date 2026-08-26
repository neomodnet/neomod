// Copyright (c) 2013, PG, All rights reserved.
#include "RenderTarget.h"

#include "ConVar.h"
#include "Engine.h"
#include "VertexArrayObject.h"
#include "Graphics.h"
#include "Logging.h"

static_assert(MultisampleType::X0 == MultisampleType{0});

RenderTarget::RenderTarget(int x, int y, int width, int height, MultisampleType multiSampleType)
    : Resource(RENDERTARGET),
      vao1(g->createVertexArrayObject(DrawPrimitive::TRIANGLES, DrawUsageType::STATIC, false)),
      vao2(g->createVertexArrayObject(DrawPrimitive::TRIANGLES, DrawUsageType::STATIC, false)),
      vPos(vec2{x, y}),
      vSize(width, height),
      multiSampleType(multiSampleType) {}

RenderTarget::~RenderTarget() = default;

static CONSTINIT std::array<vec3, 6> lastDrawVerts1{};
void RenderTarget::draw(int x, int y) {
    if(!this->isReady()) {
        logIfCV(debug_rt, "WARNING: RenderTarget is not ready!");
        return;
    }

    g->setColor(this->color);

    this->bind();
    {
        // all draw*() functions of the RenderTarget class guarantee correctly flipped images
        // if bind() is used, no guarantee can be made about the texture orientation (assuming an anonymous
        // Renderer)

        // clang-format off
        std::array<vec3, 6> newVertices = {
            vec3{x, y, 0.f},
            {x, y + this->vSize.y, 0.f},
            {x + this->vSize.x, y + this->vSize.y, 0.f},
            {x + this->vSize.x, y + this->vSize.y, 0.f},
            {x + this->vSize.x, y, 0.f},
            {x, y, 0.f}
        };
        // clang-format on

        if(!this->vao1->isReady() || lastDrawVerts1 != newVertices) {
            this->vao1->release();

            lastDrawVerts1 = newVertices;

            this->vao1->setVertices(lastDrawVerts1);

            const float vTop = g->hasFlippedTextureOrigin() ? 1.f : 0.f;
            const float vBot = g->hasFlippedTextureOrigin() ? 0.f : 1.f;
            this->vao1->setTexcoords(
                std::array<vec2, 6>{vec2{0.f, vTop}, {0.f, vBot}, {1.f, vBot}, {1.f, vBot}, {1.f, vTop}, {0.f, vTop}});

            this->vao1->loadAsync();
            this->vao1->load();
        }

        g->drawVAO(this->vao1.get());
    }
    this->unbind();
}

static CONSTINIT std::array<vec3, 6> lastDrawVerts2{};
void RenderTarget::draw(int x, int y, int width, int height) {
    if(!this->isReady()) {
        logIfCV(debug_rt, "WARNING: RenderTarget is not ready!");
        return;
    }

    g->setColor(this->color);

    this->bind();
    {
        // clang-format off
        std::array<vec3, 6> newVertices = {
            vec3{x, y, 0.f},
            {x, y + height, 0.f},
            {x + width, y + height, 0.f},
            {x + width, y + height, 0.f},
            {x + width, y, 0.f},
            {x, y, 0.f}
        };
        // clang-format on

        if(!this->vao2->isReady() || lastDrawVerts2 != newVertices) {
            this->vao2->release();

            lastDrawVerts2 = newVertices;

            this->vao2->setVertices(lastDrawVerts2);

            const float vTop = g->hasFlippedTextureOrigin() ? 1.f : 0.f;
            const float vBot = g->hasFlippedTextureOrigin() ? 0.f : 1.f;
            this->vao2->setTexcoords(
                std::array<vec2, 6>{vec2{0.f, vTop}, {0.f, vBot}, {1.f, vBot}, {1.f, vBot}, {1.f, vTop}, {0.f, vTop}});

            this->vao2->loadAsync();
            this->vao2->load();
        }

        g->drawVAO(this->vao2.get());
    }
    this->unbind();
}

static CONSTINIT VertexArrayObject rectVAO{};

void RenderTarget::drawRect(int x, int y, int width, int height) {
    if(!this->bReady) {
        logIfCV(debug_rt, "WARNING: RenderTarget is not ready!");
        return;
    }

    const float texCoordWidth0 = x / this->vSize.x;
    const float texCoordWidth1 = (x + width) / this->vSize.x;
    float texCoordHeight1 = y / this->vSize.y;
    float texCoordHeight0 = (y + height) / this->vSize.y;
    if(g->hasFlippedTextureOrigin()) {
        texCoordHeight1 = 1.0f - texCoordHeight1;
        texCoordHeight0 = 1.0f - texCoordHeight0;
    }

    g->setColor(this->color);

    this->bind();
    {
        rectVAO.setVertices(std::array<vec3, 6>{vec3{x, y, 0.f},
                                                {x, y + height, 0.f},
                                                {x + width, y + height, 0.f},
                                                {x + width, y + height, 0.f},
                                                {x + width, y, 0.f},
                                                {x, y, 0.f}});

        rectVAO.setTexcoords(std::array<vec2, 6>{vec2{texCoordWidth0, texCoordHeight1},
                                                 {texCoordWidth0, texCoordHeight0},
                                                 {texCoordWidth1, texCoordHeight0},
                                                 {texCoordWidth1, texCoordHeight0},
                                                 {texCoordWidth1, texCoordHeight1},
                                                 {texCoordWidth0, texCoordHeight1}});

        g->drawVAO(&rectVAO);
    }
    this->unbind();
}

void RenderTarget::rebuild(int x, int y, int width, int height, MultisampleType multiSampleType) {
    this->vPos.x = x;
    this->vPos.y = y;
    this->vSize.x = width;
    this->vSize.y = height;
    this->multiSampleType = multiSampleType;

    this->vao1->release();
    this->vao2->release();
    this->reload();
}

void RenderTarget::rebuild(int x, int y, int width, int height) {
    this->rebuild(x, y, width, height, this->multiSampleType);
}

void RenderTarget::rebuild(int width, int height) {
    this->rebuild(this->vPos.x, this->vPos.y, width, height, this->multiSampleType);
}

void RenderTarget::rebuild(int width, int height, MultisampleType multiSampleType) {
    this->rebuild(this->vPos.x, this->vPos.y, width, height, multiSampleType);
}
