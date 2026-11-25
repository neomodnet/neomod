// Copyright (c) 2016, PG & 2023-2025, kiwec & 2025, WH, All rights reserved.
#include "SongBrowser.h"

#include "Engine.h"
#include "Environment.h"
#include "Paths.h"
#include "i18n.h"
#include "Logging.h"
#include "ResourceManager.h"
#include "AnimationHandler.h"
#include "DirectoryWatcher.h"
#include "Mouse.h"
#include "Keyboard.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "Font.h"
#include "File.h"
#include "Timing.h"
#include "UIButtonRounded.h"
#include "UniString.h"
#include "crypto.h"
#include "SString.h"
#include "MakeDelegateWrapper.h"
#include "Icons.h"
#include "SoundEngine.h"
#include "Sound.h"
#include "Graphics.h"

#include "Osu.h"
#include "MusicTrack.h"
#include "OsuConVars.h"

#include "BatchDiffCalc.h"
#include "AsyncPPCalculator.h"
#include "VolNormalization.h"

#include "Skin.h"
#include "SkinImage.h"
#include "BeatmapInstaller.h"
#include "Database.h"
#include "DatabaseBeatmap.h"
#include "Collections.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoLeaderboard.h"
#include "BanchoSubmitter.h"
#include "RichPresence.h"
#include "MapExporter.h"

#include "HUD.h"
#include "OptionsOverlay.h"
#include "NotificationOverlay.h"
#include "BeatmapInterface.h"
#include "ModSelector.h"
#include "BackgroundImageHandler.h"
#include "MainMenu.h"
#include "RankingScreen.h"
#include "CBaseUILabel.h"
#include "BeatmapCarousel.h"
#include "CarouselButton.h"
#include "AsyncSongButtonMatcher.h"
#include "UserCard.h"
#include "InfoLabel.h"
#include "LoadingScreen.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "UISearchOverlay.h"
#include "EditorScreen.h"
#include "ScoreButton.h"
#include "BottomBar.h"
#include "RoomScreen.h"
#include "Chat.h"
#include "ContainerRanges.h"
#include "NeomodEnvInterop.h"
#include "OsuKeyBinds.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <charconv>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <utility>

#include "fmt/chrono.h"

struct SongBrowser::MD5HashMap : public Hash::flat::map<MD5Hash, u32> {};
struct SongBrowser::PendingReselect {
    Database::MapLocation where;
    MD5Hash md5;
};

namespace {
constexpr const Color highlightColor = argb(255, 0, 255, 0);
constexpr const Color defaultColor = argb(255, 255, 255, 255);
}  // namespace

// XXX: remove this
f32 SongBrowser::getUIScale() {
    auto screen = osu->getVirtScreenSize();
    bool is_widescreen = (screen.x / screen.y) > (4.f / 3.f);
    return screen.x / (is_widescreen ? 1366.f : 1024.f);
}

// Because we draw skin elements 'manually' to enforce the correct scaling,
// this helper function automatically adjusts for 2x image resolution.
f32 SongBrowser::getSkinScale(const SkinImage &img) {
    return SongBrowser::getUIScale() / img.getImageForCurrentFrame().scale;
}

// Stable sizes the bottombar button hitboxes from the image dimensions, and skins rely on
// it: wide left-padded images shift the visible button art right and expect the clickable
// area to follow (overlaps are resolved by the hit-test order in BottomBar::updateInput()).
// Only the height is clamped to the element's base size, so screen-covering overlay-art
// images (fake top bar trick) can't push the hitbox over the whole screen.
vec2 SongBrowser::getSkinDimensions(const SkinImage &img) {
    vec2 dims = img.getImageSizeForCurrentFrame() * SongBrowser::getSkinScale(img);
    dims.y = std::min(dims.y, img.getSizeBaseRawForScaling2x().y * SongBrowser::getUIScale());
    return dims;
}

namespace {
// a label with an icon on its left, for score list placeholders
class IconLabel final : public CBaseUILabel {
   public:
    IconLabel(char32_t icon, std::string text, float textScale)
        : CBaseUILabel(0, 0, 0, 0, "", std::move(text)),
          sIconString(UniString::to_utf8(std::u32string_view{&icon, 1})),
          fTextScale(textScale) {}

   protected:
    void drawText() override {
        // draw icon
        const float iconScale = 0.6f;
        McFont *iconFont = osu->getFontIcons();
        f32 iconWidth = 0;
        g->pushTransform();
        {
            const float scale = (this->getSize().y / iconFont->getHeight()) * iconScale;
            const float paddingLeft = scale * 15;

            iconWidth = paddingLeft + iconFont->getStringWidth(this->sIconString) * scale;

            g->scale(scale, scale);
            g->translate((f32)(i32)(this->getPos().x + paddingLeft),
                         (f32)(i32)(this->getPos().y + this->getSize().y / 2 + iconFont->getHeight() * scale / 2));
            g->setColor(0xffffffff);
            g->drawString(iconFont, this->sIconString);
        }
        g->popTransform();

        // draw text
        McFont *textFont = osu->getSongBrowserFont();
        g->pushTransform();
        {
            const float stringWidth = textFont->getStringWidth(this->sText);

            const float scale = ((this->getSize().x - iconWidth) / stringWidth) * this->fTextScale;

            g->scale(scale, scale);
            g->translate((f32)(i32)(this->getPos().x + iconWidth + (this->getSize().x - iconWidth) / 2 -
                                    stringWidth * scale / 2),
                         (f32)(i32)(this->getPos().y + this->getSize().y / 2 + textFont->getHeight() * scale / 2));
            g->setColor(0xff02c3e5);
            g->drawString(textFont, this->sText);
        }
        g->popTransform();
    }

   private:
    std::string sIconString;
    float fTextScale;
};

}  // namespace

namespace {
struct SortString {
    u64 headBytes{0};
    u32 offset{0}, len{0};
};

struct SortKey {
    SortString str1, str2;
    i64 num{0};
    f32 stars{0.f};
    f32 diff{0.f};
    bool missing{false};
};

struct SortKeys {
    std::string strings;
    std::vector<SortKey> keys;

    void add(const DatabaseBeatmap *map, ScoreGrade grade, SortTypes::type method) {
        SortKey &key = this->keys.emplace_back();
        if(map == nullptr) {
            key.missing = true;
            return;
        }

        key.stars = map->getStarRating(StarPrecalc::active_idx);
        key.diff = (map->getAR() + 1) * (map->getCS() + 1) * (map->getHP() + 1) * (map->getOD() + 1) *
                   ((f32)std::max(map->getMostCommonBPM(), 1));
        switch(method) {
            using enum SortTypes::type;
            case ARTIST:
                key.str1 = this->addString(map->getArtistLatin());
                key.str2 = this->addString(map->getTitleLatin());
                break;
            case BPM:
                key.num = map->getMostCommonBPM();
                break;
            case CREATOR:
                key.str1 = this->addString(map->getCreator());
                break;
            case DATEADDED:
                key.num = -map->last_modification_time;
                break;
            case DIFFICULTY:
            case MAX:
                break;
            case LENGTH:
                key.num = map->getLengthMS();
                break;
            case TITLE:
                key.str1 = this->addString(map->getTitleLatin());
                break;
            case RANKACHIEVED:
                key.num = (i64)grade;
                break;
            case DATEPLAYED:
                key.num = -map->getLastPlayTime();
                break;
        }
    }

    SortString addString(std::string_view str) {
        SortString out{.headBytes = 0, .offset = (u32)this->strings.size(), .len = (u32)str.size()};
        this->strings.append(str);
        for(size_t i = out.offset; i < this->strings.size(); i++) {
            this->strings[i] = (char)SString::ascii_tolower((unsigned char)this->strings[i]);
        }
        for(u32 i = 0; i < 8; i++) {
            out.headBytes <<= 8;
            if(i < out.len) out.headBytes |= (u8)this->strings[out.offset + i];
        }
        return out;
    }

    [[nodiscard]] int compare(SortString a, SortString b) const {
        if(a.headBytes != b.headBytes) return a.headBytes < b.headBytes ? -1 : 1;
        const int cmp =
            std::memcmp(this->strings.data() + a.offset, this->strings.data() + b.offset, std::min(a.len, b.len));
        if(cmp != 0) return cmp;
        return a.len == b.len ? 0 : (a.len < b.len ? -1 : 1);
    }

    [[nodiscard]] bool less(u32 a, u32 b) const {
        const SortKey &ka = this->keys[a];
        const SortKey &kb = this->keys[b];
        if(ka.missing || kb.missing) return ka.missing < kb.missing;
        if(const int cmp = this->compare(ka.str1, kb.str1); cmp != 0) return cmp < 0;
        if(const int cmp = this->compare(ka.str2, kb.str2); cmp != 0) return cmp < 0;
        if(ka.num != kb.num) return ka.num < kb.num;
        if(ka.stars != kb.stars) return ka.stars < kb.stars;
        if(ka.diff == kb.diff) return false;
        return ka.diff < kb.diff;
    }
};

template <typename T, typename KeyOf>
void sort_by_keys(std::vector<T> &items, SortTypes::type method, KeyOf keyOf) {
    SortKeys keys;
    keys.keys.reserve(items.size());
    for(const T &item : items) {
        const auto [map, grade] = keyOf(item);
        keys.add(map, grade, method);
    }

    std::vector<u32> order(items.size());
    for(u32 i = 0; i < order.size(); i++) order[i] = i;
    Mc::ranges::sort(order, [&keys](u32 a, u32 b) { return keys.less(a, b); });

    std::vector<T> sorted;
    sorted.reserve(items.size());
    for(const u32 i : order) sorted.push_back(items[i]);
    items = std::move(sorted);
}
}  // namespace

namespace neomod::sbr {
SongBrowser *g_songbrowser{nullptr};
BeatmapCarousel *g_carousel{nullptr};
}  // namespace neomod::sbr

SongBrowser::GlobalSongBrowserCtorDtor::GlobalSongBrowserCtorDtor(SongBrowser *sbrptr) {
    neomod::sbr::g_songbrowser = sbrptr;
}

SongBrowser::GlobalSongBrowserCtorDtor::~GlobalSongBrowserCtorDtor() {
    neomod::sbr::g_songbrowser = nullptr;
    neomod::sbr::g_carousel = nullptr;
}

// a leaderboard waiting for buildScoreButtons()
struct SongBrowser::PendingScores {
    const DatabaseBeatmap *map{nullptr};
    OnlineLeaderboard lb;  // local leaderboards only fill the scores
    // online leaderboards show the player's best on the server in a box below the list
    bool personal_best_box{false};
};

SongBrowser::SongBrowser() : ScreenBackable(), global_songbrowser_(this) {
    this->MAP_SORTING_METHODS = {{{_("By Artist")},         //
                                  {_("By BPM")},            //
                                  {_("By Creator")},        //
                                  {_("By Date Added")},     //
                                  {_("By Difficulty")},     //
                                  {_("By Length")},         //
                                  {_("By Title")},          //
                                  {_("By Rank Achieved")},  //
                                  {_("By Date Played")}}};
    this->GROUP_NAMES = {{_("By Artist"),      //
                          _("By BPM"),         //
                          _("By Creator"),     //
                          _("By Date"),        //
                          _("By Difficulty"),  //
                          _("By Length"),      //
                          _("By Title"),       //
                          _("Collections"),    //
                          _("No Grouping")}};  //
    this->SCORE_SORTING_METHODS = {{{_("By accuracy"), Database::sortScoreByAccuracy},
                                    {_("By combo"), Database::sortScoreByCombo},
                                    {_("By date"), Database::sortScoreByDate},
                                    {_("By misses"), Database::sortScoreByMisses},
                                    {_("By score"), Database::sortScoreByScore},
                                    {_("By pp"), Database::sortScoreByPP}}};

    {
        // remap possible string convars to numeric ids (old configs/defaults)
        auto remap = [](ConVar &cvar, auto &methods) -> void {
            const auto str = cvar.getString();
            for(int i = 0; i < methods.size(); ++i)
                if(str == methods[i].name) return (void)cvar.setValue(i);
        };
        remap(cv::songbrowser_sortingtype, MAP_SORTING_METHODS);
        remap(cv::songbrowser_scores_sortingtype, SCORE_SORTING_METHODS);
    }

    this->lastDiffSortModIndex = StarPrecalc::active_idx;

    this->hashToDiffButton = std::make_unique<MD5HashMap>();

    // build carousel first
    this->carousel = std::make_unique<BeatmapCarousel>(0.f, 0.f, 0.f, 0.f, "Carousel");
    neomod::sbr::g_carousel = this->carousel.get();

    // convar callback
    cv::songbrowser_search_hardcoded_filter.setCallback(
        [](std::string_view /* oldValue */, std::string_view newValue) -> void {
            if(newValue.length() == 1 && SString::is_wspace_only(newValue))
                cv::songbrowser_search_hardcoded_filter.setValue("");
        });
    // they decide whether the score buttons show pp, which buildScoreButtons() may have to wait for
    cv::scores_always_display_pp.setCallback(SA::MakeDelegate<&SongBrowser::rebuildScoreButtons>(this));
    cv::scores_sort_by_pp.setCallback(SA::MakeDelegate<&SongBrowser::rebuildScoreButtons>(this));
    cv::songbrowser_grades_current_name.setCallback(SA::MakeDelegate<&SongBrowser::refreshGrades>(this));
    cv::songbrowser_grades_current_server.setCallback(SA::MakeDelegate<&SongBrowser::refreshGrades>(this));
    cv::name.setCallback(SA::MakeDelegate<&SongBrowser::refreshGrades>(this));

    // vars
    this->bSongBrowserRightClickScrollCheck = false;
    this->bSongBrowserRightClickScrolling = false;
    this->bNextScrollToSongButtonJumpFixScheduled = false;
    this->bNextScrollToSongButtonJumpFixUseScrollSizeDelta = false;
    this->fNextScrollToSongButtonJumpFixOldScrollSizeY = 0.0f;
    this->fNextScrollToSongButtonJumpFixOldRelPosY = 0.0f;

    this->bF2Pressed = false;
    this->bF3Pressed = false;
    this->bLeft = false;
    this->bRight = false;

    this->bRandomBeatmapScheduled = false;

    // build topbar left
    this->topbarLeft = new CBaseUIContainer(0, 0, 0, 0, "");
    this->songInfo = new InfoLabel(0, 0, 0, 0, "");
    this->topbarLeft->addBaseUIElement(this->songInfo);

    this->filterScoresDropdown = new UIButtonRounded(0, 0, 0, 0, "", _("Local"), 5);
    this->filterScoresDropdown->setClickCallback(SA::MakeDelegate<&SongBrowser::onFilterScoresClicked>(this));
    this->filterScoresDropdown->setDrawShadow(true)->setDrawBackground(false);
    this->topbarLeft->addBaseUIElement(this->filterScoresDropdown);

    this->sortScoresDropdown = new UIButtonRounded(0, 0, 0, 0, "", _("By score"), 5);
    this->sortScoresDropdown->setDrawShadow(true)->setDrawBackground(false);
    this->sortScoresDropdown->setClickCallback(SA::MakeDelegate<&SongBrowser::onSortScoresClicked>(this));
    this->topbarLeft->addBaseUIElement(this->sortScoresDropdown);

    // TRANSLATORS: button that opens the selected beatmap on the website
    this->webButton = new UIButtonRounded(0, 0, 0, 0, "", _("Web"), 5);
    this->webButton->setDrawShadow(true)->setDrawBackground(false);
    this->webButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onWebClicked>(this));
    this->topbarLeft->addBaseUIElement(this->webButton);

    // build topbar right
    this->topbarRight = new CBaseUIContainer(0, 0, 0, 0, "SongBrowser::topbarRight");
    {
        this->groupLabel = (new CBaseUILabel(0, 0, 0, 0, "SongBrowser::groupLabel", ""))
                               ->setFont(osu->getSongBrowserFont())
                               ->setScale(0.75f)
                               ->setTextColor(rgba(200, 200, 255, 255))
                               ->setDrawTextShadow(true)
                               ->setDrawBackground(false)
                               ->setDrawFrame(false)
                               ->setText(_("Group:"))  // setting text later so string metrics get applied...
                               ->setSizeToContent(-1, 0);
        this->groupLabel->setDrawsOnTop(true);
        this->topbarRight->addBaseUIElement(this->groupLabel);

        this->groupButton = new UIButtonRounded(0, 0, 0, 0, "", _("No Grouping"), 5);
        this->groupButton->setDrawShadow(true)->setDrawBackground(false);
        this->groupButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onGroupClicked>(this));
        this->groupButton->setDrawsOnTop(true);
        this->topbarRight->addBaseUIElement(this->groupButton);

        this->sortLabel = (new CBaseUILabel(0, 0, 0, 0, "SongBrowser::sortLabel", ""))
                              ->setFont(osu->getSongBrowserFont())
                              ->setScale(0.75f)
                              ->setTextColor(rgba(225, 255, 225, 255))
                              ->setDrawTextShadow(true)
                              ->setDrawBackground(false)
                              ->setDrawFrame(false)
                              ->setText(_("Sort:"))
                              ->setSizeToContent(-1, 0);
        this->sortLabel->setDrawsOnTop(true);
        this->topbarRight->addBaseUIElement(this->sortLabel);

        this->sortButton = new UIButtonRounded(0, 0, 0, 0, "", _("By Date Added"), 5);
        this->sortButton->setDrawShadow(true)->setDrawBackground(false);
        this->sortButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onSortClicked>(this));
        this->sortButton->setDrawsOnTop(true);
        this->topbarRight->addBaseUIElement(this->sortButton);

        // "hardcoded" grouping tabs
        this->groupByCollectionBtn = new UIButtonRounded(0, 0, 0, 0, "", _("Collections"), 5);
        this->groupByCollectionBtn->setDrawShadow(true)->setDrawBackground(false);
        this->groupByCollectionBtn->setHandleRightMouse(true);
        this->groupByCollectionBtn->setClickCallback(SA::MakeDelegate<&SongBrowser::onQuickGroupClicked>(this));
        this->groupByCollectionBtn->setDrawsOnTop(true);
        this->topbarRight->addBaseUIElement(this->groupByCollectionBtn);
        this->groupByArtistBtn = new UIButtonRounded(0, 0, 0, 0, "", _("By Artist"), 5);
        this->groupByArtistBtn->setDrawShadow(true)->setDrawBackground(false);
        this->groupByArtistBtn->setHandleRightMouse(true);
        this->groupByArtistBtn->setClickCallback(SA::MakeDelegate<&SongBrowser::onQuickGroupClicked>(this));
        this->groupByArtistBtn->setDrawsOnTop(true);
        this->topbarRight->addBaseUIElement(this->groupByArtistBtn);
        this->groupByDifficultyBtn = new UIButtonRounded(0, 0, 0, 0, "", _("By Difficulty"), 5);
        this->groupByDifficultyBtn->setDrawShadow(true)->setDrawBackground(false);
        this->groupByDifficultyBtn->setHandleRightMouse(true);
        this->groupByDifficultyBtn->setClickCallback(SA::MakeDelegate<&SongBrowser::onQuickGroupClicked>(this));
        this->groupByDifficultyBtn->setDrawsOnTop(true);
        this->topbarRight->addBaseUIElement(this->groupByDifficultyBtn);
        this->groupByNothingBtn = new UIButtonRounded(0, 0, 0, 0, "", _("No Grouping"), 5);
        this->groupByNothingBtn->setDrawShadow(true)->setDrawBackground(false);
        this->groupByNothingBtn->setHandleRightMouse(true);
        this->groupByNothingBtn->setClickCallback(SA::MakeDelegate<&SongBrowser::onQuickGroupClicked>(this));
        this->groupByNothingBtn->setDrawsOnTop(true);
        this->topbarRight->addBaseUIElement(this->groupByNothingBtn);
    }

    // context menu
    this->contextMenu = new UIContextMenu(50, 50, 150, 0, "songbrowser_contextmenu");
    this->contextMenu->setVisible(true);

    // build scorebrowser
    this->scoreBrowser = new CBaseUIScrollView(0, 0, 0, 0, "songbrowser_scores");
    this->scoreBrowser->setScrollbarOnLeft(true);
    this->scoreBrowser->setDrawBackground(false);
    this->scoreBrowser->setDrawFrame(false);
    this->scoreBrowser->setHorizontalScrolling(false);
    this->scoreBrowser->setScrollbarSizeMultiplier(0.25f);
    this->scoreBrowser->setScrollResistance(15);
    this->scoreBrowser->setHorizontalClipping(false);
    this->scoreBrowserScoresStillLoadingElement = new IconLabel(Icons::GLOBE, _("Loading..."), 0.4f);
    this->scoreBrowserNoRecordsSetElement = new IconLabel(Icons::TROPHY, _("No records set!"), 0.6f);
    this->scoreBrowser->container.addBaseUIElement(this->scoreBrowserNoRecordsSetElement);

    // NOTE: we don't add personalBestContainer to the screen; we draw and update it manually so that
    //       it can be drawn under skins which overlay the scores list.
    this->personalBestContainer = std::make_unique<CBaseUIContainer>(0.f, 0.f, 0.f, 0.f, "");
    this->personalBestContainer->setVisible(false);
    this->personalBestLabel = new CBaseUILabel(0, 0, 0, 0, "", _("Personal Best"));
    this->personalBestLabel->setDrawBackground(false);
    this->personalBestLabel->setDrawFrame(false);
    this->personalBestLabel->setTextJustification(TEXT_JUSTIFICATION::CENTERED);

    this->thumbnailYRatio = cv::draw_songbrowser_thumbnails.getBool() ? 1.333333f : 0.f;

    // behaviour
    this->fBackgroundFadeInTime = 0.0f;

    // search
    this->search = new UISearchOverlay(0, 0, 0, 0, "songbrowser_search");
    this->search->setOffsetRight(10);
    this->search->setChangeCallback([this] { this->bSearchScheduled = true; });
    this->search->setTypingBlocked([this] { return this->bF2Pressed || this->bF3Pressed; });
    this->bInSearch = (!cv::songbrowser_search_hardcoded_filter.getString().empty());
    this->updateLayout();
}

