#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "Shortcut.h"
#include "types.h"

#include <span>
#include <string>

class CBaseUIElement;

// what the editor does when a key, a menu or a button tells it to: the commands, the keys bound to them and what they
// are called
namespace Commands {
enum class Command : u8 {
    TOGGLE_PAUSE,
    PLAY_FROM_START,
    FIRST_OBJECT,
    LAST_OBJECT,
    STEP,               // value: ticks
    NEXT_POINT,         // a timing point, inherited ones included
    PREVIOUS_POINT,     // the one in force if more than a second ago, else the one before it
    RATE,               // value: percent
    NEXT_BOOKMARK,      // more than 50 ms ahead
    PREVIOUS_BOOKMARK,  // more than 500 ms back
    DIVISOR,            // value: the divisor
    SAVE,
    TEST_PLAY,  // saves, then plays the map from the editor time (see BeatmapInterface::test())
    REVERT,
    NEW_DIFFICULTY,  // value: 1 an empty one, 0 a copy of the open one
    UNDO,
    REDO,
    ADD_BOOKMARK,
    REMOVE_BOOKMARK,
    TOOL,  // value: Compose::Tool
    NEW_COMBO,
    HITSOUND,          // value: whistle, finish or clap
    NORMAL_SET,        // value: the set (0: auto)
    ADDITION_SET,      // value: the set (0: auto)
    DELETE_SELECTION,  // the selected objects, or the selected points on the Timing tab
    SELECT_ALL,
    NUDGE,   // value: -1 a divisor tick earlier, 1 later
    MOVE,    // value: 0 left, 1 right, 2 up, 3 down (by an osu!px; only with a selection)
    CANCEL,  // takes back what's going on, or goes from another tab to Compose (neither: Escape goes back)
    CUT,
    COPY,
    PASTE,
    CLONE,
    GRID_SNAP,
    DISTANCE_SNAP,
    USE_SPACING,          // distance snap uses the spacing the map has here (see Compose::currentSpacing())
    GRID_SIZE,            // value: the size (0: the next one, after the largest the smallest)
    CURVE_TYPE,           // value: the type letter (for the selected slider)
    REVERSE,              // the selection backwards in time
    FLIP,                 // value: 0 horizontally, 1 vertically
    ROTATE,               // value: 1 clockwise, -1 anticlockwise
    RESET_SAMPLES,        // value: 0 for the selection, 1 for every object
    ROTATE_BY,            // a popup for the angle and the center
    SCALE_BY,             // a popup for the factor, the center and the axes
    MOVE_TO,              // a popup for the position (more than one object: the offset)
    POLYGON,              // a popup for circles around the playfield center
    STREAM,               // a popup for circles along the selected slider, in its place
    RESET_COLOURS,        // the colour skip on every object
    TAB,                  // value: EditorState::Tab
    ADD_TIMING_POINT,     // value: 1 an uninherited one, 0 an inherited one (which shows the Timing tab)
    DELETE_TIMING_POINT,  // the one in force at the editor time
    RESNAP,               // every object to the divisor ticks
    SLIDER_LENGTHS,       // every slider length from its curve, snapped to the divisor
    INSERT_BREAK,         // the usual break in the gap the time is in
    RESET_BREAKS,         // every break as given by the map itself
    COPY_LINK,            // a link that opens the saved map here, with the selection (see Osu::openEditorLink())
    OPEN_FOLDER,          // the map folder in the file manager
    OPEN_TEXT,            // the map .osu in the system text editor
    EXPORT,               // saves, then exports the map set as an .osz
    SET_PREVIEW,          // the song select preview starts at the editor time
    METRONOME,
    CARRY_OBJECTS,  // a changed timing point takes the objects in its section along
    TAP,            // a tap along the music, on the Timing tab (see TimingPanel::tap())
};

// the heading a command is listed under
enum class Group : u8 { PLAYBACK, FILE, EDIT, COMPOSE, HITSOUNDS, TIMING, VIEW };

struct Binding {
    Shortcut shortcut;
    Command command;
    i32 value{0};
    bool repeats{false};
};

// every key, in the order a key press tries them: the first whose command applies runs
[[nodiscard]] std::span<const Binding> bindings();
// what a command with this value is called, in a menu, a tooltip and the shortcut list
[[nodiscard]] std::string label(Command command, i32 value = 0);
[[nodiscard]] Group group(Command command);
[[nodiscard]] const char *groupName(Group group);
// the keys bound to a command with this value, e.g. "F3, F6" (none: empty)
[[nodiscard]] std::string keys(Command command, i32 value = 0);
// shows a tooltip with the command and its keys while the mouse is over `element`, e.g. "Circle (2)". called every
// frame
void hint(CBaseUIElement &element, Command command, i32 value = 0);
}  // namespace Commands
