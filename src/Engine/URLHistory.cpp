// Copyright (c) 2026, kiwec, All rights reserved.
#include "URLHistory.h"

#ifdef MCENGINE_PLATFORM_WASM
#include "noinclude.h"

#include <emscripten/em_js.h>

EM_JS(void, set_path_internal, (const char* path), { history.replaceState({}, "", UTF8ToString(path)); })

static CONSTINIT std::string last_string{};

#endif

namespace Mc::URLHistory {

void replaceState([[maybe_unused]] const std::string& path) {
#ifdef MCENGINE_PLATFORM_WASM
    if(path == last_string) return;
    last_string = path;
    set_path_internal(path.c_str());
#else
    // nothing
#endif
}

}  // namespace Mc::URLHistory
