#pragma once
// Copyright (c) 2026, kiwec, 2026, WH, All rights reserved.

#include "config.h"
#include "noinclude.h"

#include "OsuConfig.h"
#include "Vectors_fwd.h"

#include <memory>
#include <array>
#include <vector>
#include <string_view>

class CBaseUIElement;
class KeyboardEvent;
class RenderTarget;
class UIScreen;
class UIOverlay;

class AboutScreen;
class BeatmapInstallOverlay;
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

class UIDebug;

#define UIF_DEFAULT 0
#define UIF_MODAL (1 << 0)
#define UIF_CLOSE (1 << 1)  // close on switch

// The screen registry: one row per always-alive screen, in STORAGE order (NOT layer order).
// Everything below is derived from it, so adding a screen is one row. Columns:
//   rank   : position in the layer stack mapped out below; dense + unique (a gap or duplicate is
//            a compile error, see SCREEN_RANK/LAYER_ORDER)
//   flags  : UIF_* mask
//   Type   : concrete screen class and accessor name
//   member : field name, also stringized into SCREEN_NAMES (set_active_ui_screen / ui_screens)
// dummy (index 0) is the one screen NOT here: a file-local NullScreen sentinel, no accessor, built first.
//
// Layer stack, bottom -> top (= the `rank` column). The rows below are storage-ordered, so this
// comment is the only place the stack reads in order. The active base screen draws at the frame
// bottom, the overlay band draws over it; input + key routing walk the whole stack top-down.
//
//   BASE band (ranks 1-10): at most one visible at a time, so order WITHIN the band does not
//     matter; the 9 setScreen-swapped base screens (room, rankingscreen, userstatsscreen,
//     spectatorscreen, songbrowser, osudirectscreen, lobby, aboutscreen, mainmenu), then hud
//     (rank 10, drawn by the gameplay composite).
//   OVERLAY band (ranks 11-20): occlusion matters here, so this order is important:
//     11 pauseoverlay  12 modselector  13 chat  14 useractions  15 optionsoverlay
//     16 promptoverlay  17 beatmapinstalloverlay  18 tooltipoverlay  19 volumeoverlay
//     20 notificationoverlay
//   notification > volume is deliberate: VolumeOverlay::onKeyDown is ungated (KEY_MUTE, volume
//   binds) and must not eat keys ahead of notification's keybind capture. extra_overlays from
//   pushOverlay splice in just below tooltipoverlay (see EXTRAS_SPLICE).

// clang-format off
#define UI_SCREEN_REGISTRY(X) \
    X(20, UIF_DEFAULT,           NotificationOverlay,     notificationoverlay) \
    X(19, UIF_DEFAULT,           VolumeOverlay,           volumeoverlay) \
    X(16, UIF_MODAL | UIF_CLOSE, PromptOverlay,           promptoverlay) \
    X(12, UIF_MODAL | UIF_CLOSE, ModSelector,             modselector) \
    X(14, UIF_DEFAULT,           UIUserContextMenuScreen, useractions) \
    X( 1, UIF_DEFAULT,           RoomScreen,              room) \
    X(13, UIF_DEFAULT,           Chat,                    chat) \
    X(15, UIF_CLOSE,             OptionsOverlay,          optionsoverlay) \
    X( 2, UIF_DEFAULT,           RankingScreen,           rankingscreen) \
    X( 3, UIF_DEFAULT,           UserStatsScreen,         userstatsscreen) \
    X( 4, UIF_DEFAULT,           SpectatorScreen,         spectatorscreen) \
    X(11, UIF_MODAL | UIF_CLOSE, PauseOverlay,            pauseoverlay) \
    X(10, UIF_DEFAULT,           HUD,                     hud) \
    X( 5, UIF_DEFAULT,           SongBrowser,             songbrowser) \
    X( 6, UIF_DEFAULT,           OsuDirectScreen,         osudirectscreen) \
    X( 7, UIF_DEFAULT,           Lobby,                   lobby) \
    X( 8, UIF_DEFAULT,           AboutScreen,             aboutscreen) \
    X( 9, UIF_DEFAULT,           MainMenu,                mainmenu) \
    X(18, UIF_DEFAULT,           TooltipOverlay,          tooltipoverlay) \
    X(17, UIF_DEFAULT,           BeatmapInstallOverlay,   beatmapinstalloverlay)
// clang-format on

struct UI final {
    NOCOPY_NOMOVE(UI)

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

    [[nodiscard]] inline UIScreen* getActiveScreen() const { return this->active_screen; }
    inline void setScreen(std::nullptr_t) { this->hide(); }
    void setScreen(UIScreen* screen);

    // queryable with peekOverlay or removable with popOverlay
    // when pushed, the pushed overlay is set visible (but the parent is not set invisible)
    UIOverlay* pushOverlay(std::unique_ptr<UIOverlay> overlay);

