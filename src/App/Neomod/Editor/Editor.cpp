// Copyright (c) 2026, WH, All rights reserved.
#include "Editor.h"

#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BaseEnvironment.h"
#include "BeatSnap.h"
#include "BeatmapInstaller.h"
#include "BeatmapInterface.h"
#include "ByteBufferedFile.h"
#include "CBaseUIBox.h"
#include "CBaseUIEventCtx.h"
#include "CBaseUIMenuBar.h"
#include "CBaseUINumberField.h"
#include "CBaseUIPopupLayer.h"
#include "CBaseUIStyledButton.h"
#include "CBaseUIStyledLabel.h"
#include "CBaseUITabBar.h"
#include "ConVar.h"
#include "ContainerRanges.h"
#include "DatabaseBeatmap.h"
#include "EditorHistory.h"
#include "EditorScreen.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "GameRules.h"
#include "Graphics.h"
#include "HUD.h"
#include "HistoryPanel.h"
#include "HitSounds.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "KeyboardEvent.h"
#include "Logging.h"
#include "MapExporter.h"
#include "MapFiles.h"
#include "MusicTrack.h"
#include "NeomodUrl.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "Overview.h"
#include "Paths.h"
#include "PlayfieldPanel.h"
#include "Retiming.h"
#include "RichPresence.h"
#include "SString.h"
#include "SetupPanel.h"
#include "Shortcut.h"
#include "ShortcutList.h"
#include "Sidebar.h"
#include "Skin.h"
#include "SliderCurves.h"
#include "SampleSets.h"
#include "StepPopup.h"
#include "Stepper.h"
#include "TimeDisplay.h"
#include "Timeline.h"
#include "TimingPanel.h"
#include "Toolbox.h"
#include "Transport.h"
#include "UI.h"
#include "UIStyle.h"
#include "VerifyPanel.h"
#include "crypto.h"
#include "i18n.h"

#include "fmt/format.h"
#include "fmt/ranges.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <numbers>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using Kind = neomod::BeatmapFile::SectionKind;
using Step = Document::Action;
using Command = Editor::Command;
using Tool = Compose::Tool;
using Tab = EditorState::Tab;

namespace {
// the step name, for the Edit menu
const char *stepName(Step step) {
    switch(step) {
        case Step::ADD_BOOKMARK:
            return _("Add Bookmark");
        case Step::REMOVE_BOOKMARK:
            return _("Remove Bookmark");
        case Step::REVERT:
            return _("Revert to Saved");
        case Step::RESTORE:
            return _("Restore Version");
        case Step::ADD_CIRCLE:
            return _("Add Circle");
        case Step::ADD_SLIDER:
            return _("Add Slider");
        case Step::ADD_SPINNER:
            return _("Add Spinner");
        case Step::EDIT_SLIDER:
            return _("Edit Slider");
        case Step::CURVE_TYPE:
            return _("Curve Type");
        case Step::REPEATS:
            return _("Repeats");
        case Step::SPINNER_END:
            return _("Spinner End");
        case Step::REVERSE:
            return _("Reverse");
        case Step::FLIP:
            return _("Flip");
        case Step::ROTATE:
            return _("Rotate");
        case Step::SCALE:
            return _("Scale");
        case Step::POLYGON:
            return _("Polygon Circles");
        case Step::STREAM:
            return _("Convert to Stream");
        case Step::RESET_SAMPLES:
            return _("Reset Samples");
        case Step::RESET_COLOURS:
            return _("Reset Combo Colours");
        case Step::DELETE_OBJECTS:
            return _("Delete");
        case Step::MOVE:
            return _("Move");
        case Step::NUDGE:
            return _("Nudge");
        case Step::NEW_COMBO:
            return _("New Combo");
        case Step::COLOUR_SKIP:
            return _("Combo Colour");
        case Step::HITSOUNDS:
            return _("Hitsounds");
        case Step::SAMPLE_SET:
            return _("Sample Set");
        case Step::ADDITION_SET:
            return _("Additions");
        case Step::SAMPLE_INDEX:
            return _("Sample Index");
        case Step::SAMPLE_VOLUME:
            return _("Sample Volume");
        case Step::SAMPLE_FILE:
            return _("Sample File");
        case Step::REMOVE_BREAK:
            return _("Remove Break");
        case Step::MOVE_BREAK:
            return _("Move Break");
        case Step::INSERT_BREAK:
            return _("Insert Break");
        case Step::RESET_BREAKS:
            return _("Reset Breaks");
        case Step::CUT:
            return _("Cut");
        case Step::PASTE:
            return _("Paste");
        case Step::CLONE:
            return _("Clone");
        case Step::METADATA:
            return _("Metadata");
        case Step::DIFFICULTY:
            return _("Difficulty");
        case Step::MAP_SETTINGS:
            return _("Map Settings");
        case Step::COLOURS:
            return _("Colours");
        case Step::TIMING:
            return _("Timing");
        case Step::ADD_TIMING_POINT:
            return _("Add Timing Point");
        case Step::DELETE_TIMING_POINT:
            return _("Delete Timing Point");
        case Step::RESNAP:
            return _("Resnap");
        case Step::SLIDER_LENGTHS:
            return _("Recalculate Slider Lengths");
        case Step::BACKGROUND:
            return _("Background");
    }
    std::unreachable();
}

// keeps the bottom right screen corner free while the fps counter is drawn there
class FpsCorner final : public CBaseUIContainer {
    NOCOPY_NOMOVE(FpsCorner)
   public:
    FpsCorner() : CBaseUIContainer(0, 0, 0, 0, "editor_fps_corner") {}
    ~FpsCorner() override = default;

    void tick() override {
        CBaseUIContainer::tick();
        if(const vec2 size = HUD::getFpsCounterSize(); size != this->counterSize) {
            this->counterSize = size;
            this->setVisible(size.x > 0.f);
            this->requestLayout();
        }
    }
    [[nodiscard]] vec2 getNaturalSize() override { return this->counterSize; }

   private:
    vec2 counterSize{-1.f};
};

CBaseUIBox *row(std::string name) {
    auto *box = new CBaseUIBox(CBaseUIBox::Axis::ROW, std::move(name));
    box->setGap(uiStyle().metrics.gap);
    return box;
}

std::string describe(const DatabaseBeatmap &map) {
    // a new map has no artist or difficulty name yet
    std::string text =
        map.getArtist().empty() ? std::string{map.getTitle()} : fmt::format("{} - {}", map.getArtist(), map.getTitle());
    if(!map.getDifficultyName().empty()) text += fmt::format(" [{}]", map.getDifficultyName());
    return text;
}

// what the editor takes a dropped or chosen file for
bool isAudio(std::string_view path) {
    const std::string extension = SString::to_lower(Environment::getFileExtensionFromFilePath(path));
    return extension == "mp3" || extension == "ogg" || extension == "wav";
}
bool isImage(std::string_view path) {
    const std::string extension = SString::to_lower(Environment::getFileExtensionFromFilePath(path));
    return extension == "jpg" || extension == "jpeg" || extension == "png";
}

void notify(std::string text) { ui->getNotificationOverlay()->addNotification(std::move(text)); }

// a Compose tab command, which keys and menus don't run on the other tabs
bool composes(Command command) {
    switch(command) {
        case Command::TOOL:
        case Command::NEW_COMBO:
        case Command::HITSOUND:
        case Command::NORMAL_SET:
        case Command::ADDITION_SET:
        case Command::SELECT_ALL:
        case Command::NUDGE:
        case Command::MOVE:
        case Command::CLONE:
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
        case Command::RESET_SAMPLES:
        case Command::RESET_COLOURS:
            return true;
        default:
            return false;
    }
}

// the bit for the arrow key in Editor::heldArrows (0: another key)
u8 arrowBit(const KeyboardEvent &e) {
    return e == KEY_LEFT ? 1 : e == KEY_RIGHT ? 2 : e == KEY_UP ? 4 : e == KEY_DOWN ? 8 : 0;
}
}  // namespace

