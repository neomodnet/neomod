#pragma once
// Copyright (c) 2024, kiwec, All rights reserved.

#include "types.h"

#include <span>
#include <string_view>
#include <vector>

namespace BANCHO::AES {
std::vector<u8> encrypt(std::span<const u8, 32> iv, std::string_view msg);
}
