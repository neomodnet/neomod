#pragma once
// Copyright (c) 2012, PG, All rights reserved.
#include "config.h"
#include "noinclude.h"
#include "Vectors.h"

#ifndef BUILD_TOOLS_ONLY
#include "fmt/format.h"
#endif

template <typename Vec = vec2>
class McRectBase {
    using scalar = Vec::value_type;

   public:
    McRectBase(scalar x = 0, scalar y = 0, scalar width = 0, scalar height = 0, bool isCentered = false);
    McRectBase(Vec pos, Vec size, bool isCentered = false);

    ~McRectBase();
    McRectBase(const McRectBase &);
    McRectBase &operator=(const McRectBase &);
    McRectBase(McRectBase &&) noexcept;
    McRectBase &operator=(McRectBase &&) noexcept;

    template <typename OtherVec>
        requires(!std::is_same_v<OtherVec, Vec>)
    McRectBase(const McRectBase<OtherVec> &other);

    // grow to a union of another rect
    void grow(const McRectBase &other);

    // grow to include a point
    void grow(Vec point);

    // loosely within (inside or equals (+ lenience amount))
    [[nodiscard]] bool contains(Vec point, scalar lenience = 0) const;

    // strictly within (not or-equal)
    [[nodiscard]] bool containsStrict(Vec point) const;

    [[nodiscard]] bool intersects(const McRectBase &rect) const;

    [[nodiscard]] McRectBase intersect(const McRectBase &rect) const;

    [[nodiscard]] McRectBase Union(const McRectBase &other) const;

    [[nodiscard]] Vec getCenter() const;
    [[nodiscard]] Vec getMax() const;

    // get
    [[nodiscard]] const Vec &getPos() const { return this->vMin; }
    [[nodiscard]] const Vec &getMin() const { return this->vMin; }
    [[nodiscard]] const Vec &getSize() const { return this->vSize; }

    [[nodiscard]] const scalar &getX() const { return this->vMin.x; }
    [[nodiscard]] const scalar &getY() const { return this->vMin.y; }
    [[nodiscard]] const scalar &getMinX() const { return this->vMin.x; }
    [[nodiscard]] const scalar &getMinY() const { return this->vMin.y; }

    [[nodiscard]] scalar getMaxX() const { return this->vMin.x + this->vSize.x; }
    [[nodiscard]] scalar getMaxY() const { return this->vMin.y + this->vSize.y; }

    [[nodiscard]] const scalar &getWidth() const { return this->vSize.x; }
    [[nodiscard]] const scalar &getHeight() const { return this->vSize.y; }

    // set
    void setMin(Vec min) { this->vMin = min; }
    void setMax(Vec max) { this->vSize = max - this->vMin; }
    void setMinX(scalar minx) { this->vMin.x = minx; }
    void setMinY(scalar miny) { this->vMin.y = miny; }
    void setMaxX(scalar maxx) { this->vSize.x = maxx - this->vMin.x; }
    void setMaxY(scalar maxy) { this->vSize.y = maxy - this->vMin.y; }
    void setPos(Vec pos) { this->vMin = pos; }
    void setPosX(scalar posx) { this->vMin.x = posx; }
    void setPosY(scalar posy) { this->vMin.y = posy; }
    void setSize(Vec size) { this->vSize = size; }
    void setWidth(scalar width) { this->vSize.x = width; }
    void setHeight(scalar height) { this->vSize.y = height; }

    [[nodiscard]] bool operator==(const McRectBase &rhs) const;

   private:
    void set(scalar x, scalar y, scalar width, scalar height, bool isCentered = false);

    void set(Vec pos, Vec size, bool isCentered = false);

    Vec vMin;
    Vec vSize;

    template <typename>
    friend class McRectBase;

#ifndef BUILD_TOOLS_ONLY
    template <typename, typename, typename>
    friend struct fmt::formatter;
#endif
};

extern template class McRectBase<vec2>;
extern template class McRectBase<ivec2>;

using McRect = McRectBase<vec2>;
using McFRect = McRectBase<vec2>;
using McIRect = McRectBase<ivec2>;

#ifndef BUILD_TOOLS_ONLY  // avoid an unnecessary dependency on fmt when building tools only
namespace fmt {
template <typename Vec>
struct formatter<McRectBase<Vec>> {
    template <typename ParseContext>
    constexpr auto parse(ParseContext &ctx) const {
        return ctx.begin();
    }

    template <typename FormatContext>
    auto format(const McRectBase<Vec> &r, FormatContext &ctx) const {
        if constexpr(std::is_floating_point_v<typename McRectBase<Vec>::scalar>) {
            return format_to(ctx.out(), "({:.2f},{:.2f}): {:.2f}x{:.2f}", r.vMin.x, r.vMin.y, r.vSize.x, r.vSize.y);
        } else {
            return format_to(ctx.out(), "({},{}): {}x{}", r.vMin.x, r.vMin.y, r.vSize.x, r.vSize.y);
        }
    }
};
}  // namespace fmt
#endif