SongBrowser::~SongBrowser() {
    BatchDiffCalc::abort_calc();
    AsyncPPC::set_map(nullptr);
    VolNormalization::abort();
    this->checkHandleKillBackgroundSearchMatcher();

    this->scoreBrowser->invalidate();
    for(ScoreButton *button : this->scoreButtonCache) {
        SAFE_DELETE(button);
    }
    this->scoreButtonCache.clear();

    this->personalBestContainer->invalidate();  // contained elements freed manually below
    SAFE_DELETE(this->personalBestButton);
    SAFE_DELETE(this->personalBestLabel);
    SAFE_DELETE(this->scoreBrowserScoresStillLoadingElement);
    SAFE_DELETE(this->scoreBrowserNoRecordsSetElement);

    SAFE_DELETE(this->contextMenu);
    SAFE_DELETE(this->search);
    SAFE_DELETE(this->topbarLeft);
    SAFE_DELETE(this->topbarRight);
    SAFE_DELETE(this->scoreBrowser);

    cv::songbrowser_search_hardcoded_filter.removeAllCallbacks();
    cv::scores_always_display_pp.removeCallback();
    cv::scores_sort_by_pp.removeCallback();
    cv::songbrowser_grades_current_name.removeCallback();
    cv::songbrowser_grades_current_server.removeCallback();
    cv::name.removeCallback();
}

bool SongBrowser::drawBeatmapOrMenuBackground() {
    g->setColor(0xff000000);
    g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());
    bool drew = false;

    // draw background image
    if(cv::draw_songbrowser_background_image.getBool()) {
        const DatabaseBeatmap *beatmap = osu->getMapInterface()->getBeatmap();
        auto *bgHandler = osu->getBackgroundImageHandler();
        const Image *loadedImage = bgHandler->getLoadBackgroundImage(beatmap);

        float alpha = 1.0f;
        if(cv::songbrowser_background_fade_in_duration.getFloat() > 0.0f) {
            // handle fadein trigger after bgHandler is finished loading
            const bool ready = loadedImage && loadedImage->isReady();

            drew = ready;

            if(!ready)
                this->fBackgroundFadeInTime = (f32)engine->getTime();
            else if(this->fBackgroundFadeInTime > 0.0f && engine->getTime() > this->fBackgroundFadeInTime) {
                alpha = std::clamp<float>((f32)((engine->getTime() - this->fBackgroundFadeInTime) /
                                                cv::songbrowser_background_fade_in_duration.getFloat()),
                                          0.0f, 1.0f);
                alpha = 1.0f - (1.0f - alpha) * (1.0f - alpha);
            }
        }

        bgHandler->draw(loadedImage, alpha);
    } else if(cv::draw_songbrowser_menu_background_image.getBool()) {
        // menu-background
        Image *backgroundImage = osu->getSkin()->i_menu_bg;
        if(backgroundImage != nullptr && backgroundImage != MISSING_TEXTURE && backgroundImage->isReady()) {
            drew = true;

            const float scale = Osu::getImageScaleToFillResolution(backgroundImage, osu->getVirtScreenSize());

            g->setColor(0xffffffff);
            g->pushTransform();
            {
                g->scale(scale, scale);
                g->translate(osu->getVirtScreenSize() / 2.f);
                g->drawImage(backgroundImage);
            }
            g->popTransform();
        }
    }

    return drew;
}

void SongBrowser::draw() {
    if(!this->bVisible) return;

    // draw background
    this->drawBeatmapOrMenuBackground();

    {
        f32 mode_osu_scale = SongBrowser::getSkinScale(osu->getSkin()->i_mode_osu);

        g->setColor(0xffffffff);
        // XXX: Flash based on song BPM (unless cv::avoid_flashes is set)
        g->setAlpha(0.1f);

        g->setBlendMode(DrawBlendMode::ADDITIVE);
        osu->getSkin()->i_mode_osu.drawRaw(vec2(osu->getVirtScreenWidth() / 2, osu->getVirtScreenHeight() / 2),
                                           mode_osu_scale, AnchorPoint::CENTER);
        g->setBlendMode(DrawBlendMode::ALPHA);
    }

    // draw score browser
    this->scoreBrowser->draw();

    {
        // draw strain graph of currently selected beatmap
        // FIXME: ugly, where do we even put this? "No records set!" label or personal best button is in the same spot
        const bool doDrawStrainGraph = cv::draw_songbrowser_strain_graph.getBool();
        auto *noRecordsSetElem = static_cast<CBaseUILabel *>(this->scoreBrowserNoRecordsSetElement);
        const Color oldNoRecordsSetBG = noRecordsSetElem->getBackgroundColor();
        const Color oldNoRecordsSetFrame = noRecordsSetElem->getFrameColor();

        bool noRecordsSetOpacityHack = false;
        if(doDrawStrainGraph) {
            this->drawStrainGraphOverlay();
            if(this->personalBestContainer->isVisible() &&
               Mc::ranges::contains(this->personalBestContainer->getElements(), noRecordsSetElem)) {
                noRecordsSetOpacityHack = true;
                noRecordsSetElem->setBackgroundColor(Color{oldNoRecordsSetBG}.setA(0.5f));
                noRecordsSetElem->setFrameColor(Color{oldNoRecordsSetFrame}.setA(0.5f));
            }
        }

        this->personalBestContainer->draw();

        if(noRecordsSetOpacityHack) {
            noRecordsSetElem->setBackgroundColor(oldNoRecordsSetBG);
            noRecordsSetElem->setFrameColor(oldNoRecordsSetFrame);
        }
    }

    if(cv::debug_osu.getBool()) {
        this->scoreBrowser->container.draw_debug();
        this->personalBestContainer->draw_debug();
    }

    // draw beatmap carousel
    this->carousel->draw();

    // draw topbar background
    g->setColor(0xffffffff);
    g->pushTransform();
    {
        auto screen = osu->getVirtScreenSize();
        bool is_widescreen = (screen.x / screen.y) > (4.f / 3.f);

        Image *topbar = osu->getSkin()->i_songselect_top;
        f32 scale = (f32)osu->getVirtScreenWidth() / (f32)topbar->getWidth();
        if(!is_widescreen) scale /= 0.75;  // XXX: stupid

        g->scale(scale, scale);
        g->drawImage(topbar, AnchorPoint::TOP_LEFT);
    }
    g->popTransform();

    // draw bottom bar
    BottomBar::draw();

    // draw top bar
    this->topbarLeft->draw();
    if(cv::debug_osu.getBool()) this->topbarLeft->draw_debug();
    this->topbarRight->draw();
    if(cv::debug_osu.getBool()) this->topbarRight->draw_debug();

    // draw search
    this->search->setHardcodedSearchString(cv::songbrowser_search_hardcoded_filter.getString());
    this->search->setDrawNumResults(this->bInSearch);
    this->search->setNumFoundResults(this->currentVisibleSearchMatches);
    this->search->setSearching(this->searchHandle.valid() && !this->searchHandle.is_ready());
    this->search->draw();

    // NOTE: Intentionally not calling ScreenBackable::draw() here, since we're already drawing
    //       the back button in draw_bottombar().
    UIScreen::draw();  // NOLINT(bugprone-parent-virtual-call)

    // no beatmaps found (osu folder is probably invalid)
    if(db->getBeatmapSets().size() == 0) {
        std::string errorMessage1 = _("Invalid osu! folder (or no beatmaps found): ");
        errorMessage1.append(this->sLastOsuFolder);
        std::string errorMessage2 = _("Go to Options -> osu!folder");

        if constexpr(Env::cfg(OS::WASM)) {
            errorMessage1 = _("Drop .osz beatmaps onto this window to import them");
            errorMessage2 = _("Or click \"Online Beatmaps\" in the Main Menu");
        }

        McFont *subTitleFont = osu->getSubTitleFont();
        const vec2 center = osu->getVirtScreenSize() / 2.f;

        g->setColor(Env::cfg(OS::WASM) ? 0xffffffff : 0xffff0000);
        g->pushTransform();
        {
            g->translate((f32)(i32)(center.x - subTitleFont->getStringWidth(errorMessage1) / 2),
                         (f32)(i32)(center.y + subTitleFont->getHeight()));
            g->drawString(subTitleFont, errorMessage1);
        }
        g->popTransform();

        g->setColor(Env::cfg(OS::WASM) ? 0xffffffff : 0xff00ff00);
        g->pushTransform();
        {
            g->translate((f32)(i32)(center.x - subTitleFont->getStringWidth(errorMessage2) / 2),
                         (f32)(i32)(center.y + subTitleFont->getHeight() * 2 + 15));
            g->drawString(subTitleFont, errorMessage2);
        }
        g->popTransform();
    }

    // context menu
    this->contextMenu->draw();

    // click pulse animation overlay
    if(this->fPulseAnimation > 0.0f) {
        Color topColor = 0x00ffffff;
        Color bottomColor = argb((int)(25 * this->fPulseAnimation), 255, 255, 255);

        g->fillGradient(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight(), topColor, topColor, bottomColor,
                        bottomColor);
    }
}

void SongBrowser::drawStrainGraphOverlay() {
    const f32 graphWidth = this->scoreBrowser->getSize().x;

    const f32
        bottombarTopY =  // (osu->getVirtScreenHeight() - BottomBar::get_height()) doesn't work quite right? just copying from mcosu for now
        (f32)osu->getVirtScreenHeight() -
        ((f32)osu->getVirtScreenHeight() * 0.115f /*cv::songbrowser_bottombar_percent.getFloat() * dpiScale */);

    const f32 strainHeight = cv::hud_scrubbing_timeline_strains_height.getFloat() * Osu::getUIScale();

    McRect graphRect(0, bottombarTopY - strainHeight, graphWidth, strainHeight);

    const f32 alpha =
        (graphRect.contains(mouse->getPos()) ? 1.0f : cv::hud_scrubbing_timeline_strains_alpha.getFloat());

    const auto &ppInfo = osu->getMapInterface()->getWholeMapPPInfo();
    this->strainGraph.draw(ppInfo.aimStrains, ppInfo.speedStrains, ppInfo.total_stars, vec2(0.f, bottombarTopY),
                           graphWidth, strainHeight, alpha, !keyboard->isShiftDown(), !keyboard->isControlDown());
}

CarouselNode SongBrowser::getDiffNodeByHash(const MD5Hash &diff_hash) const {
    if(const auto &it = this->hashToDiffButton->find(diff_hash); it != this->hashToDiffButton->end()) {
        return CarouselNode::diff(it->second);
    }
    return {};
}

bool SongBrowser::isSelected(CarouselNode node) const {
    switch(node.kind) {
        case CarouselNode::Kind::SET:
            return this->data.setSelected[node.idx];
        case CarouselNode::Kind::DIFF:
            return this->data.diffSelected[node.idx];
        case CarouselNode::Kind::GROUP:
            return this->data.groups[node.idx].selected;
        case CarouselNode::Kind::NONE:
            break;
    }
    return false;
}

void SongBrowser::setSelected(CarouselNode node, bool selected) {
    switch(node.kind) {
        case CarouselNode::Kind::SET:
            this->data.setSelected[node.idx] = selected;
            break;
        case CarouselNode::Kind::DIFF:
            this->data.diffSelected[node.idx] = selected;
            break;
        case CarouselNode::Kind::GROUP:
            this->data.groups[node.idx].selected = selected;
            break;
        case CarouselNode::Kind::NONE:
            break;
    }
}

bool SongBrowser::isSearchMatch(CarouselNode node) const { return !node.isDiff() || this->data.diffMatch[node.idx]; }

DatabaseBeatmap *SongBrowser::getBeatmap(CarouselNode node) const {
    if(node.isSet()) {
        DatabaseBeatmap *representative = this->data.setRepresentative[node.idx];
        return representative ? representative : this->data.setMap[node.idx];
    }
    if(node.isDiff()) return this->data.diffMap[node.idx];
    return nullptr;
}

ScoreGrade SongBrowser::getGrade(CarouselNode node) const {
    if(node.isSet()) return this->data.setGrade[node.idx];
    if(node.isDiff()) return this->data.diffGrade[node.idx];
    return ScoreGrade::N;
}

bool SongBrowser::isIndependentDiff(CarouselNode node) const {
    if(!node.isDiff()) return true;
    const u32 set = this->data.diffSet[node.idx];
    if(!this->data.setSelected[set]) return true;

    // TODO: this logic is very weird and only works "accidentally";
    // you'd think returning true IFF (sibling->isSearchMatch() && sibling == this) would be enough,
    // but it doesn't work as expected...

    // check if this is the only visible sibling
    int visibleSiblings = 0;
    for(const u32 sibling : this->data.setChildren(set)) {
        if(this->data.diffMatch[sibling]) {
            visibleSiblings++;
            if(visibleSiblings > 1) return false;  // early exit
        }
    }

    return (visibleSiblings == 1);
}

bool SongBrowser::sortSetChildren(u32 set) {
    if(this->data.setDiffsSortedForStarIdx[set] == StarPrecalc::active_idx) return false;
    this->data.setDiffsSortedForStarIdx[set] = StarPrecalc::active_idx;

    const auto children = this->data.setChildren(set);
    std::vector<u32> sorted{children.begin(), children.end()};
    sort_by_keys(sorted, SortTypes::DIFFICULTY,
                 [this](u32 diff) { return std::pair{this->data.diffMap[diff], this->data.diffGrade[diff]}; });
    std::ranges::copy(sorted, children.begin());
    return true;
}

void SongBrowser::updateSetRepresentative(u32 set) {
    if(this->data.setDiffCount[set] == 0) return;

    this->sortSetChildren(set);

    // use the bottom child (hardest diff, assuming default sorting, and respecting the current search matches)
    const auto children = this->data.setChildren(set);
    for(sSz i = static_cast<sSz>(children.size()) - 1; i >= 0; --i) {
        // NOTE: if no search is active, then all search matches return true by default
        if(!this->data.diffMatch[children[i]]) continue;
        this->data.setRepresentative[set] = this->data.diffMap[children[i]];
        return;
    }
}

void SongBrowser::updateSetRepresentatives() {
    for(const u32 set : this->setOrder) this->updateSetRepresentative(set);
}

void SongBrowser::maybeUpdateGrade(u32 diff) {
    if(this->data.diffGradeScheduled[diff]) this->updateGrade(diff);
}

void SongBrowser::maybeUpdateSetGrades(u32 set) {
    for(const u32 diff : this->data.setChildren(set)) this->maybeUpdateGrade(diff);
}

void SongBrowser::updateGrade(u32 diff) {
    if(!cv::scores_enabled.getBool()) {
        return;
    }
    this->data.diffGradeScheduled[diff] = false;

    const bool currentName = cv::songbrowser_grades_current_name.getBool();
    const bool currentServer = cv::songbrowser_grades_current_server.getBool();
    const std::string_view server = BanchoState::is_online() ? std::string_view{BanchoState::endpoint} : "";

    ScoreGrade maxGrade{ScoreGrade::N};
    {
        Sync::shared_lock lock(db->scores_mtx);
        const auto &dbScoreIt = db->getScores().find(this->data.diffMap[diff]->getMD5());
        if(dbScoreIt != db->getScores().end()) {
            for(const auto &score : dbScoreIt->second) {
                if(currentName && score.playerName != BanchoState::get_username()) continue;
                if(currentServer && score.server != server) continue;
                if(score.grade < maxGrade) {
                    maxGrade = score.grade;
                }
            }
        }
    }

    this->data.diffGrade[diff] = maxGrade;
    ScoreGrade &setGrade = this->data.setGrade[this->data.diffSet[diff]];
    if(setGrade > maxGrade) setGrade = maxGrade;
}

void SongBrowser::select(CarouselNode node, SelOpts opts) {
    const bool wasSelected = this->isSelected(node);
    this->setSelected(node, true);

    // callback
    if(opts.noCallbacks) return;
    switch(node.kind) {
        case CarouselNode::Kind::SET:
            this->onSetSelected(node.idx, wasSelected, opts);
            break;
        case CarouselNode::Kind::DIFF:
            this->onDiffSelected(node.idx, wasSelected, opts);
            break;
        case CarouselNode::Kind::GROUP:
            this->onSelectionChange(node, true);
            this->scrollToSongButton(node, true);
            break;
        case CarouselNode::Kind::NONE:
            break;
    }
}

void SongBrowser::onSetSelected(u32 set, bool wasSelected, SelOpts opts) {
    // resort children (since they might have been updated in the meantime)
    this->sortSetChildren(set);

    // update grade on children if necessary
    this->maybeUpdateSetGrades(set);

    this->onSelectionChange(CarouselNode::set(set), false);

    // now, automatically select the bottom child (hardest diff, assuming default sorting, and respecting the current
    // search matches)
    if(!opts.noSelectBottomChild) {
        const auto children = this->data.setChildren(set);
        for(sSz i = static_cast<sSz>(children.size()) - 1; i >= 0; --i) {
            const u32 child = children[i];
            // NOTE: if no search is active, then all search matches return true by default
            if(!this->data.diffMatch[child]) continue;
            this->select(CarouselNode::diff(child), {.noSelectBottomChild = true, .parentUnselected = !wasSelected});
            break;
        }
    }
}

void SongBrowser::onDiffSelected(u32 diff, bool wasSelected, SelOpts opts) {
    const CarouselNode node = CarouselNode::diff(diff);
    const bool wasParentActuallySelected =
        !this->isIndependentDiff(node) && !(opts.parentUnselected) && this->data.setSelected[this->data.diffSet[diff]];

    this->updateGrade(diff);

    if(!wasParentActuallySelected) {
        this->requestNextScrollToSongButtonJumpFix(diff);
    }

    this->onSelectionChange(node, true);
    this->onDifficultySelected(this->data.diffMap[diff], wasSelected);
    this->scrollToSongButton(node, /*alignOnTop=*/false, /*knownVisible=*/true);
}

f32 SongBrowser::getNodeY(CarouselNode node) const {
    const auto &rows = this->carousel->getRows();
    if(const size_t row = this->carousel->findRow(node); row < rows.size()) return this->carousel->getRowY(row);
    if(node.isDiff()) {
        if(const size_t row = this->carousel->findRow(CarouselNode::set(this->data.diffSet[node.idx]));
           row < rows.size())
            return this->carousel->getRowY(row);
    }
    return 0.f;
}

// FIXME: this should not be a public function (way too multi-purpose, too many side effects)
bool SongBrowser::selectBeatmapset(const BeatmapSet *set) {
    if(db->isLoading()) {
        debugLog("Can't select a beatmapset while database is loading!");
        return false;
    }

    assert(set);
    if(!this->bInitializedBeatmaps) {
        const auto &first = *set->getDifficulties()[0];
        this->reselect = std::make_unique<PendingReselect>(db->locate(first), first.getMD5());
        return false;
    }

    // Just picking the hardest diff for now
    DatabaseBeatmap *best_diff = nullptr;
    const auto &diffs = set->getDifficulties();
    for(auto &diff : diffs) {
        if(!best_diff ||
           diff->getStarRating(StarPrecalc::active_idx) > best_diff->getStarRating(StarPrecalc::active_idx)) {
            best_diff = diff.get();
        }
    }

    if(best_diff == nullptr) {
        ui->getNotificationOverlay()->addToast(_("Beatmapset has no difficulties"), ERROR_TOAST);
        return false;
    } else {
        this->onDifficultySelected(best_diff, false);
        this->selectSelectedBeatmapSongButton();
        return true;
    }
}

