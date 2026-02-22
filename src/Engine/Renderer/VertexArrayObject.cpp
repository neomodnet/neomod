// Copyright (c) 2016, PG, All rights reserved.
#include "VertexArrayObject.h"

#include "Engine.h"
#include "ContainerRanges.h"

void VertexArrayObject::init() {
    // this->bReady may only be set in inheriting classes, if baking was successful
}

void VertexArrayObject::initAsync() { this->setAsyncReady(true); }

void VertexArrayObject::destroy() {
    this->clear();

    this->iNumVertices = 0;
    this->bHasTexcoords = false;
}

void VertexArrayObject::clear() {
    this->iNumVertices = this->vertices.size();
    this->bHasTexcoords = !this->texcoords.empty();

    this->vertices.clear();
    this->texcoords.clear();
    this->normals.clear();
    this->colors.clear();

    this->partialUpdateVertexIndices.clear();
    this->partialUpdateColorIndices.clear();

    // NOTE: do NOT set m_iNumVertices to 0! (also don't change m_bHasTexcoords)
}

void VertexArrayObject::addVertices(std::vector<vec3> vertices) noexcept {
    Mc::append_range(this->vertices, std::move(vertices));
    this->iNumVertices = this->vertices.size();
}

void VertexArrayObject::addTexcoords(std::vector<vec2> texcoords) noexcept {
    Mc::append_range(this->texcoords, std::move(texcoords));
}

void VertexArrayObject::addNormals(std::vector<vec3> normals) noexcept {
    Mc::append_range(this->normals, std::move(normals));
}

void VertexArrayObject::addColors(std::vector<Color> colors) noexcept {
    Mc::append_range(this->colors, std::move(colors));
}

void VertexArrayObject::setVertex(int index, vec2 v) noexcept {
    if(index < 0 || index > (this->vertices.size() - 1)) return;

    this->vertices[index] = vec3{v.x, v.y, 0.f};

    this->partialUpdateVertexIndices.push_back(index);
}

void VertexArrayObject::setVertex(int index, vec3 v) noexcept {
    if(index < 0 || index > (this->vertices.size() - 1)) return;

    this->vertices[index] = v;

    this->partialUpdateVertexIndices.push_back(index);
}

void VertexArrayObject::setColor(int index, Color color) noexcept {
    if(index < 0 || index > (this->colors.size() - 1)) return;

    this->colors[index] = color;

    this->partialUpdateColorIndices.push_back(index);
}

void VertexArrayObject::setDrawRange(int fromIndex, int toIndex) noexcept {
    this->iDrawRangeFromIndex = fromIndex;
    this->iDrawRangeToIndex = toIndex;
}

void VertexArrayObject::setDrawPercent(float fromPercent, float toPercent, int nearestMultiple) noexcept {
    this->fDrawPercentFromPercent = std::clamp<float>(fromPercent, 0.0f, 1.0f);
    this->fDrawPercentToPercent = std::clamp<float>(toPercent, 0.0f, 1.0f);
    this->iDrawPercentNearestMultiple = nearestMultiple;
}

void VertexArrayObject::reserve(size_t vertexCount) noexcept {
    this->vertices.reserve(vertexCount);
    this->texcoords.reserve(vertexCount);
}

int VertexArrayObject::nearestMultipleUp(int number, int multiple) {
    if(multiple == 0) return number;

    int remainder = number % multiple;
    if(remainder == 0) return number;

    return number + multiple - remainder;
}

int VertexArrayObject::nearestMultipleDown(int number, int multiple) {
    if(multiple == 0) return number;

    int remainder = number % multiple;
    if(remainder == 0) return number;

    return number - remainder;
}
