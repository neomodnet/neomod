// Copyright (c) 2026, WH, All rights reserved.
#include "EditorScreen.h"

#include "ByteBufferedFile.h"
#include "ConVar.h"
#include "EditorModule.h"
#include "Engine.h"
#include "Environment.h"
#include "File.h"
#include "Font.h"
#include "Graphics.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "KeyboardEvent.h"
#include "Logging.h"
#include "MakeDelegateWrapper.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "OsuKeyBinds.h"
#include "Paths.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "Timing.h"
#include "UI.h"
#include "fmt/format.h"
#include "i18n.h"

#ifdef MCENGINE_FEATURE_HOT_RELOAD
#include "DirectoryWatcher.h"
#endif

#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace cv {
// callbacks set in the EditorScreen ctor
static ConVar editor_reload("editor_reload", CLIENT | NOLOAD | NOSAVE);
static ConVar editor_test("editor_test", CLIENT | NOLOAD | NOSAVE);
}  // namespace cv

namespace {
// how long a change to the session may wait to be written (at most what a crash loses)
constexpr f64 SESSION_WRITE_DELAY{1.0};

// <editor>/session<suffix>: the session itself, ".restoring" for the marker that exists while a restore runs (see
// beginRestore()), ".bad" for a session whose restore crashed the game
std::string sessionFile(std::string_view suffix = {}) {
    return fmt::format("{}/session{}", Mc::Paths::editor(), suffix);
}

// replaced in one step (.tmp + rename)
void writeSessionFile(std::span<const u8> session) {
    ByteBufferedFile::Writer out(sessionFile());
    out.write_bytes(session.data(), session.size());
    if(!out.good()) debugLog("couldn't write the editor's session: {}", out.error());
}
}  // namespace

EditorScreen::EditorScreen() {
    this->setSize(osu->getVirtScreenSize());
    cv::editor_reload.setCallback(SA::MakeDelegate<&EditorScreen::onReloadCommand>(this));
    cv::editor_test.setCallback(SA::MakeDelegate<&EditorScreen::onTestCommand>(this));
#ifdef MCENGINE_FEATURE_HOT_RELOAD
    // the install renames a finished build into place
    this->libraryWatch = directoryWatcher->watch_directory(Mc::Paths::libs(), [this](const FileChangeEvent &ev) {
        if(ev.type != FileChangeType::DELETED && ev.path == this->library.getPath()) this->reload();
    });
#endif
}

EditorScreen::~EditorScreen() {
    cv::editor_reload.removeAllCallbacks();
    cv::editor_test.removeAllCallbacks();
    if(this->root) {
        this->session = this->module->save(this->root);
        this->destroyEditor();
    }
    // the last write takes the place of any that hasn't started (one that has is waited for); before an editor has
    // started, the file is still what the last run left
    this->sessionWrites.cancel();
    if(this->generation > 0) writeSessionFile(this->session);
    this->endRestore();
}

CBaseUIContainer *EditorScreen::setVisible(bool visible) {
    // the editor maps come from the database, which its first visit loads (as the song browser does)
    if(visible && !db->isFinished()) {
        ui->getSongBrowser()->refreshBeatmaps(this);
        return this;
    }

    const bool left = this->isVisible() && !visible;
    UIScreen::setVisible(visible);
    if(this->root) {
        if(visible) this->openPendingMap();
        this->root->setVisible(visible);
        if(left) this->saveSession();
    } else if(visible) {
        this->createEditor(Timing::getTimeReal());
    }
    return this;
}

void EditorScreen::enter(std::optional<Database::MapLocation> map, std::string timestamp) {
    if(UIScreen *active = ui->getActiveScreen(); active != this) this->returnTo = active;
    if(map) this->pendingMap = std::move(map);
    this->pendingTimestamp = std::move(timestamp);
    if(this->isVisible()) this->openPendingMap();
    ui->setScreen(this);
}

void EditorScreen::onDrop(std::span<const std::string> paths) {
    if(this->root) this->module->drop(this->root, paths);
}

void EditorScreen::onFocusGained() {
    if(this->root && this->isVisible()) this->module->focus(this->root);
}

const char *EditorScreen::failureReason() const {
    switch(this->failure) {
        case Failure::CREATE_FAILED:
            return _("The editor failed to start.");
        case Failure::LOAD_FAILED:
            return _("The editor could not be loaded, the log has the details.");
        case Failure::STALE_LIBRARY:
            return _("The editor was built for a newer build of the game, restart the game to load it.");
    }
    std::unreachable();
}

