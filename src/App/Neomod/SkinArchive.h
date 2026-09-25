// Copyright (c) 2026, WH, All rights reserved.
#pragma once
#include "config.h"
#include "types.h"

#include "AsyncFuture.h"

#include <string>
#include <string_view>

struct Skin;

// .osk files: packing a loaded skin into one, and extracting one into a skins folder
namespace SkinArchive {

// goes into every export, in place of the one from an earlier export
inline constexpr std::string_view LOG_NAME{PACKAGE_NAME "-export.log"};

struct ExportResult {
    enum class Status : u8 { Exported, DefaultSkin, Failed };
    Status status{Status::Failed};
    std::string path;  // the .osk (also set if writing it failed)
};

// packs a loaded skin (or random element mix) into <dir>/<name>.osk on a background thread, with what its fallback
// fills in, to look the same on its own; the default skin's files only go in with include_default (the only way to
// export the default skin itself). a missing name is made up, a taken one gets numbered: <name> (1).osk, (2), ...
[[nodiscard]] Async::Future<ExportResult> submit_export(const Skin &skin, std::string dir, std::string name = {},
                                                        bool include_default = false);

// extracts an .osk into <skins_dir>/<its file name without the extension>/
bool unpack(std::string_view osk_path, std::string_view skins_dir);

}  // namespace SkinArchive
