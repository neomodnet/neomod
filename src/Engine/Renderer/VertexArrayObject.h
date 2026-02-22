// Copyright (c) 2016, PG, All rights reserved.
#ifndef VERTEXARRAYOBJECT_H
#define VERTEXARRAYOBJECT_H

#include "Resource.h"
#include "Graphics.h"

class VertexArrayObject : public Resource {
    NOCOPY_NOMOVE(VertexArrayObject)
   public:
    constexpr VertexArrayObject(DrawPrimitive primitive = DrawPrimitive::TRIANGLES,
                                DrawUsageType usage = DrawUsageType::STATIC, bool keepInSystemMemory = false)
        : Resource(VAO), primitive(primitive), usage(usage), bKeepInSystemMemory(keepInSystemMemory) {}
    ~VertexArrayObject() override = default;

    void clear();

    inline void addVertex(vec3 v) noexcept {
        this->vertices.push_back(v);
        ++this->iNumVertices;
    }

    inline void addVertex(vec2 v) noexcept { return addVertex(vec3{v.x, v.y, 0.f}); }
    inline void addVertex(float x, float y, float z = 0.f) noexcept { return addVertex(vec3{x, y, z}); }
    void addVertices(std::vector<vec3> vertices) noexcept;

    inline void addTexcoord(vec2 uv) noexcept { this->texcoords.push_back(uv); }
    inline void addTexcoord(float u, float v) noexcept { return addTexcoord(vec2{u, v}); }

    void addTexcoords(std::vector<vec2> texcoords) noexcept;

    inline void addNormal(vec3 normal) noexcept { this->normals.push_back(normal); }
    inline void addNormal(float x, float y, float z) noexcept { return addNormal(vec3{x, y, z}); }
    void addNormals(std::vector<vec3> normals) noexcept;

    inline void addColor(Color color) noexcept { this->colors.push_back(color); }
    void addColors(std::vector<Color> color) noexcept;

    void setVertex(int index, vec2 v) noexcept;
    void setVertex(int index, vec3 v) noexcept;
    inline void setVertex(int index, float x, float y, float z = 0.f) noexcept {
        return setVertex(index, vec3{x, y, z});
    }
    inline void setVertices(const std::vector<vec3> &vertices) noexcept {
        this->vertices = vertices;
        this->iNumVertices = this->vertices.size();
    }

    inline void setTexcoords(const std::vector<vec2> &texcoords) noexcept {
        this->texcoords = texcoords;
        this->bHasTexcoords = !texcoords.empty();
    }

    inline void setNormals(const std::vector<vec3> &normals) noexcept { this->normals = normals; }

    inline void setColors(const std::vector<Color> &colors) noexcept { this->colors = colors; }
    void setColor(int index, Color color) noexcept;

    inline void setType(DrawPrimitive primitive) noexcept { this->primitive = primitive; }
    void setDrawRange(int fromIndex, int toIndex) noexcept;
    void setDrawPercent(float fromPercent = 0.0f, float toPercent = 1.0f,
                        int nearestMultiple = 0) noexcept;  // DEPRECATED

    // optimization: pre-allocate space to avoid reallocations during batch operations
    void reserve(size_t vertexCount) noexcept;

    [[nodiscard]] inline DrawPrimitive getPrimitive() const { return this->primitive; }
    [[nodiscard]] inline DrawUsageType getUsage() const { return this->usage; }

    [[nodiscard]] const std::vector<vec3> &getVertices() const { return this->vertices; }
    [[nodiscard]] const std::vector<vec2> &getTexcoords() const { return this->texcoords; }
    [[nodiscard]] const std::vector<vec3> &getNormals() const { return this->normals; }
    [[nodiscard]] const std::vector<Color> &getColors() const { return this->colors; }

    [[nodiscard]] inline unsigned int getNumVertices() const { return this->iNumVertices; }
    [[nodiscard]] inline bool hasTexcoords() const { return this->bHasTexcoords || this->texcoords.size() > 0; }

    virtual void draw() { assert(false); }  // implementation dependent (gl/dx11/etc.)

    VertexArrayObject *asVAO() final { return this; }
    [[nodiscard]] const VertexArrayObject *asVAO() const final { return this; }

   protected:
    static int nearestMultipleUp(int number, int multiple);
    static int nearestMultipleDown(int number, int multiple);

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
