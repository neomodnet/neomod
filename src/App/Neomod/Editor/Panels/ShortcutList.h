#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "CBaseUIList.h"
#include "noinclude.h"
#include "types.h"

#include <array>
#include <string>
#include <vector>

class CBaseUIPopupLayer;

// a popup listing every command that has keys (see Commands), under the heading of its group. a click outside or
// Escape closes it
class ShortcutList final : public CBaseUIBox, private CBaseUIList::Model {
    NOCOPY_NOMOVE(ShortcutList)
   public:
    ShortcutList();
    ~ShortcutList() override;

    void draw() override;

   private:
    [[nodiscard]] uSz getRowCount() const override;
    [[nodiscard]] u64 getRowKey(uSz row) const override;
    [[nodiscard]] std::string getCellText(uSz row, uSz column) const override;

    // the group (on its first row only), the command and its keys
    std::vector<std::array<std::string, 3>> rows;
};