Editor::Editor(std::span<const u8> session)
    : CBaseUIContainer(0, 0, 0, 0, "editor"),
      hitAnimations(
          new ConVar{"editor_hit_animations", false, cv::CLIENT,
                     "past circles in the editor show the hit animations of a play instead of the editor trail"}),
      gridSnap(
          new ConVar{"editor_grid_snap", true, cv::CLIENT, "objects placed and moved in the editor snap to its grid"}),
      distanceSnap(new ConVar{"editor_distance_snap", true, cv::CLIENT,
                              "objects placed and moved in the editor are spaced from the object before them by their "
                              "time apart (distance snap)"}),
      metronome(new ConVar{"editor_metronome", true, cv::CLIENT,
                           "the Timing tab of the editor ticks on the beats of the music while it plays"}),
      carryObjects(new ConVar{"editor_timing_moves_objects", true, cv::CLIENT,
                              "a timing point changed in the editor takes the objects and inherited points of its "
                              "section along"}),
      backgroundDim(new ConVar{"editor_background_dim", 0.25f, cv::CLIENT,
                               "how much the editor darkens the map background outside breaks (0 to 1)",
                               cv::Range{0., 1.}}),
      mapSkin(new ConVar{"editor_map_skin", true, cv::CLIENT,
                         "a map in the editor is shown with the skin elements and slider colours of its folder",
                         SA::MakeDelegate<&Editor::loadMapSkin>(this)}),
      mapHitsounds(new ConVar{"editor_map_hitsounds", true, cv::CLIENT,
                              "a map in the editor is heard with the hitsound samples of its folder",
                              SA::MakeDelegate<&Editor::loadMapSkin>(this)}),
      soundLabels(new ConVar{"editor_hitsound_labels", true, cv::CLIENT,
                             "the editor timeline says which sounds each object plays, if not the default"}),
      grid(new ConVar{"editor_grid", true, cv::CLIENT, "the editor playfield shows the grid positions snap to"}),
      session(EditorSession::read(session)) {
    this->state.hitAnimations = this->hitAnimations.get();
    this->state.gridSnap = this->gridSnap.get();
    this->state.distanceSnap = this->distanceSnap.get();
    this->state.metronome = this->metronome.get();
    this->state.carryObjects = this->carryObjects.get();
    this->state.backgroundDim = this->backgroundDim.get();
    this->state.soundLabels = this->soundLabels.get();
    this->state.grid = this->grid.get();
    if(this->session.tab <= (u8)Tab::HISTORY) this->state.tab = (Tab)this->session.tab;
    // shown by its screen (see EditorScreen::createEditor())
    this->bVisible = false;
    debugLog("editor session: {} map(s), {}{}", this->session.views.size(),
             this->session.open ? this->session.open->file : "none open", this->session.draft ? " (changed)" : "");

    const UIStyle &style = uiStyle();
    // created first: the menu bar in the content opens its menus in it. added last, over the content
    this->popups = new CBaseUIPopupLayer("editor_popups");

    this->content = new CBaseUIBox(CBaseUIBox::Axis::COLUMN, "editor_content");
    this->content->setPadding(style.metrics.padding);
    this->content->setGap(style.metrics.gap);

    auto *menuBar = new CBaseUIMenuBar(*this->popups, "editor_menu");
    menuBar->add("editor_menu_file", _("File"), [this] { return this->fileMenu(); })
        ->add("editor_menu_edit", _("Edit"), [this] { return this->editMenu(); })
        ->add("editor_menu_compose", _("Compose"), [this] { return this->composeMenu(); })
        ->add("editor_menu_timing", _("Timing"), [this] { return this->timingMenu(); })
        ->add("editor_menu_view", _("View"), [this] { return this->viewMenu(); })
        ->add("editor_menu_help", _("Help"), [this] { return this->helpMenu(); });
    auto *exitButton = new CBaseUIStyledButton(_("Back"), "editor_exit");
    exitButton->setClickCallback([] { ui->getEditorScreen()->onBack(); });
    this->title = new CBaseUIStyledLabel(_("Beatmap Editor"), "editor_title");
    // a long title keeps the difficulty name and the mark for unsaved changes at its end
    this->title->setCutInMiddle(true);
    this->tabs = new CBaseUITabBar("editor_tabs");
    this->tabs->add("editor_tab_compose", _("Compose"))
        ->add("editor_tab_timing", _("Timing"))
        ->add("editor_tab_setup", _("Setup"))
        ->add("editor_tab_verify", _("Verify"))
        ->add("editor_tab_history", _("History"));
    this->tabs->setChangeCallback([this](int tab) { this->run(Command::TAB, tab); });

    this->toolbox = new Toolbox(this->state);
    this->sidebar = new Sidebar(this->state, *this->popups, [this] { this->loadMapSkin(); });
    this->setup = new SetupPanel(this->state, *this->popups);
    this->timing = new TimingPanel(this->state, *this->popups, [this](i32 index) { this->openSampleSets(index); });
    this->verify = new VerifyPanel(this->state, [this](Tab tab) { this->setTab(tab); });
    this->history = new HistoryPanel(this->state);
    this->playfield = new PlayfieldPanel(this->state);
    this->emptyMessage = new CBaseUIStyledLabel({}, "editor_empty_message");
    this->emptyMessage->setJustification(TEXT_JUSTIFICATION::CENTERED);
    this->openSelectedButton = new CBaseUIStyledButton({}, "editor_open_selected");
    this->openSelectedButton->setClickCallback([this] { this->openSelected(); });
    auto *newBeatmapButton = new CBaseUIStyledButton(_("New Beatmap from an Audio File"), "editor_new_beatmap");
    newBeatmapButton->setClickCallback([this] { this->chooseAudio(); });
    this->emptyState = new CBaseUIBox(CBaseUIBox::Axis::COLUMN, "editor_empty");
    this->emptyState->setGap(style.metrics.gap);
    this->emptyState->add(new CBaseUIContainer(), BoxItem::flex())
        ->add(this->emptyMessage, BoxItem::fit())
        ->add(this->openSelectedButton, BoxItem::fit(BoxItem::Align::CENTER))
        ->add(newBeatmapButton, BoxItem::fit(BoxItem::Align::CENTER))
        ->add(new CBaseUIContainer(), BoxItem::flex());

    this->content->add(row("editor_top")
                           ->add(menuBar, BoxItem::fit())
                           ->add(this->title, BoxItem::flex())
                           ->add(this->tabs, BoxItem::fit())
                           ->add(exitButton, BoxItem::fit()),
                       BoxItem::fit());
    auto *timeline = new Timeline(this->state);
    auto *zoomIn = new CBaseUIStyledButton("+", "editor_zoom_in");
    zoomIn->setClickCallback([timeline] { timeline->zoom(1); });
    auto *zoomOut = new CBaseUIStyledButton("-", "editor_zoom_out");
    zoomOut->setClickCallback([timeline] { timeline->zoom(-1); });
    auto *zoom = new CBaseUIBox(CBaseUIBox::Axis::COLUMN, "editor_zoom");
    zoom->setGap(style.metrics.gap);
    zoom->add(zoomIn, BoxItem::flex())->add(zoomOut, BoxItem::flex());
    this->content->add(
        row("editor_timeline_row")
            ->add(new TimeDisplay(this->state,
                                  [this](const TimeDisplay::Timestamp &to) { this->jumpTo(to.time, to.combos); }),
                  BoxItem::fit())
            ->add(zoom, BoxItem::fit())
            ->add(timeline, BoxItem::flex()),
        BoxItem::fixed(64));
    this->content->add(row("editor_middle")
                           ->add(this->toolbox, BoxItem::fixed(110))
                           ->add(this->playfield, BoxItem::flex())
                           ->add(this->emptyState, BoxItem::flex())
                           ->add(this->sidebar, BoxItem::fixed(150))
                           ->add(this->timing, BoxItem::flex())
                           ->add(this->setup, BoxItem::flex())
                           ->add(this->verify, BoxItem::flex())
                           ->add(this->history, BoxItem::flex()),
                       BoxItem::flex());
    auto *divisor = new Stepper(
        "editor_divisor",
        [this](i32 by) {
            const auto &divisors = BeatSnap::DIVISORS;
            const auto at = Mc::ranges::find(divisors, this->state.divisor) - divisors.begin();
            this->setDivisor(divisors[std::clamp<i64>(at + by, 0, (i64)divisors.size() - 1)]);
        },
        [this] { return fmt::format("1/{}", this->state.divisor); });
    divisor->setPadding(style.metrics.padding);
    this->content->add(row("editor_bottom")
                           ->add(new Transport(this->state), BoxItem::fit())
                           ->add(new Overview(this->state), BoxItem::flex())
                           ->add(divisor, BoxItem::fixed(150))
                           ->add(new FpsCorner(), BoxItem::fit()),
                       BoxItem::fixed(48));

    this->addBaseUIElement(this->content);
    this->addBaseUIElement(this->popups);
    this->showPanels();
}

Editor::~Editor() {
    // the content first: its menu bar closes its menu in the popup layer when it goes
    this->deleteBaseUIElement(this->content);
    this->freeElements();
}

std::vector<u8> Editor::saveSession() const {
    EditorSession current = this->session;
    if(this->state.clock.isAttached()) current.views.front() = this->currentView();
    current.tool = (u8)this->state.compose.getTool();
    current.tab = (u8)this->state.tab;
    if(const Document *document = this->state.document.get()) {
        // by line numbers: ids belong to the document, which a read makes anew
        current.selection.clear();
        for(const Document::Id id : this->state.compose.getSelection()) {
            current.selection.push_back(document->getNumber(id));
        }
    }
    // the document holds the unsaved changes to the open map while it has one
    if(const Document *document = this->state.document.get();
       document && current.open && document->getLocation() == *current.open) {
        current.draft.reset();
        if(document->isChanged()) {
            current.draft = {.base = document->getMD5(),
                             .bytes = {document->getBytes().begin(), document->getBytes().end()}};
        }
    }
    return current.write();
}

EditorSession::View Editor::currentView() const {
    return {.location = *this->session.open,
            .time = this->state.clock.getTime(),
            .zoom = this->state.zoom,
            .divisor = this->state.divisor,
            .spacing = this->state.spacing,
            .gridSize = this->state.gridSize};
}

bool Editor::open(const Database::MapLocation &location) {
    if(this->session.open && *this->session.open == location) {
        if(this->isVisible()) this->showMap();
        return true;
    }
    this->pendingJump.clear();
    // what's open or being typed is for the map that goes
    this->popups->closeAll();
    this->leaveFields();

    // the unsaved changes to the open map go to its draft file (it stays open if they can't)
    bool kept = true;
    if(Document *document = this->state.document.get(); document && document->isChanged()) {
        kept = EditorSession::putDraft(
            document->getLocation(),
            {.base = document->getMD5(), .bytes = {document->getBytes().begin(), document->getBytes().end()}});
    } else if(this->session.draft && this->session.open) {
        kept = EditorSession::putDraft(*this->session.open, *this->session.draft);
    }
    if(!kept) {
        ui->getNotificationOverlay()->addToast(
            _("The unsaved changes to the open map couldn't be kept in a draft, so it stays open."), ERROR_TOAST);
        return false;
    }
    this->hideMap();
    this->timing->clear();
    this->verify->clear();
    this->history->clear();
    this->state.close();

    this->session.opened(location);
    this->session.selection.clear();
    // on disk in the session before its file goes, guarded like a restore: unsaved changes whose showing crashes the
    // game are set aside with the session, not opened again with the map
    this->session.draft = EditorSession::draftOf(location);
    if(this->session.draft) ui->getEditorScreen()->writeSessionGuarded();
    EditorSession::dropDraft(location);
    this->problem.clear();
    if(this->isVisible()) this->showMap();
    ui->getEditorScreen()->sessionChanged();
    return true;
}

void Editor::jump(std::string_view timestamp) {
    this->pendingJump = timestamp;
    if(!this->state.clock.isAttached()) return;
    if(const auto to = TimeDisplay::parse(std::exchange(this->pendingJump, {}))) {
        this->setTab(Tab::COMPOSE);
        this->jumpTo(to->time, to->combos);
    }
}

void Editor::openDifficulty(const Database::MapLocation &location) {
    const i32 time = this->state.clock.getTime();
    const bool playing = this->state.clock.isPlaying();
    const f32 rate = this->state.clock.getRate();
    this->open(location);
    if(!this->state.clock.isAttached()) return;
    this->state.clock.seek(time);
    this->state.clock.setRate(rate);
    if(playing) this->state.clock.play();
}

