#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "types.h"

#include <optional>
#include <span>
#include <vector>

// bytes packed as a zlib stream, fast rather than small, and unpacked again
namespace Mc::Deflate {
[[nodiscard]] std::vector<u8> pack(std::span<const u8> bytes);
// `size` is how many bytes were packed; none if `packed` doesn't unpack to exactly that many
[[nodiscard]] std::optional<std::vector<u8>> unpack(std::span<const u8> packed, uSz size);
}  // namespace Mc::Deflate
