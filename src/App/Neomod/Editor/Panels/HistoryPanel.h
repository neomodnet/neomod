#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "CBaseUIList.h"
#include "EditorHistory.h"
#include "MD5Hash.h"
#include "noinclude.h"
#include "types.h"

#include <optional>
#include <string>
#include <vector>

class CBaseUIStyledButton;
class CBaseUIStyledLabel;
struct EditorState;

// the History tab: the open map versions that editor saves made (see EditorHistory), the latest first, read again when
// it shows the map after a save. the selected one is compared with the map as it is now, and Restore makes the map that
// version again, as a step
class HistoryPanel final : public CBaseUIBox, private CBaseUIList::Model {
    NOCOPY_NOMOVE(HistoryPanel)
   public:
    explicit HistoryPanel(EditorState &state);
    ~HistoryPanel() override;

    void tick() override;
    // forgets what it listed (another map is shown)
    void clear();

   private:
    [[nodiscard]] uSz getRowCount() const override;
    [[nodiscard]] u64 getRowKey(uSz row) const override;
    [[nodiscard]] std::string getCellText(uSz row, uSz column) const override;
    [[nodiscard]] CBaseUIList::RowLook getRowLook(uSz row) const override;

    // the selected version (none: not exactly one)
    [[nodiscard]] const EditorHistory::Version *selected() const;
    void restore();

    EditorState &state;
    CBaseUIStyledLabel *detail;
    CBaseUIStyledButton *restoreButton;
    CBaseUIList *rows;
    std::vector<EditorHistory::Version> versions;
    std::optional<MD5Hash> listed;  // the saved content they were listed for
    // the document revision the detail line is for (none: to be made again, e.g. for another selection)
    std::optional<u32> compared;
};
