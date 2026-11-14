#pragma once
// Copyright (c) 2026, WH, All rights reserved.
#include "config.h"

#include "AsyncScope.h"
#include "Database.h"
#include "UIScreen.h"
#include "noinclude.h"
#include "types.h"

#ifdef MCENGINE_FEATURE_HOT_RELOAD
#include "Registration.h"
#include "ReloadableLibrary.h"
#endif

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct EditorModule;

// hosts the editor (see EditorModule.h): owns its root element and keeps its session, across reloads and on disk
class EditorScreen final : public UIScreen {
    NOCOPY_NOMOVE(EditorScreen)
   public:
    EditorScreen();
    ~EditorScreen() override;

    CBaseUIContainer *setVisible(bool visible) override;
    void draw() override;
    void tick() override;
    void onKeyDown(KeyboardEvent &e) override;
    void onResolutionChange(vec2 newResolution) override;

    // shows the editor (with `map` open), which goes back to the screen shown before when it's left. `timestamp` is
    // where it goes in the map then (see EditorModule::jump)
    void enter(std::optional<Database::MapLocation> map = std::nullopt, std::string timestamp = {});
    // files dropped on the window while it's shown, which the game has no use for
    void onDrop(std::span<const std::string> paths);
    // the game window has the focus again
    void onFocusGained();
    // rebuilds the editor at the start of the next tick, continuing its session unless `fresh`
    void reload(bool fresh = false);
    void onBack();
    // for editor code: its session changed, so it gets saved (EditorModule::save) and written within a second
    void sessionChanged();
    // for editor code: the user work changed (unsaved changes, a save), so the session changed and leaving the page
    // asks first until a session that has it is written and stored (see Environment::guardLeave())
    void workChanged();
    // for editor code: its session holds what's nowhere else on disk and is about to be shown, so it gets saved and
    // written now, and guarded like a restore (set aside at the next start if showing it crashes the game)
    void writeSessionGuarded();

   private:
    void onReloadCommand(std::string_view args);
    void onTestCommand(std::string_view filter);
    // the editor takes the map and the timestamp enter() was given, before it's shown with them
    void openPendingMap();
    // how long it took since `startTime` (also logged), nothing if it failed
    std::optional<f64> createEditor(f64 startTime);
    void destroyEditor();
    // keeps what the editor saves for its next start and writes it to disk (`now`: before returning)
    void saveSession(bool now = false);
    // the session the first start continues: the one on disk, unless restoring it crashed the game last time
    void readSession();
    // until the editor has made it through a frame (see tick()), a crash leaves a marker that sets the session on disk
    // aside at the next start
    void beginRestore();
    // the restore didn't crash the game: its marker goes
    void endRestore();

#ifdef MCENGINE_FEATURE_HOT_RELOAD
    Mc::ReloadableLibrary library{"neomod-editor"};
    // reloads it whenever a new build is installed
    Mc::Registration libraryWatch;
#endif
    const EditorModule *module{nullptr};
    CBaseUIElement *root{nullptr};  // our only child, but destroyed through the module
    std::optional<Database::MapLocation> pendingMap;
    std::string pendingTimestamp;
    UIScreen *returnTo{nullptr};  // where Back goes (the main menu without one)
    std::vector<u8> session;
    Async::Scope sessionWrites;
    f64 sessionWriteTime{0.0};  // when the next write is due after a sessionChanged() (0: none)
    // held from a workChanged() until a session write that has it is done
    Mc::Registration leaveGuard;
    u64 workChanges{0};               // workChanged() calls so far, to tell whether a write has the last one
    std::optional<u64> restoreFrame;  // the frame a restore started in, while its marker file exists
    u32 generation{0};

    // why the last start failed, shown while there's no root
    enum class Failure : u8 { CREATE_FAILED, LOAD_FAILED, STALE_LIBRARY } failure{Failure::CREATE_FAILED};
    [[nodiscard]] const char *failureReason() const;

    bool reloadScheduled{false};
    bool dropSession{false};
};
