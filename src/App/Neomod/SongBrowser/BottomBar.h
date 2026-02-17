#pragma once
// Copyright (c) 2025, kiwec, All rights reserved.

#include "types.h"

#include <string>

class Graphics;
struct CBaseUIEventCtx;

// Bottom bar has some hacky logic to handle osu!stable skins properly.
// Standard input handling logic won't work, as buttons can overlap.

namespace BottomBar {
enum Button : u8 { MODE = 0, MODS = 1, RANDOM = 2, OPTIONS = 3, BTN_MAX = 4, BTN_NONE = BTN_MAX };

void update_export_progress(float progress, std::string entry_being_processed, const std::string &collection);
inline void update_export_progress_cb(float progress, std::string entry_being_processed) {
    return update_export_progress(progress, std::move(entry_being_processed), "");
}

void update(CBaseUIEventCtx &c);
void draw();
void press_button(Button btn);
[[nodiscard]] f32 get_height();
[[nodiscard]] f32 get_min_height();
}  // namespace BottomBar
