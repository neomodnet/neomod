// Copyright (c) 2026, WH, All rights reserved.
#include "Commands.h"

#include "CBaseUIElement.h"
#include "Compose.h"
#include "DatabaseBeatmapTypes.h"
#include "EditorState.h"
#include "KeyBindings.h"
#include "SliderCurves.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "i18n.h"

#include "fmt/format.h"

#include <array>
#include <cstdlib>
#include <utility>

namespace Commands {
namespace {
using Tool = Compose::Tool;
using Tab = EditorState::Tab;
using HitSoundType = neomod::DatabaseBeatmapTypes::HitSoundType;

// (Escape goes back, through the screen, once it's on Compose with nothing to cancel)
constexpr KEYMOD CTRL_SHIFT{(KEYMOD)(KEYMOD_CONTROL | KEYMOD_SHIFT)};
constexpr std::array BINDINGS{
    Binding{{KEY_ESCAPE}, Command::CANCEL},
    Binding{{KEY_F1}, Command::TAB, (i32)Tab::COMPOSE},
    Binding{{KEY_F3}, Command::TAB, (i32)Tab::TIMING},
    Binding{{KEY_F6}, Command::TAB, (i32)Tab::TIMING},
    Binding{{KEY_F4}, Command::TAB, (i32)Tab::SETUP},
    Binding{{KEY_A, CTRL_SHIFT}, Command::TAB, (i32)Tab::VERIFY},
    Binding{{KEY_1}, Command::TOOL, (i32)Tool::SELECT},
    Binding{{KEY_2}, Command::TOOL, (i32)Tool::CIRCLE},
    Binding{{KEY_3}, Command::TOOL, (i32)Tool::SLIDER},
    Binding{{KEY_4}, Command::TOOL, (i32)Tool::SPINNER},
    Binding{{KEY_Q}, Command::NEW_COMBO},
    Binding{{KEY_W}, Command::HITSOUND, HitSoundType::WHISTLE},
    Binding{{KEY_E}, Command::HITSOUND, HitSoundType::FINISH},
    Binding{{KEY_R}, Command::HITSOUND, HitSoundType::CLAP},
    Binding{{KEY_Q, KEYMOD_SHIFT}, Command::NORMAL_SET, 0},
    Binding{{KEY_W, KEYMOD_SHIFT}, Command::NORMAL_SET, 1},
    Binding{{KEY_E, KEYMOD_SHIFT}, Command::NORMAL_SET, 2},
    Binding{{KEY_R, KEYMOD_SHIFT}, Command::NORMAL_SET, 3},
    Binding{{KEY_Q, KEYMOD_CONTROL}, Command::ADDITION_SET, 0},
    Binding{{KEY_W, KEYMOD_CONTROL}, Command::ADDITION_SET, 1},
    Binding{{KEY_E, KEYMOD_CONTROL}, Command::ADDITION_SET, 2},
    Binding{{KEY_R, KEYMOD_CONTROL}, Command::ADDITION_SET, 3},
    Binding{{KEY_DELETE}, Command::DELETE_SELECTION},
    Binding{{KEY_P, KEYMOD_CONTROL}, Command::ADD_TIMING_POINT, 1},
    Binding{{KEY_P, CTRL_SHIFT}, Command::ADD_TIMING_POINT, 0},
    Binding{{KEY_I, KEYMOD_CONTROL}, Command::DELETE_TIMING_POINT},
    Binding{{KEY_X, KEYMOD_CONTROL}, Command::CUT},
    Binding{{KEY_C, KEYMOD_CONTROL}, Command::COPY},
    Binding{{KEY_V, KEYMOD_CONTROL}, Command::PASTE},
    Binding{{KEY_D, KEYMOD_CONTROL}, Command::CLONE},
    Binding{{KEY_A, KEYMOD_CONTROL}, Command::SELECT_ALL},
    Binding{{KEY_J}, Command::NUDGE, -1},
    Binding{{KEY_K}, Command::NUDGE, 1},
    Binding{{KEY_G, KEYMOD_CONTROL}, Command::REVERSE},
    Binding{{KEY_H, KEYMOD_CONTROL}, Command::FLIP, 0},
    Binding{{KEY_J, KEYMOD_CONTROL}, Command::FLIP, 1},
    Binding{{KEY_PERIOD, KEYMOD_CONTROL}, Command::ROTATE, 1},
    Binding{{KEY_COMMA, KEYMOD_CONTROL}, Command::ROTATE, -1},
    Binding{{KEY_R, CTRL_SHIFT}, Command::ROTATE_BY},
    Binding{{KEY_S, CTRL_SHIFT}, Command::SCALE_BY},
    Binding{{KEY_D, CTRL_SHIFT}, Command::POLYGON},
    Binding{{KEY_F, CTRL_SHIFT}, Command::STREAM},
    Binding{{KEY_T}, Command::GRID_SNAP},
    Binding{{KEY_T}, Command::TAP},
    Binding{{KEY_Y}, Command::DISTANCE_SNAP},
    Binding{{KEY_G}, Command::GRID_SIZE, 0},
    Binding{{KEY_1, KEYMOD_CONTROL}, Command::GRID_SIZE, 4},
    Binding{{KEY_2, KEYMOD_CONTROL}, Command::GRID_SIZE, 8},
    Binding{{KEY_3, KEYMOD_CONTROL}, Command::GRID_SIZE, 16},
    Binding{{KEY_4, KEYMOD_CONTROL}, Command::GRID_SIZE, 32},
    // with a selection; the bookmark and rate keys below otherwise
    Binding{{KEY_LEFT, KEYMOD_CONTROL}, Command::MOVE, 0, true},
    Binding{{KEY_RIGHT, KEYMOD_CONTROL}, Command::MOVE, 1, true},
    Binding{{KEY_UP, KEYMOD_CONTROL}, Command::MOVE, 2, true},
    Binding{{KEY_DOWN, KEYMOD_CONTROL}, Command::MOVE, 3, true},
    Binding{{KEY_SPACE}, Command::TOGGLE_PAUSE},
    Binding{{KEY_C}, Command::TOGGLE_PAUSE},
    Binding{{KEY_X}, Command::PLAY_FROM_START},
    Binding{{KEY_Z}, Command::FIRST_OBJECT},
    Binding{{KEY_V}, Command::LAST_OBJECT},
    Binding{{KEY_LEFT}, Command::STEP, -1, true},
    Binding{{KEY_RIGHT}, Command::STEP, 1, true},
    Binding{{KEY_LEFT, KEYMOD_SHIFT}, Command::STEP, -4, true},
    Binding{{KEY_RIGHT, KEYMOD_SHIFT}, Command::STEP, 4, true},
    Binding{{KEY_UP}, Command::NEXT_POINT, 0, true},
    Binding{{KEY_DOWN}, Command::PREVIOUS_POINT, 0, true},
    Binding{{KEY_UP, KEYMOD_CONTROL}, Command::RATE, 25, true},
    Binding{{KEY_DOWN, KEYMOD_CONTROL}, Command::RATE, -25, true},
    Binding{{KEY_UP, CTRL_SHIFT}, Command::RATE, 3, true},
    Binding{{KEY_DOWN, CTRL_SHIFT}, Command::RATE, -3, true},
    Binding{{KEY_RIGHT, KEYMOD_CONTROL}, Command::NEXT_BOOKMARK},
    Binding{{KEY_LEFT, KEYMOD_CONTROL}, Command::PREVIOUS_BOOKMARK},
    Binding{{KEY_1, KEYMOD_SHIFT}, Command::DIVISOR, 1},
    Binding{{KEY_2, KEYMOD_SHIFT}, Command::DIVISOR, 2},
    Binding{{KEY_3, KEYMOD_SHIFT}, Command::DIVISOR, 3},
    Binding{{KEY_4, KEYMOD_SHIFT}, Command::DIVISOR, 4},
    Binding{{KEY_6, KEYMOD_SHIFT}, Command::DIVISOR, 6},
    Binding{{KEY_8, KEYMOD_SHIFT}, Command::DIVISOR, 8},
    Binding{{KEY_S, KEYMOD_CONTROL}, Command::SAVE},
    Binding{{KEY_F5}, Command::TEST_PLAY},
    Binding{{KEY_L, KEYMOD_CONTROL}, Command::REVERT},
    Binding{{KEY_Z, KEYMOD_CONTROL}, Command::UNDO, 0, true},
    Binding{{KEY_Y, KEYMOD_CONTROL}, Command::REDO, 0, true},
    Binding{{KEY_Z, CTRL_SHIFT}, Command::REDO, 0, true},
    Binding{{KEY_B, KEYMOD_CONTROL}, Command::ADD_BOOKMARK},
    Binding{{KEY_B, CTRL_SHIFT}, Command::REMOVE_BOOKMARK},
};

// a sample set as NORMAL_SET and ADDITION_SET number them
const char *setName(i32 set) {
    switch(set) {
        case 1:
            return _("Normal");
        case 2:
            return _("Soft");
        case 3:
            return _("Drum");
        default:
            return _("Auto");
    }
}
}  // namespace

std::span<const Binding> bindings() { return BINDINGS; }

std::string label(Command command, i32 value) {
    switch(command) {
        case Command::TOGGLE_PAUSE:
            return _("Play / Pause");
        case Command::PLAY_FROM_START:
            return _("Play from the Start");
        case Command::FIRST_OBJECT:
            return _("Go to the First Object");
        case Command::LAST_OBJECT:
            return _("Go to the Last Object");
        case Command::STEP:
            if(std::abs(value) == 1) return value < 0 ? _("Step Backward") : _("Step Forward");
            return value < 0 ? tformat("Step Backward {} Ticks", -value) : tformat("Step Forward {} Ticks", value);
        case Command::NEXT_POINT:
            return _("Next Timing Point");
        case Command::PREVIOUS_POINT:
            return _("Previous Timing Point");
        case Command::RATE:
            return tformat("Playback Rate {:+}%", value);
        case Command::NEXT_BOOKMARK:
            return _("Next Bookmark");
        case Command::PREVIOUS_BOOKMARK:
            return _("Previous Bookmark");
        case Command::DIVISOR:
            return tformat("Beat Divisor 1/{}", value);
        case Command::SAVE:
            return _("Save");
        case Command::TEST_PLAY:
            return _("Test Play");
        case Command::REVERT:
            return _("Revert to Saved");
        case Command::NEW_DIFFICULTY:
            return value != 0 ? _("Empty") : _("Copy of This One");
        case Command::UNDO:
            return _("Undo");
        case Command::REDO:
            return _("Redo");
        case Command::ADD_BOOKMARK:
            return _("Add Bookmark");
        case Command::REMOVE_BOOKMARK:
            return _("Remove Bookmark");
        case Command::TOOL:
            switch((Tool)value) {
                case Tool::SELECT:
                    return _("Select");
                case Tool::CIRCLE:
                    return _("Circle");
                case Tool::SLIDER:
                    return _("Slider");
                case Tool::SPINNER:
                    return _("Spinner");
            }
            break;
        case Command::NEW_COMBO:
            return _("New Combo");
        case Command::HITSOUND:
            return value == HitSoundType::WHISTLE  ? _("Whistle")
                   : value == HitSoundType::FINISH ? _("Finish")
                                                   : _("Clap");
        case Command::NORMAL_SET:
            return tformat("Sample Set: {}", setName(value));
        case Command::ADDITION_SET:
            return tformat("Additions: {}", setName(value));
        case Command::DELETE_SELECTION:
            return _("Delete");
        case Command::SELECT_ALL:
            return _("Select All");
        case Command::NUDGE:
            return value < 0 ? _("Nudge Backward") : _("Nudge Forward");
        case Command::MOVE:
            switch(value) {
                case 0:
                    return _("Move Selection Left");
                case 1:
                    return _("Move Selection Right");
                case 2:
                    return _("Move Selection Up");
                default:
                    return _("Move Selection Down");
            }
        case Command::CANCEL:
            return _("Cancel");
        case Command::CUT:
            return _("Cut");
        case Command::COPY:
            return _("Copy");
        case Command::PASTE:
            return _("Paste");
        case Command::CLONE:
            return _("Clone");
        case Command::GRID_SNAP:
            return _("Grid Snap");
        case Command::DISTANCE_SNAP:
            return _("Distance Snap");
        case Command::USE_SPACING:
            return _("Use Current Spacing");
        case Command::GRID_SIZE:
            return value == 0 ? std::string{_("Next Grid Size")} : tformat("Grid Size {}", value);
        case Command::CURVE_TYPE:
            switch((neomod::SLIDERCURVETYPE)value) {
                case neomod::SLIDERCURVETYPE::LINEAR:
                    return _("Linear");
                case neomod::SLIDERCURVETYPE::PASSTHROUGH:
                    return _("Perfect Circle");
                case neomod::SLIDERCURVETYPE::BEZIER:
                    return _("Bezier");
                case neomod::SLIDERCURVETYPE::CATMULL:
                    return _("Catmull");
            }
            break;
        case Command::REVERSE:
            return _("Reverse");
        case Command::FLIP:
            return value == 0 ? _("Flip Horizontally") : _("Flip Vertically");
        case Command::ROTATE:
            return value > 0 ? _("Rotate Clockwise") : _("Rotate Anticlockwise");
        case Command::RESET_SAMPLES:
            return value == 0 ? _("Reset Samples") : _("Reset All Samples");
        case Command::ROTATE_BY:
            return _("Rotate By...");
        case Command::SCALE_BY:
            return _("Scale By...");
        case Command::MOVE_TO:
            return _("Move To...");
        case Command::POLYGON:
            return _("Polygon Circles...");
        case Command::STREAM:
            return _("Convert to Stream...");
        case Command::RESET_COLOURS:
            return _("Reset Combo Colours");
        case Command::TAB:
            switch((Tab)value) {
                case Tab::COMPOSE:
                    return _("Compose");
                case Tab::TIMING:
                    return _("Timing");
                case Tab::SETUP:
                    return _("Song Setup");
                case Tab::VERIFY:
                    return _("Verify");
                case Tab::HISTORY:
                    return _("History");
            }
            break;
        case Command::ADD_TIMING_POINT:
            return value != 0 ? _("Add Timing Point") : _("Add Inherited Point");
        case Command::DELETE_TIMING_POINT:
            return _("Delete Timing Point");
        case Command::RESNAP:
            return _("Resnap All Objects");
        case Command::SLIDER_LENGTHS:
            return _("Recalculate Slider Lengths");
        case Command::INSERT_BREAK:
            return _("Insert Break Here");
        case Command::RESET_BREAKS:
            return _("Reset Breaks");
        case Command::COPY_LINK:
            return _("Copy Link to This Time");
        case Command::OPEN_FOLDER:
            return _("Open Song Folder");
        case Command::OPEN_TEXT:
            return _("Open .osu in Text Editor");
        case Command::EXPORT:
            return _("Export Beatmapset");
        case Command::SET_PREVIEW:
            return _("Set Preview Point Here");
        case Command::METRONOME:
            return _("Metronome");
        case Command::CARRY_OBJECTS:
            return _("Move Objects with Timing Changes");
        case Command::TAP:
            return _("Tap");
    }
    std::unreachable();
}

Group group(Command command) {
    switch(command) {
        case Command::SAVE:
        case Command::TEST_PLAY:
        case Command::REVERT:
        case Command::NEW_DIFFICULTY:
        case Command::OPEN_FOLDER:
        case Command::OPEN_TEXT:
        case Command::EXPORT:
            return Group::FILE;
        case Command::UNDO:
        case Command::REDO:
        case Command::CANCEL:
        case Command::CUT:
        case Command::COPY:
        case Command::PASTE:
        case Command::DELETE_SELECTION:
        case Command::SELECT_ALL:
        case Command::CLONE:
        case Command::NUDGE:
        case Command::RESET_SAMPLES:
        case Command::RESET_COLOURS:
        case Command::ADD_BOOKMARK:
        case Command::REMOVE_BOOKMARK:
        case Command::COPY_LINK:
            return Group::EDIT;
        case Command::TOOL:
        case Command::NEW_COMBO:
        case Command::MOVE:
        case Command::GRID_SNAP:
        case Command::DISTANCE_SNAP:
        case Command::USE_SPACING:
        case Command::GRID_SIZE:
        case Command::CURVE_TYPE:
        case Command::REVERSE:
        case Command::FLIP:
        case Command::ROTATE:
        case Command::ROTATE_BY:
        case Command::SCALE_BY:
        case Command::MOVE_TO:
        case Command::POLYGON:
        case Command::STREAM:
            return Group::COMPOSE;
        case Command::HITSOUND:
        case Command::NORMAL_SET:
        case Command::ADDITION_SET:
            return Group::HITSOUNDS;
        case Command::TOGGLE_PAUSE:
        case Command::PLAY_FROM_START:
        case Command::FIRST_OBJECT:
        case Command::LAST_OBJECT:
        case Command::STEP:
        case Command::NEXT_POINT:
        case Command::PREVIOUS_POINT:
        case Command::RATE:
        case Command::NEXT_BOOKMARK:
        case Command::PREVIOUS_BOOKMARK:
        case Command::DIVISOR:
            return Group::PLAYBACK;
        case Command::TAB:
            return Group::VIEW;
        case Command::ADD_TIMING_POINT:
        case Command::DELETE_TIMING_POINT:
        case Command::RESNAP:
        case Command::SLIDER_LENGTHS:
        case Command::INSERT_BREAK:
        case Command::RESET_BREAKS:
        case Command::SET_PREVIEW:
        case Command::METRONOME:
        case Command::CARRY_OBJECTS:
        case Command::TAP:
            return Group::TIMING;
    }
    std::unreachable();
}

const char *groupName(Group group) {
    switch(group) {
        case Group::FILE:
            return _("File");
        case Group::EDIT:
            return _("Edit");
        case Group::COMPOSE:
            return _("Compose");
        case Group::HITSOUNDS:
            return _("Hitsounds");
        case Group::PLAYBACK:
            return _("Playback");
        case Group::VIEW:
            return _("View");
        case Group::TIMING:
            return _("Timing");
    }
    std::unreachable();
}

std::string keys(Command command, i32 value) {
    std::string out;
    for(const Binding &binding : BINDINGS) {
        if(binding.command != command || binding.value != value) continue;
        if(!out.empty()) out += ", ";
        out += binding.shortcut.text();
    }
    return out;
}

void hint(CBaseUIElement &element, Command command, i32 value) {
    if(!element.isMouseInside()) return;
    const std::string bound = keys(command, value);
    TooltipOverlay *tooltip = ui->getTooltipOverlay();
    tooltip->begin();
    tooltip->addLine(bound.empty() ? label(command, value) : fmt::format("{} ({})", label(command, value), bound));
    tooltip->end();
}
}  // namespace Commands