CBaseUIElement *Editor::setVisible(bool visible) {
    MusicTrack *music = osu->getMusicTrack();
    if(visible) {
        // the game music waits while the editor is shown, and nothing starts it or changes its selection
        if(!this->isVisible()) {
            this->gameMusic = music->hold();
            this->resumeGameMusic = music->isPlaying() || music->isLoading();
            music->pause();
        }
        if(std::exchange(this->testing, false) && this->session.open) {
            this->session.views.front().time = osu->getMapInterface()->getTestEnd();
        }
        this->showMap();
    } else {
        this->popups->closeAll();
        this->leaveFields();
        this->hideMap();
        // a save or a test that waited for a set copy isn't made on the next visit
        this->state.afterCopy.clear();
        // and plays on once it's left if it played, unless for a play
        if(this->isVisible()) {
            this->gameMusic.reset();
            if(std::exchange(this->resumeGameMusic, false) && !osu->isInPlayMode()) music->play();
        }
    }
    return CBaseUIContainer::setVisible(visible);
}

void Editor::showMap() {
    if(this->state.clock.isAttached() || !this->session.open) {
        this->showPanels();
        return;
    }
    this->followCopy();
    const Database::MapLocation &location = *this->session.open;
    DatabaseBeatmap *map = db->getBeatmapDifficulty(location);
    if(!map) {
        // unsaved changes stay with the session
        this->problem = _("The map that was open here isn't in the song browser anymore.");
        this->showPanels();
        return;
    }

    Document *document = this->state.document.get();
    const bool reading = !document;
    if(!document) {
        const std::optional<EditorSession::Draft> draft = std::exchange(this->session.draft, std::nullopt);
        auto [read, error] = draft ? Document::read(location, map->getFilePath(), draft->bytes)
                                   : Document::read(location, map->getFilePath());
        if(!read) {
            this->session.draft = draft;
            this->problem = error.errc == neomod::Primitives::LoadError::NON_STD_GAMEMODE
                                ? std::string{_("The editor only opens osu!standard maps.")}
                                : tformat("This map can't be opened: {}", error.error_string());
            this->showPanels();
            return;
        }
        // the selection the session has for it
        std::vector<Document::Id> selected;
        for(const u32 number : std::exchange(this->session.selection, {})) selected.push_back(read->getIdAt(number));
        this->state.open(std::move(read));
        this->toldChanged.reset();
        document = this->state.document.get();
        this->state.compose.selectTouched(std::move(selected));
        if(draft && document->isChanged()) {
            debugLog("editor: {} continues its unsaved changes", location.file);
            notify(_("The unsaved changes to this map are back. File > Revert to Saved discards them."));
            if(draft->base != document->getMD5()) this->tellChanged(document->getMD5());
        }
    } else {
        this->followFile();
    }

    this->problem.clear();
    const EditorSession::View &view = this->session.views.front();
    // a file can say anything
    const f32 zoom = view.zoom.value_or(document->getTimelineZoom().value_or(1.f));
    this->state.zoom = std::isfinite(zoom) ? std::clamp(zoom, 0.1f, 8.f) : 1.f;
    this->state.divisor = view.divisor                 ? *view.divisor
                          : document->getBeatDivisor() ? BeatSnap::clampDivisor(*document->getBeatDivisor())
                                                       : this->state.divisor;
    this->state.spacing = view.spacing                     ? *view.spacing
                          : document->getDistanceSpacing() ? Snapping::clampSpacing(*document->getDistanceSpacing())
                                                           : this->state.spacing;
    this->state.gridSize = view.gridSize             ? *view.gridSize
                           : document->getGridSize() ? Snapping::clampGridSize(*document->getGridSize())
                                                     : this->state.gridSize;
    this->state.clock.attach(*map, document->getPrimitives().version, view.time);
    this->loadMapSkin();
    // the tool the session has, else Select for a map with objects, Circle for an empty one
    if(const auto tool = std::exchange(this->session.tool, std::nullopt); tool && *tool <= (u8)Tool::SPINNER) {
        this->state.compose.setTool((Tool)*tool);
    } else if(reading) {
        this->state.compose.setTool(this->state.objects->objects.empty() ? Tool::CIRCLE : Tool::SELECT);
    }
    this->title->setText(describe(*map));
    RichPresence::onEditor(*map);
    this->showPanels();
    if(!this->pendingJump.empty()) this->jump(std::exchange(this->pendingJump, {}));
}

void Editor::followCopy() {
    if(!this->session.open || this->session.open->root != Database::MapRoot::Peppy ||
       db->getBeatmapDifficulty(*this->session.open)) {
        return;
    }
    const Database::MapLocation copy = Database::standIn(*this->session.open);
    DatabaseBeatmap *map = db->getBeatmapDifficulty(copy);
    if(!map) return;
    debugLog("editor: {} goes on in maps/{}", copy.file, copy.folder);
    if(Document *document = this->state.document.get()) document->moveTo(copy);
    this->session.moved(copy);
    if(this->state.clock.isAttached()) {
        this->showRecord(*map);
        this->loadMapSkin();
    }
    ui->getEditorScreen()->workChanged();
}

void Editor::showRecord(DatabaseBeatmap &map) {
    if(BeatmapInterface *game = osu->getMapInterface(); game->getBeatmap() == this->state.clock.getMap()) {
        game->selectBeatmap(&map);
    }
    this->state.clock.reselect(map);
    // a save may have renamed it
    this->title->setText(describe(map));
    RichPresence::onEditor(map);
}

void Editor::followFile() {
    Document *document = this->state.document.get();
    // a step popup works on the map as it was when it opened
    if(!document || this->popups->isOpen()) return;
    const std::string path = db->pathOf(document->getLocation());
    const auto onDisk = crypto::hash::md5_file(path);
    if(!onDisk || *onDisk == document->getMD5()) return;
    if(document->isChanged()) {
        this->tellChanged(*onDisk);
    } else {
        this->state.compose.endGesture();
        this->state.revert(path);
    }
}

void Editor::tellChanged(const MD5Hash &md5) {
    if(this->toldChanged == md5) return;
    this->toldChanged = md5;
    debugLog("editor: {} changed on disk under its unsaved changes", this->session.open->file);
    notify(_("The map file changed since these unsaved changes were made. File > Revert to Saved loads it."));
}

void Editor::loadMapSkin() {
    const DatabaseBeatmap *map = this->state.clock.getMap();
    Skin *skin = osu->getSkinMutable();
    if(!map || !skin) return;
    const auto &primitives = this->state.document->getPrimitives();
    skin->loadBeatmapOverride(map->getFolder(), primitives.sampleFiles, primitives.sliderBorder,
                              primitives.sliderTrackOverride, this->mapSkin->getBool(), this->mapHitsounds->getBool());
}

void Editor::hideMap() {
    if(!this->state.clock.isAttached()) return;
    this->session.views.front() = this->currentView();
    this->state.playback.stop();
    this->state.clock.detach();
    // a play that has started wears the map itself by now
    if(Skin *skin = osu->getSkinMutable(); skin && !osu->isInPlayMode()) skin->unloadBeatmapOverride();
}

void Editor::showPanels() {
    const bool shown = this->state.clock.isAttached();
    // without a map every tab is the empty state
    const bool composing = !shown || this->state.tab == Tab::COMPOSE;
    this->tabs->setChosen((int)this->state.tab);
    this->toolbox->setVisible(composing);
    this->sidebar->setVisible(composing);
    this->playfield->setVisible(composing && shown);
    this->emptyState->setVisible(!shown);
    this->timing->setVisible(shown && this->state.tab == Tab::TIMING);
    this->setup->setVisible(shown && this->state.tab == Tab::SETUP);
    this->verify->setVisible(shown && this->state.tab == Tab::VERIFY);
    this->history->setVisible(shown && this->state.tab == Tab::HISTORY);
    if(shown) return;

    this->title->setText(_("Beatmap Editor"));
    this->emptyMessage->setText(this->problem.empty()
                                    ? _("Right-click a difficulty in the song browser and choose Edit to open it here.")
                                    : this->problem);
    const DatabaseBeatmap *selected = osu->getMapInterface()->getBeatmap();
    const bool offered = selected && !db->locate(*selected).file.empty() &&
                         (!this->session.open || db->locate(*selected) != *this->session.open);
    this->openSelectedButton->setVisible(offered);
    if(offered) this->openSelectedButton->setText(tformat("Edit {}", describe(*selected)));
}

void Editor::openSelected() {
    if(const DatabaseBeatmap *selected = osu->getMapInterface()->getBeatmap()) {
        if(Database::MapLocation location = db->locate(*selected); !location.file.empty()) this->open(location);
    }
}