void SongBrowser::tick() {
    ScreenBackable::tick();

    // flush diffcalc results to database
    // do this even if not visible, but not during gameplay
    if(!osu->isInGameplay()) {
        if(!BatchDiffCalc::update_mainthread()) {
            BatchDiffCalc::abort_calc();
            if(BatchDiffCalc::did_actual_work()) {
                // force re-sort with final ratings
                this->lastDiffSortModIndex = 0xFF;
                std::ranges::fill(this->data.setDiffsSortedForStarIdx, 0xFF);
                this->searchIndex = std::make_shared<AsyncSongButtonMatcher::SearchIndex>();
                this->searchIndexedDiffs = 0;
            }
        }

        // deferred batch calc for what newly imported maps left to do
        if(!BatchDiffCalc::running() && db->batch_diffcalc_pending) {
            db->batch_diffcalc_pending = false;
            BatchDiffCalc::start_calc();
        }
    }

    if(!this->bVisible) return;

    // avoid ticking extra ui elements when not visible
    this->personalBestContainer->tick();
    this->contextMenu->tick();
    BottomBar::tick();
    this->topbarRight->tick();
    this->scoreBrowser->tick();
    this->topbarLeft->tick();
    this->carousel->tick();

    // handle changed mods resort
    if(this->lastDiffSortModIndex != StarPrecalc::active_idx) {
        this->updateSetRepresentatives();
        this->onSortChange("", cv::songbrowser_sortingtype.getInt());
        this->lastDiffSortModIndex = StarPrecalc::active_idx;
    }

    // auto-download (delegates to MapFetcher; install failure toasts come from the installer)
    if(this->map_autodl || !this->md5_autodl.empty()) {
        this->autodl_fetcher.target_map(this->map_autodl, this->md5_autodl, this->set_autodl);
        const auto &fs = this->autodl_fetcher.tick();
        if(fs.status == MapFetcher::Status::Found) {
            this->onDifficultySelected(this->autodl_fetcher.result(), false);
            this->selectSelectedBeatmapSongButton();
            this->map_autodl = 0;
            this->set_autodl = 0;
            this->md5_autodl = {};
            this->autodl_fetcher.clear();
        } else if(fs.status == MapFetcher::Status::NotFound) {
            // only resolution failures / version mismatches need a message here (the linked map
            // doesn't exist in the set the server has); install failures already toasted
            if(fs.why != MapFetcher::Why::Install) {
                ui->getNotificationOverlay()->addToast(
                    this->map_autodl ? tformat("Failed to find Beatmap #{:d} :(", this->map_autodl)
                                     : std::string{_("The map isn't installed and wasn't found online.")},
                    ERROR_TOAST);
            }
            this->map_autodl = 0;
            this->set_autodl = 0;
            this->md5_autodl = {};
            this->autodl_fetcher.clear();
        }
    } else if(this->set_autodl) {
        this->autodl_fetcher.target_set(this->set_autodl);
        const auto &fs = this->autodl_fetcher.tick();
        if(fs.status == MapFetcher::Status::Found) {
            this->selectBeatmapset(this->autodl_fetcher.result());
            this->set_autodl = 0;
            this->autodl_fetcher.clear();
        } else if(fs.status == MapFetcher::Status::NotFound) {
            this->set_autodl = 0;
            this->autodl_fetcher.clear();
        }
    }

    if(this->pendingScores) this->buildScoreButtons();

    // handle async random beatmap selection
    if(this->bRandomBeatmapScheduled) {
        this->bRandomBeatmapScheduled = false;
        this->selectRandomBeatmap();
    }

    // if cursor is to the left edge of the screen, force center currently selected beatmap/diff
    // but only if the context menu is currently not visible (since we don't want move things while e.g. managing
    // collections etc.)
    // NOTE: it's very slow, so only run it every 10 vsync frames
    if(engine->throttledShouldRun(10) && !ui->getOptionsOverlay()->isVisible() &&
       mouse->getPos().x < (f32)osu->getVirtScreenWidth() * 0.1f && !this->contextMenu->isVisible()) {
        this->scheduled_scroll_to_selected_button = true;
    }

    this->updateSearchIndex(0.002);

    // handle searching
    if(this->bSearchScheduled) {
        this->bSearchScheduled = false;
        this->onSearchUpdate();
    }

    // handle background search matcher
    if(this->searchHandle.valid() && this->searchHandle.is_ready()) {
        if(auto result = this->searchHandle.get(); !result) {
            this->rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(true);
        } else if(result->index == this->searchIndex) {
            const bool rerun = this->lastSearch && this->lastSearch->query == result->query;
            for(size_t i = 0; i < result->matches.size(); i++) {
                this->data.diffMatch[this->searchIndex->ids[i]] = result->matches[i];
            }
            this->lastSearch = std::make_shared<const AsyncSongButtonMatcher::SearchResult>(std::move(*result));
            this->updateSetRepresentatives();
            this->rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(!rerun);
        }
    }
    if(this->bInitializedBeatmaps && this->bInSearch && !this->searchHandle.valid() &&
       this->searchIndexedDiffs == this->data.diffMap.size() &&
       (!this->lastSearch || this->lastSearch->index != this->searchIndex)) {
        this->submitSearch();
    }
    if(!this->searchHandle.valid()) {
        if(this->scheduled_scroll_to_selected_button) {
            this->scheduled_scroll_to_selected_button = false;
            this->scrollToBestButton();
        }
    }
}

void SongBrowser::updateInput(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;

    // registered FIRST = lowest wheel priority in this group: every hovered candidate
    // (score browser, context menu, the carousel itself) gets first refusal
    if(c.propagate_clicks) c.addWheelClaim(this);

    this->personalBestContainer->updateInput(c);
    if(this->personalBestButton && this->personalBestButton->isVisible() && this->personalBestButton->isMouseInside()) {
        // HACKHACK: don't hover score button list buttons under personal best!
        this->scoreBrowser->stealFocus();
    }
    ScreenBackable::updateInput(c);

    // NOTE: This is placed before BottomBar::updateInput(), otherwise the context menu would close
    //       on a bottombar selector click (yeah, a bit hacky)
    this->contextMenu->updateInput(c);

    BottomBar::updateInput(c);

    // update and focus handling
    this->topbarRight->updateInput(c);
    this->scoreBrowser->updateInput(c);
    this->topbarLeft->updateInput(c);

    this->carousel->updateInput(c);
}

bool SongBrowser::onWheel(WheelDelta vertical, WheelDelta horizontal) {
    return this->carousel->onWheel(vertical, horizontal);
}

void SongBrowser::onKeyDown(KeyboardEvent &key) {
    UIScreen::onKeyDown(key);  // NOLINT(bugprone-parent-virtual-call) only used for options menu
    if(!this->bVisible || key.isConsumed()) return;

    // context menu
    this->contextMenu->onKeyDown(key);
    if(key.isConsumed()) return;

    // the search's keys (deleting, Escape while searching, pasting)
    this->search->onEditKey(key);
    if(key.isConsumed()) return;

    // GAME_PAUSE goes back too, unless that key types into the search
    const KEYCODE keyCode = key.getKeyCode();
    const bool typesText = keyCode >= ' ' && keyCode != 0x7f && !(keyCode & MC_SCANCODE_MASK);
    if((key == KEY_ESCAPE || (key == binds::GAME_PAUSE && !typesText)) && !this->contextMenu->isVisible())
        this->onBack();

    // function hotkeys
    // NOTE: no held-flag for F1: the modal modselector floors our key-ups while it is open,
    // so a flag set on the opening press could never be cleared (isRepeat is ground truth)
    if((key == KEY_F1 || key == binds::TOGGLE_MODSELECT) && !key.isRepeat()) {
        BottomBar::press_button(BottomBar::MODS);
    }
    if((key == KEY_F2 || key == binds::RANDOM_BEATMAP) && !this->bF2Pressed) {
        this->bF2Pressed = true;
        BottomBar::press_button(BottomBar::RANDOM);
    }
    if(key == KEY_F3 && !this->bF3Pressed) {
        this->bF3Pressed = true;
        BottomBar::press_button(BottomBar::OPTIONS);
    }

    if(key == KEY_F5) this->refreshBeatmaps(this, /*full_rescan=*/true);

    this->carousel->onKeyDown(key);
    //if (key.isConsumed()) return;

    // toggle auto
    if(key == KEY_A && keyboard->isControlDown()) ui->getModSelector()->toggleAuto();

    key.consume();
}

void SongBrowser::onKeyUp(KeyboardEvent &key) {
    // context menu
    this->contextMenu->onKeyUp(key);
    if(key.isConsumed()) return;

    if(key == KEY_LEFT) this->bLeft = false;
    if(key == KEY_RIGHT) this->bRight = false;

    if(key == KEY_F2 || key == binds::RANDOM_BEATMAP) this->bF2Pressed = false;
    if(key == KEY_F3) this->bF3Pressed = false;
}

CBaseUIElement *SongBrowser::getTextTarget(CBaseUIElement *focus) {
    if(!this->isVisible()) return nullptr;
    if(focus) return focus;
    // an open searchable context menu has its own box
    if(auto *menuSearch = this->contextMenu->getSearchTextbox()) return menuSearch;
    return this->search;
}

void SongBrowser::onResolutionChange(vec2 newResolution) {
    // button sizes are rounded to whole pixels for the current resolution, so relaying them out moves the content by
    // (size delta * number of buttons above it), while the scroll position is an absolute offset into that content:
    // keep the button closest to the carousel's center at the same relative height instead
    const auto centerY = [this](size_t row) -> f64 {
        return this->carousel->getRowY(row) + CarouselButton::getScaledBaseSize().y / 2;
    };

    std::optional<size_t> anchor;
    f64 anchorPct = 0.0;
    if(const auto &rows = this->carousel->getRows(); !rows.empty() && this->carousel->getSize().y > 0) {
        const f64 viewCenterY = this->carousel->getSize().y / 2 - this->carousel->getRelPosY();
        f64 closest = std::numeric_limits<f64>::max();
        for(size_t row = 0; row < rows.size(); row++) {
            if(const f64 dist = std::abs(centerY(row) - viewCenterY); dist < closest) {
                closest = dist;
                anchor = row;
            }
        }
        anchorPct = (centerY(*anchor) + this->carousel->getRelPosY()) / this->carousel->getSize().y;
        // a centered button was scrolled to: keep it exactly centered, the whole-pixel scroll position would
        // otherwise let it drift over many small resizes
        if(std::abs(anchorPct - 0.5) * this->carousel->getSize().y < 1.0) anchorPct = 0.5;
    }

    CarouselButton::updateResolution();
    ScreenBackable::onResolutionChange(newResolution);

    if(anchor) {
        this->carousel->scrollToY((i32)std::round(anchorPct * this->carousel->getSize().y - centerY(*anchor)), false);
    }
}

[[nodiscard]] std::span<CBaseUIElement *const> SongBrowser::getAllChildren() const {
    this->allChildren.assign(this->vElements.begin(), this->vElements.end());
    for(CBaseUIElement *extra :
        {static_cast<CBaseUIElement *>(this->topbarLeft), static_cast<CBaseUIElement *>(this->topbarRight),
         static_cast<CBaseUIElement *>(this->contextMenu), static_cast<CBaseUIElement *>(this->scoreBrowser),
         static_cast<CBaseUIElement *>(this->carousel.get()),
         static_cast<CBaseUIElement *>(this->personalBestContainer.get()),
         static_cast<CBaseUIElement *>(this->search)}) {
        if(extra) this->allChildren.push_back(extra);
    }
    return this->allChildren;
}

CBaseUIContainer *SongBrowser::setVisible(bool visible) {
    if(BanchoState::spectating && visible) return this;  // don't allow song browser to be visible while spectating
    if(visible == this->bVisible) return this;

    // Load DB if we haven't attempted yet
    if(visible && this->setOrder.empty() && !db->isFinished()) {
        this->refreshBeatmaps(this);
        return this;
    }

    this->bVisible = visible;

    if(this->bVisible) {
        soundEngine->play(osu->getSkin()->s_expand);

        if(this->bSongButtonsNeedSorting) {
            this->rebuildAfterGroupOrSortChange(this->curGroup);
        }

        this->updateLayout();

        // we have to re-select the current beatmap to start playing music again
        osu->getMapInterface()->selectBeatmap();
        if(DatabaseBeatmap *map = osu->getMapInterface()->getBeatmapMutable()) this->playPreviewMusic(map);

        // update user name/stats
        osu->onUserCardChange(BanchoState::get_username());

        // For multiplayer: if the host exits song selection without selecting a song, we want to be able to revert
        // to that previous song.
        this->lastSelectedBeatmap = osu->getMapInterface()->getBeatmap();

        // Select button matching current song preview
        this->selectSelectedBeatmapSongButton();

        RichPresence::onSongBrowser();
    } else {
        this->contextMenu->setVisible2(false);
    }

    ui->getChat()->updateVisibility();
    return this;
}

void SongBrowser::selectSelectedBeatmapSongButton() {
    const DatabaseBeatmap *map = nullptr;
    if(this->hashToDiffButton->empty() || !(map = osu->getMapInterface()->getBeatmap())) return;

    auto it = this->hashToDiffButton->find(map->getMD5());
    if(it == this->hashToDiffButton->end()) {
        debugLog("No song button found for currently selected beatmap...");
        return;
    }

    const CarouselNode btn = CarouselNode::diff(it->second);
    if(this->data.diffMap[btn.idx] != map) {
        debugLog("Found matching beatmap, but not matching difficulty.");
        return;
    }

    this->setSelected(btn, false);  // if we select() it when already selected, it would start playing!
    this->select(btn);
}

void SongBrowser::onPlayEnd(bool quit) {
    // update score displays
    if(!quit) {
        this->rebuildScoreButtons();

        if(this->selectedButton.isDiff()) this->updateGrade(this->selectedButton.idx);
    }

    // update song info
    if(auto *bm = osu->getMapInterface()->getBeatmapMutable()) {
        // also fix up modification time for improperly stored beatmaps while we're here
        // should be cheap
        // TODO: don't do this here, probably inherently racy somehow
        // NOTE: all modification times need to be accurate to support sorting/grouping by date added
        if(bm->last_modification_time <= 0 && !bm->getFilePath().empty()) {
            struct stat64 attr;  // NOLINT(cppcoreguidelines-pro-type-member-init,hicpp-member-init)
            if(File::stat_c(bm->sFilePath, &attr) == 0) {
                bm->last_modification_time = attr.st_mtime;
            }
        }
        this->songInfo->setFromBeatmap(bm);
    }
}

void SongBrowser::onSelectionChange(CarouselNode button, bool rebuild) {
    const CarouselNode lastSelected = this->selectedButton;
    this->selectedButton = button;
    if(!button) return;

    this->contextMenu->setVisible2(false);
    if((lastSelected == button) && !rebuild) return;

    // keep track and update all selection states
    // I'm still not happy with this, but at least all state update logic is localized in this function instead of
    // spread across all buttons

    const CarouselNode prevSelDiffBtn = this->selectionPreviousSongDiffButton;
    const CarouselNode prevSelSongBtn = this->selectionPreviousSongButton;
    const CarouselNode prevSelColBtn = this->selectionPreviousCollectionButton;

    const bool isSongButton = button.isSong();

    if(button.isDiff()) {
        if(prevSelDiffBtn && prevSelDiffBtn != button) {
            this->setSelected(prevSelDiffBtn, false);
        }

        // support individual diffs independent from their parent song button container
        {
            const CarouselNode parentSongButton = CarouselNode::set(this->data.diffSet[button.idx]);
            // if the new diff has a parent song button, then update its selection state (select it to stay consistent)
            if(!this->isSelected(parentSongButton)) {
                this->sortSetChildren(
                    parentSongButton.idx);  // NOTE: workaround for disabled callback firing in select()
                this->select(parentSongButton, {.noCallbacks = true});
                this->onSelectionChange(parentSongButton, false);  // NOTE: recursive call

                // reset last-selected button to the diff button after recursion
                // instead of the parent (which will be hidden)
                this->selectedButton = button;
            }
        }

        this->selectionPreviousSongDiffButton = button;
    } else if(button.isSet()) {
        if(prevSelSongBtn && prevSelSongBtn != button) this->setSelected(prevSelSongBtn, false);
        if(prevSelDiffBtn) this->setSelected(prevSelDiffBtn, false);

        this->selectionPreviousSongButton = button;
    } else if(button.isGroup()) {
        // TODO: maybe expand this logic with per-group-type last-open-collection memory

        // logic for allowing collections to be deselected by clicking on the same button (contrary to how beatmaps
        // work)
        const bool isTogglingCollection = (prevSelColBtn && prevSelColBtn == button);

        if(prevSelColBtn) this->setSelected(prevSelColBtn, false);

        this->selectionPreviousCollectionButton = button;

        if(isTogglingCollection) this->selectionPreviousCollectionButton = {};
    }

    // try to avoid rebuilding by going through some cases we know can be skipped,
    // in increasingly expensive order (as long as it's still worth it vs rebuilding)
    // (no logic for collection buttons, it's not worth it)
    bool doUpdateLayout = false;

    // check old and new parent selections
    // NOTE: using previously selected difficulty because previously selected song button has already been updated at this point...
    if(rebuild && prevSelDiffBtn && isSongButton) {
        const u32 oldParentPtr = this->data.diffSet[prevSelDiffBtn.idx];
        const u32 newParentPtr = !button.isDiff() ? button.idx : this->data.diffSet[button.idx];

        if(oldParentPtr == newParentPtr) {
            // skip rebuilding if we merely selected a sibling difficulty button
            rebuild = false;
            // the difficulties of an expanded set are spaced the same no matter which one of them is selected, but a
            // set's only visible difficulty gets its spacing from being selected
            doUpdateLayout =
                prevSelDiffBtn != button && this->getSetVisibility(newParentPtr) != SetVisibility::SHOW_PARENT;
        } else {
            // if we got here, a slightly more expensive check: skip if the visibility of both buttons are the same
            const SetVisibility oldVis = this->getSetVisibility(oldParentPtr);
            const SetVisibility newVis = this->getSetVisibility(newParentPtr);

            // when in no grouping, parent buttons are expanded/unexpanded depending on their selection state and how many children they have visible
            // otherwise we need to rebuild everything (with the current implementation) to un-expand the old parent and expand the new one

            // EXCEPT! another edge case:when in GROUP_COLLECTIONS, there can be multiple occurrences of the same diff button
            // across collections, and some sets may be collapsed while others aren't!

            // (oldParentPtr->isVisible() == newParentPtr->isVisible()) ||
            // TODO: this still unnecessarily rebuilds (see group by difficulty, each time the buttons jump jarringly was an unnecessary rebuild)
            // above check helped but isn't correct for expanding unselected parents
            // lazily commented out for now
            if(oldVis == newVis && (oldVis != SetVisibility::SHOW_PARENT ||
                                    (!this->isInParentsCollapsedMode() && this->curGroup != GroupType::COLLECTIONS))) {
                rebuild = false;
                // sadly, we still need to update layout (cheaper than full rebuild though)
                // this is because Y coordinates change depending on selection state and depend on all surrounding buttons
                doUpdateLayout = true;
            }
        }
    }

    if(rebuild) {
        this->rebuildSongButtons();
    } else if(doUpdateLayout) {
        this->updateSongButtonLayout();
    }
}

void SongBrowser::playPreviewMusic(DatabaseBeatmap *map) {
    MusicTrack *music = osu->getMusicTrack();
    switch(music->load(map, true /*async*/)) {
        case MusicTrack::Loaded::NO_AUDIO:
            // (not while the database reloads: its reselection goes on with the music)
            if(!this->reselect) music->pause();
            return;
        case MusicTrack::Loaded::SAME_FILE:
            if(music->getPositionPct() > 0.95) {
                music->restart();
            } else {
                music->play();
            }
            break;
        case MusicTrack::Loaded::NEW_FILE:
            music->restart();
            break;
    }
    music->setLoop(cv::beatmap_preview_music_loop.getBool());
}

void SongBrowser::onDifficultySelected(DatabaseBeatmap *map, bool play) {
    // deselect = unload
    osu->getMapInterface()->deselectBeatmap();

    // select = play preview music
    osu->getMapInterface()->selectBeatmap(map);
    if(map) this->playPreviewMusic(map);

    // update song info
    if(map) {
        this->songInfo->setFromBeatmap(map);

        // start playing
        if(play) {
            // update last play time (TODO: should this only be stored if a certain amount of playtime is reached?)
            map->setLastPlayTime(Timing::getUnixTime());
            db->update_overrides(map);  // save peppy overrides
            if(this->curSortMethod == SortType::DATEPLAYED) {
                // force resort
                // XXX: slow!
                this->bSongButtonsNeedSorting = true;
            }

            if(BanchoState::is_in_a_multi_room()) {
                ui->getRoomScreen()->set_current_map(map);
                ui->setScreen(ui->getRoomScreen());
            } else {
                // CTRL + click = auto
                if(keyboard->isControlDown()) {
                    osu->bModAutoTemp = true;
                    ui->getModSelector()->enableAuto();
                }

                osu->getMapInterface()->play();
            }
        }
    }

    // animate
    this->fPulseAnimation = 1.0f;
    this->fPulseAnimation.set(0.0f, 0.55f, anim::Linear);

    // update score display
    this->rebuildScoreButtons();

    // update web button
    this->webButton->setVisible((this->songInfo->getBeatmapID() > 0) || (this->songInfo->getBeatmapSetID() > 0));
}

class SongBrowser::BeatmapLoadingOverlay final : public LoadingScreen {
    NOCOPY_NOMOVE(BeatmapLoadingOverlay)
   public:
    BeatmapLoadingOverlay() = delete;
    BeatmapLoadingOverlay(SongBrowser *songbrowser, BGImageHandler *bghandler, UIScreen *parent)
        : LoadingScreen(parent), sbr(songbrowser), bgih(bghandler) {}
    ~BeatmapLoadingOverlay() override = default;

