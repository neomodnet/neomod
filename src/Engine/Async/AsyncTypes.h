// Copyright (c) 2026, WH, All rights reserved.
#pragma once

#include "types.h"

enum class Lane : u8 {
    Foreground,  // default; short/frame-adjacent work
    Background,  // long-running work (archival, enumeration, etc.)
};

namespace Async {

enum class Status : u8 {
    completed,
    cancelled,
};

template <typename T>
struct Result {
    T value;
    Status status;
    [[nodiscard]] bool ok() const noexcept { return status == Status::completed; }
};

template <>
struct Result<void> {
    Status status;
    [[nodiscard]] bool ok() const noexcept { return status == Status::completed; }
};

}  // namespace Async
