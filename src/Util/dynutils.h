#pragma once
// Copyright (c) 2025, WH, All rights reserved.
#include <string>
#include <string_view>

namespace dynutils {
using lib_obj = struct lib_obj;

namespace detail {
void *load_func_impl(const lib_obj *lib, const char *func_name);
}  // namespace detail

// only attempt loading from system libraries (don't allow local replacements)
lib_obj *load_lib_system(const char *c_lib_name);

lib_obj *load_lib(decltype(nullptr));  // i.e. GetModuleHandle(NULL)

// example usage: load_lib("bass"), load_lib("libbass.so"), load_lib("bass.dll"), load_lib("bass", "lib/")
// you get the point
lib_obj *load_lib(const char *c_lib_name, const char *c_search_dir = "");
void unload_lib(lib_obj *&lib);

// the platform file name for a library: "bass" -> "libbass.so", "libbass.dylib" or "bass.dll"
std::string lib_file_name(std::string_view lib_name);

// whether the library loaded from this path is mapped into the process, e.g. to check that unloading it worked
bool is_loaded(const char *path);

// usage: auto func = load_func<func_prototype>(lib_obj_here, func_name_here)
template <typename F>
inline F *load_func(const lib_obj *lib, const char *func_name) {
    return reinterpret_cast<F *>(detail::load_func_impl(lib, func_name));
}

const char *get_error();

}  // namespace dynutils
