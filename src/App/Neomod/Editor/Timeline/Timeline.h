#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIElement.h"
#include "DatabaseBeatmapTypes.h"
#include "Document.h"
#include "noinclude.h"
#include "types.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

struct EditorState;

// the time around the editor time, which sits in the middle: 6 s at zoom 1, the divisor beat grid in each uninherited
// section (bar lines from its meter), the objects, timing points, bookmarks, the preview point and breaks. Alt and the
// wheel zoom it. objects are selected here as in the playfield (a drag from nothing selects a range; objects are hit
// nearer their middles than they're drawn, so that there's room for one between them) and dragged in time, a slider end
// circle to change its repeats and a spinner end to change where it ends; a click on the selected slider picks the
// circle clicked, or its body; a break edge is dragged from within the break bar. over each circle, the sounds it plays
// that aren't the default (see soundLabel())
class Timeline final : public CBaseUIElement {
    NOCOPY_NOMOVE(Timeline)
   public:
    explicit Timeline(EditorState &state);
    ~Timeline() override;

    void draw() override;

    // zooms in by `steps` tenths (out if negative), between 0.1 and 8
    void zoom(i32 steps);
    // what the hit samples say, each part only if there is one: its normal sound (its own sample set N, S or D with its
    // own sample index, or the start of its file name in `sampleFiles`), a colon and its whistle, finish and clap (W,
    // F, C), then its own volume ("S", "WC", "D2:F", "kick:C", "2 60%"; empty for a plain hit)
    [[nodiscard]] static std::string soundLabel(const neomod::DatabaseBeatmapTypes::HITSAMPLE_BITS &samples,
                                                std::span<const std::string> sampleFiles);

   protected:
    bool onWheel(WheelDelta vertical, WheelDelta horizontal) override;
    void onMouseDownInside(bool left, bool right) override;
    void onCapturedMouseMove() override;
    void onMouseUpInside(bool left, bool right) override;
    void onMouseUpOutside(bool left, bool right) override;
    void onMouseCancel() override;

   private:
    enum class Gesture : u8 { NONE, MOVE, RANGE, END, BREAK };

    void drawGrid(f64 left, f64 right) const;
    void drawObjects(f64 left, f64 right) const;
    void drawSoundLabels(f64 left, f64 right) const;
    [[nodiscard]] f32 xAt(f64 timeMS) const;
    [[nodiscard]] f64 timeAt(f32 x) const;
    // the diameter objects are drawn at (px)
    [[nodiscard]] f32 circleDiameter() const;

    EditorState &state;
    Gesture gesture{Gesture::NONE};
    f64 pressedTime{0.0};            // where a range started, or how far into the grabbed object the press was
    std::vector<Document::Id> kept;  // what a range adds to (started with Ctrl)
    Document::Id pivot{0};           // the object last clicked, which a Shift+click selects from (0: none)
    f32 pressedX{0.f};
    bool moving{false};
    bool picking{false};               // the press was on the selected slider, which a click picks a part of
    std::optional<i32> pressedNode{};  // ...the circle it was on (none: its body)
};
