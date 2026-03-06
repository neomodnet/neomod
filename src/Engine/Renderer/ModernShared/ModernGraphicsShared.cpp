// Copyright (c) 2026, WH, All rights reserved.
#include "config.h"

#if (defined(MCENGINE_FEATURE_GLES32) || defined(MCENGINE_FEATURE_DIRECTX11) || defined(MCENGINE_FEATURE_SDLGPU))

// TODO: share more code

#include "ModernGraphicsShared.h"
#include "VertexArrayObject.h"

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

    static VertexArrayObject vao(DrawPrimitive::LINES);
    {
        vao.clear();
        vao.addVertex(x1, y1);
        vao.addVertex(x2, y2);
    }
    this->drawVAO(&vao);
}

void ModernGraphicsShared::drawRectf(const RectOptions &opts) {
    this->updateTransform();

    if(opts.lineThickness > 1.0f) {
        this->setTexturing(false);
        const float halfThickness = opts.lineThickness * 0.5f;

        if(opts.withColor) {
            this->setColor(opts.top);
            this->fillRectf(opts.x - halfThickness, opts.y - halfThickness, opts.width + opts.lineThickness,
                            opts.lineThickness);
            this->setColor(opts.bottom);
            this->fillRectf(opts.x - halfThickness, opts.y + opts.height - halfThickness,
                            opts.width + opts.lineThickness, opts.lineThickness);
            this->setColor(opts.left);
            this->fillRectf(opts.x - halfThickness, opts.y + halfThickness, opts.lineThickness,
                            opts.height - opts.lineThickness);
            this->setColor(opts.right);
            this->fillRectf(opts.x + opts.width - halfThickness, opts.y + halfThickness, opts.lineThickness,
                            opts.height - opts.lineThickness);
        } else {
            this->fillRectf(opts.x - halfThickness, opts.y - halfThickness, opts.width + opts.lineThickness,
                            opts.lineThickness);
            this->fillRectf(opts.x - halfThickness, opts.y + opts.height - halfThickness,
                            opts.width + opts.lineThickness, opts.lineThickness);
            this->fillRectf(opts.x - halfThickness, opts.y + halfThickness, opts.lineThickness,
                            opts.height - opts.lineThickness);
            this->fillRectf(opts.x + opts.width - halfThickness, opts.y + halfThickness, opts.lineThickness,
                            opts.height - opts.lineThickness);
        }
    } else {
        if(opts.withColor) {
            this->setColor(opts.top);
            this->drawLinef(opts.x, opts.y, opts.x + opts.width, opts.y);
            this->setColor(opts.left);
            this->drawLinef(opts.x, opts.y, opts.x, opts.y + opts.height);
            this->setColor(opts.bottom);
            this->drawLinef(opts.x, opts.y + opts.height, opts.x + opts.width, opts.y + opts.height + 0.5f);
            this->setColor(opts.right);
            this->drawLinef(opts.x + opts.width, opts.y, opts.x + opts.width, opts.y + opts.height + 0.5f);
        } else {
            this->drawLinef(opts.x, opts.y, opts.x + opts.width, opts.y);
            this->drawLinef(opts.x, opts.y, opts.x, opts.y + opts.height);
            this->drawLinef(opts.x, opts.y + opts.height, opts.x + opts.width, opts.y + opts.height + 0.5f);
            this->drawLinef(opts.x + opts.width, opts.y, opts.x + opts.width, opts.y + opts.height + 0.5f);
        }
    }
}

void ModernGraphicsShared::fillRectf(float x, float y, float width, float height) {
    this->updateTransform();
    this->setTexturing(false);

    static VertexArrayObject vao(DrawPrimitive::TRIANGLE_STRIP);
    {
        vao.clear();
        vao.addVertex(x, y);
        vao.addVertex(x, y + height);
        vao.addVertex(x + width, y);
        vao.addVertex(x + width, y + height);
    }
    this->drawVAO(&vao);
}

void ModernGraphicsShared::fillRoundedRect(int x, int y, int width, int height, int /*radius*/) {
    // TODO: implement rounded corners
    this->fillRectf((float)x, (float)y, (float)width, (float)height);
}

void ModernGraphicsShared::fillGradient(int x, int y, int width, int height, Color topLeftColor, Color topRightColor,
                                        Color bottomLeftColor, Color bottomRightColor) {
    this->updateTransform();
    this->setTexturing(false);  // disable texturing

    static VertexArrayObject vao(DrawPrimitive::TRIANGLE_STRIP);
    {
        vao.clear();
        vao.addVertex(x, y);
        vao.addColor(topLeftColor);
        vao.addVertex(x, y + height);
        vao.addColor(bottomLeftColor);
        vao.addVertex(x + width, y);
        vao.addColor(topRightColor);
        vao.addVertex(x + width, y + height);
        vao.addColor(bottomRightColor);
    }
    this->drawVAO(&vao);
}

void ModernGraphicsShared::drawQuad(int x, int y, int width, int height) {
    this->updateTransform();
    this->setTexturing(true);  // enable texturing

    static VertexArrayObject vao(DrawPrimitive::TRIANGLE_STRIP);
    {
        vao.clear();
        vao.addVertex(x, y);
        vao.addTexcoord(0, 0);
        vao.addVertex(x, y + height);
        vao.addTexcoord(0, 1);
        vao.addVertex(x + width, y);
        vao.addTexcoord(1, 0);
        vao.addVertex(x + width, y + height);
        vao.addTexcoord(1, 1);
    }
    this->drawVAO(&vao);
}

void ModernGraphicsShared::drawQuad(vec2 topLeft, vec2 topRight, vec2 bottomRight, vec2 bottomLeft, Color topLeftColor,
                                    Color topRightColor, Color bottomRightColor, Color bottomLeftColor) {
    this->updateTransform();
    this->setTexturing(false);  // disable texturing

    static VertexArrayObject vao(DrawPrimitive::TRIANGLE_STRIP);
    {
        vao.clear();
        vao.addVertex(topLeft.x, topLeft.y);
        vao.addColor(topLeftColor);
        vao.addVertex(bottomLeft.x, bottomLeft.y);
        vao.addColor(bottomLeftColor);
        vao.addVertex(topRight.x, topRight.y);
        vao.addColor(topRightColor);
        vao.addVertex(bottomRight.x, bottomRight.y);
        vao.addColor(bottomRightColor);
    }
    this->drawVAO(&vao);
}

#endif