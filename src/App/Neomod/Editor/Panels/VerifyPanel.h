#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "CBaseUIList.h"
#include "EditorState.h"
#include "Verify.h"
#include "noinclude.h"
#include "types.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class CBaseUIStyledButton;
class CBaseUIStyledLabel;

// the Verify tab: the map issues as a list (see Verify::check()), looked for again when it shows a map that changed;
// the minor ones only when asked for. picking one goes to its time and selects its objects, activating it also shows
// the tab it's fixed on
class VerifyPanel final : public CBaseUIBox, private CBaseUIList::Model {
    NOCOPY_NOMOVE(VerifyPanel)
   public:
    // `show` switches the editor to a tab
    VerifyPanel(EditorState &state, std::function<void(EditorState::Tab)> show);
    ~VerifyPanel() override;

    void tick() override;
    // forgets what it found (another map is shown)
    void clear();

   private:
    [[nodiscard]] uSz getRowCount() const override;
    [[nodiscard]] u64 getRowKey(uSz row) const override;
    [[nodiscard]] std::string getCellText(uSz row, uSz column) const override;
    [[nodiscard]] CBaseUIList::RowLook getRowLook(uSz row) const override;

    // to the issue time, with its objects selected
    void goTo(const Verify::Issue &issue);
    // lists the issues found, with or without the minor ones
    void list(bool minor);

    EditorState &state;
    std::function<void(EditorState::Tab)> show;
    CBaseUIStyledLabel *summary;
    CBaseUIStyledButton *minor;
    CBaseUIList *rows;
    std::vector<Verify::Issue> found;
    std::vector<Verify::Issue> issues;  // the ones listed
    std::optional<u32> checked;         // the document revision they are for
};
