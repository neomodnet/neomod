#pragma once
// Copyright (c) 2016, PG & 2023-2025, kiwec & 2025, WH, All rights reserved.
#include "types.h"

#include "AnimationHandler.h"
#include "AsyncCancellable.h"
#include "AsyncSongButtonMatcher.h"
#include "CarouselData.h"
#include "Registration.h"
#include "MapFetcher.h"
#include "MD5Hash.h"
#include "MouseListener.h"
#include "ScreenBackable.h"
#include "StrainGraph.h"

#include <memory>
#include <optional>
#include <array>

class BeatmapCarousel;
class Database;
struct ReconcileResult;
class DatabaseBeatmap;
typedef DatabaseBeatmap BeatmapDifficulty;
typedef DatabaseBeatmap BeatmapSet;
class SkinImage;

class UIContextMenu;
class UISearchOverlay;
class InfoLabel;
class ScoreButton;

class CBaseUIContainer;
class CBaseUIImageButton;
class CBaseUIScrollView;
class CBaseUIButton;
class CBaseUILabel;

class McFont;
class ConVar;
struct FinishedScore;

namespace SortTypes {
enum type : i8 { ARTIST, BPM, CREATOR, DATEADDED, DIFFICULTY, LENGTH, TITLE, RANKACHIEVED, DATEPLAYED, MAX };
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
class SongBrowser final : public ScreenBackable {
    NOCOPY_NOMOVE(SongBrowser)
   private:
    struct GlobalSongBrowserCtorDtor {
        NOCOPY_NOMOVE(GlobalSongBrowserCtorDtor)
       public:
        GlobalSongBrowserCtorDtor() = delete;
        GlobalSongBrowserCtorDtor(SongBrowser *sbrptrr);
        ~GlobalSongBrowserCtorDtor();
    };

    GlobalSongBrowserCtorDtor global_songbrowser_;

    friend BeatmapCarousel;
    friend ScoreButton;

   public:
    // group indices (into CarouselData::groups) for one grouping, in display order
    using GroupList = std::vector<u32>;

    using SortType = SortTypes::type;
    using GroupType = GroupTypes::type;

    static f32 getUIScale();
    static i32 getUIScale(f32 m) { return (i32)(m * getUIScale()); }
    static f32 getSkinScale(const SkinImage &img);
    static vec2 getSkinDimensions(const SkinImage &img);

    SongBrowser();
    ~SongBrowser() override;

    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    // screen-wide wheel fallback, forwarded to the carousel (osu-stable behavior); hovered
    // scroll surfaces win first, and an empty/fits carousel declines down to the volume sink
    bool onWheel(WheelDelta vertical, WheelDelta horizontal) override;

    void onKeyDown(KeyboardEvent &e) override;
    void onKeyUp(KeyboardEvent &e) override;
    // typing searches
    [[nodiscard]] CBaseUIElement *getTextTarget(CBaseUIElement *focus) override;

    void onResolutionChange(vec2 newResolution) override;

    CBaseUIContainer *setVisible(bool visible) override;

    [[nodiscard]] std::span<CBaseUIElement *const> getAllChildren() const override;

    bool selectBeatmapset(const BeatmapSet *set);
    void selectSelectedBeatmapSongButton();
    void onPlayEnd(bool quit = true);  // called when a beatmap is finished playing (or the player quit)

    void onSelectionChange(CarouselNode node, bool rebuild);
    void onDifficultySelected(DatabaseBeatmap *map, bool play = false);

    void onScoreContextMenu(ScoreButton *scoreButton, int id);

    using SelOpts = CarouselSelOpts;
    void select(CarouselNode node, SelOpts opts = {});
    [[nodiscard]] bool isSelected(CarouselNode node) const;
    // only difficulties can not match the search
    [[nodiscard]] bool isSearchMatch(CarouselNode node) const;
    [[nodiscard]] DatabaseBeatmap *getBeatmap(CarouselNode node) const;
    [[nodiscard]] ScoreGrade getGrade(CarouselNode node) const;
    // a difficulty shown on its own instead of under its selected set (every other row is independent)
    [[nodiscard]] bool isIndependentDiff(CarouselNode node) const;
    [[nodiscard]] inline const CarouselData &getCarouselData() const { return this->data; }
    void openContextMenu(CarouselNode node, vec2 pos);
    void maybeUpdateGrade(u32 diff);
    void maybeUpdateSetGrades(u32 set);

    void highlightScore(const FinishedScore &scoreToHighlight);
    // returns whether there was another song to select
    bool selectRandomBeatmap();
    void playNextRandomBeatmap() {
        this->selectRandomBeatmap();
        this->playSelectedDifficulty();
    }

    // reloads the database and rebuilds everything. full_rescan (F5) also stats every .osu of every set folder
    // instead of trusting unchanged folder mtimes
    void refreshBeatmaps(UIScreen *next_screen, bool full_rescan = false);
    // the carousel follows the db: mirrors what a Database::reconcileFolder did to a set (created, updated in
    // place, removed) onto its buttons, keeping the selection where possible
    void applyReconcile(const ReconcileResult &r);

