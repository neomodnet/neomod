#pragma once
// Copyright (c) 2015, PG, All rights reserved.
#include <utility>

#include "AnimationHandler.h"
#include "AsyncCancellable.h"
#include "DownloadHandle.h"
#include "MouseListener.h"
#include "UIScreen.h"

#include <string>
#include <vector>

class Image;
class DatabaseBeatmap;
typedef DatabaseBeatmap BeatmapDifficulty;
typedef DatabaseBeatmap BeatmapSet;

namespace neomod::mainmenu {
class WrappedText;
class NowPlaying;
}  // namespace neomod::mainmenu
class CBaseUIButton;
class CBaseUILabel;
class CBaseUIContainer;
class UIButton;
class UIButtonWithIcon;
class UIButtonVertical;

class MainMenu final : public UIScreen, public MouseListener {
    NOCOPY_NOMOVE(MainMenu)
   public:
    void onCubePressed();

    MainMenu();
    ~MainMenu() override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void clearPreloadedMaps();
    // both play the current song again when there's no other one to go to
    void selectRandomBeatmap();
    void selectPreviousRandomBeatmap();

    void onKeyDown(KeyboardEvent &e) override;

    void onButtonChange(ButtonEvent &ev) override;

    void onResolutionChange(vec2 newResolution) override;

    CBaseUIContainer *setVisible(bool visible) override;

   private:
    class CubeButton;
    class MainButton;

    friend class CubeButton;
    friend class MainButton;
    float buttonSoundCooldown{0.f};

    void drawVersionInfo();
    void drawMainButton();
    void drawLogoImage(const McRect &mainButtonRect);
    void drawFriend(const McRect &mainButtonRect, float pulse, bool haveTimingpoints);
    std::pair<bool, float> getTimingpointPulseAmount();  // for main menu cube anim
    void updateLayout();
    void restartMusic();
    // the menu plays its picks from the start, over the song browser's preview rule (the first one from its restart
    // point with start_first_main_menu_song_at_preview_point)
    void playPickFromStart();

    void animMainButton();
    void animMainButtonBack();

    void setMenuElementsVisible(bool visible, bool animate = true);

    void writeVersionFile();
    void onPlayButtonPressed();
    void onMultiplayerButtonPressed();
    void onOptionsButtonPressed();
    void onSaveOrExitButtonPressed();
    void onOnlineBeatmapsButtonPressed();

    void onUpdatePressed();
    void onVersionPressed();

    float updateStatusTime;
    float updateButtonTextTime;
    float updateButtonAnimTime;
    AnimFloat updateButtonAnim;
    bool hasClickedUpdate;

    vec2 vSize{0.f};
    vec2 vCenter{0.f};
    AnimFloat sizeAddAnim;
    AnimFloat centerOffsetAnim;

    bool menuElementsVisible;
    float mainMenuButtonCloseTime{0.f};

    CubeButton *cube;
    std::vector<MainButton *> menuElements;

    neomod::mainmenu::NowPlaying *nowPlaying;
    neomod::mainmenu::WrappedText *tipLabel{nullptr};
    std::unique_ptr<UIButton> updateAvailableButton{nullptr};
    UIButtonVertical *onlineBeatmapsButton{nullptr};
    CBaseUIButton *versionButton;

    void onAdblockChangeCallback(float value);
    UIButtonWithIcon *discordButton{nullptr};
    UIButtonWithIcon *twitterButton{nullptr};

    bool setToggleableVisibilitiesOnce{false};

    bool drawVersionNotificationArrow;
    bool didUserUpdateFromOlderVersion;

    // custom
    float menuAnimTime;
    float menuAnimDuration;
    AnimFloat menuAnim;
    AnimFloat menuAnim1;
    AnimFloat menuAnim2;
    AnimFloat menuAnim3;
    float menuAnim1Target;
    float menuAnim2Target;
    float menuAnim3Target;
    bool inRandomAnim;
    int randomAnimType;
    unsigned int animBeatCounter;

    bool friendAnimEnabled;
    bool shouldFadeToFriendForNextAnim;
    bool friendAnimScheduled;
    float friendAnimPercent;
    AnimVec2 mainMenuAnimFriendEyeFollow;

    float shutdownScheduledTime;
    bool wasCleanShutdown;

    bool isStartupAnim{true};
    AnimFloat startupAnim;
    AnimFloat startupAnim2;
    float prevShuffleTime{0.f};

    Downloader::DownloadHandle serverIconDL;
    const Image *logoImg;

    const DatabaseBeatmap *currentMap{nullptr};
    const DatabaseBeatmap *lastMap{nullptr};
    AnimFloat mapFadeAnim{1.f};
    std::vector<std::unique_ptr<BeatmapSet>> preloadedMaps;
    // the preloaded difficulties that played before the current one, for going back while the database isn't loaded
    std::vector<BeatmapDifficulty *> previousPreloadedMaps;
    bool firstPick{true};

    // songs folder enumeration (for random beatmap before db loads)
    void submitSongsFolderEnum();
    Async::CancellableHandle<std::vector<std::string>> songsFolderHandle;
    std::vector<std::string> songsFolderEntries;
    std::string songsFolderPath;
};
