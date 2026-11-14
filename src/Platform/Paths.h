// Copyright (c) 2026, WH, All rights reserved.
// runtime-resolved directory layout: read-only assets shipped with the app vs. per-user writable data
#pragma once

#include <string>

namespace Mc::Paths {

// resolves the full path to the running executable, switches the working directory to its folder (unless the
// packager moved the data dir at build time) and resolves the directory layout below. intended to be called once
// early in main() (needs the launch args parsed), before logging is initialized; every accessor returns an empty
// string until then
namespace detail {
void init();
}

// all directories are returned without a trailing slash

// shipped with the app (read-only)
const std::string &exe();        // full canonical path to the executable file itself
const std::string &exe_dir();    // the folder containing the executable
const std::string &assets();     // root of the bundled assets
const std::string &fonts();      // <assets>/fonts
const std::string &materials();  // <assets>/materials
const std::string &libs();       // dynamically loaded libraries (bass, ffmpeg, ...)

// per-user (writable)
const std::string &data();  // root of all persistent user data (-datadir overrides it)
const std::string &cfg();
const std::string &maps();
const std::string &skins();
const std::string &replays();
const std::string &screenshots();
const std::string &exports();
const std::string &editor();  // the beatmap editor session
const std::string &db();
const std::string &cache();
const std::string &logs();

}  // namespace Mc::Paths
