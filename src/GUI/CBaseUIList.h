#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIElement.h"
#include "Color.h"
#include "Hashing.h"
#include "types.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

// rows of text in columns, drawn by the style (see UIStyle), of which any number can be selected. it holds no rows: a
// Model says how many there are and what they show, asked for the rows in view only, so a long list costs what's on
// screen. a row is selected by its key, which it keeps wherever it moves, so the selection outlasts rows coming and
// going (the keys of rows that are gone stay until its user sets the selection). a click selects a row (with Ctrl one
// more or less, with Shift all from the one last picked) and gives the list the focus, with which Up, Down, Home, End
// and the Page keys pick another (with Shift: all up to it) and Ctrl+A all; a double click or Enter activates a row.
// the wheel and the thumb at its right scroll it
class CBaseUIList : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIList)
   public:
    struct Column {
        std::string title;
        float width;  // units; 0: a share of what the others leave
    };
    struct RowLook {
        std::optional<Color> bar;  // a bar of this colour at the row's left
        bool marked{false};        // an arrow at the row's left
    };
    class Model {
       public:
        [[nodiscard]] virtual uSz getRowCount() const = 0;
        [[nodiscard]] virtual u64 getRowKey(uSz row) const = 0;
        [[nodiscard]] virtual std::string getCellText(uSz row, uSz column) const = 0;
        [[nodiscard]] virtual RowLook getRowLook(uSz /*row*/) const { return {}; }

       protected:
        ~Model() = default;
    };

    // `model` outlives it
    CBaseUIList(const Model &model, std::vector<Column> columns, std::string name = {});
    ~CBaseUIList() override;

    [[nodiscard]] const Model &getModel() const { return this->model; }
    [[nodiscard]] const Hash::flat::set<u64> &getSelection() const { return this->selection; }
    // a row selected alone is the one the keys go on from (doesn't call the callback)
    CBaseUIList *setSelection(std::span<const u64> keys);
    // called when the user changed the selection
    CBaseUIList *setSelectionCallback(std::function<void()> callback);
    // called with the row the user double-clicked or pressed Enter on
    CBaseUIList *setActivateCallback(std::function<void(uSz)> callback);
    // scrolls as little as it takes to show the row
    void scrollTo(uSz row);
    // the rows in view, the first and one past the last (for whoever checks what it asks its model)
    [[nodiscard]] std::pair<uSz, uSz> getRowsInView() const;

    void draw() override;
    void onKeyDown(KeyboardEvent &e) override;
    [[nodiscard]] vec2 getNaturalSize() override;

   protected:
    void onMouseDownInside(bool left = true, bool right = false) override;
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseUpOutside(bool left = true, bool right = false) override;
    void onMouseCancel() override;
    void onCapturedMouseMove() override;
    bool onWheel(WheelDelta vertical, WheelDelta horizontal) override;

   private:
    // where the rows show, under the header and beside the thumb's track
    [[nodiscard]] McRect getRowsRect() const;
    // how far it can scroll (px)
    [[nodiscard]] float getMaxScroll() const;
    // the thumb's top and height within the track (nothing to scroll: none)
    [[nodiscard]] std::optional<std::pair<float, float>> getThumb() const;
    // the row at this height of the screen (none: the header, or below the last row)
    [[nodiscard]] std::optional<uSz> rowAt(float y) const;
    // the row with this key (none: gone)
    [[nodiscard]] std::optional<uSz> rowOf(u64 key) const;
    void scrollBy(float px);
    // the row becomes the one picked: alone selected, or (extend) with all from the anchor to it
    void pick(uSz row, bool extend);
    void selectionChanged();

    const Model &model;
    std::vector<Column> columns;
    std::function<void()> selectionCallback;
    std::function<void(uSz)> activateCallback;
    Hash::flat::set<u64> selection;
    std::optional<u64> anchor;  // where a range starts (the row last picked without Shift)
    std::optional<u64> cursor;  // the row last picked, which the keys go on from
    float scroll{0.f};          // px
    // the thumb is being dragged: how far under its top it was grabbed
    std::optional<float> grabbed;
};