    [[nodiscard]] bool isFinished() const override { return this->progress >= 1.f || db->isFinished(); }

   protected:
    void drawBackground() override {
        if(!this->bgih->drawLastImage(0.5f)) {
            LoadingScreen::drawBackground();
        }
    }

    f32 updateProgress() override {
        if(!db->isFinished()) {
            db->update();  // finishes the load (onDBLoadComplete) once the loader thread is done
        }
        return db->getProgress();
    }

    [[nodiscard]] std::string getProgressMessage() const override {
        // the percentage only covers the database files; the .osz import and folder passes that follow would
        // sit at 99% with no sign of progress, so show their counts instead
        const u32 total = db->getStageTotal();
        if(total == 0) return LoadingScreen::getProgressMessage();
        const u32 done = db->getStageDone();
        return db->getLoadStage() == Database::LoadStage::ScanningFolders
                   ? tformat("Scanning beatmap folders ({:d}/{:d})", done, total)
                   : tformat("Importing beatmaps ({:d}/{:d})", done, total);
    }

    void finish() override {
        this->progress = 1.f;

        if(!db->isFinished()) {
            db->cancel();
        }

        assert(db->isFinished());

        assert(this->sbr->loadingOverlay && "BeatmapLoadingOverlay::finish: SongBrowser was in an invalid state");

        this->sbr->loadingOverlay = nullptr;

        // finish loading
        this->sbr->onDatabaseLoadingFinished(/*isNextScreenSongBrowser=*/this->getParent() == this->sbr);

        // kill ourselves
        auto tmp = ui->popOverlay(this);
        // (tmp == this) is deleted here
    }

   private:
    SongBrowser *sbr;
    BGImageHandler *bgih;
};

void SongBrowser::refreshBeatmaps(UIScreen *next_screen, bool full_rescan) {
    if(osu->isInPlayMode()) return;

    if(!!this->loadingOverlay) {
        // We are already refreshing beatmaps!
        return;
    }

    if(this->bInitializedBeatmaps) {
        // we need to save beatmaps we added this session
        db->save();
    }

    this->bInitializedBeatmaps = false;

    // remember for initial songbrowser load
    if(const BeatmapDifficulty *map = osu->getMapInterface()->getBeatmap();
       !!map && map->getMD5() != MD5Hash::sentinel && !map->getMD5().is_suspicious()) {
        this->reselect = std::make_unique<PendingReselect>(db->locate(*map), map->getMD5());
    }

    // reset
    this->checkHandleKillBackgroundSearchMatcher();
    this->searchIndex = std::make_shared<AsyncSongButtonMatcher::SearchIndex>();
    this->searchIndexedDiffs = 0;
    this->lastSearch.reset();

    // clear beatmap interface to lose any potential stale references
    VolNormalization::flush_priority();  // it may be calculating on preloaded maps
    osu->reloadMapInterface();
    osu->getMusicTrack()->releaseMap();
    ui->getMainMenu()->clearPreloadedMaps();

    this->selectedButton = {};
    this->selectionPreviousSongButton = {};
    this->selectionPreviousSongDiffButton = {};
    this->selectionPreviousCollectionButton = {};
    this->contextMenuNode = {};

    // delete local database and UI
    this->carousel->clearRows();

    this->hashToDiffButton->clear();
    this->data.clear();
    this->setOrder.clear();

    this->collectionButtons.clear();

    this->artistCollectionButtons.clear();
    this->bpmCollectionButtons.clear();
    this->difficultyCollectionButtons.clear();
    this->creatorCollectionButtons.clear();
    this->dateaddedCollectionButtons.clear();
    this->lengthCollectionButtons.clear();
    this->titleCollectionButtons.clear();

    this->visibleSongButtons.clear();
    this->previousRandomBeatmaps.clear();

    // the score buttons point at maps of the database being reloaded too, and nothing rebuilds them when the
    // reselected map has no button afterwards: back to the empty list it starts out as
    this->scoreBrowser->invalidate();
    this->scoreBrowser->container.addBaseUIElement(this->scoreBrowserNoRecordsSetElement);
    this->personalBestContainer->invalidate();
    this->personalBestContainer->setVisible(false);
    SAFE_DELETE(this->personalBestButton);
    this->pendingScores.reset();

    this->contextMenu->setVisible2(false);

    // clear potentially active search
    this->bInSearch = false;
    this->search->clear();
    this->sPrevSearchString.clear();
    this->bSearchScheduled = false;
    this->searchPrevGroup = std::nullopt;

    // force no grouping (TODO: is this even necessary? why is it here?)
    // if(this->curGroup != GroupType::NO_GROUPING) {
    //     this->onGroupChange("", GroupType::NO_GROUPING);
    // } else {
    //     this->groupByNothingBtn->setTextBrightColor(highlightColor);
    // }

    auto loading_screen = std::make_unique<BeatmapLoadingOverlay>(this, osu->getBackgroundImageHandler(), next_screen);
    this->loadingOverlay = loading_screen.get();

    // start loading (watching maps/ again once that's done)
    this->mapsWatch.reset();
    db->load(full_rescan);

    // make sure whatever was visible is hidden until loading finishes
    ui->hide();

    // NOTE: this also tick()s the new overlay once immediately, that's why it's put after everything above
    ui->pushOverlay(std::move(loading_screen));
}

namespace {
void add_mapset_to_alphanum_group(CarouselNode set, CarouselData &data, const SongBrowser::GroupList &group,
                                  std::string_view name) {
    if(group.size() != 28) {
        debugLog("Alphanumeric group wasn't initialized!");
        return;
    }

    const char firstChar = name.length() == 0 ? '#' : name[0];
    const bool isNumber = (firstChar >= '0' && firstChar <= '9');
    const bool isLowerCase = (firstChar >= 'a' && firstChar <= 'z');
    const bool isUpperCase = (firstChar >= 'A' && firstChar <= 'Z');

    u32 cbtn = 0;
    if(isNumber) {
        cbtn = group[0];
    } else if(isLowerCase || isUpperCase) {
        const int index = 1 + (25 - (isLowerCase ? 'z' - firstChar : 'Z' - firstChar));
        cbtn = group[index];
    } else {
        cbtn = group[27];
    }

    logIfCV(debug_osu, "Inserting {:s}", name);

    data.groups[cbtn].children.push_back(set);
}
}  // namespace

// this is an insane hack to be calling from places that don't even use songbrowser
void SongBrowser::addBeatmapSet(BeatmapSet *mapset, bool initialSongBrowserLoad) {
    if(!this->bInitializedBeatmaps && !initialSongBrowserLoad) {
        this->bSongButtonsNeedSorting = true;
        return;
    }

    // NOTE: BeatmapSets must always be created with at least itself as a difficulty.
    assert(!mapset->getDifficulties().empty());

    // some invariants are assumed to be true if we are loading for the first time
    if(initialSongBrowserLoad) {
        assert(this->difficultyCollectionButtons.size() == 12);
        assert(this->bpmCollectionButtons.size() == 6);
        assert(this->lengthCollectionButtons.size() == 7);
    } else {
        this->bSongButtonsNeedSorting = true;
    }

    const bool doDiffCollBtns = initialSongBrowserLoad || likely(this->difficultyCollectionButtons.size() == 12);
    const bool doBPMCollBtns = initialSongBrowserLoad || likely(this->bpmCollectionButtons.size() == 6);
    const bool doLengthCollBtns = initialSongBrowserLoad || likely(this->lengthCollectionButtons.size() == 7);

    // always create parent button for the set
    const u32 set = (u32)this->data.setMap.size();
    this->data.setMap.push_back(mapset);
    this->data.setRepresentative.push_back(nullptr);
    this->data.setDiffBegin.push_back((u32)this->data.setDiffs.size());
    this->data.setDiffCount.push_back((u32)mapset->getDifficulties().size());
    this->data.setGrade.push_back(ScoreGrade::N);
    this->data.setDiffsSortedForStarIdx.push_back(0xFF);
    this->data.setSelected.push_back(false);
    for(const auto &diff : mapset->getDifficulties()) {
        // must exist and be a difficulty
        assert(diff && diff->getDifficulties().empty());
        this->data.setDiffs.push_back((u32)this->data.diffMap.size());
        this->data.diffMap.push_back(diff.get());
        this->data.diffSet.push_back(set);
        this->data.diffGrade.push_back(ScoreGrade::N);
        this->data.diffGradeScheduled.push_back(true);
        this->data.diffSelected.push_back(false);
        this->data.diffMatch.push_back(true);
    }
    if(this->curSortMethod == SortType::RANKACHIEVED) {
        for(const u32 diff : this->data.setChildren(set)) this->updateGrade(diff);
    }
    this->updateSetRepresentative(set);
    this->setOrder.push_back(set);

    // add mapset to all necessary groups

    add_mapset_to_alphanum_group(CarouselNode::set(set), this->data, this->artistCollectionButtons,
                                 mapset->getArtistLatin());
    add_mapset_to_alphanum_group(CarouselNode::set(set), this->data, this->creatorCollectionButtons,
                                 mapset->getCreator());
    add_mapset_to_alphanum_group(CarouselNode::set(set), this->data, this->titleCollectionButtons,
                                 mapset->getTitleLatin());

    // use parent's children for grouping
    for(const u32 diff_idx : this->data.setChildren(set)) {
        const DatabaseBeatmap *diff = this->data.diffMap[diff_idx];
        const CarouselNode diff_btn = CarouselNode::diff(diff_idx);

        // map each difficulty hash to its button
        this->hashToDiffButton->emplace(diff->getMD5(), diff_idx);

        if(doDiffCollBtns) {
            const float stars_tmp = diff->getStarRating(StarPrecalc::active_idx);
            const int index = std::clamp<int>(
                (std::isfinite(stars_tmp) && stars_tmp >= static_cast<float>(std::numeric_limits<int>::min()) &&
                 stars_tmp <= static_cast<float>(std::numeric_limits<int>::max()))
                    ? static_cast<int>(stars_tmp)
                    : 0,
                0, 11);
            this->data.groups[this->difficultyCollectionButtons[index]].children.push_back(diff_btn);
        }

        if(doBPMCollBtns) {
            const int bpm = diff->getMostCommonBPM();
            int index = 0;
            if(bpm < 60) {
                index = 0;
            } else if(bpm < 120) {
                index = 1;
            } else if(bpm < 180) {
                index = 2;
            } else if(bpm < 240) {
                index = 3;
            } else if(bpm < 300) {
                index = 4;
            } else {
                index = 5;
            }

            this->data.groups[this->bpmCollectionButtons[index]].children.push_back(diff_btn);
        }

        // dateadded
        {
            // TODO: extremely annoying
        }

        if(doLengthCollBtns) {
            const u32 lengthMS = diff->getLengthMS();
            int btnIdx = 0;

            if(lengthMS <= 1000 * 60) {
                btnIdx = 0;
            } else if(lengthMS <= 1000 * 60 * 2) {
                btnIdx = 1;
            } else if(lengthMS <= 1000 * 60 * 3) {
                btnIdx = 2;
            } else if(lengthMS <= 1000 * 60 * 4) {
                btnIdx = 3;
            } else if(lengthMS <= 1000 * 60 * 5) {
                btnIdx = 4;
            } else if(lengthMS <= 1000 * 60 * 10) {
                btnIdx = 5;
            } else {
                btnIdx = 6;
            }

            this->data.groups[this->lengthCollectionButtons[btnIdx]].children.push_back(diff_btn);
        }
    }
}

void SongBrowser::unlinkBeatmapSet(const BeatmapSet *set) {
    auto btn_it = Mc::ranges::find_if(this->setOrder, [this, set](u32 s) { return this->data.setMap[s] == set; });
    if(btn_it == this->setOrder.end()) return;
    const CarouselNode btn = CarouselNode::set(*btn_it);

    this->checkHandleKillBackgroundSearchMatcher();
    this->contextMenu->setVisible2(false);

    auto forget = [](CarouselNode &node, CarouselNode b) {
        if(node == b) node = {};
    };
    auto unlink = [this](const GroupList &group, CarouselNode b) {
        for(const u32 cbtn : group) std::erase(this->data.groups[cbtn].children, b);
    };

    // the alphanumeric groups hold the parent, the difficulty/bpm/length groups hold the diffs
    forget(this->selectedButton, btn);
    forget(this->selectionPreviousSongButton, btn);
    unlink(this->artistCollectionButtons, btn);
    unlink(this->creatorCollectionButtons, btn);
    unlink(this->titleCollectionButtons, btn);
    std::erase(this->visibleSongButtons, btn);

    for(const u32 diff : this->data.setChildren(btn.idx)) {
        const CarouselNode child = CarouselNode::diff(diff);
        forget(this->selectedButton, child);
        forget(this->selectionPreviousSongDiffButton, child);
        this->hashToDiffButton->erase(this->data.diffMap[diff]->getMD5());
        this->data.diffMap[diff] = nullptr;
        unlink(this->difficultyCollectionButtons, child);
        unlink(this->bpmCollectionButtons, child);
        unlink(this->lengthCollectionButtons, child);
        std::erase(this->visibleSongButtons, child);
    }
    std::erase_if(this->previousRandomBeatmaps,
                  [set](const DatabaseBeatmap *m) { return m == set || m->getParentSet() == set; });

    this->setOrder.erase(btn_it);
    this->carousel->clearRows();
}

// after a set's buttons were added/removed at runtime: user collections are derived from hashToDiffButton and
// the visible list/carousel/difficulty buckets from the containers, so rebuild them (keeping the open collection)
void SongBrowser::rebuildAfterSetChange() {
    const std::string open_collection{this->selectionPreviousCollectionButton
                                          ? this->data.groups[this->selectionPreviousCollectionButton.idx].name
                                          : ""};
    this->recreateCollectionsButtons();
    this->bSongButtonsNeedSorting = true;
    this->rebuildAfterGroupOrSortChange(this->curGroup);

    if(!open_collection.empty() && this->curGroup == GroupType::COLLECTIONS) {
        for(const u32 collectionButton : this->collectionButtons) {
            if(this->data.groups[collectionButton].name == open_collection) {
                this->select(CarouselNode::group(collectionButton));
                break;
            }
        }
    }
}

void SongBrowser::applyReconcile(const ReconcileResult &r) {
    // a selected map that a new object replaced (its file's content changed, or it moved here) is that object from now
    // on, so the set changes below treat it as a map that stayed (a held selection is its holder's)
    bool followed = false;
    if(const DatabaseBeatmap *cur = osu->getMapInterface()->getBeatmap(); cur && !osu->getMusicTrack()->isHeld()) {
        if(auto it = Mc::ranges::find(r.successors, cur, &ReconcileResult::Successor::was); it != r.successors.end()) {
            osu->getMapInterface()->selectBeatmap(it->now);
            followed = true;
        }
    }

    // first, since their buttons still hold the md5s that this folder's take over
    for(const auto &[gone, remainder] : r.moved_from) {
        if(remainder) {
            this->replaceBeatmapSet(gone, remainder);
        } else {
            this->removeBeatmapSet(gone);
        }
    }

    switch(r.outcome) {
        using enum ReconcileResult::Outcome;
        case Created:
            if(!this->bInitializedBeatmaps) break;
            this->addBeatmapSet(r.set);
            if(followed) this->selectSelectedBeatmapSongButton();
            this->rebuildAfterSetChange();
            break;
        case Updated:
            this->replaceBeatmapSet(r.replaced, r.set);
            break;
        case Removed:
            this->removeBeatmapSet(r.replaced);
            break;
        case Unchanged:
        case Failed:
            break;
    }
}

void SongBrowser::removeBeatmapSet(const BeatmapSet *set) {
    if(!this->bInitializedBeatmaps) return;

    // the loaded map's object outlives its set, its button doesn't
    const DatabaseBeatmap *cur = osu->getMapInterface()->getBeatmap();
    const bool selected_inside = cur && cur->getParentSet() == set;

    this->unlinkBeatmapSet(set);
    this->rebuildAfterSetChange();
    if(selected_inside && !osu->getMusicTrack()->isHeld()) this->selectRandomBeatmap();
}

void SongBrowser::replaceBeatmapSet(const BeatmapSet *old_set, BeatmapSet *new_set) {
    if(!this->bInitializedBeatmaps) return;

    // surviving difficulties keep their objects (already re-parented to new_set), so a selected one stays valid
    // and only needs its new button selected; a dropped one falls back to the set
    const DatabaseBeatmap *cur = osu->getMapInterface()->getBeatmap();
    const bool selected_inside = cur && (cur->getParentSet() == old_set || cur->getParentSet() == new_set);

    this->unlinkBeatmapSet(old_set);
    this->addBeatmapSet(new_set);

    // a selection that stays gets its new button selected before the rebuild, whose search update would take a single
    // matching set for a new selection and pick its hardest difficulty
    const bool stays = selected_inside && cur->getParentSet() == new_set;
    if(stays) this->selectSelectedBeatmapSongButton();
    this->rebuildAfterSetChange();
    if(selected_inside && !stays && !osu->getMusicTrack()->isHeld()) this->selectBeatmapset(new_set);
}

void SongBrowser::requestNextScrollToSongButtonJumpFix(u32 diff) {
    this->bNextScrollToSongButtonJumpFixScheduled = true;

    // use parent position only if the diff is NOT independent
    // (i.e., parent is actually visible and expanded in the carousel)
    const CarouselNode diffButton = CarouselNode::diff(diff);
    this->fNextScrollToSongButtonJumpFixOldRelPosY =
        (this->isIndependentDiff(diffButton) ? this->getNodeY(diffButton)
                                             : this->getNodeY(CarouselNode::set(this->data.diffSet[diff])));

    this->fNextScrollToSongButtonJumpFixOldScrollSizeY = (f32)this->carousel->getScrollSize().y;
}

bool SongBrowser::isButtonVisible(CarouselNode songButton) const {
    const auto childrenContain = [this, songButton](CarouselNode btn) -> bool {
        if(btn.isSet())
            return Mc::ranges::contains(this->data.setChildren(btn.idx), songButton.idx) && songButton.isDiff();
        return false;
    };

    for(const CarouselNode btn : this->visibleSongButtons) {
        if(btn == songButton || childrenContain(btn)) return true;
        if(btn.isGroup()) {
            for(const CarouselNode child : this->data.groups[btn.idx].children) {
                if(child == songButton || childrenContain(child)) return true;
            }
        }
    }
    return false;
}

SongBrowser::GroupList *SongBrowser::getCollectionButtonsForGroup(GroupType group) {
    switch(group) {
        case GroupType::ARTIST:
            return &this->artistCollectionButtons;
        case GroupType::CREATOR:
            return &this->creatorCollectionButtons;
        case GroupType::DIFFICULTY:
            return &this->difficultyCollectionButtons;
        case GroupType::LENGTH:
            return &this->lengthCollectionButtons;
        case GroupType::TITLE:
            return &this->titleCollectionButtons;
        case GroupType::BPM:
            return &this->bpmCollectionButtons;
        case GroupType::COLLECTIONS:
            return &this->collectionButtons;
        default:
            return nullptr;
    }
    return nullptr;
}