    // returns false if overlay has been destroyed
    [[nodiscard]] bool peekOverlay(UIOverlay* overlay) const;

    // when popped, the parent is set visible/active
    std::unique_ptr<UIOverlay> popOverlay(UIOverlay* overlay);

    // getX() returns the concrete type; getXBase() returns UIScreen* and is defined out-of-line in
    // UI.cpp, where the concrete types are complete (the derived->base conversion needs them).
    // NOLINTBEGIN(bugprone-macro-parentheses): Type is a type name, can't be parenthesized
#define X(rank, F, Type, member)                                    \
    [[nodiscard]] inline Type* get##Type() const { return this->member; } \
    [[nodiscard]] UIScreen* get##Type##Base() const;
    UI_SCREEN_REGISTRY(X)
#undef X
    // NOLINTEND(bugprone-macro-parentheses)

   private:
    friend UIScreen;
    friend UIDebug;  // see UIDebug.h

    // ui_validate_ticks support (debug builds): logs UITEST FAIL for screens skipped by the tick pass
    void validateTicks() const;

    // walks LAYER_ORDER top -> bottom (with extra_overlays spliced in) until consumed
    void routeKey(KeyboardEvent& e, void (CBaseUIElement::*handler)(KeyboardEvent&), std::string_view traceName);
    // draws LAYER_ORDER[from, to), skipping the active screen (drawn at the frame bottom)
    void drawLayerRange(size_t from, size_t to);
    void drawExtraOverlays();

    // storage index 0, the active_screen sentinel (see the registry note for why it is not a row)
    UIScreen* dummy{nullptr};
    static constexpr const size_t EARLY_SCREENS{2};  // dummy + notification, built in the ctor

    // one typed pointer per registry row
#define X(rank, F, Type, member) Type* member{nullptr};  // NOLINT(bugprone-macro-parentheses)
    UI_SCREEN_REGISTRY(X)
#undef X

    // dummy + one per registry row
#define X(...) +1  // NOLINT(bugprone-macro-parentheses)
    static constexpr size_t NUM_SCREENS{1 UI_SCREEN_REGISTRY(X)};
#undef X

    UIScreen* active_screen{nullptr};

    // "always-alive" screens
    std::array<UIScreen*, NUM_SCREENS> screens{};

    // additional overlays added by pushOverlay (owned by UI)
    std::vector<UIOverlay*> extra_overlays;

    // debugging
    std::unique_ptr<UIDebug> debuglayer{nullptr};

    // for idle cursor fade alpha
    f64 lastCursorMoveTime{0.};

    // name -> screen lookup (findScreenByName / set_active_ui_screen), index-aligned with screens[]
#define X(rank, F, Type, member) #member,
    static constexpr std::array<std::string_view, NUM_SCREENS> SCREEN_NAMES{"dummy", UI_SCREEN_REGISTRY(X)};
#undef X

    // per-screen draw rank, from the registry. LAYER_ORDER below is the derived inverse, so a
    // duplicate or out-of-range rank is a compile error, not a silent mis-layer.
#define X(rank, F, Type, member) rank,
    static constexpr std::array<size_t, NUM_SCREENS> SCREEN_RANK{0, UI_SCREEN_REGISTRY(X)};
#undef X

    // bottom -> top, the inverse of SCREEN_RANK: draw walks it forward, input/key routing walk it in
    // reverse, so input order = reverse draw order by construction. See the layer-stack map atop the
    // registry for the band structure and the notification > volume rationale.
    static constexpr std::array<size_t, NUM_SCREENS> LAYER_ORDER = [] {
        std::array<size_t, NUM_SCREENS> order{};
        for(size_t i = 0; i < NUM_SCREENS; ++i) order[SCREEN_RANK[i]] = i;
        return order;
    }();

    // band boundaries as named ranks, not magic ints (UI.cpp static_asserts pin them). the rankOf
    // lambda lives in this IIFE because a member helper can't be used in a sibling initializer.
    static constexpr std::array<size_t, 3> BAND_RANKS = [] {
        auto rankOf = [](std::string_view name) {
            for(size_t i = 0; i < NUM_SCREENS; ++i)
                if(SCREEN_NAMES[i] == name) return SCREEN_RANK[i];
            return NUM_SCREENS;  // not found -> OOB use below = compile error
        };
        return std::array<size_t, 3>{rankOf("pauseoverlay"), rankOf("optionsoverlay") + 1, rankOf("tooltipoverlay")};
    }();
    static constexpr size_t OVERLAY_BAND_BEGIN{BAND_RANKS[0]};  // first overlay-band layer above base/hud
    static constexpr size_t PLAY_OVERLAYS_END{BAND_RANKS[1]};   // one past options; pause..options render
                                                                // into the FPoSu playfield buffer in play mode
    static constexpr size_t EXTRAS_SPLICE{BAND_RANKS[2]};       // extra_overlays walk/draw below this layer
};
