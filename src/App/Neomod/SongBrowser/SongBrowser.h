#pragma once
// Copyright (c) 2016, PG & 2023-2025, kiwec & 2025, WH, All rights reserved.
#include "types.h"

#include "AsyncCancellable.h"
#include "AsyncChannel.h"
#include "MapExporter.h"
#include "DownloadHandle.h"
#include "ScreenBackable.h"

#include <memory>
#include <optional>
#include <set>

class BeatmapCarousel;
class Database;
class DatabaseBeatmap;
typedef DatabaseBeatmap BeatmapDifficulty;
typedef DatabaseBeatmap BeatmapSet;
class SkinImage;

class UIContextMenu;
class UISearchOverlay;
class InfoLabel;
class ScoreButton;
class CarouselButton;
class SongButton;
class SongDifficultyButton;
class CollectionButton;

class CBaseUIContainer;
class CBaseUIImageButton;
class CBaseUIScrollView;
class CBaseUIButton;
class CBaseUILabel;

class McFont;
class ConVar;
struct FinishedScore;

#ifndef UTIL_MD5HASH_H

#if defined(__GNUC__) && !defined(__clang__) && (defined(__MINGW32__) || defined(__MINGW64__))
struct MD5Hash;
#else
struct alignas(16) MD5Hash;
#endif

#endif

namespace SortTypes {
enum type : i8 { ARTIST, BPM, CREATOR, DATEADDED, DIFFICULTY, LENGTH, TITLE, RANKACHIEVED, MAX };
};

namespace GroupTypes {
enum type : i8 {
    ARTIST,
    BPM,
    CREATOR,
    DATEADDED,  // unimpl
    DIFFICULTY,
    LENGTH,
    TITLE,
    COLLECTIONS,
    NO_GROUPING,
    MAX
};
};

class SongBrowser;

namespace neomod::sbr {
extern SongBrowser *g_songbrowser;
extern BeatmapCarousel *g_carousel;
}  // namespace neomod::sbr

// TODOs:
// - make more of the stuff in here private and have an actual exposed public API surface
// - move all of the scrolling/button management logic to BeatmapCarousel
// - use a more well-defined data structure for buttons that doesn't require a shitton of confusing redundant work
class SongBrowser final : public ScreenBackable {
    NOCOPY_NOMOVE(SongBrowser)
   private:
    // not used anywhere else
    static bool sort_by_artist(SongButton const *a, SongButton const *b);
    static bool sort_by_bpm(SongButton const *a, SongButton const *b);
    static bool sort_by_creator(SongButton const *a, SongButton const *b);
    static bool sort_by_date_added(SongButton const *a, SongButton const *b);
    static bool sort_by_grade(SongButton const *a, SongButton const *b);
    static bool sort_by_length(SongButton const *a, SongButton const *b);
    static bool sort_by_title(SongButton const *a, SongButton const *b);

    struct GlobalSongBrowserCtorDtor {
        NOCOPY_NOMOVE(GlobalSongBrowserCtorDtor)
       public:
        GlobalSongBrowserCtorDtor() = delete;
        GlobalSongBrowserCtorDtor(SongBrowser *sbrptrr);
        ~GlobalSongBrowserCtorDtor();
    };

    GlobalSongBrowserCtorDtor global_songbrowser_;

    friend BeatmapCarousel;
    friend SongButton;
    friend SongDifficultyButton;
    friend CollectionButton;
    friend CarouselButton;
    friend ScoreButton;

   public:
    using CollBtnContainer = std::vector<std::unique_ptr<CollectionButton>>;

    using SortType = SortTypes::type;
    using GroupType = GroupTypes::type;

    // used also by SongButton
    static bool sort_by_difficulty(SongButton const *a, SongButton const *b);

    static f32 getUIScale();
    static i32 getUIScale(f32 m) { return (i32)(m * getUIScale()); }
    static f32 getSkinScale(const SkinImage *img);
    static vec2 getSkinDimensions(const SkinImage *img);

    SongBrowser();
    ~SongBrowser() override;

    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    void onResolutionChange(vec2 newResolution) override;

    CBaseUIContainer *setVisible(bool visible) override;

    bool selectBeatmapset(i32 set_id);
    void selectSelectedBeatmapSongButton();
    void onPlayEnd(bool quit = true);  // called when a beatmap is finished playing (or the player quit)

    void onSelectionChange(CarouselButton *button, bool rebuild);
    void onDifficultySelected(DatabaseBeatmap *map, bool play = false);

    void onScoreContextMenu(ScoreButton *scoreButton, int id);
    void onSongButtonContextMenu(SongButton *songButton, const UString &text, int id);
    void onCollectionButtonContextMenu(CollectionButton *collectionButton, const UString &text, int id);

    void highlightScore(const FinishedScore &scoreToHighlight);
    void selectRandomBeatmap();
    void playNextRandomBeatmap() {
        this->selectRandomBeatmap();
        this->playSelectedDifficulty();
    }

