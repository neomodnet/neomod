// Copyright (c) 2012, PG, All rights reserved.
#include "Rect.h"

template <typename Vec>
McRectBase<Vec>::~McRectBase() = default;

template <typename Vec>
McRectBase<Vec>::McRectBase(const McRectBase &) = default;

template <typename Vec>
McRectBase<Vec> &McRectBase<Vec>::operator=(const McRectBase &) = default;

template <typename Vec>
McRectBase<Vec>::McRectBase(McRectBase &&) noexcept = default;

template <typename Vec>
McRectBase<Vec> &McRectBase<Vec>::operator=(McRectBase &&other) noexcept = default;

template <typename Vec>
McRectBase<Vec>::McRectBase(scalar x, scalar y, scalar width, scalar height, bool isCentered) {
    this->set(x, y, width, height, isCentered);
}

template <typename Vec>
McRectBase<Vec>::McRectBase(Vec pos, Vec size, bool isCentered) {
    this->set(pos, size, isCentered);
}

template <typename Vec>
template <typename OtherVec>
    requires(!std::is_same_v<OtherVec, Vec>)
McRectBase<Vec>::McRectBase(const McRectBase<OtherVec> &other) : vMin(other.vMin), vSize(other.vSize) {}

namespace {
template <typename V>
forceinline V vmax(V v1, V v2) {
    return {(v1.x < v2.x) ? v2.x : v1.x, (v1.y < v2.y) ? v2.y : v1.y};
}
template <typename V>
forceinline V vmin(V v1, V v2) {
    return {(v1.x < v2.x) ? v1.x : v2.x, (v1.y < v2.y) ? v1.y : v2.y};
}

}  // namespace

// grow to a union of another rect
template <typename Vec>
void McRectBase<Vec>::grow(const McRectBase &other) {
    const Vec oldMax = this->vMin + this->vSize;
    const Vec otherMax = other.vMin + other.vSize;

    this->vMin = vmin(this->vMin, other.vMin);
    Vec resultMax = vmax(oldMax, otherMax);
    this->vSize = resultMax - this->vMin;
}

// grow to include a point
template <typename Vec>
void McRectBase<Vec>::grow(Vec point) {
    const Vec oldMax = this->vMin + this->vSize;
    this->vMin = vmin(this->vMin, point);
    this->vSize = vmax(oldMax, point) - this->vMin;
}

// loosely within (inside or equals (+ lenience amount))
template <typename Vec>
bool McRectBase<Vec>::contains(Vec point, scalar lenience) const {
    return vec::all(vec::greaterThanEqual(point + lenience, this->vMin)) &&
           vec::all(vec::lessThanEqual(point - lenience, this->vMin + this->vSize));
}

// strictly within (not or-equal)
template <typename Vec>
bool McRectBase<Vec>::containsStrict(Vec point) const {
    return vec::all(vec::greaterThan(point, this->vMin)) && vec::all(vec::lessThan(point, this->vMin + this->vSize));
}

template <typename Vec>
bool McRectBase<Vec>::intersects(const McRectBase &rect) const {
    const Vec maxMin = vmax(this->vMin, rect.vMin);
    const Vec minMax = vmin(this->vMin + this->vSize, rect.vMin + rect.vSize);
    return maxMin.x < minMax.x && maxMin.y < minMax.y;
}

template <typename Vec>
McRectBase<Vec> McRectBase<Vec>::intersect(const McRectBase &rect) const {
    McRectBase intersection;

    Vec thisMax = this->vMin + this->vSize;
    Vec rectMax = rect.vMin + rect.vSize;

    intersection.vMin = vmax(this->vMin, rect.vMin);
    Vec intersectMax = vmin(thisMax, rectMax);

    if(vec::any(vec::greaterThan(intersection.vMin, intersectMax))) {
        intersection.vMin = Vec{0};
        intersection.vSize = Vec{0};
    } else {
        intersection.vSize = intersectMax - intersection.vMin;
    }

    return intersection;
}

template <typename Vec>
McRectBase<Vec> McRectBase<Vec>::Union(const McRectBase &other) const {
    McRectBase result;

    Vec thisMax = this->vMin + this->vSize;
    Vec rectMax = other.vMin + other.vSize;

    result.vMin = vmin(this->vMin, other.vMin);
    Vec resultMax = vmax(thisMax, rectMax);
    result.vSize = resultMax - result.vMin;

    return result;
}

template <typename Vec>
Vec McRectBase<Vec>::getCenter() const {
    return this->vMin + this->vSize / scalar(2);
}
template <typename Vec>
Vec McRectBase<Vec>::getMax() const {
    return this->vMin + this->vSize;
}

// set

template <typename Vec>
bool McRectBase<Vec>::operator==(const McRectBase &rhs) const {
    return (this->vMin == rhs.vMin) && (this->vSize == rhs.vSize);
}

template <typename Vec>
void McRectBase<Vec>::set(scalar x, scalar y, scalar width, scalar height, bool isCentered) {
    this->set(Vec(x, y), Vec(width, height), isCentered);
}

template <typename Vec>
void McRectBase<Vec>::set(Vec pos, Vec size, bool isCentered) {
    if(isCentered) {
        this->vMin = pos - size / scalar(2);
    } else {
        this->vMin = pos;
    }
    this->vSize = size;
}

template class McRectBase<vec2>;
template class McRectBase<ivec2>;
