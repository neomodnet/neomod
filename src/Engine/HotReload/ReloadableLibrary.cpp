// Copyright (c) 2026, WH, All rights reserved.
#include "ReloadableLibrary.h"

#include "config.h"

#ifdef MCENGINE_FEATURE_HOT_RELOAD

#include "BaseEnvironment.h"
#include "Environment.h"
#include "File.h"
#include "HostStamp.h"
#include "Logging.h"
#include "Paths.h"
#include "Profiler.h"

#include "fmt/format.h"

#include <ctime>
#include <filesystem>
#include <system_error>
#include <vector>

#include <unistd.h>

namespace Mc {
namespace {
void removeCopy(const std::string &dir) {
    std::error_code ec;
    std::filesystem::remove_all(File::getFsPath(dir), ec);
}
}  // namespace

ReloadableLibrary::ReloadableLibrary(std::string name)
    : name(std::move(name)), path(fmt::format("{}/{}", Paths::libs(), dynutils::lib_file_name(this->name))) {}

ReloadableLibrary::~ReloadableLibrary() { this->unload(); }

ReloadableLibrary::LoadResult ReloadableLibrary::load() {
    this->unload();

    if(File::exists(this->path) != File::FILETYPE::FILE) {
        this->error = fmt::format("{} not found", this->path);
        return LoadResult::MISSING;
    }

    // a new copy in a new directory every time: the build can replace the original while it's loaded (windows wouldn't
    // let it otherwise), and loading a path whose image failed to unload hands back that stale image (macos). in a
    // directory of their own the files keep their names, which is how debuggers find the pdb next to the copy
    // (windows). the pid keeps instances apart; copies left behind by instances that didn't get to unload them are
    // pruned by age
    const std::string dir = Paths::cache() + "/hotreload";
    if(Environment::createDirectory(dir) && this->loads == 0) {
        std::vector<File::DirEntry> copies;
        File::getDirectoryEntries(dir, File::DirContents::ALL, copies);
        const i64 now = time(nullptr);
        for(const auto &entry : copies) {
            if(now - entry.mtime > 24 * 60 * 60) removeCopy(dir + '/' + entry.name);
        }
    }
    // a copy that failed to unload is still there, e.g. after an app relaunch in the same process
    do {
        this->copyDir = fmt::format("{}/{}.{}", dir, getpid(), ++this->loads);
    } while(File::exists(this->copyDir) == File::FILETYPE::FOLDER);
    this->copyPath = fmt::format("{}/{}", this->copyDir, dynutils::lib_file_name(this->name));
    if(!Environment::createDirectory(this->copyDir) || !File::copy(this->path, this->copyPath)) {
        this->error = fmt::format("couldn't copy {} to {}", this->path, this->copyPath);
        removeCopy(this->copyDir);
        return LoadResult::COPY_FAILED;
    }
    if constexpr(Env::cfg(OS::WINDOWS)) {
        // the library names its pdb without a directory (/pdbaltpath:%_PDB%), so debuggers look for it next to the copy
        const std::string pdb = fmt::format("{}/{}.pdb", Paths::libs(), this->name);
        if(File::exists(pdb) == File::FILETYPE::FILE &&
           !File::copy(pdb, fmt::format("{}/{}.pdb", this->copyDir, this->name))) {
            debugLog("couldn't copy {} along with the library, debuggers won't find it", pdb);
        }
    }

    this->lib = dynutils::load_lib(this->copyPath.c_str());
    if(!this->lib) {
        this->error = dynutils::get_error();
        removeCopy(this->copyDir);
        return LoadResult::LOAD_FAILED;
    }

    // before anything else in it gets used: built against another program build, it may disagree with this one about
    // any class layout
    const auto *stamp = dynutils::load_func<const u64>(this->lib, "mc_library_host_stamp");
    if(!stamp || *stamp != hostStamp()) {
        const LoadResult result = stamp ? LoadResult::STALE_HOST : LoadResult::NOT_A_LIBRARY;
        this->error = stamp ? fmt::format("{} was built for another build of the program", this->path)
                            : fmt::format("{} is not a reloadable library", this->path);
        this->unload();
        return result;
    }

    this->error.clear();
    return LoadResult::OK;
}

void ReloadableLibrary::unload() {
    if(!this->lib) return;

    // nothing may point into the library after this: queued log messages point at its strings (source locations), and
    // the profiler would match a name at an address that the next library may use for a different one
    g_profCurrentProfile.forgetNameAddresses();
    if(!Logger::flush(true)) {
        // better leaked than unmapped under the logger thread
        debugLog("the log couldn't be written out, leaving {} loaded", this->copyPath);
        this->lib = nullptr;
        return;
    }

    dynutils::unload_lib(this->lib);
    if(dynutils::is_loaded(this->copyPath.c_str())) {
        // the next load uses a new path, so it still gets the new code
        debugLog(
            "{} is still loaded after unloading it (does it define a thread_local, or a static that is destroyed "
            "at thread exit?)",
            this->copyPath);
        return;
    }
    // a pdb that a debugger still holds open stays behind, for the pruning
    removeCopy(this->copyDir);
}

}  // namespace Mc

#endif
