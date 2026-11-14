#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "noinclude.h"

#include <array>

class CBaseUIStyledButton;
class PauseButton;
struct EditorState;

// play or pause, and the rate the music plays at (25 to 100%, the pitch kept)
class Transport final : public CBaseUIBox {
    NOCOPY_NOMOVE(Transport)
   public:
    explicit Transport(EditorState &state);
    ~Transport() override;

    void tick() override;

   private:
    EditorState &state;
    PauseButton *playButton;
    std::array<CBaseUIStyledButton *, 4> rateButtons{};
    i32 shownRate{100};  // the percentage the buttons' texts are for
};