void Editor::tick() {
    // a step popup closed by a click outside keeps its step
    if(this->state.compose.isPreviewing() && !this->popups->isOpen()) this->state.compose.endPreview(true);
    // once a set copy is done, the open map goes on in it if it's from that set, and the writes that waited for it are
    // made, one after another until one opens another map or starts a test (a copy that failed said so; one finished
    // while the editor was hidden was followed when it showed the map)
    if(!this->state.copying.empty() && !osu->getBeatmapInstaller()->is_writing(this->state.copying)) {
        this->state.copying.clear();
        const auto writes = std::exchange(this->state.afterCopy, {});
        this->followCopy();
        if(const Document *document = this->state.document.get();
           document && document->getLocation().root == Database::MapRoot::Neomod) {
            const Database::MapLocation location = document->getLocation();
            for(const auto &write : writes) {
                if(this->testing || !this->state.document || this->state.document->getLocation() != location) break;
                write();
            }
        }
    }
    // what holds for the open map between frames
    assert(
        (!this->state.document || (this->session.open && this->state.document->getLocation() == *this->session.open)) &&
        "a document for another map than the open one");
    assert((!this->state.document || !this->session.draft) && "the unsaved changes to the open map in two places");
    assert((!this->state.document || this->state.objects->revision == this->state.document->getRevision()) &&
           "a change that didn't make the objects again (see EditorState)");
    assert((!this->state.clock.isAttached() || this->state.document) && "a map shown without its document");
    if(Document *document = this->state.document.get()) {
        if(document->getRevision() != this->sessionRevision) {
            this->sessionRevision = document->getRevision();
            ui->getEditorScreen()->workChanged();
            // an edit can change which files hit samples name, and the numbers they go by
            if(Skin *skin = osu->getSkinMutable(); skin && this->state.clock.isAttached()) {
                const auto &primitives = document->getPrimitives();
                skin->updateBeatmapOverride(primitives.sampleFiles, primitives.sliderBorder,
                                            primitives.sliderTrackOverride);
            }
        }
        // the database read a save (or a change from another program that the document read too): its new object is the
        // map record from now on, also for the game selection if that was the old one (held, the game doesn't follow
        // it). it's looked for by place only when the content record isn't the one shown; never for osu!stable maps,
        // which aren't written or watched
        if(this->state.clock.isAttached() && document->getLocation().root == Database::MapRoot::Neomod) {
            const DatabaseBeatmap *shown = this->state.clock.getMap();
            if(const DatabaseBeatmap *same = db->getBeatmapDifficulty(document->getMD5()); same && same != shown) {
                if(DatabaseBeatmap *map = db->getBeatmapDifficulty(document->getLocation());
                   map && map->getMD5() == document->getMD5() && map != shown) {
                    this->showRecord(*map);
                }
            }
        }
        // unsaved changes mark the title
        if(const DatabaseBeatmap *map = this->state.clock.getMap();
           map && this->title->getText().ends_with(" *") != document->isChanged()) {
            this->title->setText(document->isChanged() ? describe(*map) + " *" : describe(*map));
        }
    }
    if(this->state.clock.isAttached()) {
        this->state.clock.update();
        this->state.compose.update();
        this->state.playback.update(this->state.clock, *this->state.objects, *this->state.document);
        // playing, seeks and the view settings change it; compared in place, without copying the location
        const EditorSession::View &told = this->sessionView;
        if(told.time != this->state.clock.getTime() || told.zoom != this->state.zoom ||
           told.divisor != this->state.divisor || told.spacing != this->state.spacing ||
           told.gridSize != this->state.gridSize || told.location != *this->session.open) {
            this->sessionView = this->currentView();
            ui->getEditorScreen()->sessionChanged();
        }
    }
    CBaseUIContainer::tick();
}

void Editor::draw() {
    if(!this->isVisible()) return;

    g->setColor(uiStyle().palette.background);
    g->fillRect(this->getRect());

    CBaseUIContainer::draw();
}

void Editor::onResized() {
    this->content->setSize(this->getSize());
    this->popups->setSize(this->getSize());
    CBaseUIContainer::onResized();
}

void Editor::updateInput(CBaseUIEventCtx &c) {
    if(!this->isVisible()) return;
    // registered first: whatever the cursor is over gets the wheel before the editor takes it for the time
    if(c.propagate_clicks) c.addWheelClaim(this);
    CBaseUIContainer::updateInput(c);
}

bool Editor::onWheel(WheelDelta vertical, WheelDelta /*horizontal*/) {
    // Alt and the wheel change the volume, as everywhere
    if(keyboard->isAltDown()) return false;
    if(!this->state.clock.isAttached() || vertical.ticks == 0) return true;

    if(keyboard->isControlDown()) {
        // up doubles the divisor, down halves it
        i32 divisor = this->state.divisor;
        for(i32 i = 0; i < std::abs(vertical.ticks); i++) {
            divisor = BeatSnap::clampDivisor(vertical.ticks > 0 ? std::min(16, divisor * 2) : std::max(1, divisor / 2));
        }
        this->setDivisor(divisor);
    } else {
        // up goes back
        this->step(-vertical.ticks * (keyboard->isShiftDown() ? 4 : 1));
    }
    return true;
}

void Editor::onKeyDown(KeyboardEvent &e) {
    // an open popup owns its keys, also while one of its fields has the focus
    if(this->isVisible() && this->popups->isOpen()) {
        this->popups->onKeyDown(e);
        return;
    }
    CBaseUIContainer::onKeyDown(e);
    if(e.isConsumed() || !this->isVisible()) return;
    const bool shown = this->state.clock.isAttached();
    // Enter takes the offer from the empty state, as it plays in the main menu
    if(!shown && (e == KEY_ENTER || e == KEY_NUMPAD_ENTER) && this->openSelectedButton->isVisible()) {
        e.consume();
        this->openSelected();
        return;
    }

    if(!e.isRepeat()) this->heldArrows |= arrowBit(e);
    // the first key binding that means something now (without a map only the tabs do); with none Escape goes on, to
    // go back. Alt is held to turn distance snap around, so a key means the same with it
    const auto bindings = Commands::bindings();
    const auto binding = Mc::ranges::find_if(bindings, [&](const Commands::Binding &b) {
        return b.shortcut.matches(e, KEYMOD_ALT) && (shown || b.command == Command::TAB) && this->applies(b.command);
    });
    if(binding == bindings.end()) return;
    e.consume();
    if(e.isRepeat() && !binding->repeats) return;
    this->run(binding->command, binding->value);
}

void Editor::onKeyUp(KeyboardEvent &e) {
    CBaseUIContainer::onKeyUp(e);
    if(const u8 bit = arrowBit(e)) {
        this->heldArrows &= ~bit;
        // moving by the arrows is one step until they're all released
        if(this->heldArrows == 0) this->state.compose.endNudge();
    }
}

bool Editor::applies(Command command) const {
    if(composes(command) && this->state.tab != Tab::COMPOSE) return false;
    switch(command) {
        case Command::TAP:
            return this->state.tab == Tab::TIMING;
        // the objects, or the points on the Timing tab
        case Command::DELETE_SELECTION:
        case Command::CUT:
        case Command::COPY:
        case Command::PASTE:
            return this->state.tab == Tab::COMPOSE || this->state.tab == Tab::TIMING;
        case Command::MOVE:
            return !this->state.compose.getSelection().empty();
        case Command::CANCEL:
            return this->state.tab != Tab::COMPOSE || this->state.compose.canCancel();
        default:
            return true;
    }
}

bool Editor::canRun(Command command, i32 value) const {
    if(composes(command) && this->state.tab != Tab::COMPOSE) return false;
    // without a map only the tab changes, which shows once one is
    if(!this->state.clock.isAttached()) return command == Command::TAB;
    const Document &document = *this->state.document;
    const bool selected = !this->state.compose.getSelection().empty();
    switch(command) {
        case Command::DELETE_TIMING_POINT:
            return !document.getTimingPoints().empty();
        case Command::UNDO:
            return document.canUndo();
        case Command::REDO:
            return document.canRedo();
        case Command::DELETE_SELECTION:
        case Command::CUT:
            return this->state.tab == Tab::TIMING ? this->timing->hasSelection()
                                                  : this->state.tab == Tab::COMPOSE && selected;
        case Command::COPY:
            return this->state.tab == Tab::TIMING ? this->timing->hasSelection() : this->state.tab == Tab::COMPOSE;
        case Command::NUDGE:
        case Command::MOVE:
        case Command::CLONE:
        case Command::REVERSE:
        case Command::FLIP:
        case Command::ROTATE:
            return selected;
        case Command::USE_SPACING:
            return this->state.compose.currentSpacing().has_value();
        case Command::ROTATE_BY:
        case Command::SCALE_BY:
        case Command::MOVE_TO:
            return this->state.compose.selectionCenter().has_value();
        case Command::STREAM:
            return this->state.compose.getCurveType().has_value();
        case Command::RESET_SAMPLES:
            return value == 1 ? !this->state.objects->objects.empty() : selected;
        case Command::RESET_COLOURS:
            return !this->state.objects->objects.empty();
        case Command::PASTE:
            return this->state.tab == Tab::TIMING ? TimingPanel::canPastePoints()
                                                  : this->state.tab == Tab::COMPOSE && this->state.compose.canPaste();
        case Command::CURVE_TYPE:
            return this->state.compose.getCurveType().has_value();
        case Command::SELECT_ALL:
            return !this->state.objects->objects.empty();
        default:
            return true;
    }
}

