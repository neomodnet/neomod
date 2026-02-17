#pragma once
// Copyright (c) 2026, kiwec, All rights reserved.

#include "config.h"
#include "noinclude.h"

#include "OsuConfig.h"
#include "Vectors.h"
#include "Hashing.h"

#include <memory>
#include <array>

class CWindowManager;
class KeyboardEvent;
class RenderTarget;
class UIScreen;
class UIOverlay;

class Changelog;
class Chat;
class HUD;
class Lobby;
class MainMenu;
class ModSelector;
class NotificationOverlay;
class OptionsOverlay;
class OsuDirectScreen;
class PauseOverlay;
class PromptOverlay;
class RankingScreen;
class RoomScreen;
class SongBrowser;
class SpectatorScreen;
class TooltipOverlay;
class UIUserContextMenuScreen;
class UserStatsScreen;
class VolumeOverlay;

// global for convenience, created in osu constructor, destroyed in osu constructor
struct UI;
extern UI* ui;

// UIScreens, manually created + added to the "overlays" array and destroyed in reverse order in dtor

struct UI final {
    NOCOPY_NOMOVE(UI)

   private:
    class NullScreen;

   public:
    UI();
    ~UI();

    bool init();
    void hide();
    void show();

    void update();
    void draw();
    void onKeyDown(KeyboardEvent& key);
    void onKeyUp(KeyboardEvent& key);
    void onChar(KeyboardEvent& e);
    void onResolutionChange(vec2 newResolution);
    void stealFocus();

    [[nodiscard]] inline UIScreen* getActiveScreen() { return this->active_screen; }
    inline void setScreen(std::nullptr_t) { this->hide(); }
    void setScreen(UIScreen* screen);

    // queryable with peekOverlay or removable with popOverlay
    // when pushed, the pushed overlay is set visible (but the parent is not set invisible)
    UIOverlay* pushOverlay(std::unique_ptr<UIOverlay> overlay);

    // returns false if overlay has been destroyed
    [[nodiscard]] bool peekOverlay(UIOverlay* overlay) const;

    // when popped, the parent is set visible/active
    std::unique_ptr<UIOverlay> popOverlay(UIOverlay* overlay);

    // created early, in ctor, for error notifications
    [[nodiscard]] inline NotificationOverlay* getNotificationOverlay() { return this->notificationOverlay; }

    // rest are created on init()
    [[nodiscard]] inline VolumeOverlay* getVolumeOverlay() { return this->volumeOverlay; }
    [[nodiscard]] inline PromptOverlay* getPromptOverlay() { return this->promptOverlay; }
    [[nodiscard]] inline ModSelector* getModSelector() { return this->modSelector; }
    [[nodiscard]] inline UIUserContextMenuScreen* getUserActions() { return this->userActionsOverlay; }
    [[nodiscard]] inline RoomScreen* getRoom() { return this->room; }
    [[nodiscard]] inline Chat* getChat() { return this->chatOverlay; }
    [[nodiscard]] inline OptionsOverlay* getOptionsOverlay() { return this->optionsOverlay; }
    [[nodiscard]] inline RankingScreen* getRankingScreen() { return this->rankingScreen; }
    [[nodiscard]] inline UserStatsScreen* getUserStatsScreen() { return this->userStatsScreen; }
    [[nodiscard]] inline SpectatorScreen* getSpectatorScreen() { return this->spectatorScreen; }
    [[nodiscard]] inline PauseOverlay* getPauseOverlay() { return this->pauseOverlay; }
    [[nodiscard]] inline HUD* getHUD() { return this->hud; }
    [[nodiscard]] inline SongBrowser* getSongBrowser() { return this->songBrowser; }
    [[nodiscard]] inline OsuDirectScreen* getOsuDirectScreen() { return this->osuDirectScreen; }
    [[nodiscard]] inline Lobby* getLobby() { return this->lobby; }
    [[nodiscard]] inline Changelog* getChangelog() { return this->changelog; }
    [[nodiscard]] inline MainMenu* getMainMenu() { return this->mainMenu; }
    [[nodiscard]] inline TooltipOverlay* getTooltipOverlay() { return this->tooltipOverlay; }

   private:
    friend UIScreen;
    // for debugging
    void setScreenByName(std::string_view screenGetterNameWithoutGet);

    NullScreen* dummy;
    NotificationOverlay* notificationOverlay;
    static constexpr const size_t EARLY_SCREENS{2};  // dummy+notificationOverlay

    VolumeOverlay* volumeOverlay{nullptr};
    PromptOverlay* promptOverlay{nullptr};
    ModSelector* modSelector{nullptr};
    UIUserContextMenuScreen* userActionsOverlay{nullptr};
    RoomScreen* room{nullptr};
    Chat* chatOverlay{nullptr};
    OptionsOverlay* optionsOverlay{nullptr};
    RankingScreen* rankingScreen{nullptr};
    UserStatsScreen* userStatsScreen{nullptr};
    SpectatorScreen* spectatorScreen{nullptr};
    PauseOverlay* pauseOverlay{nullptr};
    HUD* hud{nullptr};
    SongBrowser* songBrowser{nullptr};
    OsuDirectScreen* osuDirectScreen{nullptr};
    Lobby* lobby{nullptr};
    Changelog* changelog{nullptr};
    MainMenu* mainMenu{nullptr};
    TooltipOverlay* tooltipOverlay{nullptr};

    static constexpr const size_t NUM_SCREENS{20};  // update this when adding screens

    UIScreen* active_screen{nullptr};

    // "always-alive" screens
    std::array<UIScreen*, NUM_SCREENS> screens{};

    // additional overlays added by pushOverlay (owned by UI)
    Hash::flat::set<UIOverlay*> extra_overlays;

    // interfaces (debugging)
    // std::unique_ptr<CWindowManager> windowManager{nullptr};
};
