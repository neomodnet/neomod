// Copyright (c) 2025, WH, All rights reserved.
#include "dynutils.h"
#include "Environment.h"
#include "Paths.h"

#include <SDL3/SDL_loadso.h>
#include <cassert>

#ifdef MCENGINE_PLATFORM_WINDOWS
#define LPREFIX ""
#define LSUFFIX ".dll"

#include "RuntimePlatform.h"

#include "WinDebloatDefs.h"
#include <windows.h>
#include <libloaderapi.h>

#else
#define LPREFIX "lib"
#ifdef MCENGINE_PLATFORM_MACOS
#define LSUFFIX ".dylib"
#else
#define LSUFFIX ".so"
#endif
#endif

#define LNAMESTR(lib) fmt::format(LPREFIX "{:s}" LSUFFIX, (lib))

namespace dynutils {
namespace {
static thread_local std::string last_error{"No error"};
}

namespace detail {

void *load_func_impl(const lib_obj *lib, const char *func_name) {
    void *retfunc = nullptr;
    if(lib) {
        retfunc = reinterpret_cast<void *>(
            SDL_LoadFunction(reinterpret_cast<SDL_SharedObject *>(const_cast<lib_obj *>(lib)), func_name));
        if(!retfunc) {
            last_error = SDL_GetError();
        } else {
            last_error = "No error";
        }
    }
    return retfunc;
}

}  // namespace detail

void unload_lib(lib_obj *&lib) {
    if(lib) {
        SDL_UnloadObject(reinterpret_cast<SDL_SharedObject *>(lib));
    }
    lib = nullptr;
}

// example usage: load_lib("bass"), load_lib("libbass.so"), load_lib("bass.dll"), load_lib("bass", "lib/")
// you get the point
lib_obj *load_lib(const char *c_lib_name, const char *c_search_dir) {
    std::string lib_name{c_lib_name};
    std::string search_dir{c_search_dir};
    lib_obj *ret = nullptr;
    if(!lib_name.empty()) {
        if(Environment::getFileExtensionFromFilePath(lib_name).empty()) {
            lib_name = LNAMESTR(lib_name);
        }
        if(!search_dir.empty()) {
            if(!search_dir.ends_with('/') && search_dir.ends_with('\\')) {
                search_dir.push_back('/');
            }
            lib_name = search_dir + lib_name;
        }
        ret = reinterpret_cast<lib_obj *>(SDL_LoadObject(lib_name.c_str()));
    }
    if(!ret) {
        if(!lib_name.empty() && !lib_name.contains('/')) {
            // try to fall back to relative local paths first before giving up entirely
            for(const auto &path : {Mc::Paths::libs() + "/", Mc::Paths::assets() + "/"}) {
                std::string temp_relative = fmt::format("{}{}", path, lib_name);
                if((ret = reinterpret_cast<lib_obj *>(SDL_LoadObject(temp_relative.c_str())))) {
                    // found
                    break;
                }
            }
        }
    }
    if(!ret) {
        if(lib_name.empty()) {
            last_error = "Empty library name";
        } else {
            last_error = SDL_GetError();
        }
    } else {
        last_error = "No error";
    }
    return ret;
}

#ifdef MCENGINE_PLATFORM_WINDOWS

lib_obj *load_lib_system(const char *c_lib_name) {
    if(!c_lib_name || *c_lib_name == '\0') {
        last_error = "Empty library name";
        return nullptr;
    }

    std::string lib_name{c_lib_name};
    if(Environment::getFileExtensionFromFilePath(lib_name).empty()) {
        lib_name = LNAMESTR(lib_name);
    }

    // LOAD_LIBRARY_SEARCH_SYSTEM32 is not supported on windows xp
    const auto plat = RuntimePlatform::current();
    const int ldlib_flags =
        (!(plat & RuntimePlatform::WIN_WINE) && (plat & RuntimePlatform::WIN_XP)) ? 0 : LOAD_LIBRARY_SEARCH_SYSTEM32;

    auto *ret = reinterpret_cast<lib_obj *>(LoadLibraryExA(lib_name.c_str(), nullptr, ldlib_flags));

    if(!ret) {
        DWORD errorCode = GetLastError();
        if(errorCode != 0) {
            LPSTR messageBuffer = nullptr;
            DWORD size = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, nullptr);
            if(size != 0) {
                std::string result(messageBuffer, size);
                LocalFree(messageBuffer);
                while(!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
                last_error = result;
            } else {
                last_error = std::string("Unknown error (code: ") + std::to_string(errorCode) + ")";
            }
        } else {
            last_error = "No error";
        }
    }
    return ret;
}

lib_obj *load_lib(decltype(nullptr)) {
    auto *ret = reinterpret_cast<lib_obj *>(GetModuleHandle(nullptr));
    assert(ret);
    return ret;
}

#else

#include <dlfcn.h>

// not required for other platforms (they will load anything from LD_LIBRARY_PATH, which doesn't include relative paths by default)
lib_obj *load_lib_system(const char *c_lib_name) { return load_lib(c_lib_name); }

lib_obj *load_lib(decltype(nullptr)) {
    auto *ret = reinterpret_cast<lib_obj *>(dlopen(nullptr, RTLD_NOW | RTLD_GLOBAL));
    assert(ret);
    return ret;
}

#endif

const char *get_error() { return last_error.c_str(); }

#undef LNAME
#undef LNAMESTR
#undef LPREFIX
#undef LSUFFIX

}  // namespace dynutils
