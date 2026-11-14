#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "types.h"

namespace Mc {
// identifies this program build (with --enable-hot-reload): a new value whenever any of its objects was rebuilt, so a
// reloadable library compiled against another build (LibraryStamp.cpp) can be told apart
u64 hostStamp();
}  // namespace Mc
