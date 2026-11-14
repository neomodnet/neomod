#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "noinclude.h"
#include "types.h"

#include <functional>
#include <string>

class CBaseUIStyledLabel;

// `< value >`: a value its owner steps (back by `<`, on by `>`) and shows. its parts are named after it: <name>_previous,
// <name>_value, <name>_next
class Stepper final : public CBaseUIBox {
    NOCOPY_NOMOVE(Stepper)
   public:
    // `step` moves the value a step (negative: back), `text` shows it (asked every tick)
    Stepper(const std::string &name, std::function<void(i32)> step, std::function<std::string()> text);
    ~Stepper() override;

    void tick() override;

   private:
    std::function<std::string()> text;
    CBaseUIStyledLabel *label;
};
