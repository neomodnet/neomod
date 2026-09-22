#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIScrollView.h"

#include <compare>
#include <string>
#include <string_view>

class McFont;

// a scrollview over text that can be selected with the mouse and copied out (logs and the like): a drag over the
// text selects it in place of the base's drag-scroll, children keep their clicks. the subclass describes its text as
// runs in reading order (getRunCount()/getRun()) and draws the highlight behind its text (drawSelection())
class CBaseUISelectableTextView : public CBaseUIScrollView {
    NOCOPY_NOMOVE(CBaseUISelectableTextView)
   public:
    using CBaseUIScrollView::CBaseUIScrollView;
    ~CBaseUISelectableTextView() override = default;

    void updateInput(CBaseUIEventCtx &c) override;

    CBaseUISelectableTextView *setSelectionColor(Color color) {
        this->selectionColor = color;
        return this;
    }

    [[nodiscard]] bool hasSelection() const { return this->bHasSelection && this->selAnchor != this->selHead; }
    [[nodiscard]] std::string getSelectedText() const;
    void clearSelection() { this->bHasSelection = this->bSelecting = false; }

   protected:
    struct TextRun {
        std::string_view text;
        McFont *font;
        vec2 pos;                    // top left of the text's row, relative to the content origin
        float height;                // of the row
        float width;                 // of the text, in font units
        float scale;                 // the text is drawn at this scale
        std::string_view separator;  // what the copied text puts between the previous run and this one
    };

    // the runs in reading order: top to bottom, left to right within a row (the runs of a row share their pos.y)
    [[nodiscard]] virtual size_t getRunCount() const = 0;
    [[nodiscard]] virtual TextRun getRun(size_t index) const = 0;

    // screen position of the content's top left, which the runs are placed against (moves with the scrolling)
    [[nodiscard]] vec2 getContentOrigin() const;

    // the highlight behind the selected text; the subclass draws it before its text
    void drawSelection() const;

    // the first removedRuns runs are gone: the selection slides along with the rest
    void shiftSelection(size_t removedRuns);

    void onMouseDownInside(bool left, bool right) override;
    void onMouseUpInside(bool left, bool right) override;
    void onMouseUpOutside(bool left, bool right) override;
    void onMouseCancel() override;
    void onMouseOutside() override;
    void onCapturedMouseMove() override;
    void onCapturedMoveThrough() override;

    Color selectionColor{0xff2a5a9a};

   private:
    // a position in the text: run index + byte offset into it (x = that boundary's offset in font units, derived
    // from the two and only kept for drawing)
    struct TextPos {
        size_t run;
        size_t byte;
        float x;
        auto operator<=>(const TextPos &o) const {
            return this->run != o.run ? this->run <=> o.run : this->byte <=> o.byte;
        }
        bool operator==(const TextPos &o) const { return this->run == o.run && this->byte == o.byte; }
    };

    [[nodiscard]] TextPos hitTest(vec2 pos) const;
    void beginSelection(vec2 pos);

    TextPos selAnchor{};
    TextPos selHead{};
    vec2 vPressPos;  // where a press on a child landed (the selection's anchor if the drag takes it)
    bool bHasSelection{false};
    bool bSelecting{false};
};
