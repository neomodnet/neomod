#pragma once
// Copyright (c) 2026, WH, All rights reserved.

#include "bemanproject/cstring_view/cstring_view.hpp"

namespace Mc {
// a std::string_view that is known to be null-terminated, for text that ends up in a C API: c_str() costs nothing.
// take it in place of a `const char *`, or of a `const std::string &` that only has its c_str() used. text that is only
// read or formatted stays a std::string_view. a ternary of one and a std::string is ambiguous: use std::string{view}.
using cstr_view = beman::cstring_view::cstring_view;
}  // namespace Mc
