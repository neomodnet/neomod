#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "dynutils.h"
#include "noinclude.h"
#include "types.h"

#include <string>

namespace Mc {
// A dynamic library that gets rebuilt while the program runs (--enable-hot-reload). load() maps a private copy of the
// file, so the build can replace the original, after checking that it was built against this program build. Everything
// that came out of the library has to be destroyed before unload().
class ReloadableLibrary final {
    NOCOPY_NOMOVE(ReloadableLibrary)
   public:
    // `name` without the platform prefix and extension (e.g. "mylib"), looked up in Mc::Paths::libs()
    explicit ReloadableLibrary(std::string name);
    ~ReloadableLibrary();

    enum class LoadResult : u8 { OK, MISSING, COPY_FAILED, LOAD_FAILED, NOT_A_LIBRARY, STALE_HOST };
    // unloads the library first if it's loaded; getError() has the failure details
    LoadResult load();
    void unload();

    template <typename F>
    [[nodiscard]] F *getFunction(const char *name) const {
        return dynutils::load_func<F>(this->lib, name);
    }
    [[nodiscard]] const std::string &getError() const { return this->error; }
    // where the build installs the library (load() maps a copy of it)
    [[nodiscard]] const std::string &getPath() const { return this->path; }

   private:
    std::string name;
    std::string path;
    std::string error;
    std::string copyDir;  // a directory per load, with the copy that's loaded now
    std::string copyPath;
    dynutils::lib_obj *lib{nullptr};
    u32 loads{0};
};
}  // namespace Mc
