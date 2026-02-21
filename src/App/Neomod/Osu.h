#pragma once
// Copyright (c) 2015, PG, All rights reserved.
#include "App.h"
#include "MouseListener.h"
#include "Rect.h"

#include "OsuConfig.h"

#include <atomic>
#include <vector>
#include <array>

class ThumbnailManager;
class ConVar;
class Image;
class McFont;
class RenderTarget;

struct Skin;
class BGImageHandler;
class ModFPoSu;
class BeatmapInterface;
class LiveScore;
class UpdateHandler;
class UserCard;
struct FinishedScore;

namespace LegacyReplay {
enum KeyFlags : uint8_t;
}
using GameplayKeys = LegacyReplay::KeyFlags;

namespace Replay {
struct Mods;
}

#ifndef CONVAR_H
enum class CvarEditor : uint8_t;
#endif

struct UI;
extern UI *ui;

class Osu final : public App, public MouseListener {
    NOCOPY_NOMOVE(Osu)
   public:
    Osu();

   private:
    struct GlobalOsuCtorDtorThing {
        NOCOPY_NOMOVE(GlobalOsuCtorDtorThing)
       public:
        GlobalOsuCtorDtorThing() = delete;
        GlobalOsuCtorDtorThing(Osu *optr);
        ~GlobalOsuCtorDtorThing();
    };

    // make sure the global "osu" name is created first and destroyed last... funny way to do it, but it works
    // so we don't have to break the compile barrier and do "osu = nullptr" in Engine.cpp
    GlobalOsuCtorDtorThing global_osu_;

    // clang-format off
    enum ResolutionRequestFlags : uint8_t {
        R_NOT_PENDING =             1u << 0,
        R_ENGINE =                  1u << 1,
        R_CV_RESOLUTION =           1u << 2,
        R_CV_LETTERBOXED_RES =      1u << 3,
        R_CV_LETTERBOXING =         1u << 4,
        R_CV_WINDOWED_RESOLUTION =  1u << 5,
        R_DELAYED_DESYNC_FIX =      1u << 6,
        R_MISC_MANUAL =             1u << 7
    };
    friend constexpr bool is_flag(Osu::ResolutionRequestFlags /**/);

    // clang-format on
   public:
    /////////////////////////////////////////////////
    // BASE CLASS OVERRIDES ACCESSIBLE FROM ENGINE //
    /////////////////////////////////////////////////

    ~Osu() override;

    void draw() override;
    void update() override;

    [[nodiscard]] forceinline bool isInGameplay() const override { return isInPlayMode(); }
    [[nodiscard]] forceinline bool isInUnpausedGameplay() const override { return isInPlayModeAndNotPaused(); }

    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;
    void stealFocus() override;

    void onButtonChange(ButtonEvent ev) override;

    forceinline void onResolutionChanged(vec2 newResolution) override {
        onResolutionChanged(newResolution, ResolutionRequestFlags::R_ENGINE);
    }
    void onDPIChanged() override;

    void onFocusGained() override;
    void onFocusLost() override;
    inline void onRestored() override {}
    void onMinimized() override;
    bool onShutdown() override;

    [[nodiscard]] Sound *getSound(ActionSound action) const override;
    void showNotification(const NotificationInfo &info) override;

    /////////////////////////////////////////////////////
    // CUSTOM METHODS/MEMBERS INACCESSIBLE FROM ENGINE //
    /////////////////////////////////////////////////////

    static inline const vec2 osuBaseResolution{640.0f, 480.0f};
    static inline float rawUIScale{1.f};

    static float getRectScaleToFitResolution(vec2 size, vec2 resolution);
    static float getRectScaleToFillResolution(vec2 size, vec2 resolution);
    static float getRectScale(vec2 size, float osuSize);
    static float getImageScaleToFitResolution(const Image *img, vec2 resolution);
    static float getImageScaleToFillResolution(const Image *img, vec2 resolution);
    static float getImageScale(const Image *img, float osuSize);
    static float getUIScale(float osuSize);
    static float getUIScale();                                  // NOTE: includes premultiplied dpi scale!
    static inline float getRawUIScale() { return rawUIScale; }  // equivalent to cv::ui_scale.getFloat()
    [[nodiscard]] inline bool UIReady() const { return !!this->ui_memb && this->bUILoaded; };

    void onResolutionChanged(vec2 newResolution, ResolutionRequestFlags src);

    void onPlayEnd(const FinishedScore &score, bool quit = true);

    void reloadMapInterface();
    void reloadSkin() { this->onSkinReload(); }
    void saveEverything();
    void saveScreenshot();

    // threading-related
    [[nodiscard]] bool isInPlayModeAndNotPaused() const;

    [[nodiscard]] inline bool shouldPauseBGThreads() const {
        return this->pause_bg_threads.load(std::memory_order_acquire);
    }
    inline void setShouldPauseBGThreads(bool pause) { this->pause_bg_threads.store(pause, std::memory_order_release); }