bool SongBrowser::scrollToBestButton() {
    const auto ifSelected = [this](CarouselNode node) -> CarouselNode {
        return node && this->isSelected(node) ? node : CarouselNode{};
    };
    const CarouselNode selDiff = ifSelected(this->selectionPreviousSongDiffButton);
    const CarouselNode selParent = ifSelected(this->selectionPreviousSongButton);
    const CarouselNode selCollection = ifSelected(this->selectionPreviousCollectionButton);

    const CarouselNode best = selDiff ? selDiff : selParent ? selParent : selCollection;

    if(!best) {
        this->carousel->scrollToTop();
        return true;
    }

    if(this->curGroup == GroupType::NO_GROUPING) {
        // trivial case
        if(this->isSearchMatch(best)) this->scrollToSongButton(best, false, true);
        return this->isSearchMatch(best);
    } else {
        if(best.isGroup()) {
            // nothing better to do
            if(this->isSearchMatch(best)) {
                this->scrollToSongButton(best);
            }
            return this->isSearchMatch(best);
        }

        auto *curCollBtnsUnprioritized = this->getCollectionButtonsForGroup(this->curGroup);
        if(!curCollBtnsUnprioritized) {
            this->carousel->scrollToTop();
            return true;
        }

        // FIXME: SHIT, the logic falls apart in GROUP_COLLECTIONS, since parents may or may not be collapsed depending on if the full beatmapset
        // was added to a certain collection or not! need to rework this garbage again
        const bool isAlphanumeric = this->isInAlphanumericCollection();

        CarouselNode selectedCollection;
        CarouselNode collectionContainingTarget;
        CarouselNode target;

        // XXX: special cases for collection grouping (same beatmap can be under multiple collection buttons!)
        // basically: if there is already a collection open, don't open another collection button with the same beatmap
        // a better way would be to build "candidates" no matter what group we're in and not just scroll to the first occurrence
        // AGHH this is so slow and ugly JUST SHOOT ME!
        std::vector<u32> curCollBtnsCopyPrioritized;
        curCollBtnsCopyPrioritized.reserve(curCollBtnsUnprioritized->size() + 1);
        if(this->curGroup == GroupType::COLLECTIONS && selCollection) {
            curCollBtnsCopyPrioritized.push_back(selCollection.idx);
        }
        Mc::ranges::append(curCollBtnsCopyPrioritized, *curCollBtnsUnprioritized);

        for(const u32 curColIdx : curCollBtnsCopyPrioritized) {
            const CarouselNode curColBtn = CarouselNode::group(curColIdx);
            if(this->isSelected(curColBtn)) {
                selectedCollection = curColBtn;
            }
            // no selected parent in alphanumeric or no selected difficulty in non-alphanumeric, scroll to current collection
            if((isAlphanumeric && !selParent) || (!isAlphanumeric && !selDiff)) {
                if(!selectedCollection) {
                    continue;
                } else {
                    collectionContainingTarget = curColBtn;
                    target = selectedCollection;
                    break;
                }
            }

            const auto &collChildren = this->data.groups[curColIdx].children;
            // alphanumeric grouping contains parent song buttons instead of difficulties directly
            // XXX: more special cases for collection grouping (they can be grouped in multiple ways)
            if(isAlphanumeric || this->curGroup == GroupType::COLLECTIONS) {
                if(selParent && Mc::ranges::contains(collChildren, selParent)) {
                    collectionContainingTarget = curColBtn;
                    if(!selDiff) {
                        target = selParent;
                        // no selected difficulty, scroll to parent
                        break;
                    }
                    if(Mc::ranges::contains(this->data.setChildren(selParent.idx), selDiff.idx)) {
                        // found difficulty
                        target = selDiff;
                        break;
                    }
                }
            }

            if(!target && (!isAlphanumeric || this->curGroup == GroupType::COLLECTIONS)) {
                if(selDiff && Mc::ranges::contains(collChildren, selDiff)) {
                    collectionContainingTarget = curColBtn;
                    // found difficulty
                    target = selDiff;
                    break;
                }
            }
        }

        if(target) {
            if(this->isSearchMatch(target)) {
                bool selectedOrDeselected = false;
                // deselect previous
                if(selectedCollection && collectionContainingTarget &&
                   selectedCollection != collectionContainingTarget) {
                    this->setSelected(selectedCollection, false);
                    selectedOrDeselected = true;
                }
                if(collectionContainingTarget && !this->isSelected(collectionContainingTarget)) {
                    this->select(collectionContainingTarget);
                    selectedOrDeselected = true;
                }
                if(target.isDiff() && selectedOrDeselected) {
                    this->requestNextScrollToSongButtonJumpFix(target.idx);
                }
                this->scrollToSongButton(target, false, true);
            }
            return this->isSearchMatch(target);
        } else {
            // TODO: only scroll to top if we literally don't see any buttons on screen
            this->carousel->scrollToTop();
            return true;
        }
    }
}

void SongBrowser::scrollToSongButton(CarouselNode songButton, bool alignOnTop, bool knownVisible) {
    if(!songButton || (!knownVisible && !this->isButtonVisible(songButton))) {
        return;
    }

    // NOTE: compensate potential scroll jump due to added/removed elements (feels a lot better this way, also easier on
    // the eyes)
    if(this->bNextScrollToSongButtonJumpFixScheduled) {
        this->bNextScrollToSongButtonJumpFixScheduled = false;

        float delta = 0.0f;
        {
            if(!this->bNextScrollToSongButtonJumpFixUseScrollSizeDelta)
                delta =
                    (this->getNodeY(songButton) - this->fNextScrollToSongButtonJumpFixOldRelPosY);  // (default case)
            else
                delta = (f32)(this->carousel->getScrollSize().y -
                              this->fNextScrollToSongButtonJumpFixOldScrollSizeY);  // technically not correct but feels
                                                                                    // a lot better for KEY_LEFT
                                                                                    // navigation
        }
        this->carousel->scrollToY((i32)(this->carousel->getRelPosY() - delta), false);
    }

    this->carousel->scrollToY(
        (i32)(-this->getNodeY(songButton) +
              (alignOnTop ? (0) : (this->carousel->getSize().y / 2 - CarouselButton::getScaledBaseSize().y / 2))));
}

void SongBrowser::rebuildSongButtons() {
    std::vector<CarouselNode> rows;
    rows.reserve(this->visibleSongButtons.size());

    const auto forEachChild = [this](CarouselNode node, auto &&fn) {
        if(node.isSet()) {
            for(const u32 diff : this->data.setChildren(node.idx)) fn(CarouselNode::diff(diff));
        } else if(node.isGroup()) {
            for(const CarouselNode child : this->data.groups[node.idx].children) fn(child);
        }
    };

    // NOTE: currently supports 3 depth layers (collection > beatmap > diffs)
    for(const CarouselNode button : this->visibleSongButtons) {
        if(likely(!this->isSelected(button) || !button.isSet())) {
            rows.push_back(button);
        }

        // if it's a collection button, recount the number of search-matching children to use as a label
        if(button.isGroup()) {
            i32 numVisibleDescendants = 0;
            for(const CarouselNode c : this->data.groups[button.idx].children) {
                if(c.isSet()) {
                    for(const u32 cc : this->data.setChildren(c.idx)) {
                        if(this->data.diffMatch[cc]) numVisibleDescendants++;
                    }
                } else if(this->isSearchMatch(c))
                    numVisibleDescendants++;
            }
            this->data.groups[button.idx].numVisibleChildren = numVisibleDescendants;
        }

        // children
        if(this->isSelected(button)) {
            forEachChild(button, [&](CarouselNode button2) {
                bool isButton2SearchMatch = false;
                if(button2.isSet()) {
                    for(const u32 button3 : this->data.setChildren(button2.idx)) {
                        if(this->data.diffMatch[button3]) {
                            isButton2SearchMatch = true;
                            break;
                        }
                    }
                } else
                    isButton2SearchMatch = this->isSearchMatch(button2);

                if(this->bInSearch && !isButton2SearchMatch) return;

                bool addedSingleChild = false;

                if(!(this->isSelected(button2) && button2.isSet())) {
                    if(button2.isSet() && this->getSetVisibility(button2.idx) == SetVisibility::SINGLE_CHILD) {
                        for(const u32 child : this->data.setChildren(button2.idx)) {
                            if(this->data.diffMatch[child]) {
                                addedSingleChild = true;
                                rows.push_back(CarouselNode::diff(child));
                                break;  // only one visible child
                            }
                        }
                    } else {
                        rows.push_back(button2);
                    }
                }

                // child children
                if(!addedSingleChild && this->isSelected(button2)) {
                    forEachChild(button2, [&](CarouselNode button3) {
                        if(this->bInSearch && !this->isSearchMatch(button3)) return;

                        if(!(this->isSelected(button3) && button3.isSet())) rows.push_back(button3);
                    });
                }
            });
        }
    }

    this->carousel->resetAnimations();
    std::vector<f32> rowY = this->layoutRows(rows);
    this->carousel->setRows(std::move(rows), std::move(rowY));
}

std::vector<f32> SongBrowser::layoutRows(const std::vector<CarouselNode> &rows) const {
    // only the y axis is set, because the x axis is constantly animated and handled within the button classes
    // themselves
    const f32 buttonHeight = CarouselButton::getScaledBaseSize().y;
    const f32 actualButtonHeight = CarouselButton::getScaledRowStep();

    f32 y = this->carousel->getSize().y / 4;
    if(rows.size() <= 1) y = this->carousel->getSize().y / 2;

    std::vector<f32> rowY;
    rowY.reserve(rows.size());

    bool isSelected = false;
    bool inOpenCollection = false;
    for(const CarouselNode carouselButton : rows) {
        // depending on the object type, layout differently
        const bool isDiffButton = carouselButton.isSong();
        const bool isCollectionButton = carouselButton.isGroup();
        const bool isIndependentDiffButton = isDiffButton && this->isIndependentDiff(carouselButton);
        const bool buttonSelected = this->isSelected(carouselButton);

        // give selected items & diffs a bit more spacing, to make them stand out
        if(((buttonSelected && !isCollectionButton) || isSelected || (isDiffButton && !isIndependentDiffButton)))
            y += buttonHeight * 0.1f;

        isSelected = buttonSelected || (isDiffButton && !isIndependentDiffButton);

        // give collections a bit more spacing at start & end
        if((buttonSelected && isCollectionButton)) y += buttonHeight * 0.2f;
        if(inOpenCollection && isCollectionButton && !buttonSelected) y += buttonHeight * 0.2f;
        if(isCollectionButton) {
            if(buttonSelected)
                inOpenCollection = true;
            else
                inOpenCollection = false;
        }

        rowY.push_back(y);

        y += actualButtonHeight;
    }
    return rowY;
}

void SongBrowser::updateSongButtonLayout() {
    // this rebuilds the entire songButton layout (songButtons in relation to others)
    this->carousel->setRowPositions(this->layoutRows(this->carousel->getRows()));
}

SongBrowser::SetVisibility SongBrowser::getSetVisibility(u32 set) const {
    // count visible children
    int visibleCount = 0;
    for(const u32 child : this->data.setChildren(set)) {
        if(this->data.diffMatch[child]) {
            visibleCount++;
        }
        if(visibleCount > 1) break;  // break early
    }

    if(visibleCount == 0) return SetVisibility::HIDDEN;
    if(visibleCount == 1) return SetVisibility::SINGLE_CHILD;
    return SetVisibility::SHOW_PARENT;
}

void SongBrowser::updateLayout() {
    ScreenBackable::updateLayout();

    const float dpiScale = Osu::getUIScale();
    const f32 margin = 5 * dpiScale;

    // topbar left
    this->topbarLeft->setSize((f32)SongBrowser::getUIScale(390.f), (f32)SongBrowser::getUIScale(145.f));

    this->songInfo->onResized();  // force update metrics
    this->songInfo->setRelPos(margin, margin);
    this->songInfo->setSize(
        this->topbarLeft->getSize().x - margin,
        std::max(this->topbarLeft->getSize().y * 0.75f, this->songInfo->getMinimumHeight() + margin));

    const f32 topbarLeftButtonMargin = 5 * dpiScale;
    const f32 topbarLeftButtonHeight = 30 * dpiScale;
    const f32 topbarLeftButtonWidth = 55 * dpiScale;

    const f32 dropdowns_width =
        this->topbarLeft->getSize().x - 3 * topbarLeftButtonMargin - (topbarLeftButtonWidth + topbarLeftButtonMargin);
    const f32 dropdowns_y = this->topbarLeft->getSize().y - topbarLeftButtonHeight;

    this->webButton->setSize(topbarLeftButtonWidth, topbarLeftButtonHeight);
    this->webButton->onResized();  // HACKHACK: framework bug (should update string metrics on setSize())
    this->webButton->setRelPos(this->topbarLeft->getSize().x - (topbarLeftButtonMargin + topbarLeftButtonWidth),
                               dropdowns_y);

    this->filterScoresDropdown->setSize((dropdowns_width / 2) - 1, topbarLeftButtonHeight);
    this->filterScoresDropdown->onResized();  // HACKHACK: framework bug (should update string metrics on setSize())
    this->filterScoresDropdown->setRelPos(topbarLeftButtonMargin, dropdowns_y);

    this->sortScoresDropdown->setSize((dropdowns_width / 2) - 1, topbarLeftButtonHeight);
    this->sortScoresDropdown->onResized();  // HACKHACK: framework bug (should update string metrics on setSize())
    this->sortScoresDropdown->setRelPos(topbarLeftButtonMargin + (dropdowns_width / 2) + 1, dropdowns_y);

    this->topbarLeft->update_pos();

    // topbar right
    this->topbarRight->setPosX((f32)osu->getVirtScreenWidth() / 2);
    this->topbarRight->setSize((f32)osu->getVirtScreenWidth() - this->topbarRight->getPos().x,
                               (f32)SongBrowser::getUIScale(80.f));

    // horizontal positioning by DPI makes no sense here if we want them to ever have a chance of lining up with skin elements
    this->sortButton->setSize(200.f * dpiScale, 30.f * dpiScale);
    this->sortButton->setRelPos((this->topbarRight->getSize() * (5.f / 6.f)).x - (this->sortButton->getSize().x / 2.f),
                                std::max(0.f, this->topbarRight->getSize().y - (2 * (30.f * dpiScale) + 10.f)));

    this->sortLabel->onResized();  // HACKHACK: framework bug (should update string metrics on setSizeToContent())
    this->sortLabel->setSizeToContent(-1, 0);
    this->sortLabel->setRelPos(this->sortButton->getRelPos().x - (this->sortLabel->getSize().x - 5.f),
                               this->sortButton->getRelPos().y - (this->sortButton->getSize().y / 6.f));

    this->groupButton->setSize(this->sortButton->getSize());
    this->groupButton->setRelPos(
        (this->topbarRight->getSize().x * (5.25f / 12.f)) - (this->groupButton->getSize().x / 2.f),
        this->sortButton->getRelPos().y);

    this->groupLabel->onResized();  // HACKHACK: framework bug (should update string metrics on setSizeToContent())
    this->groupLabel->setSizeToContent(-1, 0);
    this->groupLabel->setRelPos(this->groupButton->getRelPos().x - (this->groupLabel->getSize().x - 10.f),
                                this->groupButton->getRelPos().y - (this->groupButton->getSize().y / 6.f));

    // "hardcoded" group buttons
    const float group_margin = 10.f * dpiScale;
    const f32 group_btn_width =
        std::clamp<f32>((this->topbarRight->getSize().x - 2 * group_margin) / 4, 0.f, 200 * dpiScale);
    this->groupByCollectionBtn->setSize(group_btn_width - 1, 30 * dpiScale);
    this->groupByCollectionBtn->setRelPos(this->topbarRight->getSize().x - (group_margin + (4 * (group_btn_width + 1))),
                                          this->topbarRight->getSize().y - 30 * dpiScale);
    this->groupByArtistBtn->setSize(group_btn_width - 1, 30 * dpiScale);
    this->groupByArtistBtn->setRelPos(this->topbarRight->getSize().x - (group_margin + (3 * (group_btn_width + 1))),
                                      this->topbarRight->getSize().y - 30 * dpiScale);
    this->groupByDifficultyBtn->setSize(group_btn_width - 1, 30 * dpiScale);
    this->groupByDifficultyBtn->setRelPos(this->topbarRight->getSize().x - (group_margin + (2 * (group_btn_width + 1))),
                                          this->topbarRight->getSize().y - 30 * dpiScale);
    this->groupByNothingBtn->setSize(group_btn_width - 1, 30 * dpiScale);
    this->groupByNothingBtn->setRelPos(this->topbarRight->getSize().x - (group_margin + (1 * (group_btn_width + 1))),
                                       this->topbarRight->getSize().y - 30 * dpiScale);

    this->topbarRight->update_pos();

    // score browser
    this->updateScoreBrowserLayout();

    // song browser
    this->carousel->setPos(this->topbarLeft->getPos().x + this->topbarLeft->getSize().x + 1, 0);
    this->carousel->setSize(
        (f32)osu->getVirtScreenWidth() - (this->topbarLeft->getPos().x + this->topbarLeft->getSize().x),
        (f32)osu->getVirtScreenHeight());
    // the carousel extends underneath the top/bottom bars for translucent skins
    this->carousel->setVerticalScrollbarInsets(this->topbarRight->getPos().y + this->topbarRight->getSize().y,
                                               BottomBar::get_min_height());
    this->updateSongButtonLayout();

    this->search->setPos((f32)osu->getVirtScreenWidth() / 2, this->topbarRight->getSize().y + 8 * dpiScale);
    this->search->setSize((f32)osu->getVirtScreenWidth() / 2, 20 * dpiScale);
}

void SongBrowser::onBack() {
    if(BanchoState::is_in_a_multi_room()) {
        ui->setScreen(ui->getRoomScreen());

        // We didn't select a map; revert to previously selected one
        auto map = this->lastSelectedBeatmap;
        if(map != nullptr) {
            BanchoState::room.map_name =
                fmt::format("{:s} - {:s} [{:s}]", map->getArtist(), map->getTitle(), map->getDifficultyName());
            BanchoState::room.map_md5 = map->getMD5();
            BanchoState::room.map_id = map->getID();

            Packet packet;
            packet.id = OUTP_MATCH_CHANGE_SETTINGS;
            BanchoState::room.pack(packet);
            BANCHO::Net::send_packet(packet);

            ui->getRoomScreen()->on_map_change();
        }
    } else {
        ui->setScreen(ui->getMainMenu());
    }
}

void SongBrowser::updateScoreBrowserLayout() {
    const float dpiScale = Osu::getUIScale();

    const bool shouldScoreBrowserBeVisible =
        (cv::scores_enabled.getBool() && cv::songbrowser_scorebrowser_enabled.getBool());
    if(shouldScoreBrowserBeVisible != this->scoreBrowser->isVisible())
        this->scoreBrowser->setVisible(shouldScoreBrowserBeVisible);

    const f32 scoreButtonWidthMax = this->topbarLeft->getSize().x;

    f32 browserHeight = (f32)osu->getVirtScreenHeight() -
                        (BottomBar::get_height() + (this->topbarLeft->getPos().y + this->topbarLeft->getSize().y)) +
                        2 * dpiScale;
    this->scoreBrowser->setPos(this->topbarLeft->getPos().x + 2 * dpiScale,
                               this->topbarLeft->getPos().y + this->topbarLeft->getSize().y + 4 * dpiScale);
    this->scoreBrowser->setSize(scoreButtonWidthMax, browserHeight);
    const i32 scoreHeight = SongBrowser::getUIScale(53.f);

    // In stable, even when looking at local scores, there is space where the "personal best" would be.
    f32 personal_best_size = (f32)(scoreHeight + SongBrowser::getUIScale(61));
    browserHeight -= personal_best_size;
    this->scoreBrowser->setSize(this->scoreBrowser->getSize().x, browserHeight);
    this->scoreBrowser->setScrollSizeToContent();

    if(this->personalBestContainer->isVisible()) {
        this->personalBestContainer->setPos(this->scoreBrowser->getPos().x,
                                            this->scoreBrowser->getPos().y + this->scoreBrowser->getSize().y);
        this->personalBestContainer->setSize(this->scoreBrowser->getSize().x, personal_best_size);
        this->personalBestLabel->setRelPos(0, 0);
        this->personalBestLabel->setSize(this->scoreBrowser->getSize().x, 40);
        if(this->personalBestButton) {
            this->personalBestButton->setRelPos(0, 40);
            this->personalBestButton->setSize(this->scoreBrowser->getSize().x, (f32)scoreHeight);
        }
    }

    const std::vector<CBaseUIElement *> &elements = this->scoreBrowser->container.getElements();
    for(size_t i = 0; i < elements.size(); i++) {
        CBaseUIElement *scoreButton = elements[i];
        scoreButton->setSize(this->scoreBrowser->getSize().x, (f32)scoreHeight);
        scoreButton->setRelPos(0, (f32)i * scoreButton->getSize().y);
    }
    this->scoreBrowserScoresStillLoadingElement->setSize(this->scoreBrowser->getSize().x * 0.9f,
                                                         (f32)scoreHeight * 0.75f);
    this->scoreBrowserScoresStillLoadingElement->setRelPos(
        this->scoreBrowser->getSize().x / 2 - this->scoreBrowserScoresStillLoadingElement->getSize().x / 2,
        (browserHeight / 2) * 0.65f - this->scoreBrowserScoresStillLoadingElement->getSize().y / 2);
    this->scoreBrowserNoRecordsSetElement->setSize(this->scoreBrowser->getSize().x * 0.9f, (f32)scoreHeight * 0.75f);
    if(elements[0] == this->scoreBrowserNoRecordsSetElement) {
        this->scoreBrowserNoRecordsSetElement->setRelPos(
            this->scoreBrowser->getSize().x / 2 - this->scoreBrowserScoresStillLoadingElement->getSize().x / 2,
            (browserHeight / 2) * 0.65f - this->scoreBrowserScoresStillLoadingElement->getSize().y / 2);
    } else {
        this->scoreBrowserNoRecordsSetElement->setRelPos(
            this->scoreBrowser->getSize().x / 2 - this->scoreBrowserScoresStillLoadingElement->getSize().x / 2, 45);
    }
    this->personalBestContainer->update_pos();
    this->scoreBrowser->container.update_pos();
    this->scoreBrowser->setScrollSizeToContent();
}

void SongBrowser::onGotNewLeaderboard(const MD5Hash &lbHash) {
    if(!this->isVisible()) return;
    // rare scenario, but we could be called on the error path and now be logged out
    // assert(BanchoState::is_online());

    auto *map = osu->getMapInterface()->getBeatmap();
    if(!map) return;
    // skip rebuild requests if we have switched off of the map in the meantime
    if(map->getMD5() != lbHash) return;

    // update song info and web button if we got a new map(set) id on leaderboard response
    if((this->songInfo->getBeatmapID() != map->getID()) || (this->songInfo->getBeatmapSetID() != map->getSetID())) {
        this->songInfo->setFromBeatmap(map);
        this->webButton->setVisible((this->songInfo->getBeatmapID() > 0) || (this->songInfo->getBeatmapSetID() > 0));
    }

    this->rebuildScoreButtons();
}

