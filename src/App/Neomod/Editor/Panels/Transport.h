#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "noinclude.h"
#include "types.h"

#include <array>

class CBaseUIStyledButton;
class PauseButton;
struct EditorState;

// play or pause, and the rate the music plays at (25 to 150%, the pitch kept)
class Transport final : public CBaseUIBox {
    NOCOPY_NOMOVE(Transport)
   public:
    explicit Transport(EditorState &state);
    ~Transport() override;

    void tick() override;

   private:
    static constexpr std::array<i32, 5> RATES{25, 50, 75, 100, 150};

    EditorState &state;
    PauseButton *playButton;
    std::array<CBaseUIStyledButton *, RATES.size()> rateButtons{};
    i32 shownRate{100};  // the percentage the buttons' texts are for
};