    [[nodiscard]] inline McRect getVirtScreenRect() const { return this->internalRect; }
    [[nodiscard]] inline vec2 getVirtScreenSize() const { return this->internalRect.getSize(); }
    [[nodiscard]] inline int getVirtScreenWidth() const { return (int)this->internalRect.getWidth(); }
    [[nodiscard]] inline int getVirtScreenHeight() const { return (int)this->internalRect.getHeight(); }

    [[nodiscard]] inline const std::unique_ptr<UserCard> &getUserButton() const { return this->userButton; }
    [[nodiscard]] inline const std::unique_ptr<BGImageHandler> &getBackgroundImageHandler() const {
        return this->backgroundImageHandler;
    }

    [[nodiscard]] inline const std::unique_ptr<ModFPoSu> &getFPoSu() const { return this->fposu; }
    [[nodiscard]] inline const std::unique_ptr<LiveScore> &getScore() const { return this->score; }
    [[nodiscard]] inline const std::unique_ptr<UpdateHandler> &getUpdateHandler() const { return this->updateHandler; }
    [[nodiscard]] inline const std::unique_ptr<BeatmapInterface> &getMapInterface() const { return this->map_iface; }
    [[nodiscard]] inline const std::unique_ptr<ThumbnailManager> &getThumbnailManager() const {
        return this->thumbnailManager;
    }

    [[nodiscard]] inline RenderTarget *getBackBuffer() const { return this->backBuffer; }
    [[nodiscard]] inline RenderTarget *getPlayfieldBuffer() const { return this->playfieldBuffer; }
    [[nodiscard]] inline RenderTarget *getSliderFrameBuffer() const { return this->sliderFrameBuffer; }
    [[nodiscard]] inline RenderTarget *getAAFrameBuffer() const { return this->AAFrameBuffer; }
    [[nodiscard]] inline RenderTarget *getFrameBuffer() const { return this->frameBuffer; }
    [[nodiscard]] inline RenderTarget *getFrameBuffer2() const { return this->frameBuffer2; }
    [[nodiscard]] inline McFont *getTitleFont() const { return this->titleFont; }
    [[nodiscard]] inline McFont *getSubTitleFont() const { return this->subTitleFont; }
    [[nodiscard]] inline McFont *getSongBrowserFont() const { return this->songBrowserFont; }
    [[nodiscard]] inline McFont *getSongBrowserFontBold() const { return this->songBrowserFontBold; }
    [[nodiscard]] inline McFont *getFontIcons() const { return this->fontIcons; }
    [[nodiscard]] inline const Skin *getSkin() const { return this->skin.get(); }
    [[nodiscard]] inline Skin *getSkinMutable() { return this->skin.get(); }

    [[nodiscard]] static float getDifficultyMultiplier();
    [[nodiscard]] static float getCSDifficultyMultiplier();
    [[nodiscard]] float getScoreMultiplier() const;
    [[nodiscard]] float getAnimationSpeedMultiplier() const;

    [[nodiscard]] bool getModAuto() const;
    [[nodiscard]] bool getModAutopilot() const;
    [[nodiscard]] bool getModRelax() const;
    [[nodiscard]] bool getModSpunout() const;
    [[nodiscard]] bool getModTarget() const;
    [[nodiscard]] bool getModScorev2() const;
    [[nodiscard]] bool getModFlashlight() const;
    [[nodiscard]] bool getModNF() const;
    [[nodiscard]] bool getModHD() const;
    [[nodiscard]] bool getModHR() const;
    [[nodiscard]] bool getModEZ() const;
    [[nodiscard]] bool getModSD() const;
    [[nodiscard]] bool getModSS() const;
    [[nodiscard]] bool getModNightmare() const;
    [[nodiscard]] bool getModTD() const;
    [[nodiscard]] bool getModDT() const;
    [[nodiscard]] bool getModNC() const;
    [[nodiscard]] bool getModHT() const;

    [[nodiscard]] static bool isBleedingEdge();
    [[nodiscard]] constexpr bool isInPlayMode() const { return this->bIsPlayingASelectedBeatmap; }
    [[nodiscard]] inline bool isSkinLoading() const {
        return this->bSkinLoadScheduled ||
               (this->skin && this->skinScheduledToLoad && this->skin.get() != this->skinScheduledToLoad);
    }

    [[nodiscard]] inline bool isSkipScheduled() const { return this->bSkipScheduled; }
    [[nodiscard]] inline bool isSeeking() const { return this->bSeeking; }
    [[nodiscard]] inline u32 getQuickSaveTimeMS() const { return this->iQuickSaveMS; }

    [[nodiscard]] bool shouldFallBackToLegacySliderRenderer()
        const;  // certain mods or actions require Sliders to render dynamically
                // (e.g. wobble or the CS override slider)

    void updateMods();
    void updateCursorVisibility();
    void updateConfineCursor();
    void updateMouseSettings();
    void updateWindowsKeyDisable();

    // im not sure why this is a change callback but im too scared to change it at this point
    inline void updateModsForConVarTemplate(float /*oldValue*/, float /*newValue*/) { this->updateMods(); }

