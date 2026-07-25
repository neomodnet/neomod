// Copyright (c) 2016, PG, All rights reserved.
#include "VertexArrayObject.h"

#include "Engine.h"
#include "ContainerRanges.h"
#include "Graphics.h"

static_assert(DrawUsageType::STATIC == DrawUsageType{0});
static_assert(DrawPrimitive::TRIANGLES == DrawPrimitive{2});

void VertexArrayObject::init() {
    // this->bReady may only be set in inheriting classes, if baking was successful
}

void VertexArrayObject::initAsync() { this->setAsyncReady(true); }

void VertexArrayObject::destroy() {
    this->clear();

    this->iNumVertices = 0;
    this->bHasTexcoords = false;
}

void VertexArrayObject::clear() noexcept {
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

void VertexArrayObject::addVertex(vec3 v) noexcept {
    this->vertices.push_back(v);
    ++this->iNumVertices;
}

void VertexArrayObject::addTexcoord(vec2 uv) noexcept { this->texcoords.push_back(uv); }

void VertexArrayObject::addNormal(vec3 normal) noexcept { this->normals.push_back(normal); }

void VertexArrayObject::addColor(Color color) noexcept { this->colors.push_back(color); }

void VertexArrayObject::addVertices(std::vector<vec3> vertices) noexcept {
    Mc::ranges::append(this->vertices, std::move(vertices));
    this->iNumVertices = this->vertices.size();
}

void VertexArrayObject::addTexcoords(std::vector<vec2> texcoords) noexcept {
    Mc::ranges::append(this->texcoords, std::move(texcoords));
}

void VertexArrayObject::addNormals(std::vector<vec3> normals) noexcept {
    Mc::ranges::append(this->normals, std::move(normals));
}

void VertexArrayObject::addColors(std::vector<Color> colors) noexcept {
    Mc::ranges::append(this->colors, std::move(colors));
}

void VertexArrayObject::addVertices(std::span<const vec3> vertices) noexcept {
    Mc::ranges::append(this->vertices, vertices);
    this->iNumVertices = this->vertices.size();
}

void VertexArrayObject::addTexcoords(std::span<const vec2> texcoords) noexcept {
    Mc::ranges::append(this->texcoords, texcoords);
}

void VertexArrayObject::addNormals(std::span<const vec3> normals) noexcept {
    Mc::ranges::append(this->normals, normals);
}

void VertexArrayObject::addColors(std::span<const Color> colors) noexcept { Mc::ranges::append(this->colors, colors); }

void VertexArrayObject::setVertices(std::span<const vec3> vertices) noexcept {
    Mc::ranges::assign(this->vertices, vertices);
    this->iNumVertices = this->vertices.size();
}

void VertexArrayObject::setTexcoords(std::span<const vec2> texcoords) noexcept {
    Mc::ranges::assign(this->texcoords, texcoords);
    this->bHasTexcoords = !this->texcoords.empty();
}

void VertexArrayObject::setNormals(std::span<const vec3> normals) noexcept {
    Mc::ranges::assign(this->normals, normals);
}

void VertexArrayObject::setColors(std::span<const Color> colors) noexcept { Mc::ranges::assign(this->colors, colors); }

void VertexArrayObject::setVertices(std::vector<vec3> &&vertices) noexcept {
    this->vertices = std::move(vertices);
    this->iNumVertices = this->vertices.size();
}

void VertexArrayObject::setTexcoords(std::vector<vec2> &&texcoords) noexcept {
    this->texcoords = std::move(texcoords);
    this->bHasTexcoords = !this->texcoords.empty();
}

void VertexArrayObject::setNormals(std::vector<vec3> &&normals) noexcept { this->normals = std::move(normals); }

void VertexArrayObject::setColors(std::vector<Color> &&colors) noexcept { this->colors = std::move(colors); }

void VertexArrayObject::setVertices(const std::vector<vec3> &vertices) noexcept {
    Mc::ranges::assign(this->vertices, vertices);
    this->iNumVertices = this->vertices.size();
}

void VertexArrayObject::setTexcoords(const std::vector<vec2> &texcoords) noexcept {
    Mc::ranges::assign(this->texcoords, texcoords);
    this->bHasTexcoords = !this->texcoords.empty();
}

void VertexArrayObject::setNormals(const std::vector<vec3> &normals) noexcept {
    Mc::ranges::assign(this->normals, normals);
}

void VertexArrayObject::setColors(const std::vector<Color> &colors) noexcept {
    Mc::ranges::assign(this->colors, colors);
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

int VertexArrayObject::nearestMultipleUp(int number, int multiple) noexcept {
    if(multiple == 0) return number;

    int remainder = number % multiple;
    if(remainder == 0) return number;

    return number + multiple - remainder;
}

int VertexArrayObject::nearestMultipleDown(int number, int multiple) noexcept {
    if(multiple == 0) return number;

    int remainder = number % multiple;
    if(remainder == 0) return number;

    return number - remainder;
}