void EditorScreen::draw() {
    if(!this->isVisible()) return;
    if(this->root) {
        UIScreen::draw();
        return;
    }

    // instead of the editor: why it isn't there, and the ways out
    const char *reason = this->failureReason();
    const char *hint = _("Ctrl+Alt+E tries again, Escape goes back.");

    const vec2 center = this->getPos() + this->getSize() / 2.f;
    McFont *reasonFont = osu->getSubTitleFont();
    McFont *hintFont = engine->getDefaultFont();
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(std::round(center.x - reasonFont->getStringWidth(reason) / 2.f), std::round(center.y));
        g->drawString(reasonFont, reason);
    }
    g->popTransform();
    g->setColor(0xffaaaaaa);
    g->pushTransform();
    {
        g->translate(std::round(center.x - hintFont->getStringWidth(hint) / 2.f),
                     std::round(center.y + reasonFont->getHeight() * 1.5f));
        g->drawString(hintFont, hint);
    }
    g->popTransform();
}

void EditorScreen::tick() {
    // the restored editor made it through a frame
    if(this->restoreFrame && engine->getFrameCount() > *this->restoreFrame) this->endRestore();

    // swapped here, where no editor code is on the stack
    if(this->reloadScheduled) {
        this->reloadScheduled = false;
        const f64 startTime = Timing::getTimeReal();
        const bool hadEditor = this->root != nullptr;
        if(this->dropSession) {
            if(hadEditor) this->destroyEditor();
            // on disk too, along with any write that hasn't started
            this->session.clear();
            this->sessionWrites.cancel();
            this->sessionWriteTime = 0.0;
            this->leaveGuard.reset();
            Environment::deleteFile(sessionFile());
        } else if(hadEditor) {
            this->saveSession();
            this->destroyEditor();
        }
        this->dropSession = false;
        if(hadEditor || this->isVisible()) {
            const auto ms = this->createEditor(startTime);
            // none for an editor that's out of sight, e.g. one rebuilt during gameplay
            if(this->isVisible()) {
                if(ms) {
                    // replaces the previous one, unlike toasts, which wait for the window to get focus
                    ui->getNotificationOverlay()->addNotification(tformat("Reloaded the editor in {:.2f} ms", *ms));
                } else {
                    std::string reason{this->failureReason()};
#ifdef MCENGINE_FEATURE_HOT_RELOAD
                    // what the loader said is what helps here (e.g. a symbol the game doesn't have)
                    if(this->failure == Failure::LOAD_FAILED) {
                        reason = tformat("The editor could not be loaded: {}", this->library.getError());
                    }
#endif
                    ui->getNotificationOverlay()->addToast(std::move(reason), ERROR_TOAST);
                }
            }
        }
    }

    if(this->sessionWriteTime != 0.0 && engine->getTime() >= this->sessionWriteTime) this->saveSession();

    UIScreen::tick();
}

void EditorScreen::onKeyDown(KeyboardEvent &e) {
    if(!this->isVisible()) return;

    // ahead of the editor, so that it works whatever state the editor is in
    if(e == KEY_E && keyboard->isControlDown() && keyboard->isAltDown()) {
        if(!e.isRepeat()) this->reload();
        e.consume();
        return;
    }

    UIScreen::onKeyDown(e);
    if(e.isConsumed()) return;

    if(e == KEY_ESCAPE || e == binds::GAME_PAUSE) {
        this->onBack();
        e.consume();
    }
}

void EditorScreen::onResolutionChange(vec2 /*newResolution*/) {
    this->setSize(osu->getVirtScreenSize());
    if(this->root) this->root->setSize(this->getSize());
}

void EditorScreen::reload(bool fresh) {
    this->reloadScheduled = true;
    this->dropSession |= fresh;
}

void EditorScreen::onBack() {
    soundEngine->play(osu->getSkin()->s_menu_back);
    ui->setScreen(this->returnTo ? this->returnTo : ui->getMainMenuBase());
}

void EditorScreen::sessionChanged() {
    if(this->sessionWriteTime == 0.0) this->sessionWriteTime = engine->getTime() + SESSION_WRITE_DELAY;
}

void EditorScreen::workChanged() {
    ++this->workChanges;
    this->leaveGuard = env->guardLeave();
    this->sessionChanged();
}