    void rebuildRenderTargets();
    void reloadFonts();

    // callbacks
    void onWindowedResolutionChanged(std::string_view args);
    void onFSResChanged(std::string_view args);
    void onFSLetterboxedResChanged(std::string_view args);

    void onSkinReload();
    void onSkinChange(std::string_view newValue);
    void onAnimationSpeedChange();
    void updateAnimationSpeed();

    void onSpeedChange(float speed);
    void onThumbnailsToggle();

    void onPlayfieldChange();

    void onUIScaleChange(float oldValue, float newValue);
    void onUIScaleToDPIChange(float oldValue, float newValue);
    void onLetterboxingChange(float oldValue, float newValue);

    void onGameplayKey(GameplayKeys key_flag, bool down, u64 timestamp, bool fromMouse = false);

    void onLetterboxingOffsetChange();

    void onUserCardChange(std::string_view new_username);

   private:
    float prevUIScale{1.f};  // hack-ish dpi change reload check

    void globalOnSetValueProtectedCallback();
    static bool globalOnGetValueProtectedCallback(const char *cvarname);
    static bool globalOnSetValueGameplayCallback(const char *cvarname, CvarEditor setterkind);
    static bool globalOnAreAllCvarsSubmittableCallback();

    // NOTE: unique_ptrs are destroyed in reverse order of declaration in header

   public:
    std::unique_ptr<Replay::Mods> previous_mods{nullptr};  // XXX: hacky and out of place

   private:
    // user interface
    friend struct UI;
    std::unique_ptr<UI> ui_memb{nullptr};

    bool bUILoaded{false};

    void doDeferredInitTasks();
    // defer some things to post-construction on the first update tick so that we're fully initialized
    bool bFirstUpdateTasksDone{false};

    // interfaces (other)
    std::unique_ptr<Skin> skin{nullptr};
    std::unique_ptr<BeatmapInterface> map_iface{nullptr};
    std::unique_ptr<UpdateHandler> updateHandler{nullptr};
    std::unique_ptr<ThumbnailManager> thumbnailManager{nullptr};
    std::unique_ptr<UserCard> userButton{nullptr};
    std::unique_ptr<BGImageHandler> backgroundImageHandler{nullptr};
    std::unique_ptr<LiveScore> score{nullptr};
    std::unique_ptr<ModFPoSu> fposu{nullptr};

    // rendering
    RenderTarget *backBuffer{nullptr};
    RenderTarget *playfieldBuffer{nullptr};
    RenderTarget *sliderFrameBuffer{nullptr};
    RenderTarget *AAFrameBuffer{nullptr};
    RenderTarget *frameBuffer{nullptr};
    RenderTarget *frameBuffer2{nullptr};

    McRect internalRect{0.f};

    // i don't like how these have to be public, but it's too annoying to change for now.
    // public members just mean their values can get rugpulled from under your feet at any moment,
    // and make it more annoying to find everywhere its actually changed

    // mods
    // public because of many external access
   public:
    bool bModAutoTemp{false};  // when ctrl+clicking a map, the auto mod should disable itself after the map finishes

    // XXX: dirty
    float fQuickRetryTime{0.f};
    bool bQuickRetryDown{false};
    bool bSkinLoadScheduled{false};
    Skin *skinScheduledToLoad{nullptr};

   private:
    bool bF1{false};
    bool bUIToggleCheck{false};
    bool bScoreboardToggleCheck{false};
    bool bSkipScheduled{false};
    bool bSeekKey{false};
    bool bSeeking{false};
    bool bClickedSkipButton{false};
    float fPrevSeekMousePosX{-1.f};

   public:  // public due to BeatmapInterface access
    u32 iQuickSaveMS{0};

   private:
    // global resources
    std::vector<McFont *> fonts;
    McFont *titleFont{nullptr};
    McFont *subTitleFont{nullptr};
    McFont *fontIcons{nullptr};
    McFont *songBrowserFont{nullptr};
    McFont *songBrowserFontBold{nullptr};

    // replay
   public:
    std::string watched_user_name;
    i32 watched_user_id{0};

    // custom
   private:
    ResolutionRequestFlags last_res_change_req_src{ResolutionRequestFlags::R_NOT_PENDING};
    std::atomic<bool> pause_bg_threads{false};
    bool bScheduleEndlessModNextBeatmap{false};
    bool bWasBossKeyPaused{false};
    bool bSkinLoadWasReload{false};
    bool bFontReloadScheduled{false};

    friend class BeatmapInterface;
    bool bIsPlayingASelectedBeatmap{false};

    [[nodiscard]] static std::string getDefaultFallbackOsuFolder();

    // internal audio setup
    void setupAudio();
    void audioRestartCallbackBefore();
    void audioRestartCallbackAfter();

    // for audio restart callbacks
    bool music_unpause_scheduled{false};
    bool music_was_playing{false};
    u32 music_prev_position_ms{0};
};

MAKE_FLAG_ENUM(Osu::ResolutionRequestFlags)

extern Osu *osu;