    void refreshBeatmaps();
    void refreshBeatmaps(UIScreen *next_screen);
    void addBeatmapSet(BeatmapSet *beatmap, bool initialSongBrowserLoad = false);
    void addSongButtonToAlphanumericGroup(SongButton *btn, CollBtnContainer &group, std::string_view name);

    void requestNextScrollToSongButtonJumpFix(SongDifficultyButton *diffButton);
    [[nodiscard]] bool isButtonVisible(CarouselButton *songButton) const;
    bool scrollToBestButton();  // returns true if a scroll happened
    void scrollToSongButton(CarouselButton *songButton, bool alignOnTop = false, bool knownVisible = false);
    void rebuildSongButtons();
    void recreateCollectionsButtons();
    void onGotNewLeaderboard(const MD5Hash &lbHash);
    void updateSongButtonLayout();

    enum class SetVisibility : u8 {
        HIDDEN,        // 0 visible children
        SINGLE_CHILD,  // 1 visible child (show child directly)
        SHOW_PARENT    // 2+ children or parent selected (show parent+children)
    };

    [[nodiscard]] SetVisibility getSetVisibility(const SongButton *parent) const;

    [[nodiscard]] inline const CollBtnContainer &getCollectionButtons() const { return this->collectionButtons; }

    [[nodiscard]] inline bool isInSearch() const { return this->bInSearch; }
    [[nodiscard]] inline bool isRightClickScrolling() const { return this->bSongBrowserRightClickScrolling; }

    [[nodiscard]] inline InfoLabel *getInfoLabel() const { return this->songInfo; }
    [[nodiscard]] SongDifficultyButton *getDiffButtonByHash(const MD5Hash &diff_hash) const;

    using SORTING_COMPARATOR = bool (*)(const SongButton *a, const SongButton *b);
    struct SORTING_METHOD {
        std::string_view name;
        SORTING_COMPARATOR comparator;
    };

    static constexpr std::array<SORTING_METHOD, SortType::MAX> SORTING_METHODS{
        {{"By Artist", sort_by_artist},          //
         {"By BPM", sort_by_bpm},                //
         {"By Creator", sort_by_creator},        //
         {"By Date Added", sort_by_date_added},  //
         {"By Difficulty", sort_by_difficulty},  //
         {"By Length", sort_by_length},          //
         {"By Title", sort_by_title},            //
         {"By Rank Achieved", sort_by_grade}}};  //

    static constexpr std::array<std::string_view, GroupType::MAX> GROUP_NAMES{{"By Artist",      //
                                                                               "By BPM",         //
                                                                               "By Creator",     //
                                                                               "By Date",        //
                                                                               "By Difficulty",  //
                                                                               "By Length",      //
                                                                               "By Title",       //
                                                                               "Collections",    //
                                                                               "No Grouping"}};  //

    [[nodiscard]] inline GroupType getGroupingMode() const { return this->curGroup; }

    [[nodiscard]] inline bool isInAlphanumericCollection() const {
        return this->curGroup == GroupType::ARTIST ||   //
               this->curGroup == GroupType::CREATOR ||  //
               this->curGroup == GroupType::TITLE;      //
    }

    // TODO: get rid of this garbage (all grouping modes should behave in a predictable way)
    [[nodiscard]] inline bool isInParentsCollapsedMode() const {
        return this->isInAlphanumericCollection() || this->curGroup == GroupType::NO_GROUPING;
    }

    void updateLayout() override;
    void onBack() override;

    void updateScoreBrowserLayout();

    void scheduleSearchUpdate(bool immediately = false);
    void checkHandleKillBackgroundSearchMatcher();

    void initializeGroupingButtons();
    void onDatabaseLoadingFinished();

    void onSearchUpdate();
    void rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(bool scrollToTop,
                                                                       bool doRebuildSongButtons = true);

    void onFilterScoresClicked(CBaseUIButton *button);
    static constexpr int LOGIN_STATE_FILTER_ID{100};
    void onFilterScoresChange(const UString &text, int id = -1);
    void onSortScoresClicked(CBaseUIButton *button);
    void onSortScoresChange(const UString &text, int id = -1);
    void onWebClicked(CBaseUIButton *button);

    void onQuickGroupClicked(CBaseUIButton *button, bool left = true, bool right = false);
    void onGroupClicked(CBaseUIButton *button);
    void onGroupChange(const UString &text, int id = -1);

    void onSortClicked(CBaseUIButton *button);
    void onSortChange(const UString &text, int id = -1);

    void rebuildAfterGroupOrSortChange(GroupType group, const std::optional<SortType> &sortMethod = std::nullopt);
    void rebucketDifficultyCollections();

    void onSelectionMode();
    void onSelectionMods();
    void onSelectionRandom();
    void onSelectionOptions();

    void onScoreClicked(ScoreButton *button);

