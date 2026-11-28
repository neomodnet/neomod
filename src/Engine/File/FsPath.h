// Copyright (c) 2026, kiwec, All rights reserved.
// std::filesystem bridge for utf8 paths
// separated from File.h so that only std::filesystem users include <filesystem>
#pragma once

#include <filesystem>
#include <string_view>

namespace Mc {
// fs::path works differently depending on the type of string it was constructed with
// so use this to get a unicode-constructed path on windows (convert), utf8 otherwise (passthrough)
[[nodiscard]] std::filesystem::path getFsPath(std::string_view utf8path);
}  // namespace Mc
