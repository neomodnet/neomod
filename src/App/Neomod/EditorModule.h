#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Database.h"
#include "types.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

class CBaseUIElement;

// The editor (src/App/Neomod/Editor/) as seen by the game: nothing else in it is reachable from game code, so it can be
// torn down and rebuilt in place while the game runs. Editor code keeps all its state under the root element: no static
// objects with constructors or destructors, no thread_local; its ConVars are members of its objects (client settings,
// whose values outlive them). What it hands to the game (callbacks, async work) ends with the editor object that holds
// its Mc::Registration or Async::Scope; the apis that would keep it regardless are marked MC_UNREVOCABLE and don't
// compile in editor code.
struct EditorModule {
    // builds the editor UI. `session` is what save() returned, in this run or an earlier one (empty: fresh start). the
    // root setVisible() follows the editor screen (shown, left), starting right after this
    CBaseUIElement *(*create)(std::span<const u8> session);
    // what a later create() needs to continue from here. called whenever the game writes the session (on the main
    // thread, so it has to stay cheap): when the editor says it changed (EditorScreen::sessionChanged()), is left or
    // is destroyed
    std::vector<u8> (*save)(CBaseUIElement *root);
    // tears everything down
    void (*destroy)(CBaseUIElement *root);
    // the map to show, by its place on disk, given before the editor is shown with it. whether it's the open one now
    bool (*open)(CBaseUIElement *root, const Database::MapLocation &map);
    // where to go in the open map: an osu! timestamp, mm:ss:mmm with the combo numbers for the objects to select in
    // parentheses or without any. given before or while the editor is shown, nothing happens for one that doesn't parse
    void (*jump)(CBaseUIElement *root, std::string_view timestamp);
    // files dropped on the game window while the editor is shown, which the game has no use for
    void (*drop)(CBaseUIElement *root, std::span<const std::string> paths);
    // the game window has the focus again while the editor is shown: another program may have changed the open map
    void (*focus)(CBaseUIElement *root);
    // runs the editor tests whose names contain `filter` (all of them for an empty one), logging what they find;
    // whether they all passed
    bool (*test)(std::string_view filter);
};

extern "C" const EditorModule *neomod_editor_module();
