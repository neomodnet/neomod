// Copyright (c) 2026, WH, All rights reserved.
#include "Matrices.h"

namespace fmt {
format_context::iterator formatter<Matrix2>::format(const Matrix2& m, format_context& ctx) const {
    return format_to(ctx.out(), "[{:10.5f} {:10.5f}]\n[{:10.5f} {:10.5f}]", m[0], m[2], m[1], m[3]);
}

format_context::iterator formatter<Matrix3>::format(const Matrix3& m, format_context& ctx) const {
    return format_to(ctx.out(),
                     "[{:10.5f} {:10.5f} {:10.5f}]\n[{:10.5f} {:10.5f} {:10.5f}]\n[{:10.5f} {:10.5f} {:10.5f}]", m[0],
                     m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]);
}

format_context::iterator formatter<Matrix4>::format(const Matrix4& m, format_context& ctx) const {
    return format_to(ctx.out(),
                     "[{:10.5f} {:10.5f} {:10.5f} {:10.5f}]\n[{:10.5f} {:10.5f} {:10.5f} {:10.5f}]\n[{:10.5f} {:10.5f} "
                     "{:10.5f} {:10.5f}]\n[{:10.5f} {:10.5f} {:10.5f} {:10.5f}]",
                     m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14], m[3], m[7], m[11],
                     m[15]);
}
}  // namespace fmt