void Editor::run(Command command, i32 value) {
    if(command == Command::TAB) {
        this->setTab((Tab)std::clamp(value, 0, (i32)Tab::HISTORY));
        return;
    }
    if(!this->state.clock.isAttached()) return;
    // what's being typed into a field counts for whatever is run
    this->setup->commit();
    this->timing->commit();
    this->sidebar->commit();
    Clock &clock = this->state.clock;
    Compose &compose = this->state.compose;
    Document &document = *this->state.document;
    const i32 time = clock.getTime();
    const auto &objects = this->state.objects->objects;
    const auto points = document.getTimingPoints();
    const auto bookmarks = document.getBookmarks();
    switch(command) {
        case Command::TOGGLE_PAUSE:
            clock.togglePause();
            break;
        case Command::PLAY_FROM_START:
            clock.seek(0);
            clock.play();
            break;
        case Command::FIRST_OBJECT:
            // from the first object, back to the start, stopped
            if(objects.empty() || objects.front()->getClickTime() == time) {
                clock.pause();
                clock.seek(0);
            } else {
                clock.seek(objects.front()->getClickTime());
            }
            break;
        case Command::LAST_OBJECT:
            if(!objects.empty() && time < objects.back()->getEndTime()) {
                clock.seek(objects.back()->getEndTime());
            } else {
                clock.seek(std::max(0, (i32)clock.getLength() - 500));
            }
            break;
        case Command::STEP:
            this->step(value);
            break;
        case Command::NEXT_POINT:
        case Command::PREVIOUS_POINT: {
            // the point in force a little after the time, so that one the time sits on counts as reached
            const auto inForce =
                Mc::ranges::upper_bound(points, time + 5.0, {}, &neomod::BeatmapFile::TimingPoint::time);
            const sSz active = (inForce - points.begin()) - 1;
            if(command == Command::NEXT_POINT) {
                if(active + 1 < (sSz)points.size()) clock.seek((i32)points[active + 1].time);
            } else if(active >= 0) {
                if(time - points[active].time > 1000.0) {
                    clock.seek((i32)points[active].time);
                } else if(active > 0) {
                    clock.seek((i32)points[active - 1].time);
                }
            }
            break;
        }
        case Command::RATE:
            clock.setRate((f32)(std::lround(clock.getRate() * 100.f) + value) / 100.f);
            break;
        case Command::NEXT_BOOKMARK:
            if(const auto it = Mc::ranges::find_if(bookmarks, [&](i32 b) { return b > time + 50; });
               it != bookmarks.end()) {
                clock.seek(*it);
            }
            break;
        case Command::PREVIOUS_BOOKMARK:
            for(auto it = bookmarks.rbegin(); it != bookmarks.rend(); ++it) {
                if(*it < time - 500) {
                    clock.seek(*it);
                    break;
                }
            }
            break;
        case Command::DIVISOR:
            this->setDivisor(value);
            break;
        case Command::SAVE:
            this->save();
            break;
        case Command::TEST_PLAY:
            // a map without them isn't played
            if(objects.empty()) {
                notify(_("A map without objects can't be tested."));
            } else {
                // what's being placed is tested as it is
                compose.endPlacing();
                this->save([this] { this->startTest(); });
            }
            break;
        case Command::NEW_DIFFICULTY:
            this->newDifficulty(value == 1);
            break;
        case Command::REVERT:
            if(const DatabaseBeatmap *map = db->getBeatmapDifficulty(document.getLocation());
               !map || !this->state.revert(map->getFilePath())) {
                notify(_("The map file couldn't be read."));
            }
            break;
        case Command::UNDO:
        case Command::REDO:
            // a slider being placed loses its last point first, a spinner being placed goes; a drag ends
            if(command == Command::UNDO && compose.isPlacingSlider()) {
                compose.takeBackSliderPoint();
                break;
            }
            if(command == Command::UNDO && compose.isPlacingSpinner()) {
                compose.cancelSpinner();
                break;
            }
            compose.endGesture();
            // what the step changed is selected afterwards
            if(auto touched = command == Command::UNDO ? this->state.undo() : this->state.redo()) {
                compose.selectTouched(std::move(*touched));
            }
            break;
        case Command::ADD_BOOKMARK:
        case Command::REMOVE_BOOKMARK:
            this->bookmark(command == Command::ADD_BOOKMARK);
            break;
        case Command::TOOL:
            compose.setTool((Tool)value);
            break;
        case Command::NEW_COMBO:
            compose.toggleNewCombo();
            break;
        case Command::HITSOUND:
            compose.toggleHitSound((u8)value);
            break;
        case Command::NORMAL_SET:
        case Command::ADDITION_SET:
            compose.setSampleSet(command == Command::ADDITION_SET, value);
            break;
        case Command::DELETE_SELECTION:
            if(this->state.tab == Tab::TIMING) {
                this->timing->removePoints(false);
            } else {
                compose.deleteSelection();
            }
            break;
        case Command::SELECT_ALL:
            compose.selectAll();
            break;
        case Command::NUDGE:
            compose.nudgeInTime(value);
            break;
        case Command::MOVE: {
            constexpr std::array<vec2, 4> DIRECTIONS{vec2{-1.f, 0.f}, vec2{1.f, 0.f}, vec2{0.f, -1.f}, vec2{0.f, 1.f}};
            compose.nudge(DIRECTIONS[std::clamp(value, 0, 3)], this->state.snapSettings());
            break;
        }
        case Command::CANCEL:
            // the other tabs go back to Compose first
            if(this->state.tab != Tab::COMPOSE) {
                this->setTab(Tab::COMPOSE);
            } else {
                compose.cancel();
            }
            break;
        case Command::CUT:
            if(this->state.tab == Tab::TIMING) {
                this->timing->cutPoints();
            } else {
                compose.cut();
            }
            break;
        case Command::COPY:
            if(this->state.tab == Tab::TIMING) {
                this->timing->copyPoints();
            } else {
                compose.copy();
            }
            break;
        case Command::PASTE:
            if(this->state.tab == Tab::TIMING) {
                this->timing->pastePoints();
                break;
            }
            [[fallthrough]];
        case Command::CLONE:
            if((command == Command::PASTE ? compose.paste() : compose.clone()) == Compose::Placed::PAST_THE_END) {
                notify(_("Nothing was added: an object would start after the song ends."));
            }
            break;
        case Command::GRID_SNAP:
            this->gridSnap->setValue(!this->gridSnap->getBool());
            break;
        case Command::DISTANCE_SNAP:
            this->distanceSnap->setValue(!this->distanceSnap->getBool());
            break;
        case Command::USE_SPACING:
            if(const auto spacing = compose.currentSpacing()) this->state.spacing = Snapping::clampSpacing(*spacing);
            break;
        case Command::CURVE_TYPE:
            compose.setCurveType((neomod::SLIDERCURVETYPE)value);
            break;
        case Command::REVERSE:
            compose.reverse();
            break;
        case Command::FLIP:
            compose.flip(value != 0);
            break;
        case Command::ROTATE:
            compose.rotate(value);
            break;
        case Command::ROTATE_BY:
        case Command::SCALE_BY:
        case Command::MOVE_TO:
        case Command::POLYGON:
        case Command::STREAM:
            this->openStepPopup(command);
            break;
        case Command::RESET_SAMPLES:
            compose.resetSamples(value == 1);
            break;
        case Command::RESET_COLOURS:
            compose.resetColourSkips();
            break;
        case Command::GRID_SIZE: {
            // G goes round: after the largest the smallest
            const auto &sizes = Snapping::GRID_SIZES;
            const auto at = Mc::ranges::find(sizes, this->state.gridSize) - sizes.begin();
            this->state.gridSize = value != 0 ? value : sizes[(at + 1) % sizes.size()];
            break;
        }
        case Command::TAB:
            break;  // see above
        case Command::ADD_TIMING_POINT:
            // an inherited point is added for its velocity: its field is typed in next
            if(this->timing->addPoint(value != 0) && value == 0) {
                this->setTab(Tab::TIMING);
                this->timing->visitVelocity();
            }
            break;
        case Command::DELETE_TIMING_POINT:
            this->timing->removePoints(true);
            break;
        case Command::RESNAP:
            this->state.edit(Step::RESNAP,
                             [&](Document::Change &c) { Retiming::resnap(c, document, this->state.divisor); });
            break;
        case Command::INSERT_BREAK:
            if(!compose.insertBreak()) {
                notify(_("A break goes between two objects at least 1.2 seconds apart, where there's none yet."));
            }
            break;
        case Command::RESET_BREAKS:
            compose.resetBreaks();
            break;
        case Command::SLIDER_LENGTHS:
            compose.recalculateSliderLengths();
            break;
        case Command::COPY_LINK: {
            const auto [time, numbers] = compose.timestamp();
            std::string link = fmt::format("https://" NEOMOD_DOMAIN "/online/?" NEOMOD_URL_SCHEME "edit/{}/{}",
                                           document.getMD5().to_chars().string(), TimeDisplay::format(time));
            if(!numbers.empty()) link += fmt::format("/{}", fmt::join(numbers, ","));
            env->setClipBoardText(link);
            notify(document.isChanged() ? _("Link copied. It opens the map as it was last saved.") : _("Link copied."));
            break;
        }
        case Command::OPEN_FOLDER:
            env->openFileBrowser(db->pathOf(document.getLocation()));
            break;
        case Command::OPEN_TEXT:
            env->openInTextEditor(db->pathOf(document.getLocation()));
            break;
        case Command::EXPORT:
            // what's exported is the map as saved: an osu!stable map from its copy once it has one
            this->save([this] {
                const std::string folder =
                    Environment::getFolderFromFilePath(db->pathOf(this->state.document->getLocation()));
                osu->getMapExports()->start(
                    {.beatmap_folder_paths = {folder}, .toplevel_archive_bundle = {}, .cb = {}});
            });
            break;
        case Command::SET_PREVIEW:
            this->state.edit(Step::MAP_SETTINGS, [&](Document::Change &c) {
                c.setValue(Kind::GENERAL, "PreviewTime", fmt::format("{}", time));
            });
            break;
        case Command::METRONOME:
            this->metronome->setValue(!this->metronome->getBool());
            break;
        case Command::CARRY_OBJECTS:
            this->carryObjects->setValue(!this->carryObjects->getBool());
            break;
        case Command::TAP:
            this->timing->tap();
            break;
    }
}

MenuItem Editor::commandItem(std::string name, Command command, i32 value) {
    return {.name = std::move(name),
            .label = Commands::label(command, value),
            .shortcut = Commands::keys(command, value),
            .action = [this, command, value] { this->run(command, value); },
            .enabled = this->canRun(command, value)};
}

std::vector<MenuItem> Editor::fileMenu() {
    const Document *document = this->state.document.get();
    std::vector<MenuItem> items{
        MenuItem{
            .name = "editor_menu_new_beatmap", .label = _("New Beatmap..."), .action = [this] { this->chooseAudio(); }},
        MenuItem{.name = "editor_menu_new_difficulty",
                 .label = _("New Difficulty"),
                 .submenu =
                     [this] {
                         return std::vector<MenuItem>{
                             this->commandItem("editor_menu_new_difficulty_empty", Command::NEW_DIFFICULTY, 1),
                             this->commandItem("editor_menu_new_difficulty_copy", Command::NEW_DIFFICULTY, 0)};
                     },
                 .enabled = this->canRun(Command::NEW_DIFFICULTY)},
        MenuItem{.name = "editor_menu_open_difficulty",
                 .label = _("Open Difficulty"),
                 // the open map set, by stars
                 .submenu =
                     [this] {
                         std::vector<MenuItem> items;
                         const Database::MapLocation &open = this->state.document->getLocation();
                         const DatabaseBeatmap *map = db->getBeatmapDifficulty(open);
                         if(!map || !map->getParentSet()) return items;
                         std::vector<const DatabaseBeatmap *> diffs;
                         for(const auto &diff : map->getParentSet()->getDifficulties()) diffs.push_back(diff.get());
                         Mc::ranges::stable_sort(diffs, {}, &DatabaseBeatmap::getStarsNomod);
                         for(const DatabaseBeatmap *diff : diffs) {
                             const Database::MapLocation location = db->locate(*diff);
                             items.push_back({.name = fmt::format("editor_menu_difficulty_{}", items.size()),
                                              .label = std::string{diff->getDifficultyName()},
                                              .action = [this, location] { this->openDifficulty(location); },
                                              .checked = location == open});
                         }
                         return items;
                     },
                 .enabled = document != nullptr && this->state.clock.isAttached()},
        MenuItem::line(),
        this->commandItem("editor_menu_save", Command::SAVE),
        this->commandItem("editor_menu_revert", Command::REVERT),
        this->commandItem("editor_menu_test", Command::TEST_PLAY),
        MenuItem::line(),
        this->commandItem("editor_menu_open_folder", Command::OPEN_FOLDER),
        this->commandItem("editor_menu_open_text", Command::OPEN_TEXT),
        this->commandItem("editor_menu_export", Command::EXPORT),
        MenuItem::line(),
        MenuItem{.name = "editor_menu_back",
                 .label = _("Back"),
                 .shortcut = Shortcut{KEY_ESCAPE}.text(),
                 .action = [] { ui->getEditorScreen()->onBack(); }},
    };
    // a browser has no file manager to show the folder in
    if constexpr(Env::cfg(OS::WASM)) {
        std::erase_if(items, [](const MenuItem &item) { return item.name == "editor_menu_open_folder"; });
    }
    return items;
}