    void requestNextScrollToSongButtonJumpFix(u32 diff);
    [[nodiscard]] bool isButtonVisible(CarouselNode node) const;
    bool scrollToBestButton();  // returns true if a scroll happened
    void scrollToSongButton(CarouselNode node, bool alignOnTop = false, bool knownVisible = false);
    void rebuildSongButtons();
    void recreateCollectionsButtons();
    void onGotNewLeaderboard(const MD5Hash &lbHash);
    // recomputes the grade every difficulty and set button shows
    void refreshGrades();
    void rebuildScoreButtons();
    void updateSongButtonLayout();

    enum class SetVisibility : u8 {
        HIDDEN,        // 0 visible children
        SINGLE_CHILD,  // 1 visible child (show child directly)
        SHOW_PARENT    // 2+ children or parent selected (show parent+children)
    };

    [[nodiscard]] SetVisibility getSetVisibility(u32 set) const;

    [[nodiscard]] inline bool isInSearch() const { return this->bInSearch; }
    [[nodiscard]] inline bool isRightClickScrolling() const { return this->bSongBrowserRightClickScrolling; }

    [[nodiscard]] inline InfoLabel *getInfoLabel() const { return this->songInfo; }
    [[nodiscard]] CarouselNode getDiffNodeByHash(const MD5Hash &diff_hash) const;
    [[nodiscard]] inline bool isCarouselBuilt() const { return !this->setOrder.empty(); }

    // Map sorting/filtering
    struct MAP_SORTING_METHOD {
        std::string_view name;
    };

    std::array<MAP_SORTING_METHOD, SortType::MAX> MAP_SORTING_METHODS;
    std::array<std::string_view, GroupType::MAX> GROUP_NAMES;

    // Score sorting
    struct SCORE_SORTING_METHOD {
        using SCORE_SORTING_COMPARATOR = bool (*)(const FinishedScore &, const FinishedScore &);

        std::string_view name;
        SCORE_SORTING_COMPARATOR comparator;
    };

    static constexpr int NUM_SCORE_SORTING_METHODS{6};
    static constexpr int DEFAULT_SCORE_SORTING_INDEX{NUM_SCORE_SORTING_METHODS - 1};
    std::array<SCORE_SORTING_METHOD, NUM_SCORE_SORTING_METHODS> SCORE_SORTING_METHODS;
    // the one songbrowser_scores_sortingtype picks (the default one for an invalid value)
    [[nodiscard]] const SCORE_SORTING_METHOD &getScoreSortingMethod() const;

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

    void updateSearchIndex(f64 budget);
    void checkHandleKillBackgroundSearchMatcher();

    void onSearchUpdate();
    void submitSearch();
    void rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(bool scrollToTop,
                                                                       bool doRebuildSongButtons = true);

    void onFilterScoresClicked(CBaseUIButton *button);
    static constexpr int LOGIN_STATE_FILTER_ID{100};
    void onFilterScoresChange(std::string_view text, int id = -1);
    void onSortScoresClicked(CBaseUIButton *button);
    void onSortScoresChange(std::string_view text, int id = -1);
    void onWebClicked(CBaseUIButton *button);

    void onQuickGroupClicked(CBaseUIButton *button, MouseButtonFlags buttons = MouseButtonFlags::MF_LEFT);
    void onGroupClicked(CBaseUIButton *button);
    void onGroupChange(std::string_view text, int id = -1);

    void onSortClicked(CBaseUIButton *button);
    void onSortChange(std::string_view text, int id = -1);

    void rebuildAfterGroupOrSortChange(GroupType group, const std::optional<SortType> &sortMethod = std::nullopt);
    void rebucketDifficultyCollections();

    void onSelectionMode();
    void onSelectionMods();
    void onSelectionRandom();
    void onSelectionOptions();

    void onScoreClicked(ScoreButton *button);

    void selectSongButton(CarouselNode node);
    // returns whether there was a previous random beatmap to go back to
    bool selectPreviousRandomBeatmap();
    void playSelectedDifficulty();

    // TODO: make more stuff private
   private:
    void initializeGroupingButtons();
    // shows the scores rebuildScoreButtons() collected once the pp the buttons show is known (polled from tick())
    void buildScoreButtons();
    void onDatabaseLoadingFinished(bool isNextScreenSongBrowser);
    // plays a selected map's music as stable's song select does: the file that was already loaded goes on where it
    // was (also where a quit play paused it) unless it's nearly at its end, a new one starts at its restart point
    void playPreviewMusic(DatabaseBeatmap *map);

    // returns true if we drew anything
    bool drawBeatmapOrMenuBackground();

    void addBeatmapSet(BeatmapSet *beatmap, bool initialSongBrowserLoad = false);
    // unlinks the set's buttons from every container and deletes them (the db side has already happened);
    // if the loaded map was in the set, a random one is selected instead
    void removeBeatmapSet(const BeatmapSet *set);
    // a set the db updated in place (new/changed/removed difficulties): swap its buttons, keep the selection
    void replaceBeatmapSet(const BeatmapSet *old_set, BeatmapSet *new_set);
    void unlinkBeatmapSet(const BeatmapSet *set);
    void rebuildAfterSetChange();

