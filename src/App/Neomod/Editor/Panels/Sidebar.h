#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "noinclude.h"
#include "types.h"

#include <array>
#include <functional>
#include <string>

class CBaseUIDropdown;
class CBaseUINumberField;
class CBaseUIPopupLayer;
class CBaseUIStyledButton;
class SampleChooser;
struct EditorState;

// the compose toggles: new combo (with the colours one selected new combo can skip to), whistle, finish and clap, the
// normal and addition sample sets and the sample on a hit itself (its index, its volume and its file, picked in a
// SampleChooser), which show and change the selection (or what the next object placed gets while nothing is
// selected); grid snap with the grid size, distance snap with its spacing
class Sidebar final : public CBaseUIBox {
    NOCOPY_NOMOVE(Sidebar)
   public:
    // the dropdown menus and the sample chooser open in `popups`, which outlives it; `folderChanged` is called when a
    // file came into the shown map folder
    Sidebar(EditorState &state, CBaseUIPopupLayer &popups, std::function<void()> folderChanged);
    ~Sidebar() override;

    void tick() override;
    // what's being typed into one of its fields becomes a step now (the field keeps the focus)
    void commit();
    // a sound file dropped on the game window goes into the open sample chooser (see SampleChooser::addFile()); false
    // while it isn't open
    bool addSample(const std::string &path);

   private:
    class Swatch;

    // opens the sample chooser beside the file button (closes it if it's open)
    void chooseSample();
    // says why the sample on a hit itself wasn't set, if it wasn't
    static void explainUnset(bool set);

    EditorState &state;
    CBaseUIPopupLayer &popups;
    std::function<void()> folderChanged;
    CBaseUIStyledButton *newCombo;
    std::array<CBaseUIStyledButton *, 3> hitSounds{};
    CBaseUIStyledButton *gridSnap;
    CBaseUIStyledButton *distanceSnap;
    CBaseUIBox *skips;
    std::array<Swatch *, 8> swatches{};
    CBaseUIDropdown *normalSet;
    CBaseUIDropdown *additionSet;
    CBaseUINumberField *sampleIndex;
    CBaseUINumberField *sampleVolume;
    CBaseUIStyledButton *sampleFile;
    SampleChooser *chooser{nullptr};  // owned by the popup layer, there while chooserId is open
    u32 chooserId{0};
};