std::vector<MenuItem> Editor::editMenu() {
    const Document *document = this->state.document.get();
    MenuItem undo = this->commandItem("editor_menu_undo", Command::UNDO);
    if(undo.enabled) undo.label = tformat("Undo {}", stepName(document->getUndoAction()));
    MenuItem redo = this->commandItem("editor_menu_redo", Command::REDO);
    if(redo.enabled) redo.label = tformat("Redo {}", stepName(document->getRedoAction()));
    return {
        std::move(undo),
        std::move(redo),
        MenuItem::line(),
        this->commandItem("editor_menu_cut", Command::CUT),
        this->commandItem("editor_menu_copy", Command::COPY),
        this->commandItem("editor_menu_paste", Command::PASTE),
        this->commandItem("editor_menu_delete", Command::DELETE_SELECTION),
        MenuItem::line(),
        this->commandItem("editor_menu_select_all", Command::SELECT_ALL),
        this->commandItem("editor_menu_clone", Command::CLONE),
        MenuItem::line(),
        this->commandItem("editor_menu_nudge_backward", Command::NUDGE, -1),
        this->commandItem("editor_menu_nudge_forward", Command::NUDGE, 1),
        MenuItem::line(),
        this->commandItem("editor_menu_reset_samples", Command::RESET_SAMPLES, 0),
        this->commandItem("editor_menu_reset_all_samples", Command::RESET_SAMPLES, 1),
        this->commandItem("editor_menu_reset_colours", Command::RESET_COLOURS),
        MenuItem::line(),
        this->commandItem("editor_menu_add_bookmark", Command::ADD_BOOKMARK),
        this->commandItem("editor_menu_remove_bookmark", Command::REMOVE_BOOKMARK),
        MenuItem::line(),
        this->commandItem("editor_menu_copy_link", Command::COPY_LINK),
    };
}

std::vector<MenuItem> Editor::composeMenu() {
    MenuItem gridSnap = this->commandItem("editor_menu_grid_snap", Command::GRID_SNAP);
    gridSnap.checked = this->gridSnap->getBool();
    MenuItem distanceSnap = this->commandItem("editor_menu_distance_snap", Command::DISTANCE_SNAP);
    distanceSnap.checked = this->distanceSnap->getBool();
    return {
        std::move(gridSnap),
        std::move(distanceSnap),
        this->commandItem("editor_menu_use_spacing", Command::USE_SPACING),
        MenuItem{.name = "editor_menu_grid_size",
                 .label = _("Grid Size"),
                 .submenu =
                     [this] {
                         std::vector<MenuItem> items;
                         for(const i32 size : Snapping::GRID_SIZES) {
                             MenuItem item = this->commandItem(fmt::format("editor_menu_grid_size_{}", size),
                                                               Command::GRID_SIZE, size);
                             item.label = fmt::format("{}", size);
                             item.checked = this->state.gridSize == size;
                             items.push_back(std::move(item));
                         }
                         return items;
                     }},
        MenuItem::line(),
        MenuItem{.name = "editor_menu_curve_type",
                 .label = _("Curve Type"),
                 // from the selected slider
                 .submenu =
                     [this] {
                         using enum neomod::SLIDERCURVETYPE;
                         const Compose &compose = this->state.compose;
                         std::vector<MenuItem> items;
                         for(const auto &[type, name] : {std::pair{LINEAR, "editor_menu_curve_linear"},
                                                         std::pair{PASSTHROUGH, "editor_menu_curve_perfect"},
                                                         std::pair{BEZIER, "editor_menu_curve_bezier"},
                                                         std::pair{CATMULL, "editor_menu_curve_catmull"}}) {
                             MenuItem item = this->commandItem(name, Command::CURVE_TYPE, (i32)type);
                             item.checked = compose.getCurveType() == type;
                             item.enabled = item.enabled && compose.takesCurveType(type);
                             items.push_back(std::move(item));
                         }
                         return items;
                     },
                 .enabled = this->canRun(Command::CURVE_TYPE)},
        this->commandItem("editor_menu_reverse", Command::REVERSE),
        this->commandItem("editor_menu_flip_horizontally", Command::FLIP, 0),
        this->commandItem("editor_menu_flip_vertically", Command::FLIP, 1),
        this->commandItem("editor_menu_rotate_clockwise", Command::ROTATE, 1),
        this->commandItem("editor_menu_rotate_anticlockwise", Command::ROTATE, -1),
        this->commandItem("editor_menu_rotate_by", Command::ROTATE_BY),
        this->commandItem("editor_menu_scale_by", Command::SCALE_BY),
        this->commandItem("editor_menu_move_to", Command::MOVE_TO),
        MenuItem::line(),
        this->commandItem("editor_menu_polygon", Command::POLYGON),
        this->commandItem("editor_menu_stream", Command::STREAM),
    };
}

std::vector<MenuItem> Editor::viewMenu() {
    MenuItem compose = this->commandItem("editor_menu_tab_compose", Command::TAB, (i32)Tab::COMPOSE);
    compose.checked = this->state.tab == Tab::COMPOSE;
    MenuItem timing = this->commandItem("editor_menu_tab_timing", Command::TAB, (i32)Tab::TIMING);
    timing.checked = this->state.tab == Tab::TIMING;
    MenuItem setup = this->commandItem("editor_menu_tab_setup", Command::TAB, (i32)Tab::SETUP);
    setup.checked = this->state.tab == Tab::SETUP;
    MenuItem verify = this->commandItem("editor_menu_tab_verify", Command::TAB, (i32)Tab::VERIFY);
    verify.checked = this->state.tab == Tab::VERIFY;
    MenuItem history = this->commandItem("editor_menu_tab_history", Command::TAB, (i32)Tab::HISTORY);
    history.checked = this->state.tab == Tab::HISTORY;
    return {
        std::move(compose),
        std::move(timing),
        std::move(setup),
        std::move(verify),
        std::move(history),
        MenuItem::line(),
        MenuItem{.name = "editor_menu_hit_animations",
                 .label = _("Hit Animations"),
                 .action = [this] { this->hitAnimations->setValue(!this->hitAnimations->getBool()); },
                 .checked = this->hitAnimations->getBool()},
        MenuItem{.name = "editor_menu_map_skin",
                 .label = _("Map Skin"),
                 .action = [this] { this->mapSkin->setValue(!this->mapSkin->getBool()); },
                 .checked = this->mapSkin->getBool()},
        MenuItem{.name = "editor_menu_map_hitsounds",
                 .label = _("Map Hitsounds"),
                 .action = [this] { this->mapHitsounds->setValue(!this->mapHitsounds->getBool()); },
                 .checked = this->mapHitsounds->getBool()},
        MenuItem{.name = "editor_menu_background_dim",
                 .label = _("Background Dim"),
                 .submenu =
                     [this] {
                         std::vector<MenuItem> items;
                         for(const i32 percent : {0, 25, 50, 75, 100}) {
                             items.push_back(
                                 {.name = fmt::format("editor_menu_background_dim_{}", percent),
                                  .label = fmt::format("{}%", percent),
                                  .action = [this, percent] { this->backgroundDim->setValue(percent / 100.f); },
                                  .checked = std::lround(this->backgroundDim->getFloat() * 100.f) == percent});
                         }
                         return items;
                     }},
        MenuItem{.name = "editor_menu_hitsound_labels",
                 .label = _("Hitsound Labels"),
                 .action = [this] { this->soundLabels->setValue(!this->soundLabels->getBool()); },
                 .checked = this->soundLabels->getBool()},
        MenuItem{.name = "editor_menu_grid",
                 .label = _("Grid"),
                 .action = [this] { this->grid->setValue(!this->grid->getBool()); },
                 .checked = this->grid->getBool()},
    };
}

std::vector<MenuItem> Editor::helpMenu() {
    return {MenuItem{.name = "editor_menu_shortcuts", .label = _("Keyboard Shortcuts..."), .action = [this] {
                         this->popups->closeAll();
                         auto *list = new ShortcutList;
                         this->popups->open(list, this->getPos() + (this->getSize() - list->getNaturalSize()) / 2.f);
                     }}};
}

std::vector<MenuItem> Editor::timingMenu() {
    MenuItem metronome = this->commandItem("editor_menu_metronome", Command::METRONOME);
    metronome.checked = this->metronome->getBool();
    MenuItem carry = this->commandItem("editor_menu_carry_objects", Command::CARRY_OBJECTS);
    carry.checked = this->carryObjects->getBool();
    return {
        this->commandItem("editor_menu_add_timing_point", Command::ADD_TIMING_POINT, 1),
        this->commandItem("editor_menu_add_inherited_point", Command::ADD_TIMING_POINT, 0),
        this->commandItem("editor_menu_delete_timing_point", Command::DELETE_TIMING_POINT),
        MenuItem::line(),
        this->commandItem("editor_menu_resnap", Command::RESNAP),
        this->commandItem("editor_menu_slider_lengths", Command::SLIDER_LENGTHS),
        this->commandItem("editor_menu_set_preview", Command::SET_PREVIEW),
        this->commandItem("editor_menu_insert_break", Command::INSERT_BREAK),
        this->commandItem("editor_menu_reset_breaks", Command::RESET_BREAKS),
        MenuItem::line(),
        std::move(metronome),
        std::move(carry),
        MenuItem::line(),
        // the index in force at the editor time, or the first one where none is
        MenuItem{.name = "editor_menu_sample_sets",
                 .label = _("Custom Samples..."),
                 .action =
                     [this] {
                         const auto &timing = this->state.document->getPrimitives().timingpoints;
                         this->openSampleSets(std::max(
                             neomod::HitSoundUtils::samplesAt(timing, this->state.clock.getTime()).sampleIndex, 1));
                     },
                 .enabled = this->state.clock.isAttached()},
    };
}

