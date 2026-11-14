#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "noinclude.h"

#include <array>

class CBaseUIStyledButton;
struct EditorState;

// the compose tools (keys 1 to 4): Select, Circle, Slider, Spinner
class Toolbox final : public CBaseUIBox {
    NOCOPY_NOMOVE(Toolbox)
   public:
    explicit Toolbox(EditorState &state);
    ~Toolbox() override;

    void tick() override;

   private:
    EditorState &state;
    std::array<CBaseUIStyledButton *, 4> buttons{};
};