void SongBrowser::refreshGrades() {
    for(const auto &[md5, diff] : *this->hashToDiffButton)
        this->data.setGrade[this->data.diffSet[diff]] = ScoreGrade::N;
    for(const auto &[md5, diff] : *this->hashToDiffButton) this->updateGrade(diff);
}

void SongBrowser::rebuildScoreButtons() {
    if(!this->isVisible()) return;

    // XXX: When online, it would be nice to scroll to the current user's highscore

    // reset
    this->scoreBrowser->invalidate();
    this->personalBestContainer->invalidate();
    this->personalBestContainer->setVisible(false);
    SAFE_DELETE(this->personalBestButton);
    // (a menu opened from a score button would act on a deleted button, or one that shows another score below)
    this->contextMenu->setVisible2(false);
    this->pendingScores.reset();

    // until buildScoreButtons() has everything it needs
    this->scoreBrowser->container.addBaseUIElement(this->scoreBrowserScoresStillLoadingElement);
    this->updateScoreBrowserLayout();

    auto pending = std::make_unique<PendingScores>();
    pending->map = osu->getMapInterface()->getBeatmap();
    if(const auto *map = pending->map) {
        const MD5Hash &mapHash = map->getMD5();
        const bool is_online = (BanchoState::is_online() || BanchoState::is_logging_in()) &&
                               cv::songbrowser_scores_filteringtype.getString() != _("Local");

        if(is_online) {
            const auto &search = db->getOnlineLeaderboards().find(mapHash);
            if(search == db->getOnlineLeaderboards().end()) {
                // onGotNewLeaderboard() comes back here
                BANCHO::Leaderboard::fetch_online_scores(map);
                return;
            }
            pending->lb = search->second;
            const auto &personal_best = pending->lb.personal_best;
            auto &scores = pending->lb.scores;

            // show passes that are still being submitted where the server will list them
            const bool mods_filter = cv::songbrowser_scores_filteringtype.getString() == _("Selected mods");
            for(FinishedScore &sc : BANCHO::Net::get_pending_scores(mapHash)) {
                if(!sc.passed) continue;
                if(mods_filter && sc.mods.to_legacy() != ui->getModSelector()->getModFlags()) continue;

                // the server lists one score per player, and which one it keeps depends on how it ranks them:
                // only show a pending one for a player without a score yet (which can be outside of the listed ones)
                if((personal_best && personal_best->player_id == sc.player_id) ||
                   Mc::ranges::contains(scores, sc.player_id, &FinishedScore::player_id)) {
                    continue;
                }
                scores.push_back(std::move(sc));
            }

            // without online scores, "No records set!" is already displayed in the list
            pending->personal_best_box = !scores.empty();
        } else {
            Sync::shared_lock lock(db->scores_mtx);
            if(const auto &local_scores = db->getScores().find(mapHash); local_scores != db->getScores().end()) {
                pending->lb.scores = local_scores->second;
            }
        }
    }

    this->pendingScores = std::move(pending);
    this->buildScoreButtons();
}

void SongBrowser::buildScoreButtons() {
    const auto *map = this->pendingScores->map;
    // the selection moved on while the song browser was hidden (pp is only calculated for the selected map)
    if(map != osu->getMapInterface()->getBeatmap()) return this->rebuildScoreButtons();
    auto &lb = this->pendingScores->lb;

    // pp shown on the buttons (always the case when they're sorted by it) has to be known before they're built,
    // so that nothing changes or reorders once they're shown
    if(map && ScoreButton::showsPP()) {
        bool all_known = true;
        const auto resolve = [map, &all_known](FinishedScore &sc) {
            if(sc.get_pp() != -1.0) return;
            if(sc.get_or_calc_pp(map) != -1.0) {
                db->setScorePP(sc);
            } else {
                all_known = false;
            }
        };
        for(auto &sc : lb.scores) resolve(sc);
        if(lb.personal_best) resolve(*lb.personal_best);

        // polled again on the next tick
        if(!all_known) return;
    }

    const std::unique_ptr<PendingScores> pending = std::move(this->pendingScores);
    std::vector<FinishedScore> &scores = pending->lb.scores;
    const int numScores = (int)scores.size();

    this->scoreBrowser->invalidate();

    if(numScores > 1) {
        Mc::ranges::sort(scores, this->getScoreSortingMethod().comparator);
    }

    // top up cache as necessary
    if(numScores > this->scoreButtonCache.size()) {
        const int numNewButtons = (int)(numScores - this->scoreButtonCache.size());
        for(size_t i = 0; std::cmp_less(i, numNewButtons); i++) {
            auto *scoreButton = new ScoreButton(this->contextMenu, 0, 0, 0, 0);
            scoreButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onScoreClicked>(this));
            this->scoreButtonCache.push_back(scoreButton);
        }
    }

    // and build the ui
    if(numScores < 1) {
        // NOTE(spec): not sure if it was a typo, but the code here was using scoreBrowserScoresStillLoadingElement's position in both cases
        //             just leaving it like that for now...
        this->scoreBrowser->container.addBaseUIElement(this->scoreBrowserNoRecordsSetElement,
                                                       this->scoreBrowserScoresStillLoadingElement->getRelPos());
    } else {
        for(int i = 0; i < numScores; i++) {
            ScoreButton *button = this->scoreButtonCache[i];
            button->setScore(scores[i], map, i + 1);
            button->setVisible(false);
            this->scoreBrowser->container.addBaseUIElement(button);
            button->resetHighlight();
        }
    }

    if(pending->personal_best_box) {
        this->personalBestContainer->addBaseUIElement(this->personalBestLabel);
        if(pending->lb.personal_best) {
            this->personalBestButton = new ScoreButton(this->contextMenu, 0, 0, 0, 0);
            this->personalBestButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onScoreClicked>(this));
            this->personalBestButton->setScore(*pending->lb.personal_best, map, pending->lb.personal_best_rank);
            this->personalBestButton->resetHighlight();
            this->personalBestButton->setDrawsOnTop(true);
            this->personalBestContainer->addBaseUIElement(this->personalBestButton);
        } else {
            this->personalBestContainer->addBaseUIElement(this->scoreBrowserNoRecordsSetElement);
        }
        this->personalBestContainer->setVisible(true);
    }

    // layout
    this->updateScoreBrowserLayout();

    // update grade of difficulty button for current map
    // (weird place for this to be, i think the intent is to update them after you set a score)
    if(!map) return;
    if(const auto &it = this->hashToDiffButton->find(map->getMD5()); it != this->hashToDiffButton->end()) {
        this->updateGrade(it->second);
    }
}

void SongBrowser::updateSearchIndex(f64 budget) {
    if(this->searchIndexedDiffs == this->data.diffMap.size()) return;
    if(this->searchIndex.use_count() > 1) {
        this->searchIndex = std::make_shared<AsyncSongButtonMatcher::SearchIndex>(*this->searchIndex);
    }

    const f64 deadline = Timing::getTimeReal() + budget;
    while(this->searchIndexedDiffs < this->data.diffMap.size()) {
        const u32 diff = this->searchIndexedDiffs++;
        if(const DatabaseBeatmap *map = this->data.diffMap[diff]) this->searchIndex->add(*map, diff);
        if((diff & 255) == 255 && Timing::getTimeReal() > deadline) break;
    }
}

void SongBrowser::checkHandleKillBackgroundSearchMatcher() {
    this->searchHandle.cancel();
    if(this->searchHandle.valid()) this->searchHandle.wait();
}

void SongBrowser::initializeGroupingButtons() {
    const auto mkcbtn = [this](std::string name) -> u32 {
        this->data.groups.emplace_back().name = std::move(name);
        return (u32)this->data.groups.size() - 1;
    };
    // artist, title, creator
    for(auto *coll : {&this->artistCollectionButtons, &this->titleCollectionButtons, &this->creatorCollectionButtons}) {
        if(coll->size() == 28) continue;
        coll->resize(28);

        // 0-9
        {
            coll->at(0) = mkcbtn(_("0-9"));
        }

        // A-Z
        for(size_t i = 0; i < 26; i++) {
            coll->at(i + 1) = mkcbtn(fmt::format("{:c}", 'A' + i));
        }

        // Other
        {
            coll->at(27) = mkcbtn(_("Other"));
        }
    }

    // difficulty
    if(auto &diffbtns = this->difficultyCollectionButtons; diffbtns.size() != 12) {
        diffbtns.resize(12);
        for(size_t i = 0; i < 12; i++) {
            std::string difficultyCollectionName;
            if(i < 1)
                difficultyCollectionName = _("Below 1 star");
            else if(i > 10)
                difficultyCollectionName = _("Above 10 stars");
            else if(i == 1)
                difficultyCollectionName = _("1 star");
            else
                // TRANSLATORS: song browser group of beatmaps by star rating, {:d} is between 2 and 10
                difficultyCollectionName = tformat("{:d} stars", i);

            diffbtns[i] = mkcbtn(std::move(difficultyCollectionName));
        }
    }

    // bpm
    if(auto &bpmbtns = this->bpmCollectionButtons; bpmbtns.size() != 6) {
        bpmbtns.resize(6);
        bpmbtns[0] = mkcbtn(_("Under 60 BPM"));
        bpmbtns[1] = mkcbtn(_("Under 120 BPM"));
        bpmbtns[2] = mkcbtn(_("Under 180 BPM"));
        bpmbtns[3] = mkcbtn(_("Under 240 BPM"));
        bpmbtns[4] = mkcbtn(_("Under 300 BPM"));
        bpmbtns[5] = mkcbtn(_("Over 300 BPM"));
    }

    // dateadded
    {
        // TODO: annoying
    }

    // length
    if(auto &lenbtns = this->lengthCollectionButtons; lenbtns.size() != 7) {
        lenbtns.resize(7);
        lenbtns[0] = mkcbtn(_("1 minute or less"));
        lenbtns[1] = mkcbtn(_("2 minutes or less"));
        lenbtns[2] = mkcbtn(_("3 minutes or less"));
        lenbtns[3] = mkcbtn(_("4 minutes or less"));
        lenbtns[4] = mkcbtn(_("5 minutes or less"));
        lenbtns[5] = mkcbtn(_("10 minutes or less"));
        lenbtns[6] = mkcbtn(_("Over 10 minutes"));
    }
}

void SongBrowser::onDatabaseLoadingFinished(bool isNextScreenSongBrowser) {
    // loose .osz files dropped into maps/ were already extracted (Database::importLooseOsz) and imported
    // (Database::reconcileRoot) by the loader, so by the time we get here the database already contains them.

    Timer t;
    t.start();

    debugLog("Loading {:d} beatmapsets from database.", db->getBeatmapSets().size());

    // initialize all collection (grouped) buttons
    this->initializeGroupingButtons();

    // add all beatmaps (build buttons)
    {
        const size_t numSets = db->getBeatmapSets().size();
        size_t numDiffs = 0;
        for(const auto &mapset : db->getBeatmapSets()) {
            numDiffs += mapset->getDifficulties().size();
        }
        this->setOrder.reserve(numSets);
        this->data.reserve(numSets, numDiffs);
        this->hashToDiffButton->reserve(numDiffs);
        for(const auto &mapset : db->getBeatmapSets()) {
            this->addBeatmapSet(mapset.get(), true /* initial songbrowser load flag (skip some checks) */);
        }
    }

    // build collections
    this->recreateCollectionsButtons();

    this->bInitializedBeatmaps = true;
    this->bSongButtonsNeedSorting = true;

    this->onSortChange("", cv::songbrowser_sortingtype.getInt());
    this->onGroupChange("", this->curGroup);  // does nothing besides re-highlight the buttons
    this->onSortScoresChange("", cv::songbrowser_scores_sortingtype.getInt());

    // update rich presence (discord total pp)
    RichPresence::onSongBrowser();

    // update user name/stats
    osu->onUserCardChange(BanchoState::get_username());

    if(cv::songbrowser_search_hardcoded_filter.getString().length() > 0) {
        this->onSearchUpdate();
    }

    // necessary long variable name to document spaghetti songbrowser database load
    // maybe not actually resulting in songbrowser being opened after this function is called
    bool wasMusicPausedBeforeSongBrowserLoadAndNextScreenIsntSongbrowser = false;
    if(!isNextScreenSongBrowser) {
        if(!osu->getMusicTrack()->isEmpty() && !osu->getMusicTrack()->isPlaying()) {
            wasMusicPausedBeforeSongBrowserLoadAndNextScreenIsntSongbrowser = true;
        }
    }

    // ugly hack to transition from preloaded main menu beatmap to database-loaded beatmap without pausing music
    {
        DatabaseBeatmap *reselectMap = nullptr;
        if(this->reselect) {
            // the same file, edited or not, else the same content wherever it went
            reselectMap = db->getBeatmapDifficulty(this->reselect->md5);
            if(!reselectMap || db->locate(*reselectMap) != this->reselect->where) {
                if(DatabaseBeatmap *atLocation = db->getBeatmapDifficulty(this->reselect->where)) {
                    reselectMap = atLocation;
                }
            }
            if(!reselectMap) {
                // FIXME: why does this happen? every time i see this log, the md5hash is from a map that we never load in
                // from the database... where are we loading these stray .osu files from
                debugLog("failed to get reselect map for {}", this->reselect->md5);
            }
        }

        this->onDifficultySelected(reselectMap, false);  // select even if null (clear existing)
        if(reselectMap) {
            this->selectSelectedBeatmapSongButton();
        }
        this->reselect.reset();
    }

    // ok, if we still haven't selected a song, do so now (unless a pending score is about to select its own)
    if(osu->getMapInterface()->getBeatmap() == nullptr && !osu->isScoreOpenPending()) {
        this->selectRandomBeatmap();
    }

    if(wasMusicPausedBeforeSongBrowserLoadAndNextScreenIsntSongbrowser) {
        osu->getMusicTrack()->pause();
    }

    t.update();
    debugLog("Took {} seconds.", t.getElapsedTime());

    // Watch for new maps now
    this->mapsWatch = directoryWatcher->watch_directory(Mc::Paths::maps() + "/", [](const FileChangeEvent &ev) {
        // a set folder dropped in (or changed, or removed) while running: the installer syncs the db and the
        // carousel with it like with an import, and tells its own writes (imports, uninstalls) and what its passes
        // saw already apart from real changes. a deletion can't be stat'ed (so on windows it isn't known to be a folder), reconciling a name
        // that isn't a set folder (a deleted .osz) is a no-op
        if(ev.is_dir || ev.type == FileChangeType::DELETED) {
            osu->getBeatmapInstaller()->enqueue_folder(Environment::getFileNameFromFilePath(ev.path));
            return;
        }

        if(ev.type != FileChangeType::CREATED) return;
        if(SString::to_lower(env->getFileExtensionFromFilePath(ev.path)) != "osz") return;
        logRaw("[DirectoryWatcher] Importing new beatmap {}", ev.path);

        // drop-zone file: import async via the installer, navigate to it, and delete the .osz on success.
        osu->getBeatmapInstaller()->enqueue_local(ev.path, /*auto_select=*/true, /*delete_after=*/true);
    });
}

void SongBrowser::onSearchUpdate() {
    const std::string hardcodedFilterString{cv::songbrowser_search_hardcoded_filter.getString()};  // NOLINT
    const bool hasHardcodedSearchStringChanged = (this->sPrevHardcodedSearchString != hardcodedFilterString);
    const std::string &searchString = this->search->getSearchString();
    const bool hasSearchStringChanged = (this->sPrevSearchString != searchString);

    const bool prevInSearch = this->bInSearch;
    this->bInSearch = (!searchString.empty() || !hardcodedFilterString.empty());
    const bool hasInSearchChanged = (prevInSearch != this->bInSearch);

    if(this->bInSearch) {
        const bool shouldRefreshMatches =
            hasSearchStringChanged || hasHardcodedSearchStringChanged || hasInSearchChanged;

        // GROUP_COLLECTIONS is the only group that can filter beatmaps, so skip some work if we're not switching between that and something else
        this->bShouldRecountMatchesAfterSearch =
            this->bShouldRecountMatchesAfterSearch ||             //
            shouldRefreshMatches ||                               //
            (!this->searchPrevGroup.has_value() ||                //
             (this->curGroup != this->searchPrevGroup.value() &&  //
              (this->curGroup == GroupType::COLLECTIONS || this->searchPrevGroup.value() == GroupType::COLLECTIONS)));

        this->searchPrevGroup = this->curGroup;

        // flag all search matches across entire database
        if(shouldRefreshMatches) {
            this->submitSearch();
        } else
            this->rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(true, true);

        // (results are handled in update() once available)
    } else  // exit search
    {
        // exiting the search does not need any async work, so we can just directly do it in here

        // stop potentially running async search
        this->checkHandleKillBackgroundSearchMatcher();

        // reset container and visible buttons list
        this->carousel->clearRows();
        this->visibleSongButtons.clear();

        // reset all search flags
        std::ranges::fill(this->data.diffMatch, true);
        this->updateSetRepresentatives();

        // remember which tab was selected, instead of defaulting back to no grouping
        // this also rebuilds the visible buttons list
        if(this->searchPrevGroup.has_value()) {
            auto prevgid = this->searchPrevGroup.value();
            this->onGroupChange(GROUP_NAMES[prevgid], prevgid);
        }
    }

    this->sPrevSearchString = searchString;
    this->sPrevHardcodedSearchString = cv::songbrowser_search_hardcoded_filter.getString();
}

void SongBrowser::submitSearch() {
    // stop potentially running async search
    this->checkHandleKillBackgroundSearchMatcher();
    this->bShouldRecountMatchesAfterSearch = true;
    this->updateSearchIndex(std::numeric_limits<f64>::infinity());

    std::string query = AsyncSongButtonMatcher::combineQuery(this->search->getSearchString(),
                                                             cv::songbrowser_search_hardcoded_filter.getString());
    std::vector<f32> stars;
    std::shared_ptr<const AsyncSongButtonMatcher::SearchResult> previous = this->lastSearch;
    if(AsyncSongButtonMatcher::filtersByStars(query)) {
        stars.reserve(this->searchIndex->size());
        for(const u32 diff : this->searchIndex->ids) {
            const DatabaseBeatmap *map = this->data.diffMap[diff];
            stars.push_back(map ? map->getStarRating(StarPrecalc::active_idx) : 0.f);
        }
        previous.reset();
    }

    this->searchHandle =
        AsyncSongButtonMatcher::submitSearchMatch(this->searchIndex, std::move(stars), std::move(query),
                                                  osu->getMapInterface()->getSpeedMultiplier(), std::move(previous));
}

void SongBrowser::rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(bool scrollToTop,
                                                                                bool doRebuildSongButtons) {
    // reset container and visible buttons list
    this->carousel->clearRows();
    this->visibleSongButtons.clear();

    // optimization: currently, only grouping by collections can actually filter beatmaps
    // so don't reset search matches if switching between any other grouping mode
    const bool recountMatches = this->bShouldRecountMatchesAfterSearch && this->bInSearch;
    const bool canBreakEarly = !recountMatches;
    if(recountMatches) {
        this->currentVisibleSearchMatches = 0;
        // don't re-count again next time
        this->bShouldRecountMatchesAfterSearch = false;
    }

    // use flagged search matches to rebuild visible song buttons
    if(this->curGroup == GroupType::NO_GROUPING) {
        for(const u32 parentButton : this->setOrder) {
            const SetVisibility visibility = this->getSetVisibility(parentButton);

            switch(visibility) {
                case SetVisibility::HIDDEN:
                    // don't add to carousel
                    break;

                case SetVisibility::SINGLE_CHILD:
                    // add only the visible child
                    for(const u32 child : this->data.setChildren(parentButton)) {
                        if(this->data.diffMatch[child]) {
                            if(recountMatches) this->currentVisibleSearchMatches++;
                            this->visibleSongButtons.push_back(CarouselNode::diff(child));
                            break;  // only one visible child
                        }
                    }
                    break;

                case SetVisibility::SHOW_PARENT:
                    // add parent (which will expand to show children if selected)
                    if(recountMatches) {
                        for(const u32 child : this->data.setChildren(parentButton)) {
                            if(this->data.diffMatch[child]) {
                                this->currentVisibleSearchMatches++;
                            }
                        }
                    }
                    this->visibleSongButtons.push_back(CarouselNode::set(parentButton));
                    break;
            }
        }
    } else {
        if(auto *groupButtons = this->getCollectionButtonsForGroup(this->curGroup); !!groupButtons) {
            for(const u32 groupButton : *groupButtons) {
                bool isAnyMatchInGroup = false;

                const auto &children = this->data.groups[groupButton].children;
                for(const CarouselNode c : children) {
                    if(c.isSet()) {
                        for(const u32 cc : this->data.setChildren(c.idx)) {
                            if(this->data.diffMatch[cc]) {
                                isAnyMatchInGroup = true;
                                // also count total matching children while we're here
                                // break out early if we're not searching, though
                                if(canBreakEarly) {
                                    break;
                                } else {
                                    this->currentVisibleSearchMatches++;
                                }
                            }
                        }

                        if(canBreakEarly && isAnyMatchInGroup) break;
                    } else if(this->isSearchMatch(c)) {
                        isAnyMatchInGroup = true;
                        if(canBreakEarly) {
                            break;
                        } else {
                            this->currentVisibleSearchMatches++;
                        }
                    }
                }

                if(isAnyMatchInGroup || !this->bInSearch) {
                    this->visibleSongButtons.push_back(CarouselNode::group(groupButton));
                }
            }
        }
    }

    if(doRebuildSongButtons) this->rebuildSongButtons();

    // scroll to top search result, or auto select the only result
    if(scrollToTop && !this->visibleSongButtons.empty()) {
        if(this->visibleSongButtons.size() == 1 && !this->isSelected(this->visibleSongButtons[0])) {
            this->selectSongButton(this->visibleSongButtons[0]);
        } else if(!this->scrollToBestButton()) {
            // scroll to the currently selected button if it's a search match, otherwise the first one
            this->scrollToSongButton(this->visibleSongButtons[0]);
        }
    }
}

