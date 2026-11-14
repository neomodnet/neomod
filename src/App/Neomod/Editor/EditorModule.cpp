// Copyright (c) 2026, WH, All rights reserved.
#include "EditorModule.h"

#include "Editor.h"
#include "EditorTests.h"
#include "Logging.h"

namespace {
CBaseUIElement *create(std::span<const u8> session) {
#ifdef EDITOR_PROBE
    // tells builds apart in tests that rebuild the library while the game runs (CXXFLAGS_EXTRA=-DEDITOR_PROBE=n)
    debugLog("editor probe {}", EDITOR_PROBE);
#endif
    return new Editor(session);
}

std::vector<u8> save(CBaseUIElement *root) { return static_cast<Editor *>(root)->saveSession(); }

void destroy(CBaseUIElement *root) { delete static_cast<Editor *>(root); }

bool open(CBaseUIElement *root, const Database::MapLocation &map) { return static_cast<Editor *>(root)->open(map); }

void jump(CBaseUIElement *root, std::string_view timestamp) { static_cast<Editor *>(root)->jump(timestamp); }

void drop(CBaseUIElement *root, std::span<const std::string> paths) { static_cast<Editor *>(root)->drop(paths); }

void focus(CBaseUIElement *root) { static_cast<Editor *>(root)->followFile(); }

constexpr EditorModule MODULE{.create = create,
                              .save = save,
                              .destroy = destroy,
                              .open = open,
                              .jump = jump,
                              .drop = drop,
                              .focus = focus,
                              .test = runEditorTests};
}  // namespace

const EditorModule *neomod_editor_module() { return &MODULE; }
