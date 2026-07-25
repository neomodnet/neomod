// Copyright (c) 2016, PG, All rights reserved.
#ifndef VERTEXARRAYOBJECT_H
#define VERTEXARRAYOBJECT_H

#include "Resource.h"
#include "Graphics_fwd.h"
#include "Color.h"
#include "Vectors.h"

#include <span>
#include <vector>

class VertexArrayObject : public Resource {
    MOVECONSTRUCTONLY(VertexArrayObject)
   public:
    constexpr VertexArrayObject(DrawPrimitive primitive = DrawPrimitive{2} /* TRIANGLES */,
                                DrawUsageType usage = DrawUsageType{0} /* STATIC */, bool keepInSystemMemory = false)
        : Resource(VAO), primitive(primitive), usage(usage), bKeepInSystemMemory(keepInSystemMemory) {}
    ~VertexArrayObject() override = default;

    void clear() noexcept;

    void addVertex(vec3 v) noexcept;
    inline void addVertex(vec2 v) noexcept { return addVertex(vec3{v.x, v.y, 0.f}); }
    inline void addVertex(float x, float y, float z = 0.f) noexcept { return addVertex(vec3{x, y, z}); }
    void addVertices(std::vector<vec3> vertices) noexcept;
    void addVertices(std::span<const vec3> vertices) noexcept;

    void addTexcoord(vec2 uv) noexcept;
    inline void addTexcoord(float u, float v) noexcept { return addTexcoord(vec2{u, v}); }

    void addTexcoords(std::vector<vec2> texcoords) noexcept;
    void addTexcoords(std::span<const vec2> texcoords) noexcept;

    void addNormal(vec3 normal) noexcept;
    inline void addNormal(float x, float y, float z) noexcept { return addNormal(vec3{x, y, z}); }
    void addNormals(std::vector<vec3> normals) noexcept;
    void addNormals(std::span<const vec3> normals) noexcept;

    void addColor(Color color) noexcept;
    void addColors(std::vector<Color> color) noexcept;
    void addColors(std::span<const Color> color) noexcept;

    // NOTE/TODO: this is "broken" for baked VAOs + primitives which need to be converted on non-GL backends (e.g. quads->triangles)
    void setVertex(int index, vec2 v) noexcept;
    void setVertex(int index, vec3 v) noexcept;
    inline void setVertex(int index, float x, float y, float z = 0.f) noexcept {
        return setVertex(index, vec3{x, y, z});
    }

    void setVertices(std::span<const vec3> vertices) noexcept;
    void setTexcoords(std::span<const vec2> texcoords) noexcept;
    void setNormals(std::span<const vec3> normals) noexcept;
    void setColors(std::span<const Color> colors) noexcept;

    void setVertices(std::vector<vec3> &&vertices) noexcept;
    void setTexcoords(std::vector<vec2> &&texcoords) noexcept;
    void setNormals(std::vector<vec3> &&normals) noexcept;
    void setColors(std::vector<Color> &&colors) noexcept;

    void setVertices(const std::vector<vec3> &vertices) noexcept;
    void setTexcoords(const std::vector<vec2> &texcoords) noexcept;
    void setNormals(const std::vector<vec3> &normals) noexcept;
    void setColors(const std::vector<Color> &colors) noexcept;

    void setColor(int index, Color color) noexcept;

    inline void setType(DrawPrimitive primitive) noexcept { this->primitive = primitive; }
    void setDrawRange(int fromIndex, int toIndex) noexcept;
    void setDrawPercent(float fromPercent = 0.0f, float toPercent = 1.0f,
                        int nearestMultiple = 0) noexcept;  // DEPRECATED

    // optimization: pre-allocate space to avoid reallocations during batch operations
    void reserve(size_t vertexCount) noexcept;

    [[nodiscard]] inline DrawPrimitive getPrimitive() const noexcept { return this->primitive; }
    [[nodiscard]] inline DrawUsageType getUsage() const noexcept { return this->usage; }

    [[nodiscard]] std::span<const vec3> getVertices() const noexcept { return {this->vertices.data(), this->vertices.size()}; }
    [[nodiscard]] std::span<const vec2> getTexcoords() const noexcept {
        return {this->texcoords.data(), this->texcoords.size()};
    }
    [[nodiscard]] std::span<const vec3> getNormals() const noexcept { return {this->normals.data(), this->normals.size()}; }
    [[nodiscard]] std::span<const Color> getColors() const noexcept { return {this->colors.data(), this->colors.size()}; }

    [[nodiscard]] inline unsigned int getNumVertices() const noexcept { return this->iNumVertices; }
    [[nodiscard]] inline bool hasTexcoords() const noexcept { return this->bHasTexcoords || this->texcoords.size() > 0; }

    virtual void draw() { assert(false); }  // implementation dependent (gl/dx11/etc.)

    VertexArrayObject *asVAO() final { return this; }
    [[nodiscard]] const VertexArrayObject *asVAO() const final { return this; }

   protected:
    static int nearestMultipleUp(int number, int multiple) noexcept;
    static int nearestMultipleDown(int number, int multiple) noexcept;

    void init() override;
    void initAsync() override;
    void destroy() override;

    std::vector<vec3> vertices;
    std::vector<vec2> texcoords;
    std::vector<vec3> normals;
    std::vector<Color> colors;

    std::vector<int> partialUpdateVertexIndices;
    std::vector<int> partialUpdateColorIndices;

    unsigned int iNumVertices{0};
    int iDrawRangeFromIndex{-1};
    int iDrawRangeToIndex{-1};
    int iDrawPercentNearestMultiple{0};
    float fDrawPercentFromPercent{0.f};
    float fDrawPercentToPercent{1.f};

    DrawPrimitive primitive;
    DrawUsageType usage;
    bool bKeepInSystemMemory;
    bool bHasTexcoords{false};
};

#endif
