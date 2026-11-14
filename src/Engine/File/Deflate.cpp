// Copyright (c) 2026, WH, All rights reserved.
#include "Deflate.h"

#include <zlib.h>

namespace Mc::Deflate {
std::vector<u8> pack(std::span<const u8> bytes) {
    uLongf size = compressBound((uLong)bytes.size());
    std::vector<u8> packed(size);
    if(compress2(packed.data(), &size, bytes.data(), (uLong)bytes.size(), Z_BEST_SPEED) != Z_OK) return {};
    packed.resize(size);
    return packed;
}

std::optional<std::vector<u8>> unpack(std::span<const u8> packed, uSz size) {
    // deflate packs 1032 bytes into one at best: a larger size is wrong, and isn't allocated to find out
    if(size / 1032 > packed.size()) return std::nullopt;
    std::vector<u8> bytes(size);
    uLongf unpacked = (uLongf)size;
    if(uncompress(bytes.data(), &unpacked, packed.data(), (uLong)packed.size()) != Z_OK || unpacked != size) {
        return std::nullopt;
    }
    return bytes;
}
}  // namespace Mc::Deflate