void Editor::leaveFields() {
    this->setup->stealFocus();
    this->timing->stealFocus();
    this->sidebar->stealFocus();
}

void Editor::setTab(Tab tab) {
    if(tab == this->state.tab) return;
    this->state.compose.endPlacing();
    this->popups->closeAll();
    this->leaveFields();

    this->state.tab = tab;
    this->showPanels();
    if(tab == Tab::TIMING && this->state.clock.isAttached()) this->timing->selectInForce();
    ui->getEditorScreen()->sessionChanged();
}

void Editor::step(i32 ticks) {
    const BeatSnap &snap = this->state.document->getBeatSnap();
    Clock &clock = this->state.clock;
    clock.seek(snap.step(clock.getTime(), ticks, this->state.divisor, clock.isPlaying()));
}

void Editor::openSampleSets(i32 index) {
    if(!this->state.clock.isAttached()) return;
    this->popups->closeAll();
    this->sampleSets = new SampleSets(this->state, *this->popups, index, [this] { this->loadMapSkin(); });
    this->sampleSetsId =
        this->sampleSets->open(this->getPos() + (this->getSize() - this->sampleSets->getNaturalSize()) / 2.f);
}

void Editor::openStepPopup(Command command) {
    Compose &compose = this->state.compose;
    constexpr vec2 FIELD{GameRules::OSU_COORD_WIDTH, GameRules::OSU_COORD_HEIGHT};
    const auto selectionCenter = compose.selectionCenter();
    const bool transform = command == Command::ROTATE_BY || command == Command::SCALE_BY || command == Command::MOVE_TO;
    if(transform && !selectionCenter) return;
    const std::array centers{FIELD / 2.f, selectionCenter.value_or(FIELD / 2.f)};
    const std::vector<std::string> centerNames{_("Playfield Centre"), _("Selection Centre")};
    StepPopup *popup = nullptr;
    Step action{};
    switch(command) {
        case Command::ROTATE_BY:
            action = Step::ROTATE;
            popup = new StepPopup(this->state, *this->popups, "editor_rotate_by", _("Rotate By"),
                                  [this, centers](const StepPopup &p) {
                                      this->rotateCenter = p.choice(0);
                                      const vec2 c = centers[p.choice(0)];
                                      // clockwise: y points down
                                      const f64 radians = p.number(0) * std::numbers::pi / 180.0;
                                      const auto cos = (f32)std::cos(radians), sin = (f32)std::sin(radians);
                                      return this->state.compose.previewMap(
                                          [&](vec2 v) {
                                              const vec2 d = v - c;
                                              return vec::round(c + vec2{d.x * cos - d.y * sin, d.x * sin + d.y * cos});
                                          },
                                          false);
                                  });
            popup->addNumber("editor_rotate_angle", _("Angle"), 0.0, -180.0, 180.0, 1.0, 0);
            popup->addChoice("editor_rotate_center", _("Around"), centerNames, this->rotateCenter);
            break;
        case Command::SCALE_BY: {
            action = Step::SCALE;
            // from the objects as they are now, which the preview changes: for each center, X, Y and both
            std::array<std::array<f32, 3>, 2> limits{};
            for(uSz i = 0; i < centers.size(); i++) {
                limits[i] = {compose.maxScale(centers[i], true, false), compose.maxScale(centers[i], false, true),
                             compose.maxScale(centers[i], true, true)};
            }
            popup = new StepPopup(this->state, *this->popups, "editor_scale_by", _("Scale By"),
                                  [this, centers, limits](const StepPopup &p) {
                                      this->scaleCenter = p.choice(0);
                                      const vec2 c = centers[p.choice(0)];
                                      const bool x = p.toggle(0), y = p.toggle(1);
                                      if(!x && !y)
                                          return this->state.compose.previewMap([](vec2 v) { return v; }, false);
                                      const f32 limit = limits[p.choice(0)][x && y ? 2 : y ? 1 : 0];
                                      p.numberField(0)->setRange(0.5, limit);
                                      const auto f = (f32)std::min(p.number(0), (f64)limit);
                                      const vec2 factor{x ? f : 1.f, y ? f : 1.f};
                                      return this->state.compose.previewMap(
                                          [&](vec2 v) { return vec::round(c + (v - c) * factor); }, true);
                                  });
            popup->addNumber("editor_scale_factor", _("Factor"), 1.0, 0.5, limits[this->scaleCenter][2], 0.05, 2);
            popup->addChoice("editor_scale_center", _("Around"), centerNames, this->scaleCenter);
            popup->addToggle("editor_scale_x", _("X Axis"), true);
            popup->addToggle("editor_scale_y", _("Y Axis"), true);
            break;
        }
        case Command::MOVE_TO: {
            action = Step::MOVE;
            // one object to a position, more by an offset
            const std::vector<Document::Id> &selection = compose.getSelection();
            const auto record =
                selection.size() == 1 ? this->state.document->getObject(selection.front()) : std::nullopt;
            const std::optional<vec2> head = record ? std::optional{vec2{record->x, record->y}} : std::nullopt;
            popup = new StepPopup(this->state, *this->popups, "editor_move_to", head ? _("Move To") : _("Move By"),
                                  [this, head](const StepPopup &p) {
                                      const vec2 to{(f32)p.number(0), (f32)p.number(1)};
                                      const vec2 by = head ? to - *head : to;
                                      return this->state.compose.previewMap([by](vec2 v) { return v + by; }, false);
                                  });
            const vec2 at = head ? vec::round(*head) : vec2{0.f};
            const vec2 low = head ? vec2{0.f} : -FIELD;
            popup->addNumber("editor_move_x", "X", at.x, low.x, FIELD.x, 1.0, 0);
            popup->addNumber("editor_move_y", "Y", at.y, low.y, FIELD.y, 1.0, 0);
            break;
        }
        case Command::POLYGON:
            action = Step::POLYGON;
            popup = new StepPopup(
                this->state, *this->popups, "editor_polygon", _("Polygon Circles"), [this](const StepPopup &p) {
                    return this->state.compose.previewPlace(
                        this->state.compose.polygon((i32)p.number(0), (i32)p.number(1), p.number(2), p.number(3)));
                });
            popup->addNumber("editor_polygon_corners", _("Corners"), 3.0, 3.0, 32.0, 1.0, 0);
            popup->addNumber("editor_polygon_repeats", _("Repeats"), 1.0, 1.0, 10.0, 1.0, 0);
            popup->addNumber("editor_polygon_angle", _("Offset Angle"), 0.0, 0.0, 180.0, 1.0, 0);
            // the spacing is kept as an f32 in hundredths, which would show as 1.600000023841858
            popup->addNumber("editor_polygon_spacing", _("Spacing"),
                             std::round((f64)this->state.spacing * 100.0) / 100.0, 0.1, 6.0, 0.1, 1);
            break;
        case Command::STREAM: {
            action = Step::STREAM;
            const auto slider = compose.getSelectedSlider();
            if(!slider) return;
            static constexpr std::array DIVISORS{1, 2, 3, 4, 6, 8};
            popup = new StepPopup(this->state, *this->popups, "editor_stream", _("Convert to Stream"),
                                  [this, slider = *slider](const StepPopup &p) {
                                      const bool byCount = p.choice(1) == 1;
                                      return this->state.compose.previewPlace(
                                          this->state.compose.stream(slider.second, DIVISORS[p.choice(0)], p.number(0),
                                                                     byCount ? (i32)p.number(1) : 0),
                                          slider.first);
                                  });
            popup->addNumber("editor_stream_spacing", _("Spacing"), 1.0, 0.1, 9.0, 0.1, 2);
            popup->addNumber("editor_stream_count", _("Count"), 5.0, 1.0, 100.0, 1.0, 0);
            popup->addChoice("editor_stream_divisor", _("Divisor"), {"1/1", "1/2", "1/3", "1/4", "1/6", "1/8"},
                             this->state.divisor == 3 || this->state.divisor == 6 ? 2 : 3);
            popup->addChoice("editor_stream_by", _("Spaced"), {_("By Spacing"), _("By Count")}, 0);
            break;
        }
        default:
            return;
    }
    const f32 padding = uiStyle().px(uiStyle().metrics.padding);
    popup->open(action, this->playfield->getPos() + vec2{padding});
}

void Editor::jumpTo(i32 timeMS, std::span<const i32> combos) {
    Clock &clock = this->state.clock;
    if(!clock.isAttached()) return;
    clock.pause();
    if(combos.empty()) {
        clock.seek(timeMS);
        return;
    }
    this->setTab(Tab::COMPOSE);
    clock.seek(this->state.compose.selectCombos(timeMS, combos));
}

void Editor::setDivisor(i32 divisor) { this->state.divisor = BeatSnap::clampDivisor(divisor); }