    void selectSongButton(CarouselButton *songButton);
    void selectPreviousRandomBeatmap();
    void playSelectedDifficulty();

    // TODO: make more stuff private
   private:
    void rebuildScoreButtons();
    CollBtnContainer *getCollectionButtonsForGroup(GroupType group);

    GroupType curGroup{GroupType::NO_GROUPING};
    SortType curSortMethod{SortType::ARTIST};
    u8 lastDiffSortModIndex;

    // top bar left
    CBaseUIContainer *topbarLeft;
    InfoLabel *songInfo;
    CBaseUIButton *filterScoresDropdown;
    CBaseUIButton *sortScoresDropdown;
    CBaseUIButton *webButton;

    // top bar right
    CBaseUIContainer *topbarRight;
    CBaseUILabel *groupLabel;
    CBaseUIButton *groupButton;
    CBaseUILabel *sortLabel;
    CBaseUIButton *sortButton;
    UIContextMenu *contextMenu;

    CBaseUIButton *groupByCollectionBtn;
    CBaseUIButton *groupByArtistBtn;
    CBaseUIButton *groupByDifficultyBtn;
    CBaseUIButton *groupByNothingBtn;

    // score browser
    std::vector<ScoreButton *> scoreButtonCache;
    CBaseUIScrollView *scoreBrowser;
    CBaseUIElement *scoreBrowserScoresStillLoadingElement;
    CBaseUIElement *scoreBrowserNoRecordsSetElement;
    std::unique_ptr<CBaseUIContainer> localBestContainer{nullptr};
    CBaseUILabel *localBestLabel;
    ScoreButton *localBestButton = nullptr;
    bool score_resort_scheduled = false;

    // song carousel
    std::unique_ptr<BeatmapCarousel> carousel{nullptr};
    CarouselButton *selectedButton = nullptr;
    bool bSongBrowserRightClickScrollCheck;
    bool bSongBrowserRightClickScrolling;
    bool bNextScrollToSongButtonJumpFixScheduled;
    bool bNextScrollToSongButtonJumpFixUseScrollSizeDelta;
    bool scheduled_scroll_to_selected_button = false;
    bool bInitializedBeatmaps{false};
    bool bSongButtonsNeedSorting{false};
    float fNextScrollToSongButtonJumpFixOldRelPosY;
    float fNextScrollToSongButtonJumpFixOldScrollSizeY;

   public:
    f32 thumbnailYRatio = 0.f;

   private:
    // song browser selection state logic
    SongButton *selectionPreviousSongButton;
    SongDifficultyButton *selectionPreviousSongDiffButton;
    CollectionButton *selectionPreviousCollectionButton;

    // beatmap database
   public:
    std::vector<SongButton *> parentButtons;

   private:
    std::vector<CarouselButton *> visibleSongButtons;

    UIOverlay *loadingOverlay{nullptr};

    // to avoid transitive includes
    struct MD5HashMap;
    std::unique_ptr<MD5HashMap> hashToDiffButton;

    CollBtnContainer titleCollectionButtons;
    CollBtnContainer artistCollectionButtons;
    CollBtnContainer creatorCollectionButtons;

    CollBtnContainer dateaddedCollectionButtons;  // not implemented yet
    CollBtnContainer difficultyCollectionButtons;
    CollBtnContainer bpmCollectionButtons;
    CollBtnContainer lengthCollectionButtons;

    CollBtnContainer collectionButtons;

    UString sLastOsuFolder;

    // keys
    bool bF1Pressed;
    bool bF2Pressed;
    bool bF3Pressed;
    bool bShiftPressed;
    bool bLeft;
    bool bRight;
    bool bRandomBeatmapScheduled;

    // behaviour
    const DatabaseBeatmap *lastSelectedBeatmap{nullptr};
    float fPulseAnimation;
    float fBackgroundFadeInTime;
    std::vector<const DatabaseBeatmap *> previousRandomBeatmaps;

    // map auto-download
   public:
    i32 map_autodl = 0;
    i32 set_autodl = 0;
    Downloader::DownloadHandle map_dl;
    Downloader::DownloadHandle set_dl;

   private:
    // search
    UISearchOverlay *search;
    std::string sSearchString;
    std::string sPrevSearchString;
    std::string sPrevHardcodedSearchString;
    float fSearchWaitTime;
    bool bInSearch;
    bool bShouldRecountMatchesAfterSearch{true};
    i32 currentVisibleSearchMatches{0};
    std::optional<GroupType> searchPrevGroup{std::nullopt};
    Async::CancellableHandle<void> searchHandle;

    // map export
    void startExport(MapExporter::ExportContext ctx);
    // channel before handle so handle's destructor runs first
    Async::Channel<MapExporter::Notification> exportNotifications;
    Async::CancellableHandle<void> exportHandle;
    std::set<MapExporter::ExportContext> pendingExports;
};
