#pragma once
// Copyright (c) 2015, PG, All rights reserved.
#include <utility>

#include "CBaseUIButton.h"
#include "DownloadHandle.h"
#include "MouseListener.h"
#include "UIScreen.h"
#include "Resource.h"
#include "ResourceManager.h"

class Image;
class DatabaseBeatmap;
typedef DatabaseBeatmap BeatmapDifficulty;
typedef DatabaseBeatmap BeatmapSet;

class CBaseUILabel;
class CBaseUIContainer;
class UIButton;
class UIButtonWithIcon;
class UIButtonVertical;

class PauseButton final : public CBaseUIButton {
   public:
    PauseButton(float xPos, float yPos, float xSize, float ySize, UString name, UString text)
        : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), std::move(text)) {}

    void draw() override;
    inline void setPaused(bool paused) { this->bIsPaused = paused; }

   private:
    bool bIsPaused{true};
};

class MainMenu final : public UIScreen, public MouseListener {
    NOCOPY_NOMOVE(MainMenu)
   public:
    void onPausePressed();
    void onCubePressed();

    MainMenu();
    ~MainMenu() override;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void clearPreloadedMaps();
    void selectRandomBeatmap();

    void onKeyDown(KeyboardEvent &e) override;

    void onButtonChange(ButtonEvent ev) override;

    void onResolutionChange(vec2 newResolution) override;

    CBaseUIContainer *setVisible(bool visible) override;

   private:
    class CubeButton;
    class MainButton;

    friend class CubeButton;
    friend class MainButton;
    float button_sound_cooldown{0.f};

    void drawVersionInfo();
    void drawMainButton();
    void drawLogoImage(const McRect &mainButtonRect);
    void drawFriend(const McRect &mainButtonRect, float pulse, bool haveTimingpoints);
    std::pair<bool, float> getTimingpointPulseAmount();  // for main menu cube anim
    void updateLayout();

    void animMainButton();
    void animMainButtonBack();

    void setMenuElementsVisible(bool visible, bool animate = true);

    void writeVersionFile();

    MainButton *addMainMenuButton(UString text);

    void onPlayButtonPressed();
    void onMultiplayerButtonPressed();
    void onOptionsButtonPressed();
    void onSaveOrExitButtonPressed();

    void onUpdatePressed();
    void onVersionPressed();

    float fUpdateStatusTime;
    float fUpdateButtonTextTime;
    float fUpdateButtonAnimTime;
    float fUpdateButtonAnim;
    bool bHasClickedUpdate;
    bool shuffling = false;

    vec2 vSize{0.f};
    vec2 vCenter{0.f};
    float fSizeAddAnim;
    float fCenterOffsetAnim;

    bool bMenuElementsVisible;
    float fMainMenuButtonCloseTime = 0.f;

    CubeButton *cube;
    std::vector<MainButton *> menuElements;

    PauseButton *pauseButton;
    CBaseUILabel *tipLabel{nullptr};
    UIButton *updateAvailableButton{nullptr};
    UIButtonVertical *onlineBeatmapsButton{nullptr};
    CBaseUIButton *versionButton;

    UIButtonWithIcon *discordButton{nullptr};
    UIButtonWithIcon *twitterButton{nullptr};

    bool bDrawVersionNotificationArrow;
    bool bDidUserUpdateFromOlderVersion;

    // custom
    float fMainMenuAnimTime;
    float fMainMenuAnimDuration;
    float fMainMenuAnim;
    float fMainMenuAnim1;
    float fMainMenuAnim2;
    float fMainMenuAnim3;
    float fMainMenuAnim1Target;
    float fMainMenuAnim2Target;
    float fMainMenuAnim3Target;
    bool bInMainMenuRandomAnim;
    int iMainMenuRandomAnimType;
    unsigned int iMainMenuAnimBeatCounter;

    bool bMainMenuAnimFriend;
    bool bMainMenuAnimFadeToFriendForNextAnim;
    bool bMainMenuAnimFriendScheduled;
    float fMainMenuAnimFriendPercent;
    float fMainMenuAnimFriendEyeFollowX;
    float fMainMenuAnimFriendEyeFollowY;

    float fShutdownScheduledTime;
    bool bWasCleanShutdown;

    bool bStartupAnim{true};
    f32 fStartupAnim{0.f};
    f32 fStartupAnim2{0.f};
    float fPrevShuffleTime{0.f};

    Downloader::DownloadHandle server_icon_dl;
    Image *logo_img;

    void drawMapBackground(DatabaseBeatmap *beatmap, f32 alpha);
    DatabaseBeatmap *currentMap{nullptr};
    DatabaseBeatmap *lastMap{nullptr};
    Shader *background_shader = nullptr;
    f32 mapFadeAnim{1.f};
    std::vector<std::unique_ptr<BeatmapSet>> preloadedMaps;

    struct SongsFolderEnumerator final : public Resource {
        NOCOPY_NOMOVE(SongsFolderEnumerator)
       public:
        SongsFolderEnumerator();
        ~SongsFolderEnumerator() override;

        [[nodiscard]] inline const std::vector<std::string> &getEntries() const { return this->entries; }
        [[nodiscard]] inline std::string getFolderPath() const { return this->osuSongsFolderPath; }
        void rebuild();

       protected:
        void init() override { this->setReady(true); }
        void initAsync() override;
        void destroy() override { this->entries.clear(); }

       private:
        std::vector<std::string> entries{};
        std::string osuSongsFolderPath{""};
    };

    SongsFolderEnumerator songs_enumerator;
};