void SongBrowser::onFilterScoresClicked(CBaseUIButton *button) {
    static const std::array filters{_("Local"), _("Global"), _("Selected mods"), _("Country"), _("Friends"), _("Team")};

    this->contextMenu->setPos(button->getPos());
    this->contextMenu->setRelPos(button->getRelPos());
    this->contextMenu->begin((i32)button->getSize().x);
    {
        if(BanchoState::is_online()) {
            for(const auto &filter : filters) {
                CBaseUIButton *button = this->contextMenu->addButton(filter);
                if(filter == cv::songbrowser_scores_filteringtype.getString()) {
                    button->setTextBrightColor(0xff00ff00);
                }
            }
        } else {
            CBaseUIButton *button = this->contextMenu->addButton(_("Local"));
            button->setTextBrightColor(0xff00ff00);
        }
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onFilterScoresChange>(this));
}

void SongBrowser::onSortScoresClicked(CBaseUIButton *button) {
    this->contextMenu->setPos(button->getPos());
    this->contextMenu->setRelPos(button->getRelPos());
    this->contextMenu->begin((i32)button->getSize().x);
    {
        int i = 0;
        for(const auto &scoreSortingMethod : this->SCORE_SORTING_METHODS) {
            CBaseUIButton *button = this->contextMenu->addButton(std::string{scoreSortingMethod.name}, i);
            if(i == cv::songbrowser_scores_sortingtype.getInt()) button->setTextBrightColor(0xff00ff00);
            i++;
        }
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onSortScoresChange>(this));
}

void SongBrowser::onFilterScoresChange(std::string_view text, int id) {
    std::string text_to_set{text};
    auto *type_cv = &cv::songbrowser_scores_filteringtype;
    auto *manual_type_cv = &cv::songbrowser_scores_filteringtype_manual;

    // abusing "id" to determine whether it was a click callback or due to login
    if(id != LOGIN_STATE_FILTER_ID) {
        manual_type_cv->setValue(text);
    }

    // always change for manual setting, otherwise allow login state to affect filtering (if it was never manually set)
    const bool should_change = id != LOGIN_STATE_FILTER_ID || (manual_type_cv->isDefault());
    if(!should_change) {
        text_to_set = std::string{manual_type_cv->getString()};
    }
    type_cv->setValue(text_to_set);  // NOTE: remember

    if(id == LOGIN_STATE_FILTER_ID) this->refreshGrades();

    this->filterScoresDropdown->setText(text_to_set);
    db->getOnlineLeaderboards().clear();
    this->rebuildScoreButtons();
    this->scoreBrowser->scrollToTop();
}

const SongBrowser::SCORE_SORTING_METHOD &SongBrowser::getScoreSortingMethod() const {
    const u32 sortIndex = cv::songbrowser_scores_sortingtype.getInt();
    return this->SCORE_SORTING_METHODS[sortIndex < this->SCORE_SORTING_METHODS.size() ? sortIndex
                                                                                      : DEFAULT_SCORE_SORTING_INDEX];
}

void SongBrowser::onSortScoresChange(std::string_view /*text*/, int id) {
    if(id < 0 || id >= this->SCORE_SORTING_METHODS.size()) id = DEFAULT_SCORE_SORTING_INDEX;

    cv::songbrowser_scores_sortingtype.setValue(id);  // NOTE: remember
    this->sortScoresDropdown->setText(std::string{this->SCORE_SORTING_METHODS[id].name});
    this->rebuildScoreButtons();
    this->scoreBrowser->scrollToTop();
    ui->getHUD()->updateScoringMetric();
}

void SongBrowser::onWebClicked(CBaseUIButton * /*button*/) {
    const i32 map_id = this->songInfo->getBeatmapID();
    const i32 set_id = this->songInfo->getBeatmapSetID();
    if((map_id > 0) || (set_id > 0)) {
        const bool use_set_id = map_id <= 0;
        const i32 real_id = use_set_id ? set_id : map_id;
        const auto b_or_s_endpoint = use_set_id ? "s" : "b";

        const auto scheme = cv::use_https.getBool() ? "https://" : "http://";
        const auto endpoint = BanchoState::is_online() ? BanchoState::endpoint : "ppy.sh";
        osu->openURLWithNotification(fmt::format("{:s}osu.{:s}/{:s}/{:d}", scheme, endpoint, b_or_s_endpoint, real_id));
    }
}

void SongBrowser::onQuickGroupClicked(CBaseUIButton *button, MouseButtonFlags buttons) {
    if(flags::has<MouseButtonFlags::MF_RIGHT>(buttons) || button->getText().empty()) return;
    const std::string_view btntxt = button->getText();

    for(int gid = -1; const auto &gname : GROUP_NAMES) {
        ++gid;
        if(btntxt == gname) {
            this->onGroupChange(gname, gid);
            break;
        }
    }
}

void SongBrowser::onGroupClicked(CBaseUIButton *button) {
    this->contextMenu->setPos(button->getPos());
    this->contextMenu->setRelPos(button->getRelPos());
    this->contextMenu->begin((i32)button->getSize().x);
    {
        for(int gid = -1; const auto &gname : GROUP_NAMES) {
            ++gid;
            CBaseUIButton *button = this->contextMenu->addButton(std::string{gname}, gid);
            if(gid == this->curGroup) button->setTextBrightColor(0xff00ff00);
        }
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onGroupChange>(this));
}

void SongBrowser::onGroupChange(std::string_view /*text*/, int id) {
    auto group_id = this->curGroup;
    if(id >= 0 && id < GroupType::MAX) group_id = (GroupType)id;

    // update group combobox button text
    this->groupButton->setText(std::string{GROUP_NAMES[group_id]});

    // set highlighted colour
    this->groupByCollectionBtn->setTextBrightColor(defaultColor);
    this->groupByArtistBtn->setTextBrightColor(defaultColor);
    this->groupByDifficultyBtn->setTextBrightColor(defaultColor);
    this->groupByNothingBtn->setTextBrightColor(defaultColor);

    switch(group_id) {
        case GroupType::ARTIST:
            this->groupByArtistBtn->setTextBrightColor(highlightColor);
            break;
        case GroupType::DIFFICULTY:
            this->groupByDifficultyBtn->setTextBrightColor(highlightColor);
            break;
        case GroupType::COLLECTIONS:
            this->groupByCollectionBtn->setTextBrightColor(highlightColor);
            break;
        case GroupType::NO_GROUPING:
        default:
            this->groupByNothingBtn->setTextBrightColor(highlightColor);
            break;
    }

    rebuildAfterGroupOrSortChange(group_id);
}

void SongBrowser::onSortClicked(CBaseUIButton *button) {
    this->contextMenu->setPos(button->getPos());
    this->contextMenu->setRelPos(button->getRelPos());
    this->contextMenu->begin((i32)button->getSize().x);

    // Order the map sorting methods by name but keep the same sort_id
    std::array<std::string_view, SortType::MAX> sorting_names;
    for(int i = -1; const auto &sortmeth : MAP_SORTING_METHODS) {
        ++i;
        sorting_names[i] = sortmeth.name;
    }
    Mc::ranges::sort(sorting_names, SString::strcase_comp);
    for(const auto &name : sorting_names) {
        for(int stype = -1; const auto &sortmeth : MAP_SORTING_METHODS) {
            ++stype;
            if(name == sortmeth.name) {
                CBaseUIButton *button = this->contextMenu->addButton(std::string{name}, stype);
                if(stype == this->curSortMethod) button->setTextBrightColor(0xff00ff00);
            }
        }
    }

    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onSortChange>(this));
}

void SongBrowser::onSortChange(std::string_view /*text*/, int id) {
    auto sort_id = this->curSortMethod;
    if(id >= 0 && id < SortType::MAX) sort_id = (SortType)id;

    MAP_SORTING_METHOD newMethod = MAP_SORTING_METHODS[sort_id];
    const bool sortChanged = sort_id != this->curSortMethod;

    this->sortButton->setText(std::string{newMethod.name});
    cv::songbrowser_sortingtype.setValue(sort_id);

    // reuse the group update logic instead of duplicating it
    if(!sortChanged) {
        this->rebuildAfterGroupOrSortChange(this->curGroup);
    } else {
        this->rebuildAfterGroupOrSortChange(this->curGroup, sort_id);
    }
}

void SongBrowser::rebucketDifficultyCollections() {
    if(this->difficultyCollectionButtons.size() != 12) return;

    for(const u32 btn : this->difficultyCollectionButtons) this->data.groups[btn].children.clear();

    for(const u32 parentBtn : this->setOrder) {
        for(const u32 child : this->data.setChildren(parentBtn)) {
            const DatabaseBeatmap *diff = this->data.diffMap[child];
            const float stars = diff->getStarRating(StarPrecalc::active_idx);
            const int idx =
                std::clamp<int>((std::isfinite(stars) && stars >= static_cast<float>(std::numeric_limits<int>::min()) &&
                                 stars <= static_cast<float>(std::numeric_limits<int>::max()))
                                    ? static_cast<int>(stars)
                                    : 0,
                                0, 11);
            this->data.groups[this->difficultyCollectionButtons[idx]].children.push_back(CarouselNode::diff(child));
        }
    }
}

void SongBrowser::rebuildAfterGroupOrSortChange(GroupType group, const std::optional<SortType> &sortMethod) {
    // TODO: with "No Grouping", some sorting methods randomly fail...?
    const SortType newSortMethod = sortMethod.value_or(this->curSortMethod);
    const bool sortingChanged = this->curSortMethod != newSortMethod;
    const bool groupingChanged = this->curGroup != group;

    const bool diffSortChanged = (newSortMethod == SortType::DIFFICULTY || group == GroupType::DIFFICULTY) &&
                                 this->lastDiffSortModIndex != StarPrecalc::active_idx;

    if(!this->bSongButtonsNeedSorting && !sortingChanged && !groupingChanged && !diffSortChanged &&
       !this->visibleSongButtons.empty()) {
        return;
    }

    this->curGroup = group;
    this->curSortMethod = newSortMethod;
    this->lastDiffSortModIndex = StarPrecalc::active_idx;
    const bool forceSort = this->bSongButtonsNeedSorting;

    if(forceSort || sortingChanged || diffSortChanged) {
        // lazy update grade
        if(this->curSortMethod == SortType::RANKACHIEVED) {
            for(const u32 set : this->setOrder) this->maybeUpdateSetGrades(set);
        }
        // the master button list should be sorted for all groupings
        sort_by_keys(this->setOrder, this->curSortMethod, [this](u32 set) {
            return std::pair{this->getBeatmap(CarouselNode::set(set)), this->data.setGrade[set]};
        });
        this->bSongButtonsNeedSorting = false;
    }

    this->visibleSongButtons.clear();

    if(group == GroupType::NO_GROUPING) {
        this->visibleSongButtons.reserve(this->setOrder.size());

        // apply visibility logic
        for(const u32 parentButton : this->setOrder) {
            const SetVisibility visibility = this->getSetVisibility(parentButton);

            switch(visibility) {
                case SetVisibility::HIDDEN:
                    break;

                case SetVisibility::SINGLE_CHILD:
                    for(const u32 child : this->data.setChildren(parentButton)) {
                        if(this->data.diffMatch[child]) {
                            this->visibleSongButtons.push_back(CarouselNode::diff(child));
                            break;
                        }
                    }
                    break;

                case SetVisibility::SHOW_PARENT:
                    this->visibleSongButtons.push_back(CarouselNode::set(parentButton));
                    break;
            }
        }
    } else {
        if(auto *collBtns = this->getCollectionButtonsForGroup(group); !!collBtns) {
            this->visibleSongButtons.reserve(collBtns->size());
            for(const u32 group : *collBtns) {
                this->visibleSongButtons.push_back(CarouselNode::group(group));
            }

            if(forceSort || groupingChanged || sortingChanged || diffSortChanged) {
                // rebucketing puts each set's difficulties next to each other, only the sort below orders them again
                if(group == GroupType::DIFFICULTY) {
                    this->rebucketDifficultyCollections();
                }

                for(const u32 button : *collBtns) {
                    auto &children = this->data.groups[button].children;
                    if(!children.empty()) {
                        sort_by_keys(children, this->curSortMethod, [this](CarouselNode node) {
                            return std::pair<const DatabaseBeatmap *, ScoreGrade>{this->getBeatmap(node),
                                                                                  this->getGrade(node)};
                        });
                    }
                }
            }
        }
    }
    this->rebuildSongButtons();

    // keep search state consistent between tab changes
    if(this->bInSearch) this->onSearchUpdate();

    // (can't call it right here because we maybe have async)
    this->scheduled_scroll_to_selected_button = true;
}

void SongBrowser::onSelectionMode() {
    if(cv::mod_fposu.getBool()) {
        cv::mod_fposu.setValue(false);
        ui->getNotificationOverlay()->addToast(_("Disabled FPoSu mode."), INFO_TOAST);
    } else {
        cv::mod_fposu.setValue(true);
        ui->getNotificationOverlay()->addToast(_("Enabled FPoSu mode."), SUCCESS_TOAST);
    }
}

void SongBrowser::onSelectionMods() {
    // mod selector is an overlay, so the browser stays visible (and active) beneath
    this->contextMenu->setVisible2(false);
    ui->getModSelector()->setVisible(true);
    soundEngine->play(osu->getSkin()->s_expand);
}

void SongBrowser::onSelectionRandom() {
    soundEngine->play(osu->getSkin()->s_click_button);
    if(keyboard->isShiftDown())
        this->selectPreviousRandomBeatmap();
    else
        this->bRandomBeatmapScheduled = true;
}

void SongBrowser::onSelectionOptions() {
    soundEngine->play(osu->getSkin()->s_click_button);

    if(this->selectedButton) {
        this->scrollToSongButton(this->selectedButton);

        const vec2 heuristicSongButtonPositionAfterSmoothScrollFinishes =
            (this->carousel->getPos() + this->carousel->getSize() / 2.f);

        this->openContextMenu(this->selectedButton, heuristicSongButtonPositionAfterSmoothScrollFinishes);
    }
}

void SongBrowser::onScoreClicked(ScoreButton *button) {
    ui->getRankingScreen()->setScore(button->getScore());
    ui->setScreen(ui->getRankingScreen());
    soundEngine->play(osu->getSkin()->s_menu_hit);
}

void SongBrowser::onScoreContextMenu(ScoreButton *scoreButton, int id) {
    // NOTE: see ScoreButton::onContextMenu()

    if(id == 2) {
        db->deleteScore(scoreButton->getScore());

        this->rebuildScoreButtons();
        osu->getUserButton()->updateUserStats();
    }
}

void SongBrowser::openContextMenu(CarouselNode node, vec2 pos) {
    this->contextMenuNode = node;
    if(node.isGroup())
        this->openGroupContextMenu(pos);
    else if(node.isSong())
        this->openSongContextMenu(pos);
}

void SongBrowser::openSongContextMenu(vec2 pos) {
    const DatabaseBeatmap *map = this->getBeatmap(this->contextMenuNode);
    auto *cmenu{this->contextMenu};

    cmenu->setPos(pos);
    cmenu->setRelPos(pos);
    cmenu->begin(0, true);
    {
        if(map)
            cmenu->addButtonJustified("[=] Open Beatmap Folder", TEXT_JUSTIFICATION::LEFT, 0)
                ->setClickCallback(SA::MakeDelegate<&SongBrowser::onOpenBeatmapFolderClicked>(this));

        // a childless button stands for a single difficulty. not in a room, where the song browser picks its map, and
        // not for a map outside the folders the editor opens maps from
        if(map && this->contextMenuNode.isDiff() && !BanchoState::is_in_a_multi_room() &&
           !db->locate(*map).file.empty())
            cmenu->addButtonJustified(fmt::format("[=] {}", _("Edit")), TEXT_JUSTIFICATION::LEFT, 8)
                ->setClickCallback(SA::MakeDelegate<&SongBrowser::onEditClicked>(this));

        if(map != nullptr && map->getDifficulties().size() < 1)
            cmenu->addButtonJustified("[+] Add to Collection", TEXT_JUSTIFICATION::LEFT, 1);

        cmenu->addButtonJustified("[+Set] Add to Collection", TEXT_JUSTIFICATION::LEFT, 2);

        if(this->getGroupingMode() == SongBrowser::GroupType::COLLECTIONS) {
            CBaseUIButton *spacer = cmenu->addButtonJustified("---", TEXT_JUSTIFICATION::CENTERED);
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            if(map == nullptr || map->getDifficulties().size() < 1) {
                cmenu->addButtonJustified("[-] Remove from Collection", TEXT_JUSTIFICATION::LEFT, 3);
            }

            cmenu->addButtonJustified("[-Set] Remove from Collection", TEXT_JUSTIFICATION::LEFT, 4);
        }

        CBaseUIButton *spacer = cmenu->addButtonJustified("---", TEXT_JUSTIFICATION::CENTERED);
        spacer->setEnabled(false);
        spacer->setTextColor(0xff888888);
        spacer->setTextDarkColor(0xff000000);

        if(map) {
            cmenu->addButtonJustified("[=] Export Beatmapset", TEXT_JUSTIFICATION::LEFT, 5);

            spacer = cmenu->addButtonJustified("---", TEXT_JUSTIFICATION::CENTERED);
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            // the osu!stable songs folder is osu!stable's to manage: its sets get the items, greyed out
            const BeatmapSet *set = map->getParentSet() ? map->getParentSet() : map;
            const bool deletable = set->type == DatabaseBeatmap::BeatmapType::NEOMOD_BEATMAPSET;
            auto grey_out = [](CBaseUIButton *button) {
                button->setEnabled(false);
                button->setTextColor(0xff888888);
                button->setTextDarkColor(0xff000000);
            };

            // (a childless button stands for a single difficulty, a parent button for its set)
            if(this->contextMenuNode.isDiff()) {
                auto *button = cmenu->addButtonJustified("[-] Delete Beatmap", TEXT_JUSTIFICATION::LEFT, 6);
                if(!deletable) grey_out(button);
            }
            auto *button = cmenu->addButtonJustified("[-Set] Delete Beatmapset", TEXT_JUSTIFICATION::LEFT, 7);
            if(!deletable) grey_out(button);
        }
    }
    cmenu->end(false, false);
    cmenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onSongContextMenu>(this));
    cmenu->clampToRightScreenEdge();
    cmenu->clampToBottomScreenEdge();
}

void SongBrowser::onSongContextMenu(std::string_view text, int id) {
    auto *cmenu{this->contextMenu};

    if(id == 1 || id == 2) {
        // 1 = add map to collection
        // 2 = add set to collection
        cmenu->begin(0, true);
        {
            cmenu->addButtonJustified("[+] Create new Collection?", TEXT_JUSTIFICATION::LEFT, -id * 2);

            auto sorted_collections = Collections::get_loaded();

            // sort by name
            Mc::ranges::stable_sort(sorted_collections, SString::strcase_comp, &Collections::Collection::get_name);

            for(const auto &collection : sorted_collections) {
                if(!collection.get_maps().empty()) {
                    CBaseUIButton *spacer = cmenu->addButtonJustified("---", TEXT_JUSTIFICATION::CENTERED);
                    spacer->setEnabled(false);
                    spacer->setTextColor(0xff888888);
                    spacer->setTextDarkColor(0xff000000);

                    break;
                }
            }

            auto map_hash = this->getBeatmap(this->contextMenuNode)->getMD5();
            for(const auto &collection : sorted_collections) {
                if(collection.get_maps().empty()) continue;

                bool can_add_to_collection = true;

                if(id == 1) {
                    if(Mc::ranges::contains(collection.get_maps(), map_hash)) {
                        // Map already is present in the collection
                        can_add_to_collection = false;
                    }
                }

                if(id == 2) {
                    // XXX: Don't mark as valid if the set is fully present in the collection
                }

                auto collectionButton =
                    cmenu->addButtonJustified(collection.get_name(), TEXT_JUSTIFICATION::CENTERED, id);
                if(!can_add_to_collection) {
                    collectionButton->setEnabled(false);
                    collectionButton->setTextColor(0xff555555);
                    collectionButton->setTextDarkColor(0xff000000);
                }
            }
        }
        cmenu->end(false, true);
        cmenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onAddToCollectionConfirmed>(this));
        cmenu->clampToRightScreenEdge();
        cmenu->clampToBottomScreenEdge();
    } else if(id == 3 || id == 4 || id == 5) {
        // 3 = remove map from collection
        // 4 = remove set from collection
        // 5 = export beatmapset
        this->onSongContextMenuAction(text, id);
    } else if(id == 6 || id == 7) {
        // 6 = delete beatmap (from disk)
        // 7 = delete beatmapset (from disk)
        cmenu->begin(0, true);
        {
            cmenu
                ->addButtonJustified(id == 6 ? "Really delete this beatmap?" : "Really delete this beatmapset?",
                                     TEXT_JUSTIFICATION::LEFT)
                ->setEnabled(false);

            CBaseUIButton *spacer = cmenu->addButtonJustified("---", TEXT_JUSTIFICATION::CENTERED);
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            cmenu->addButtonJustified("Yes", TEXT_JUSTIFICATION::LEFT, id);
            cmenu->addButtonJustified("No", TEXT_JUSTIFICATION::LEFT);
        }
        cmenu->end(false, false);
        cmenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onDeleteBeatmapConfirmed>(this));
        cmenu->clampToRightScreenEdge();
        cmenu->clampToBottomScreenEdge();
    }
}

