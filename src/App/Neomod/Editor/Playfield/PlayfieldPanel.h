#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIElement.h"
#include "Document.h"
#include "noinclude.h"

#include <span>
#include <string>
#include <utility>
#include <vector>

class Image;
class RenderTarget;
struct EditorState;
namespace neomod {
class HitObject;
}

// the shown map objects at the editor time as a play draws them, in the game 640x480 around the field, so that objects
// at its edges show whole. under them the grid and the map background, dimmed by editor_background_dim outside breaks.
// past circles stay as a trail unless editor_hit_animations asks for the play look.
// the compose tools work here: selecting (a click, Ctrl+click, a box), picking a part of the selected slider (a click
// on it), moving the selection, placing circles, sliders and spinners, right-clicks. Alt and the wheel set the distance
// snap spacing.
// a readout shows the position and the spacing around it. a slider being placed, or selected alone, shows its control
// points
class PlayfieldPanel final : public CBaseUIElement {
    NOCOPY_NOMOVE(PlayfieldPanel)
   public:
    explicit PlayfieldPanel(EditorState &state);
    ~PlayfieldPanel() override;

    void draw() override;
    void tick() override;

   protected:
    bool onWheel(WheelDelta vertical, WheelDelta horizontal) override;
    void onMouseDownInside(bool left, bool right) override;
    void onCapturedMouseMove() override;
    void onMouseUpInside(bool left, bool right) override;
    void onMouseUpOutside(bool left, bool right) override;
    void onMouseCancel() override;

   private:
    enum class Gesture : u8 { NONE, MOVE, BOX, POINT };

    // where the field is on the screen: osu!px * scale + offset
    void fit();
    [[nodiscard]] vec2 toField(vec2 screen) const { return (screen - this->offset) / this->scale; }
    // how near (osu!px) a press has to be to a slider control point to take it
    [[nodiscard]] f32 controlPointReach() const;
    // draws the selection rings into their target if they changed (a whole marathon has tens of thousands, too many to
    // blend every frame)
    void updateRings();
    // these rings into the target (clear: instead of what it has)
    void drawRings(std::span<const vec2> centers, bool clear);
    void drawBackground() const;
    void drawGrid() const;
    void drawMarks();
    void drawControlPoints();
    // the angle snap line (osu!px)
    void drawGuide(vec2 from, vec2 to) const;
    void drawReadout();

    EditorState &state;
    std::vector<neomod::HitObject *> shown;  // posed for the frame drawn, latest-ending first
    f32 scale{1.f};
    vec2 offset{0.f};
    Gesture gesture{Gesture::NONE};
    vec2 pressed{0.f};               // where (osu!px)
    std::vector<Document::Id> kept;  // what a box adds to (started with Ctrl)
    vec2 pressedScreen{0.f};         // a move starts once the cursor left it
    bool moving{false};
    bool picking{false};  // the press was on the selected slider, which a click picks a part of

    // what the ring target was drawn with, besides their centers
    struct RingsLook {
        const Image *image{nullptr};
        vec2 half{0.f};  // half the ring size (osu!px)
        f32 scale{0.f};
        vec2 offset{0.f};
        vec2 size{0.f};  // the target size
        bool operator==(const RingsLook &) const = default;
    };
    // the background the map names when its folder has it (one chosen since the last save too), as of the last tick
    // (empty: the one the record has, which also finds an image its name doesn't quite match)
    std::string backgroundFolder;
    std::string backgroundFile;
    bool backgroundOnDisk{false};
    RenderTarget *ringsTarget{nullptr};  // owned by the resource manager, destroyed with the panel
    std::vector<vec2> drawnRings;        // the ring centers the target has (osu!px), one for each selected object
    std::vector<Document::Id> drawnIds;  // ...as of the object revision the target was drawn from
    u32 drawnRevision{0};
    RingsLook drawnLook;
    vec2 ringsShift{0.f};                // how far they all moved since (osu!px)
    std::pair<u32, u32> ringsFor{0, 0};  // the object revision and the selection version they're up to date with
};
