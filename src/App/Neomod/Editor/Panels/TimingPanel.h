#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "BeatmapFile/BeatmapFile.h"
#include "CBaseUIBox.h"
#include "CBaseUIList.h"
#include "Document.h"
#include "noinclude.h"
#include "types.h"

#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

class CBaseUINumberField;
class CBaseUIPopupLayer;
class CBaseUIStyledButton;
class CBaseUIStyledLabel;
class Form;
struct EditorState;

// the Timing tab: the map timing points as a list, and fields for the ones selected there, which write what's set in
// them to every selected point they apply to, as one step. while it shows, the metronome ticks with the music
class TimingPanel final : public CBaseUIBox, private CBaseUIList::Model {
    NOCOPY_NOMOVE(TimingPanel)
   public:
    // its dropdown menu opens in `popups`, which outlives it; `showSamples` shows the map samples for a sample index
    // (see SampleSets)
    TimingPanel(EditorState &state, CBaseUIPopupLayer &popups, std::function<void(i32)> showSamples = {});
    ~TimingPanel() override;

    void tick() override;
    void onKeyDown(KeyboardEvent &e) override;
    // what's being typed into one of its fields becomes a step now (the field keeps the focus)
    void commit();
    // forgets the selection (another map is shown)
    void clear();

    // a point at the editor time with what is in force there, selected; one of that kind that's there already is
    // selected instead. false (with the reason said) when there can't be one
    bool addPoint(bool uninherited);
    // takes out the selected points, or (inForce) the one in force at the editor time, as one `action` step; never the
    // last uninherited one
    void removePoints(bool inForce, Document::Action action = Document::Action::DELETE_TIMING_POINT);
    // the selected points as .osu lines on the system clipboard
    void copyPoints();
    // copies the selected points, then takes them out (one step)
    void cutPoints();
    // whether every line on the system clipboard that says something is a timing point
    [[nodiscard]] static bool canPastePoints();
    // those points at their own times, one step: points of the same kind at exactly those times are replaced, and the
    // pasted ones become the selection
    void pastePoints();
    [[nodiscard]] bool hasSelection();
    // selects the point in force at the editor time if none is selected (what showing the tab does)
    void selectInForce();
    // goes to the velocity field, for the point just added
    void visitVelocity();
    // a tap along the music: from the fourth on, the tap tempo goes to the uninherited points selected when they
    // started (to the one in force then without any). starts the music if it's paused; a while without a tap, or
    // another selection, starts over
    void tap();

   private:
    class Lamps;
    using TimingPoint = neomod::BeatmapFile::TimingPoint;
    enum class Which : u8 { ALL, UNINHERITED, INHERITED };
    static constexpr f64 NONE{std::numeric_limits<f64>::quiet_NaN()};
    // what the selected points have: a value they all have, or none (NaN, -1, off) when they differ or there's none
    struct Summary {
        std::vector<uSz> rows;  // the selected points, in order
        bool uninherited{false};
        bool inherited{false};
        f64 time{NONE};  // from the first one
        f64 bpm{NONE};
        f64 velocity{NONE};
        f64 meter{NONE};
        f64 sampleIndex{NONE};
        f64 volume{NONE};
        int sampleSet{-1};
        bool kiai{false};
        bool omitBarLine{false};
    };

    [[nodiscard]] uSz getRowCount() const override;
    [[nodiscard]] u64 getRowKey(uSz row) const override;
    [[nodiscard]] std::string getCellText(uSz row, uSz column) const override;
    [[nodiscard]] CBaseUIList::RowLook getRowLook(uSz row) const override;

    // the selection summary, made again if the document or the selection changed since it was made (selected points
    // that are gone drop out of the selection)
    const Summary &summarize();
    // the selected points of this kind as `edit` (taking a TimingPoint &) makes them, in `c`, with what follows for
    // their sections (see Retiming::apply())
    template <typename F>
    void edit(Document::Change &c, Which which, F &&edit);
    // these points of this kind as `edit` (taking a TimingPoint &) makes them. calls with the same `gesture` are one
    // run (0 starts one and is set to its id): one step, which carries the point sections from where they were before
    // the run, however many times it changes them
    template <typename F>
    void retime(std::span<const Document::Id> targets, Which which, u64 &gesture, F &&edit);
    // the tempo on the selected uninherited points, a tenth of a beat a minute down (-1) or up (1)
    void nudgeTempo(f64 by);
    // the time on the selected points, a millisecond earlier (-1) or later (1)
    void nudgeTime(f64 by);
    [[nodiscard]] bool isTapping() const;
    // the row for the last point at or before the editor time (the first one before them all; none without points), of
    // any kind or the uninherited ones only
    [[nodiscard]] std::optional<uSz> rowInForce(bool uninherited) const;
    void select(Document::Id id);
    // the list has another selection, set by the user or from here: its summary is made again and its runs end
    void selectionChanged();

    EditorState &state;
    CBaseUIList *list;
    Form *form;
    CBaseUINumberField *velocity;
    CBaseUIStyledButton *removeButton;
    CBaseUIStyledButton *earlier;
    CBaseUIStyledButton *later;
    CBaseUIStyledButton *slower;
    CBaseUIStyledButton *faster;
    CBaseUIStyledLabel *tapped;
    u64 tempoGesture{0};                   // the run of tempo nudges on this selection (0: none yet)
    u64 timeGesture{0};                    // ...of time nudges
    std::vector<i32> taps;                 // the tap times in a run
    std::vector<Document::Id> tapTargets;  // whose tempo they are
    u64 tapGesture{0};
    Summary summary;
    std::optional<u32> summarized;  // the document revision the summary is for (none: to be made again)
    // the rows marked as in force, found each tick
    std::optional<uSz> inForce;
    std::optional<uSz> inForceUninherited;
};
