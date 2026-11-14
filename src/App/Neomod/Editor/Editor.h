#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "CBaseUIContainer.h"
#include "CBaseUIMenu.h"
#include "Commands.h"
#include "Database.h"
#include "EditorSession.h"
#include "EditorState.h"
#include "MD5Hash.h"
#include "Registration.h"
#include "noinclude.h"
#include "types.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class CBaseUIBox;
class CBaseUIPopupLayer;
class CBaseUIStyledButton;
class CBaseUIStyledLabel;
class CBaseUITabBar;
class PlayfieldPanel;
class SampleSets;
class SetupPanel;
class TimingPanel;
class VerifyPanel;
class HistoryPanel;
class Sidebar;
class Toolbox;
class ConVar;

// the editor root element; everything else in the editor hangs off it: the open map (shown with its music while the
// editor is), the panels, the menus and the commands that keys and menus run
class Editor final : public CBaseUIContainer {
    NOCOPY_NOMOVE(Editor)
   public:
    using Command = Commands::Command;

    // continues the session saveSession() returned earlier; anything it can't read starts a fresh one
    explicit Editor(std::span<const u8> session);
    ~Editor() override;

    [[nodiscard]] std::vector<u8> saveSession() const;
    // opens the map at `location`: shown now if the editor is visible, else when it next is. unsaved changes to the map
    // that was open go to its draft file, and the draft the new one has is continued. false if the old map stays open
    // (its draft couldn't be written)
    bool open(const Database::MapLocation &location);
    // goes to an osu! timestamp in the open map (see jumpTo()) on the Compose tab, once the map is shown. nothing
    // happens for one that doesn't parse
    void jump(std::string_view timestamp);
    // files dropped on the game window that the game had no use for: an audio file is the sample an open sample popup
    // is choosing, or starts a new beatmap while no map is shown; an image becomes the shown map background
    void drop(std::span<const std::string> paths);
    // reads the open map file again if another program changed it since the document read or saved it (a step, which
    // undo takes back). unsaved changes stay instead, with a notification
    void followFile();

    CBaseUIElement *setVisible(bool visible) override;
    void tick() override;
    void draw() override;
    void updateInput(CBaseUIEventCtx &c) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onResized() override;

   protected:
    bool onWheel(WheelDelta vertical, WheelDelta horizontal) override;

   private:
    void run(Command command, i32 value);
    // whether it can run now with this value (a menu shows it disabled otherwise)
    [[nodiscard]] bool canRun(Command command, i32 value = 0) const;
    // whether a key bound to it means it now (a key bound to several runs the first that does)
    [[nodiscard]] bool applies(Command command) const;
    // a menu item that runs it, with its name and keys (see Commands), enabled as canRun() says
    [[nodiscard]] MenuItem commandItem(std::string name, Command command, i32 value = 0);
    [[nodiscard]] std::vector<MenuItem> fileMenu();
    [[nodiscard]] std::vector<MenuItem> editMenu();
    [[nodiscard]] std::vector<MenuItem> composeMenu();
    [[nodiscard]] std::vector<MenuItem> timingMenu();
    [[nodiscard]] std::vector<MenuItem> viewMenu();
    [[nodiscard]] std::vector<MenuItem> helpMenu();

    // shows the open map: reads its file (with its draft) if the document doesn't have it, loads its music
    void showMap();
    // the open map in the osu!stable songs folder goes on in the neomod library if its set was copied there (the song
    // browser lists the copy in its place)
    void followCopy();
    // `map` is the shown map record from now on, also for the game selection if that was the old one
    void showRecord(DatabaseBeatmap &map);
    // says the open map file changed to `md5` under its unsaved changes (once)
    void tellChanged(const MD5Hash &md5);
    // the skin wears the shown map (the samples, images and slider colours in its folder, as the two convars say),
    // again after they or its folder changed
    void loadMapSkin();
    // remembers where the shown map was and frees its music
    void hideMap();
    // shows the panels of the current tab. without a shown map: the empty state, which says how to open one or why the
    // open one can't be shown
    void showPanels();
    // a field being typed in takes what it has and loses the focus
    void leaveFields();
    // ends what's being placed, dragged or typed where it is and shows the tab
    void setTab(EditorState::Tab tab);
    void openSelected();
    // the open map view, with what the editor shows now
    [[nodiscard]] EditorSession::View currentView() const;
    // another difficulty in the open map set, at the time the editor is at, playing on if it plays
    void openDifficulty(const Database::MapLocation &location);

