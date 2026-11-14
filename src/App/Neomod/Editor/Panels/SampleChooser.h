#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "CBaseUIList.h"
#include "Registration.h"
#include "SamplePreview.h"
#include "noinclude.h"
#include "types.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

class CBaseUIPopupLayer;
class CBaseUIStyledButton;
struct EditorState;

// a popup to pick the file a hit plays from the sound files in the shown map folder (the song aside): a file picked is
// heard, Use (a double click, Enter) takes it, No File takes none, and Add File... copies one from elsewhere into the
// folder and picks it. a click outside or Escape closes it with nothing taken
class SampleChooser final : public CBaseUIBox, private CBaseUIList::Model {
    NOCOPY_NOMOVE(SampleChooser)
   public:
    struct Callbacks {
        std::function<void(std::string_view)> take;  // the file taken (empty: none)
        std::function<void()> folderChanged;         // a file came into the map folder
    };

    // `current` is picked as it opens
    SampleChooser(EditorState &state, CBaseUIPopupLayer &popups, std::string_view current, Callbacks callbacks);
    ~SampleChooser() override;

    // shows it at `pos` (the layer owns it from here; presses inside `passThrough` reach what's beneath). returns the
    // layer id
    u32 open(vec2 pos, const McRect &passThrough);
    // the sound file at `path` comes into the map folder and is picked (what Add File... does with the one chosen); an
    // osu!stable map set is copied into the neomod library first, and the file goes into that
    void addFile(const std::string &path);

    void tick() override;
    void draw() override;

   private:
    [[nodiscard]] uSz getRowCount() const override { return this->files.size(); }
    [[nodiscard]] u64 getRowKey(uSz row) const override { return row; }
    [[nodiscard]] std::string getCellText(uSz row, uSz /*column*/) const override { return this->files[row]; }

    // lists the sound files in the folder again, `picked` picked if it's one of them
    void list(std::string_view picked);
    // the one picked (none: not exactly one)
    [[nodiscard]] const std::string *picked() const;
    // plays the picked file
    void hear();
    void take(std::string_view file);

    EditorState &state;
    CBaseUIPopupLayer &popups;
    Callbacks callbacks;
    CBaseUIList *rows;
    CBaseUIStyledButton *use;
    std::vector<std::string> files;
    std::string folder;  // the one listed, with a slash at its end
    SamplePreview preview;
    Mc::Registration fileDialog;  // the Add File dialog, whose answer goes nowhere once the popup is gone
    u32 id{0};                    // owned by the popup layer
};
