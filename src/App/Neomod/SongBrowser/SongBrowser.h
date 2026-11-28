#pragma once
// Copyright (c) 2016, PG & 2023-2025, kiwec & 2025, WH, All rights reserved.
#include "types.h"

#include "ScreenBackable.h"
#include "StaticPImpl.h"

#include <span>
#include <string_view>
#include <vector>

struct ReconcileResult;
class DatabaseBeatmap;
typedef DatabaseBeatmap BeatmapSet;
class SkinImage;
class ScoreButton;
struct FinishedScore;
struct MD5Hash;
enum class ScoreGrade : u8;
struct CarouselNode;
struct CarouselData;
struct SongBrowserImpl;

class SongBrowser;

namespace neomod::sbr {
extern SongBrowser *g_songbrowser;
}  // namespace neomod::sbr

class SongBrowser final : public ScreenBackable {
    NOCOPY_NOMOVE(SongBrowser)
   public:
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

    void onDifficultySelected(DatabaseBeatmap *map, bool play = false);

    void onScoreContextMenu(ScoreButton *scoreButton, int id);

    void select(CarouselNode node);
    [[nodiscard]] bool isSelected(CarouselNode node) const;
    [[nodiscard]] DatabaseBeatmap *getBeatmap(CarouselNode node) const;
    [[nodiscard]] ScoreGrade getGrade(CarouselNode node) const;
    // a difficulty shown on its own instead of as one of its selected set (every other row is independent)
    [[nodiscard]] bool isIndependentDiff(CarouselNode node) const;
    [[nodiscard]] const CarouselData &getCarouselData() const;
    void openContextMenu(CarouselNode node, vec2 pos);
    void maybeUpdateGrade(u32 diff);
    void maybeUpdateSetGrades(u32 set);

    void highlightScore(const FinishedScore &scoreToHighlight);
    // returns whether there was another song to select
    bool selectRandomBeatmap();
    // returns whether there was a previous random beatmap to go back to
    bool selectPreviousRandomBeatmap();
    void playNextRandomBeatmap();
    void selectSongButton(CarouselNode node);
    // selects a difficulty by id or md5 (or a set by id without them), downloading it first if it isn't installed
    void selectOrDownload(i32 set_id, i32 map_id, const MD5Hash &md5);

    // reloads the database and rebuilds everything. full_rescan (F5) also stats every .osu of every set folder
    // instead of trusting unchanged folder mtimes
    void refreshBeatmaps(UIScreen *next_screen, bool full_rescan = false);
    // the carousel follows the db: mirrors what a Database::reconcileFolder did to a set (created, updated in
    // place, removed) onto its buttons, keeping the selection where possible
    void applyReconcile(const ReconcileResult &r);

    void onGotNewLeaderboard(const MD5Hash &lbHash);
    void rebuildScoreButtons();

    // the shown difficulties of the same set next to diff, including it (a set row is these collapsed)
    [[nodiscard]] std::span<const u32> getNeighbours(u32 diff) const;
    [[nodiscard]] const std::vector<CarouselNode> &getRows() const;
    [[nodiscard]] CarouselNode getDiffNodeByHash(const MD5Hash &diff_hash) const;
    [[nodiscard]] bool isCarouselBuilt() const;

    using ScoreComparator = bool (*)(const FinishedScore &, const FinishedScore &);
    // the one songbrowser_scores_sortingtype picks (the default one for an invalid value)
    [[nodiscard]] ScoreComparator getScoreComparator() const;

    static constexpr int LOGIN_STATE_FILTER_ID{100};
    void onFilterScoresChange(std::string_view text, int id = -1);

    void onSelectionMode();
    void onSelectionMods();
    void onSelectionRandom();
    void onSelectionOptions();

   private:
    void updateLayout() override;
    void onBack() override;

    friend struct SongBrowserImpl;
    StaticPImpl<SongBrowserImpl, 2560> pImpl;
};