    // goes where FIRST_OBJECT, LAST_OBJECT, NEXT_POINT, PREVIOUS_POINT, NEXT_BOOKMARK or PREVIOUS_BOOKMARK says (see
    // Commands)
    void seek(Command command);
    void step(i32 ticks);
    // the popup of ROTATE_BY, SCALE_BY, MOVE_TO, POLYGON or STREAM over the playfield
    void openStepPopup(Command command);
    // the popup with the map samples for a sample index (see SampleSets), in the middle
    void openSampleSets(i32 index);
    // goes to a time, paused. with combo numbers, the objects they name from that time on are selected on the Compose
    // tab, and it goes to the first of them (see Compose::selectCombos())
    void jumpTo(i32 timeMS, std::span<const i32> combos);
    void setDivisor(i32 divisor);
    // writes the document to its file (nothing without changes) for the database to read; `then` runs once it's written
    // (after an osu!stable map set was copied first), never after a refused or failed save
    void save(const std::function<void()> &then = {});
    // plays the saved map from the editor time as a test, which comes back here once it ends
    void startTest();
    // a file dialog for the audio file in a new beatmap
    void chooseAudio();
    // a new set in maps/ with the audio file at `audio` and a first difficulty timed at 120 BPM, shown on the Timing tab
    void newBeatmap(const std::string &audio);
    // a difficulty in the open map set made from it (empty: without its objects and breaks), shown on the Setup tab to
    // be named
    void newDifficulty(bool empty);
    // one at the time snapped to the divisor, or (remove) the one nearest to the time if it's less than 2 s away
    void bookmark(bool add);

    // before the state, which points at them
    std::unique_ptr<ConVar> hitAnimations;
    std::unique_ptr<ConVar> gridSnap;
    std::unique_ptr<ConVar> distanceSnap;
    std::unique_ptr<ConVar> metronome;
    std::unique_ptr<ConVar> carryObjects;
    std::unique_ptr<ConVar> backgroundDim;
    std::unique_ptr<ConVar> mapSkin;
    std::unique_ptr<ConVar> mapHitsounds;
    std::unique_ptr<ConVar> soundLabels;
    std::unique_ptr<ConVar> grid;
    EditorState state;
    EditorSession session;
    Mc::Registration gameMusic;       // holds the game music track while the editor is shown
    bool resumeGameMusic{false};      // ...which played (or was loading to) when it was taken
    Mc::Registration fileDialog;      // the file dialog that's open, whose answer goes nowhere once the editor is gone
    EditorSession::View sessionView;  // the shown map view the session was last told about
    u32 sessionRevision{0};           // the document revision the session was last told about
    std::string problem;              // why the open map isn't shown
    std::string pendingJump;          // the timestamp jump() was given, until the map is shown
    i32 rotateCenter{0};              // what Rotate By turned around last: 0 the playfield, 1 the selection
    i32 scaleCenter{0};               // ...and Scale By scaled around
    u8 heldArrows{0};                 // the arrow keys held down (a nudge is one step until they're released)
    // the md5 the open map file changed to under its unsaved changes, once that was said
    std::optional<MD5Hash> toldChanged;
    bool testing{false};  // a test it started is going on: the editor shows again at the time the test ended at

    SampleSets *sampleSets{nullptr};  // owned by the popup layer, there while sampleSetsId is open
    u32 sampleSetsId{0};

    CBaseUIBox *content;
    CBaseUIPopupLayer *popups;
    CBaseUIStyledLabel *title;
    CBaseUITabBar *tabs;
    Toolbox *toolbox;
    Sidebar *sidebar;
    SetupPanel *setup;
    TimingPanel *timing;
    VerifyPanel *verify;
    HistoryPanel *history;
    PlayfieldPanel *playfield;
    CBaseUIBox *emptyState;
    CBaseUIStyledLabel *emptyMessage;
    CBaseUIStyledButton *openSelectedButton;
};