void SongBrowser::onAddToCollectionConfirmed(std::string_view text, int id) {
    auto *cmenu{this->contextMenu};

    if(id == -2 || id == -4) {
        cmenu->begin(0, true);
        {
            CBaseUIButton *label = cmenu->addButtonJustified("Enter Collection Name:", TEXT_JUSTIFICATION::CENTERED);
            label->setEnabled(false);

            CBaseUIButton *spacer = cmenu->addButtonJustified("---", TEXT_JUSTIFICATION::CENTERED);
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            cmenu->addTextbox("", id);

            spacer = cmenu->addButtonJustified("---", TEXT_JUSTIFICATION::CENTERED);
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            label = cmenu->addButtonJustified("(Press ENTER to confirm.)", TEXT_JUSTIFICATION::CENTERED, id);
            label->setTextColor(0xff555555);
            label->setTextDarkColor(0xff000000);
        }
        cmenu->end(false, false);
        cmenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onCreateNewCollectionConfirmed>(this));
        cmenu->clampToRightScreenEdge();
        cmenu->clampToBottomScreenEdge();
    } else {
        // just forward it
        this->onSongContextMenuAction(text, id);
    }
}

void SongBrowser::onCreateNewCollectionConfirmed(std::string_view text, int id) {
    if(id == -2 || id == -4) {
        // just forward it
        this->onSongContextMenuAction(text, id);
    }
}

void SongBrowser::onDeleteBeatmapConfirmed(std::string_view text, int id) {
    if(id == 6 || id == 7) {
        this->onSongContextMenuAction(text, id);
    }
}

void SongBrowser::onEditClicked() {
    this->contextMenu->setVisible2(false);
    const DatabaseBeatmap *map = this->getBeatmap(this->contextMenuNode);
    if(!map) return;
    ui->getEditorScreen()->enter(db->locate(*map));
}

void SongBrowser::onOpenBeatmapFolderClicked() {
    this->contextMenu->setVisible2(false);  // why is this manual setVisible not required in mcosu?
    const DatabaseBeatmap *map = this->getBeatmap(this->contextMenuNode);
    if(!map) return;
    env->openFileBrowser(map->getFolder());
}

void SongBrowser::openGroupContextMenu(vec2 pos) {
    if(this->getGroupingMode() != GroupType::COLLECTIONS) return;

    auto *cmenu{this->contextMenu};

    cmenu->setPos(pos);
    cmenu->setRelPos(pos);
    cmenu->begin(0, true);
    {
        cmenu->addButton("[...]      Rename Collection", 1);

        CBaseUIButton *spacer = cmenu->addButton("---");
        spacer->setEnabled(false);
        spacer->setTextColor(0xff888888);
        spacer->setTextDarkColor(0xff000000);

        cmenu->addButton("[-]         Delete Collection", 2);

        CBaseUIButton *spacer2 = cmenu->addButton("---");
        spacer2->setEnabled(false);
        spacer2->setTextColor(0xff888888);
        spacer2->setTextDarkColor(0xff000000);

        cmenu->addButton("[=]         Export Collection", 5);
    }
    cmenu->end(false, false);
    cmenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onGroupContextMenu>(this));
    cmenu->clampToRightScreenEdge();
    cmenu->clampToBottomScreenEdge();
}

void SongBrowser::onGroupContextMenu(std::string_view text, int id) {
    auto *cmenu{this->contextMenu};
    const std::string_view collectionName = this->data.groups[this->contextMenuNode.idx].name;

    if(id == 1) {
        cmenu->begin(0, true);
        {
            CBaseUIButton *label = cmenu->addButton("Enter Collection Name:");
            label->setEnabled(false);

            CBaseUIButton *spacer = cmenu->addButton("---");
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            cmenu->addTextbox(std::string{collectionName}, id)->setCursorPosRight();

            spacer = cmenu->addButton("---");
            spacer->setEnabled(false);
            spacer->setTextColor(0xff888888);
            spacer->setTextDarkColor(0xff000000);

            label = cmenu->addButton("(Press ENTER to confirm.)", id);
            label->setTextColor(0xff555555);
            label->setTextDarkColor(0xff000000);
        }
        cmenu->end(false, false);
        cmenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onRenameCollectionConfirmed>(this));
        cmenu->clampToRightScreenEdge();
        cmenu->clampToBottomScreenEdge();
    } else if(id == 2) {
        if(keyboard->isShiftDown())
            this->onDeleteCollectionConfirmed(text, id);
        else {
            cmenu->begin(0, true);
            {
                cmenu->addButton("Really delete collection?")->setEnabled(false);
                CBaseUIButton *spacer = cmenu->addButton("---");
                spacer->setEnabled(false);
                spacer->setTextColor(0xff888888);
                spacer->setTextDarkColor(0xff000000);
                cmenu->addButton("Yes", 2);
                cmenu->addButton("No");
            }
            cmenu->end(false, false);
            cmenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onDeleteCollectionConfirmed>(this));
            cmenu->clampToRightScreenEdge();
            cmenu->clampToBottomScreenEdge();
        }
    } else if(id == 5) {
        // TODO: custom export names, maybe
        this->onGroupContextMenuAction(std::string{collectionName}, id);
    }
}

void SongBrowser::onRenameCollectionConfirmed(std::string_view text, int /*id*/) {
    if(text.length() > 0) {
        // forward it
        this->onGroupContextMenuAction(text, 3);
    }
}

void SongBrowser::onDeleteCollectionConfirmed(std::string_view /*text*/, int id) {
    if(id != 2) return;

    // just forward it
    this->onGroupContextMenuAction(std::string{this->data.groups[this->contextMenuNode.idx].name}, id);
}

void SongBrowser::onSongContextMenuAction(std::string_view text, int id) {
    const CarouselNode songButton = this->contextMenuNode;
    const DatabaseBeatmap *songButtonMap = this->getBeatmap(songButton);

    const auto getBeatmapSetHashesForSongButton = [&]() -> std::vector<MD5Hash> {
        std::vector<MD5Hash> beatmapSetHashes;
        if(songButton.isSet()) {
            for(const u32 diff : this->data.setChildren(songButton.idx)) {
                beatmapSetHashes.push_back(this->data.diffMap[diff]->getMD5());
            }
        } else {
            const BeatmapSet *mapset = songButtonMap->getParentSet() ? songButtonMap->getParentSet() : songButtonMap;
            for(const auto &diff : mapset->getDifficulties()) {
                beatmapSetHashes.push_back(diff->getMD5());
            }
        }
        return beatmapSetHashes;
    };

    bool updateUI = false;
    {
        if(id == 1) {
            // add diff to collection
            auto &collection = Collections::get_or_create_collection(text);
            collection.add_map(songButtonMap->getMD5());
            Collections::save_collections_async();
            updateUI = true;
        } else if(id == 2) {
            // add set to collection
            auto &collection = Collections::get_or_create_collection(text);
            const std::vector<MD5Hash> beatmapSetHashes = getBeatmapSetHashesForSongButton();
            for(const auto &hash : beatmapSetHashes) {
                collection.add_map(hash);
            }
            Collections::save_collections_async();
            updateUI = true;
        } else if(id == 3) {
            // remove diff from collection

            // get collection name by selection
            std::string collectionName;
            {
                for(const u32 collectionButton : this->collectionButtons) {
                    if(this->data.groups[collectionButton].selected) {
                        collectionName = this->data.groups[collectionButton].name;
                        break;
                    }
                }
            }

            auto &collection = Collections::get_or_create_collection(collectionName);
            collection.remove_map(songButtonMap->getMD5());
            Collections::save_collections_async();
            updateUI = true;
        } else if(id == 4) {
            // remove entire set from collection

            // get collection name by selection
            std::string collectionName;
            {
                for(const u32 collectionButton : this->collectionButtons) {
                    if(this->data.groups[collectionButton].selected) {
                        collectionName = this->data.groups[collectionButton].name;
                        break;
                    }
                }
            }

            auto &collection = Collections::get_or_create_collection(collectionName);
            const std::vector<MD5Hash> beatmapSetHashes = getBeatmapSetHashesForSongButton();
            for(const auto &hash : beatmapSetHashes) {
                collection.remove_map(hash);
            }
            Collections::save_collections_async();
            updateUI = true;
        } else if(id == -2 || id == -4) {
            // add beatmap(set) to new collection
            auto &collection = Collections::get_or_create_collection(text);

            if(id == -2) {
                // id == -2 means beatmap
                collection.add_map(songButtonMap->getMD5());
                updateUI = true;
            } else if(id == -4) {
                // id == -4 means beatmapset
                const std::vector<MD5Hash> beatmapSetHashes = getBeatmapSetHashesForSongButton();
                for(const auto &hash : beatmapSetHashes) {
                    collection.add_map(hash);
                }
                updateUI = true;
            }

            Collections::save_collections_async();
        } else if(id == 5) {  // export beatmapset
            assert(songButtonMap);
            std::string folder{songButtonMap->getFolder()};
            auto ctx = MapExporter::ExportContext{{folder}, "", BottomBar::update_export_progress_cb};
            osu->getMapExports()->start(std::move(ctx));
        } else if(id == 6 || id == 7) {  // delete beatmap (6) / beatmapset (7) from disk
            assert(songButtonMap);
            osu->getBeatmapInstaller()->uninstall(songButtonMap, id == 7);
        }
    }

    if(updateUI) {
        const i32 prevScrollPosY = (i32)this->carousel->getRelPosY();  // usability
        // owned copy, since recreateCollectionsButtons() deletes the button
        const std::string previouslySelectedCollectionName{
            this->selectionPreviousCollectionButton
                ? this->data.groups[this->selectionPreviousCollectionButton.idx].name
                : ""};  // usability

        this->recreateCollectionsButtons();
        // only change carousel state if we're in collections
        if(this->getGroupingMode() == GroupType::COLLECTIONS) {
            this->rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(
                false, false);  // (last false = skipping rebuildSongButtons() here)
            this->bSongButtonsNeedSorting = true;
            this->onSortChange("",
                               cv::songbrowser_sortingtype.getInt());  // (because this does the rebuildSongButtons())
            if(previouslySelectedCollectionName.length() > 0) {
                for(const u32 collectionButton : this->collectionButtons) {
                    if(this->data.groups[collectionButton].name == previouslySelectedCollectionName) {
                        this->select(CarouselNode::group(collectionButton));
                        this->carousel->scrollToY(prevScrollPosY, false);
                        // the scheduled scroll would open another collection with the selected map, or scroll to top
                        this->scheduled_scroll_to_selected_button = false;
                        break;
                    }
                }
            }
        }
    }
}

void SongBrowser::onGroupContextMenuAction(std::string_view text, int id) {
    std::string collection_name{text};
    const u32 collectionButton = this->contextMenuNode.idx;

    if(id == 2) {  // delete collection
        if(const auto &it = Mc::ranges::find_if(
               this->collectionButtons, [&](u32 group) { return this->data.groups[group].name == collection_name; });
           it != this->collectionButtons.end() && Collections::delete_collection(collection_name)) {
            Collections::save_collections_async();

            // delete UI
            if(this->selectedButton == CarouselNode::group(*it)) this->selectedButton = {};
            this->collectionButtons.erase(it);

            // reset UI state
            this->selectionPreviousCollectionButton = {};

            // update UI
            this->bSongButtonsNeedSorting = true;
            this->rebuildAfterGroupOrSortChange(GroupType::COLLECTIONS);
        }
    } else if(id == 3) {  // rename collection
        const std::string currentButtonName = this->data.groups[collectionButton].name;
        if(!currentButtonName.empty()) {
            auto &existingCollection = Collections::get_or_create_collection(currentButtonName);

            if(existingCollection.rename_to(collection_name)) {
                Collections::save_collections_async();

                // rename button
                if(Mc::ranges::contains(this->collectionButtons, collectionButton)) {
                    this->data.groups[collectionButton].name = collection_name;

                    // resort collection buttons
                    Mc::ranges::stable_sort(
                        this->collectionButtons, SString::strcase_comp,
                        [this](u32 group) -> std::string_view { return this->data.groups[group].name; });
                }

                // update UI
                this->bSongButtonsNeedSorting = true;
                this->rebuildAfterGroupOrSortChange(GroupType::COLLECTIONS);
            }
        }
    } else if(id == 5) {  // export collection
        std::vector<std::string> pathsToExport;
        auto &existingCollection = Collections::get_or_create_collection(collection_name);
        for(auto &mapHash : existingCollection.get_maps()) {
            if(auto *diff = db->getBeatmapDifficulty(mapHash); diff) {
                pathsToExport.emplace_back(diff->getFolder());
            }
        }

        // uber sanity
        std::string colNameSanitized = collection_name;
        SString::trim_inplace(colNameSanitized);
        if(colNameSanitized.empty()) {
            colNameSanitized = fmt::format("Untitled-Collection-{:%F-%H-%M-%S}", fmt::gmtime(std::time(nullptr)));
        } else {
            Mc::ranges::replace(colNameSanitized, '\\', '_');
            Mc::ranges::replace(colNameSanitized, '/', '_');
            if(colNameSanitized.starts_with('.')) {
                colNameSanitized[0] = '_';
            } else if(colNameSanitized.starts_with("..")) {
                colNameSanitized[0] = '_';
                colNameSanitized[1] = '_';
            }
        }

        // TODO: custom export name maybe
        Environment::createDirectory(MapExporter::export_root() + "collections");

        auto ctx = MapExporter::ExportContext{
            .beatmap_folder_paths = pathsToExport,
            .toplevel_archive_bundle = fmt::format("collections/{:s}", colNameSanitized),
            .cb = [namestr = std::string{collection_name}](f32 progress, std::string entry) -> void {
                return BottomBar::update_export_progress(progress, std::move(entry), namestr);
            }};
        osu->getMapExports()->start(std::move(ctx));
    }
}

void SongBrowser::highlightScore(const FinishedScore &scoreToHighlight) {
    if(auto cachedit = Mc::ranges::find(this->scoreButtonCache, scoreToHighlight, &ScoreButton::getScore);
       cachedit != this->scoreButtonCache.end()) {
        this->scoreBrowser->scrollToElement(*cachedit, 0, 10);
        (*cachedit)->highlight();
    }
}

void SongBrowser::selectSongButton(CarouselNode songButton) {
    if(songButton && !this->isSelected(songButton)) {
        this->contextMenu->setVisible2(false);
        this->select(songButton);
    }
}

bool SongBrowser::selectRandomBeatmap() {
    const DatabaseBeatmap *current = osu->getMapInterface()->getBeatmap();
    const auto setOf = [](const DatabaseBeatmap *map) { return map->getParentSet() ? map->getParentSet() : map; };

    // filter songbuttons or independent diffs, other than the current song's
    const auto &elements{this->carousel->getRows()};

    std::vector<CarouselNode> songButtons;
    songButtons.reserve(elements.size());
    for(const CarouselNode element : elements) {
        if(!element.isSong() || !this->isIndependentDiff(element)) continue;
        const DatabaseBeatmap *map = this->getBeatmap(element);
        if(current != nullptr && map != nullptr && setOf(map) == setOf(current)) continue;
        songButtons.push_back(element);
    }

    if(songButtons.empty()) return false;

    // remember previous
    if(current != nullptr && !current->do_not_store) {
        this->previousRandomBeatmaps.push_back(current);
    }

    this->selectSongButton(songButtons[songButtons.size() == 1 ? 0 : prand() % songButtons.size()]);
    return true;
}

bool SongBrowser::selectPreviousRandomBeatmap() {
    auto &history = this->previousRandomBeatmaps;

    // the current beatmap may also be at the top (so we don't switch to ourself)
    while(!history.empty() && history.back() == osu->getMapInterface()->getBeatmap()) history.pop_back();
    if(history.empty()) return false;
    const DatabaseBeatmap *previousRandomBeatmap = history.back();

    // select it, if we can find it (and remove it from memory)
    for(const CarouselNode songButton : this->carousel->getRows()) {
        if(!songButton.isSong()) continue;  // allow ALL songbuttons

        CarouselNode found = this->getBeatmap(songButton) == previousRandomBeatmap ? songButton : CarouselNode{};
        if(!found && songButton.isSet()) {
            for(const u32 child : this->data.setChildren(songButton.idx)) {
                if(this->data.diffMap[child] == previousRandomBeatmap) {
                    found = CarouselNode::diff(child);
                    break;
                }
            }
        }
        if(found) {
            history.pop_back();
            this->selectSongButton(found);
            return true;
        }
    }
    return false;
}

void SongBrowser::playSelectedDifficulty() {
    const auto &elements{this->carousel->getRows()};

    for(const CarouselNode element : elements) {
        if(element.isDiff() && this->isSelected(element)) {
            this->select(element);
            break;
        }
    }
}

void SongBrowser::recreateCollectionsButtons() {
    // reset
    {
        // sanity
        if(this->curGroup == GroupType::COLLECTIONS) {
            this->carousel->clearRows();
            this->visibleSongButtons.clear();
        }

        // (an open collection is the selection, and it may not get a button again)
        const auto isCollection = [this](CarouselNode node) {
            return node.isGroup() && this->data.groups[node.idx].isCollection;
        };
        if(isCollection(this->selectedButton)) this->selectedButton = {};
        if(isCollection(this->selectionPreviousCollectionButton)) this->selectionPreviousCollectionButton = {};
        if(isCollection(this->contextMenuNode)) this->contextMenuNode = {};
        this->collectionButtons.clear();
        auto &groups = this->data.groups;
        const auto firstCollection = std::ranges::find_if(groups, &CarouselGroup::isCollection);
        assert(std::all_of(firstCollection, groups.end(), [](const CarouselGroup &g) { return g.isCollection; }));
        groups.erase(firstCollection, groups.end());
    }

    Timer t;
    t.start();

    for(const auto &collection : Collections::get_loaded()) {
        const auto &coll_maps = collection.get_maps();
        if(coll_maps.empty()) continue;

        std::vector<CarouselNode> folder;
        Hash::flat::set<const BeatmapSet *> matched_sets;
        std::vector<CarouselNode> matching_diffs;

        for(const auto &map_hash : coll_maps) {
            auto it = this->hashToDiffButton->find(map_hash);
            if(it == this->hashToDiffButton->end()) continue;

            const u32 diff_btn = it->second;

            // get parent button and siblings
            const u32 parent = this->data.diffSet[diff_btn];
            const auto diff_btn_group = this->data.setChildren(parent);

            matching_diffs.clear();

            // filter to only diffs in this collection
            for(const u32 sbc : diff_btn_group) {
                if(coll_maps.contains(this->data.diffMap[sbc]->getMD5())) {
                    matching_diffs.push_back(CarouselNode::diff(sbc));
                }
            }

            // by parent set, not set id: every unsubmitted set shares id -1
            if(auto [_, inserted] = matched_sets.insert(this->data.diffMap[diff_btn]->getParentSet()); !inserted) {
                // We already added the maps from this set to the collection!
                continue;
            }

            if(diff_btn_group.size() == matching_diffs.size()) {
                // all diffs match: add the set button (user added all diffs of beatmap into collection)
                folder.push_back(CarouselNode::set(parent));
            } else {
                // only add matched diff buttons
                Mc::ranges::append(folder, matching_diffs);
            }
        }

        if(!folder.empty()) {
            this->collectionButtons.push_back((u32)this->data.groups.size());
            this->data.groups.push_back(CarouselGroup{.name = std::string{collection.get_name()},
                                                      .children = std::move(folder),
                                                      .numVisibleChildren = 0,
                                                      .selected = false,
                                                      .isCollection = true});
        }
    }

    // sort buttons by name
    Mc::ranges::stable_sort(this->collectionButtons, SString::strcase_comp,
                            [this](u32 group) -> std::string_view { return this->data.groups[group].name; });

    t.update();
    debugLog("recreateCollectionsButtons(): {:f} seconds", t.getElapsedTime());
}
