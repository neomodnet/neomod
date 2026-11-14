// Copyright (c) 2026, WH, All rights reserved.
#include "EditorState.h"

#include "BeatmapInstaller.h"
#include "ConVar.h"
#include "Engine.h"
#include "Keyboard.h"
#include "NotificationOverlay.h"
#include "Osu.h"
#include "UI.h"
#include "i18n.h"

#include <cassert>
#include <utility>

bool EditorState::copyFirst(std::function<void()> then) {
    const Database::MapLocation &location = this->document->getLocation();
    if(location.root != Database::MapRoot::Peppy) return false;
    if(Database::standIn(location).folder != this->copying) {
        this->copying = osu->getBeatmapInstaller()->enqueue_copy(location.folder);
        if(this->copying.empty()) {
            ui->getNotificationOverlay()->addNotification(
                _("This beatmap set can't be copied into the neomod library."));
            return true;
        }
        ui->getNotificationOverlay()->addNotification(
            _("This is an osu!stable map: copying its set into the neomod library first."));
    }
    this->afterCopy.push_back(std::move(then));
    return true;
}

void EditorState::explainRefusal() const {
    using Error = neomod::Primitives::LoadError;
    const Error &error = this->document->getError();
    if(!error) return;
    ui->getNotificationOverlay()->addNotification(
        error.errc == Error::NO_TIMINGPOINTS      ? _("The edit wasn't made: sliders need a timing point.")
        : error.errc == Error::TOOMANY_HITOBJECTS ? _("The edit wasn't made: the map would have too many objects.")
                                                  : _("The edit wasn't made: the game couldn't read the result."));
}

bool EditorState::carriesObjects() const { return !this->carryObjects || this->carryObjects->getBool(); }

void EditorState::open(std::unique_ptr<Document> document) {
    assert(!this->document && "a map opened over another");
    this->document = std::move(document);
    this->refreshObjects();
}

void EditorState::close() {
    assert(!this->clock.isAttached() && "a map closed while it's shown");
    this->compose.reset();
    this->objects.reset();
    this->document.reset();
    this->afterCopy.clear();
}

std::optional<std::vector<Document::Id>> EditorState::undo() {
    auto touched = this->document->undo();
    this->refreshObjects();
    return touched;
}

std::optional<std::vector<Document::Id>> EditorState::redo() {
    auto touched = this->document->redo();
    this->refreshObjects();
    return touched;
}

bool EditorState::revert(std::string_view path) {
    const bool read = this->document->revert(path);
    this->refreshObjects();
    return read;
}

bool EditorState::restore(std::vector<u8> bytes) {
    const bool changed = this->document->restore(std::move(bytes));
    if(!changed) this->explainRefusal();
    this->refreshObjects();
    return changed;
}

void EditorState::refreshObjects() {
    // every change, undo and redo makes a revision
    if(this->objects && this->objects->revision == this->document->getRevision()) return;
    if(!this->objects || !this->objects->update(*this->document)) {
        this->objects = std::make_unique<MapObjects>(*this->document);
    }
    this->compose.onObjectsRebuilt();
}

bool EditorState::showsHitAnimations() const { return this->hitAnimations && this->hitAnimations->getBool(); }

bool EditorState::showsSoundLabels() const { return this->soundLabels && this->soundLabels->getBool(); }

bool EditorState::showsGrid() const { return !this->grid || this->grid->getBool(); }

Snapping::Settings EditorState::snapSettings() const {
    const bool shift = keyboard->isShiftDown();
    const bool grid = (this->gridSnap && this->gridSnap->getBool()) != shift;
    const bool distance = (this->distanceSnap && this->distanceSnap->getBool()) != keyboard->isAltDown();
    return {.grid = grid,
            .gridSize = this->gridSize,
            .distance = distance,
            .spacing = this->spacing,
            .angle = distance && !grid && shift != keyboard->isControlDown()};
}