    GroupList *getCollectionButtonsForGroup(GroupType group);

    void setSelected(CarouselNode node, bool selected);
    void onSetSelected(u32 set, bool wasSelected, SelOpts opts);
    void onDiffSelected(u32 diff, bool wasSelected, SelOpts opts);
    bool sortSetChildren(u32 set);
    // sorts the set difficulties and makes the bottom matching one represent the set
    void updateSetRepresentative(u32 set);
    void updateSetRepresentatives();
    void updateGrade(u32 diff);
    [[nodiscard]] f32 getNodeY(CarouselNode node) const;
    [[nodiscard]] std::vector<f32> layoutRows(const std::vector<CarouselNode> &rows) const;

    void openSongContextMenu(vec2 pos);
    void onSongContextMenu(std::string_view text, int id = -1);
    void onAddToCollectionConfirmed(std::string_view text, int id = -1);
    void onCreateNewCollectionConfirmed(std::string_view text, int id = -1);
    void onDeleteBeatmapConfirmed(std::string_view text, int id = -1);
    void onSongContextMenuAction(std::string_view text, int id);
    void onOpenBeatmapFolderClicked();
    void onEditClicked();
    void openGroupContextMenu(vec2 pos);
    void onGroupContextMenu(std::string_view text, int id = -1);
    void onRenameCollectionConfirmed(std::string_view text, int id = -1);
    void onDeleteCollectionConfirmed(std::string_view text, int id = -1);
    void onGroupContextMenuAction(std::string_view text, int id);

    void drawStrainGraphOverlay();
    StrainGraph strainGraph;

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
    std::unique_ptr<CBaseUIContainer> personalBestContainer{nullptr};
    CBaseUILabel *personalBestLabel;
    ScoreButton *personalBestButton = nullptr;
    struct PendingScores;
    std::unique_ptr<PendingScores> pendingScores;

    // song carousel
    std::unique_ptr<BeatmapCarousel> carousel{nullptr};
    // vElements + the manually-managed containers (topbars, score browser, carousel, ...);
    // rebuilt on each getAllChildren() call (not for per-frame use)
    mutable std::vector<CBaseUIElement *> allChildren;
    CarouselNode selectedButton;
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
    CarouselNode selectionPreviousSongButton;
    CarouselNode selectionPreviousSongDiffButton;
    CarouselNode selectionPreviousCollectionButton;
    CarouselNode contextMenuNode;

    // beatmap database
    CarouselData data;
    std::vector<u32> setOrder;

    std::vector<CarouselNode> visibleSongButtons;
    class BeatmapLoadingOverlay;
    friend BeatmapLoadingOverlay;
    UIOverlay *loadingOverlay{nullptr};

    // keeps the database in sync with maps/ while it's loaded (a load scans maps/ itself)
    Mc::Registration mapsWatch;

    // to avoid transitive includes
    struct MD5HashMap;
    std::unique_ptr<MD5HashMap> hashToDiffButton;
    // the selection a database load selects again once it's done
    struct PendingReselect;
    std::unique_ptr<PendingReselect> reselect;

    GroupList titleCollectionButtons;
    GroupList artistCollectionButtons;
    GroupList creatorCollectionButtons;

    GroupList dateaddedCollectionButtons;  // not implemented yet
    GroupList difficultyCollectionButtons;
    GroupList bpmCollectionButtons;
    GroupList lengthCollectionButtons;

    GroupList collectionButtons;

    std::string sLastOsuFolder;

    // keys
    bool bF2Pressed;
    bool bF3Pressed;
    bool bLeft;
    bool bRight;
    bool bRandomBeatmapScheduled;

    // behaviour
    const DatabaseBeatmap *lastSelectedBeatmap{nullptr};
    AnimFloat fPulseAnimation;
    float fBackgroundFadeInTime;
    std::vector<const DatabaseBeatmap *> previousRandomBeatmaps;

    // map auto-download (HACK exposed for ChatLink)
   public:
    i32 map_autodl = 0;
    i32 set_autodl = 0;
    MD5Hash md5_autodl{};

   private:
    // drives the map_autodl/set_autodl intent flags to a terminal verdict
    MapFetcher autodl_fetcher;

    // search
    UISearchOverlay *search;
    std::string sPrevSearchString;
    std::string sPrevHardcodedSearchString;
    bool bSearchScheduled{false};
    bool bInSearch;
    bool bShouldRecountMatchesAfterSearch{true};
    i32 currentVisibleSearchMatches{0};
    std::optional<GroupType> searchPrevGroup{std::nullopt};
    Async::CancellableHandle<std::optional<AsyncSongButtonMatcher::SearchResult>> searchHandle;
    std::shared_ptr<AsyncSongButtonMatcher::SearchIndex> searchIndex{
        std::make_shared<AsyncSongButtonMatcher::SearchIndex>()};
    u32 searchIndexedDiffs{0};
    std::shared_ptr<const AsyncSongButtonMatcher::SearchResult> lastSearch;
};
