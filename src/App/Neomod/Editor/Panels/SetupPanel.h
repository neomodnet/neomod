#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIBox.h"
#include "Registration.h"
#include "noinclude.h"

#include <array>
#include <string>
#include <vector>

class CBaseUIPopupLayer;
class CBaseUIStyledButton;
class CBaseUIStyledLabel;
class CBaseUIStyledTextbox;
class Form;
struct EditorState;

// the Setup tab: the map metadata, its difficulty, its other settings and its combo colours as fields that show what
// its file says and write it when changed (a colour in a picker its square opens)
class SetupPanel final : public CBaseUIBox {
    NOCOPY_NOMOVE(SetupPanel)
   public:
    // its dropdown menus open in `popups`, which outlives it
    SetupPanel(EditorState &state, CBaseUIPopupLayer &popups);
    ~SetupPanel() override;

    void tick() override;
    void onKeyDown(KeyboardEvent &e) override;
    // what's being typed into one of its fields becomes a step now (the field keeps the focus)
    void commit();
    // the shown map difficulty name focused, selected to type another one
    void editVersion();
    // the image at `path` becomes the shown map background (as a step), copied into its folder unless it's there
    void useBackground(const std::string &path);

   private:
    EditorState &state;
    std::vector<Form *> forms;
    CBaseUIStyledTextbox *version{nullptr};
    CBaseUIStyledLabel *background;
    Mc::Registration fileDialog;  // the background dialog, whose answer goes nowhere once the panel is gone
    std::array<CBaseUIBox *, 8> colourRows{};  // a row for each combo colour
    CBaseUIStyledLabel *noColours;
    CBaseUIStyledButton *addColour;
};