void EditorScreen::onReloadCommand(std::string_view args) { this->reload(args == "fresh"); }

void EditorScreen::onTestCommand(std::string_view filter) {
    // the tests belong to the editor, so they need it (one out of sight when the screen isn't shown)
    if(!this->module) this->createEditor(Timing::getTimeReal());
    const bool passed = this->module && this->module->test(filter);
    logRaw("UITEST {} editor_test {}", passed ? "OK" : "FAIL", filter);
}

void EditorScreen::openPendingMap() {
    if(!this->root) return;
    // a timestamp is for the map it came with
    const bool opened = !this->pendingMap || this->module->open(this->root, *this->pendingMap);
    if(opened && !this->pendingTimestamp.empty()) this->module->jump(this->root, this->pendingTimestamp);
    this->pendingMap.reset();
    this->pendingTimestamp.clear();
}

std::optional<f64> EditorScreen::createEditor(f64 startTime) {
#ifdef MCENGINE_FEATURE_HOT_RELOAD
    // the editor is in its own library here, which load() swaps for the one on disk (the session survives that)
    if(const auto result = this->library.load(); result != Mc::ReloadableLibrary::LoadResult::OK) {
        debugLog("the editor failed to start: {}", this->library.getError());
        this->failure =
            result == Mc::ReloadableLibrary::LoadResult::STALE_HOST ? Failure::STALE_LIBRARY : Failure::LOAD_FAILED;
        return std::nullopt;
    }
    if(auto *entry = this->library.getFunction<decltype(neomod_editor_module)>("neomod_editor_module")) {
        this->module = entry();
    }
#else
    this->module = neomod_editor_module();
#endif
    if(this->module) {
        if(this->generation == 0) this->readSession();
        if(!this->session.empty()) this->beginRestore();
        this->root = this->module->create(this->session);
    }
    if(!this->root) {
        debugLog("the editor failed to start");
        this->failure = Failure::CREATE_FAILED;
        this->module = nullptr;
        // a second start in this frame must not take the marker for a crash
        this->endRestore();
        return std::nullopt;
    }
    this->addBaseUIElement(this->root);
    this->root->setSize(this->getSize());
    this->openPendingMap();
    this->root->setVisible(this->isVisible());

    this->generation++;
    const f64 ms = (Timing::getTimeReal() - startTime) * 1000.;
    debugLog("started the editor (generation {}) in {:.2f} ms", this->generation, ms);
    return ms;
}

void EditorScreen::destroyEditor() {
    this->removeBaseUIElement(this->root);
    this->module->destroy(this->root);
    this->root = nullptr;
    this->module = nullptr;
}

void EditorScreen::writeSessionGuarded() {
    if(!this->root) return;
    this->saveSession(true);
    this->beginRestore();
}

void EditorScreen::saveSession(bool now) {
    this->sessionWriteTime = 0.0;
    if(!this->root) return;
    this->session = this->module->save(this->root);
    // an older write that hasn't started is out of date, and one that has must not finish after this one
    this->sessionWrites.cancel();
    if(now) {
        writeSessionFile(this->session);
        this->leaveGuard.reset();
    } else {
        this->sessionWrites.then_on_main(
            this->sessionWrites.submit([session = this->session] { writeSessionFile(session); }),
            [this, work = this->workChanges] {
                if(work == this->workChanges) this->leaveGuard.reset();
            });
    }
}

void EditorScreen::beginRestore() {
    File marker(sessionFile(".restoring"), File::MODE::WRITE);
    this->restoreFrame = engine->getFrameCount();
}

void EditorScreen::endRestore() {
    if(!this->restoreFrame) return;
    Environment::deleteFile(sessionFile(".restoring"));
    this->restoreFrame.reset();
}

void EditorScreen::readSession() {
    if(Environment::fileExists(sessionFile(".restoring"))) {
        // set aside instead of crashing the game again
        Environment::renameFile(sessionFile(), sessionFile(".bad"));
        Environment::deleteFile(sessionFile(".restoring"));
        debugLog("restoring the editor session crashed the game last time, moved it to {}", sessionFile(".bad"));
        ui->getNotificationOverlay()->addToast(
            tformat("Restoring the last editor session crashed the game, so the editor started a new one. The old "
                    "session was kept as {}.",
                    sessionFile(".bad")),
            ERROR_TOAST);
        return;
    }
    File file(sessionFile());
    file.readToVector(this->session);
}