void Editor::save(const std::function<void()> &then) {
    Document &document = *this->state.document;
    // what the song browser would drop (and with it the map offsets and collections)
    if(document.isChanged() && document.getTimingPoints().empty()) {
        notify(_("A map without timing points can't be saved."));
        return;
    }
    if(!document.isChanged()) {
        // the file has this content already
        if(then) {
            then();
        } else {
            notify(_("Saved"));
        }
        return;
    }

    // the editor settings go into every save (no change to the map: undo and redo stay as they are)
    const std::vector<u8> bytes = document.getBytesWith({.zoom = this->state.zoom,
                                                         .divisor = this->state.divisor,
                                                         .spacing = this->state.spacing,
                                                         .gridSize = this->state.gridSize});
    if(const DatabaseBeatmap *same = db->getBeatmapDifficulty(crypto::hash::md5(bytes));
       same && db->locate(*same) != document.getLocation()) {
        notify(tformat("This map is the same as {} now, so it can't be saved.", describe(*same)));
        return;
    }
    // osu!stable maps are saved into the copy of their set in the neomod library
    if(this->state.copyFirst([this, then] { this->save(then); })) return;
    if(!db->getBeatmapDifficulty(document.getLocation())) {
        notify(_("This map isn't in the song browser anymore, so it can't be saved."));
        return;
    }

    const std::string fileBefore = document.getLocation().file;
    // the file name follows the metadata once that changed (a name of its own stays until then): renamed first, its
    // record with it, so that the write below is new content for the same map (its offsets and collections stay)
    if(const std::string name = MapFiles::nameFor(document.getFile());
       name != document.getLocation().file &&
       name != MapFiles::nameFor(neomod::BeatmapFile{document.getSavedBytes()})) {
        Database::MapLocation renamed = document.getLocation();
        renamed.file = name;
        const std::string from = db->pathOf(document.getLocation()), to = db->pathOf(renamed);
        std::error_code ec;
        // on a filesystem that ignores case, a name that differs only in case is the same file
        if(Environment::fileExists(to) &&
           !std::filesystem::equivalent(File::getFsPath(from), File::getFsPath(to), ec)) {
            notify(tformat("The file keeps its name, another file in its folder is called {}.", name));
        } else if(!Environment::renameFile(from, to)) {
            notify(tformat("The file keeps its name, it couldn't be renamed to {}.", name));
        } else if(osu->getBeatmapInstaller()->register_folder(renamed.folder)) {
            document.moveTo(renamed);
            this->session.moved(renamed);
        } else {
            Environment::renameFile(to, from);
        }
    }

    const std::string path = db->pathOf(document.getLocation());
    // what another program wrote since the document read the file is overwritten too: the history keeps it
    std::vector<u8> outside;
    if(const auto onDisk = crypto::hash::md5_file(path); onDisk && *onDisk != document.getMD5()) {
        File(path).readToVector(outside);
    }
    // a writer holds a lock that other files share until it's gone
    {
        ByteBufferedFile::Writer out(path);
        out.write_bytes(bytes.data(), bytes.size());
        if(!out.commit()) {
            ui->getNotificationOverlay()->addToast(tformat("The map couldn't be saved: {}", out.error()), ERROR_TOAST);
            return;
        }
    }
    // the content the save overwrote is the saved document content until here, unless another program wrote the file
    EditorHistory::record(
        fileBefore, outside.empty() ? document.getMD5() : crypto::hash::md5(outside),
        outside.empty() ? document.getSavedBytes() : std::span<const u8>{outside}, document.getLocation(), bytes,
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    document.saved(bytes);
    // the song browser reads the folder again, giving the new content the map offsets and collections
    osu->getBeatmapInstaller()->enqueue_folder(document.getLocation().folder, true);
    notify(outside.empty() ? _("Saved")
                           : _("Saved. Another program had changed the file: the History tab has its version."));
    ui->getEditorScreen()->workChanged();
    if(then) then();
}

void Editor::startTest() {
    const Document &document = *this->state.document;
    const Database::MapLocation &location = document.getLocation();
    // the record for what's saved, which a save pass may still be making
    DatabaseBeatmap *map = db->getBeatmapDifficulty(location);
    if(map && map->getMD5() != document.getMD5() && location.root == Database::MapRoot::Neomod &&
       osu->getBeatmapInstaller()->register_folder(location.folder, /*reread=*/true)) {
        map = db->getBeatmapDifficulty(location);
    }
    if(!map || map->getMD5() != document.getMD5()) {
        notify(
            _("The map can't be tested yet: the song browser is still reading the saved file. Try again in a moment."));
        return;
    }
    BeatmapInterface *game = osu->getMapInterface();
    game->selectBeatmap(map);
    this->testing = game->test(this->state.clock.getTime());
}

void Editor::drop(std::span<const std::string> paths) {
    if(const auto audio = Mc::ranges::find_if(paths, [](const std::string &path) { return isAudio(path); });
       audio != paths.end()) {
        // dropped while a sample is being chosen, it's that
        if(this->popups->isOpen(this->sampleSetsId)) {
            this->sampleSets->setFile(*audio);
        } else if(!this->sidebar->addSample(*audio)) {
            // on a shown map it may have been meant as a sample
            if(this->state.clock.isAttached()) {
                notify(
                    _("To start a beatmap from an audio file, use File > New Beatmap. To use it as a sample, "
                      "drop it while a sample is being chosen."));
            } else {
                this->newBeatmap(*audio);
            }
        }
    } else if(const auto image = Mc::ranges::find_if(paths, [](const std::string &path) { return isImage(path); });
              image != paths.end() && this->state.clock.isAttached()) {
        this->setup->useBackground(*image);
    }
}

void Editor::chooseAudio() {
    // the filter name, then its extensions
    const std::string filter = std::string{_("Audio files")} + '\0' + "mp3;ogg;wav" + '\0';
    this->fileDialog = env->openFileWindow(
        [this](const std::vector<std::string> &paths) {
            if(!paths.empty() && isAudio(paths.front())) this->newBeatmap(paths.front());
        },
        filter.c_str(), _("New Beatmap"));
}

void Editor::newBeatmap(const std::string &audio) {
    const std::string name = Environment::getFileNameFromFilePath(audio);
    const std::string folder = BeatmapInstaller::new_folder_name(name.substr(0, name.rfind('.')));
    // a name with a line break in it can't be written into the map
    if(folder.empty() || Mc::ranges::any_of(name, [](char c) { return (unsigned char)c < 32; })) {
        notify(tformat("{} can't be used: its name can't be written into a map.", name));
        return;
    }
    const std::string text = MapFiles::newSet(name, BanchoState::get_username());
    const Database::MapLocation location{
        .root = Database::MapRoot::Neomod, .folder = folder, .file = MapFiles::nameFor(neomod::BeatmapFile{text})};

    // the audio first: a .osu in the folder makes it a set
    const std::string dir = fmt::format("{}/{}", Mc::Paths::maps(), folder);
    bool made = false;
    {
        ByteBufferedFile::Writer out(db->pathOf(location));
        out.write_bytes(reinterpret_cast<const u8 *>(text.data()), text.size());
        made = File::copy(audio, fmt::format("{}/{}", dir, name)) && out.commit();
    }
    // the writer closed its file first, which windows doesn't delete while it's open
    if(!made) {
        ui->getNotificationOverlay()->addToast(tformat("The beatmap couldn't be made from {}.", name), ERROR_TOAST);
        Environment::deletePathsRecursive(dir);
        return;
    }
    osu->getBeatmapInstaller()->register_folder(folder);
    if(this->open(location)) this->setTab(Tab::TIMING);
}

void Editor::newDifficulty(bool empty) {
    if(this->state.copyFirst([this, empty] { this->newDifficulty(empty); })) return;
    const Document &document = *this->state.document;
    const DatabaseBeatmap *map = db->getBeatmapDifficulty(document.getLocation());
    if(!map || !map->getParentSet()) return;

    // a name for the map file, so not translated
    const std::string base =
        empty ? std::string{"New Difficulty"}
              : fmt::format("{} (copy)", document.getFile().getValue(Kind::METADATA, "Version").value_or(""));
    const auto &siblings = map->getParentSet()->getDifficulties();
    // the first in base, "base (2)", ... that no difficulty in the set is called and whose file name is free (a name
    // cut to fit a file name might never be)
    for(i32 n = 1; n <= 100; n++) {
        const std::string version = n == 1 ? base : fmt::format("{} ({})", base, n);
        if(Mc::ranges::any_of(siblings, [&](const auto &diff) {
               return SString::strcase_equal(diff->getDifficultyName(), version);
           })) {
            continue;
        }
        const std::vector<u8> text = MapFiles::newDifficulty(document.getBytes(), version, empty);
        if(text.empty()) {
            notify(_("The difficulty couldn't be made: the game couldn't read it."));
            return;
        }
        Database::MapLocation location = document.getLocation();
        location.file = MapFiles::nameFor(neomod::BeatmapFile{text});
        if(Environment::fileExists(db->pathOf(location))) continue;

        {
            ByteBufferedFile::Writer out(db->pathOf(location));
            out.write_bytes(text.data(), text.size());
            if(!out.commit()) {
                ui->getNotificationOverlay()->addToast(tformat("The difficulty couldn't be made: {}", out.error()),
                                                       ERROR_TOAST);
                return;
            }
        }
        // in the song browser now, which the editor shows maps from
        osu->getBeatmapInstaller()->register_folder(location.folder);
        if(this->open(location)) {
            this->setTab(Tab::SETUP);
            this->setup->editVersion();
        }
        return;
    }
    notify(_("The difficulty couldn't be made: its file name is taken."));
}

void Editor::bookmark(bool add) {
    Document &document = *this->state.document;
    std::vector<i32> bookmarks{document.getBookmarks().begin(), document.getBookmarks().end()};
    const i32 time = this->state.clock.getTime();
    if(add) {
        const i32 at = document.getBeatSnap().snap(time, this->state.divisor, time);
        if(Mc::ranges::contains(bookmarks, at)) return;
        bookmarks.insert(Mc::ranges::upper_bound(bookmarks, at), at);
    } else {
        // the earlier one of two as near
        auto nearest = bookmarks.end();
        for(auto it = bookmarks.begin(); it != bookmarks.end(); ++it) {
            if(std::abs(*it - time) < 2000 &&
               (nearest == bookmarks.end() || std::abs(*it - time) < std::abs(*nearest - time))) {
                nearest = it;
            }
        }
        if(nearest == bookmarks.end()) return;
        bookmarks.erase(nearest);
    }
    this->state.edit(add ? Step::ADD_BOOKMARK : Step::REMOVE_BOOKMARK, [&](Document::Change &c) {
        c.setValue(Kind::EDITOR, "Bookmarks",
                   bookmarks.empty() ? std::nullopt
                                     : std::optional<std::string_view>{fmt::format("{}", fmt::join(bookmarks, ","))});
        // the older key, which is read too
        c.setValue(Kind::GENERAL, "EditorBookmarks", std::nullopt);
    });
}
