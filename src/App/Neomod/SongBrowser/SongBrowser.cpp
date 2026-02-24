// Copyright (c) 2016, PG & 2023-2025, kiwec & 2025, WH, All rights reserved.
#include "SongBrowser.h"

#include "Engine.h"
#include "Environment.h"
#include "Logging.h"
#include "ResourceManager.h"
#include "AnimationHandler.h"
#include "DirectoryWatcher.h"
#include "Mouse.h"
#include "Keyboard.h"
#include "Font.h"
#include "File.h"
#include "Timing.h"
#include "crypto.h"
#include "SString.h"
#include "MakeDelegateWrapper.h"
#include "Icons.h"
#include "SoundEngine.h"
#include "Sound.h"

#include "Osu.h"
#include "OsuConVars.h"

#include "BatchDiffCalc.h"
#include "AsyncPPCalculator.h"
#include "LoudnessCalcThread.h"

#include "Skin.h"
#include "SkinImage.h"
#include "DatabaseBeatmap.h"
#include "Downloader.h"
#include "Collections.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "BanchoLeaderboard.h"
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
#include "SongButton.h"
#include "SongDifficultyButton.h"
#include "AsyncSongButtonMatcher.h"
#include "CollectionButton.h"
#include "UserCard.h"
#include "InfoLabel.h"
#include "LoadingScreen.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "UISearchOverlay.h"
#include "ScoreButton.h"
#include "BottomBar.h"
#include "RoomScreen.h"
#include "Chat.h"
#include "ContainerRanges.h"

#include <algorithm>
#include <memory>
#include <charconv>
#include <cwctype>
#include <unordered_set>
#include <utility>

#define WANT_PDQSORT
#include "Sorting.h"

struct SongBrowser::MD5HashMap : public Hash::flat::map<MD5Hash, SongDifficultyButton *> {};

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
f32 SongBrowser::getSkinScale(const SkinImage *img) { return SongBrowser::getUIScale() * (img->is_2x ? 0.5f : 1.f); }

vec2 SongBrowser::getSkinDimensions(const SkinImage *img) {
    return img->getImageSizeForCurrentFrame() * SongBrowser::getSkinScale(img);
}

namespace {
class ScoresStillLoadingElement final : public CBaseUILabel {
   public:
    ScoresStillLoadingElement(const UString &text) : CBaseUILabel(0, 0, 0, 0, "", text) {
        this->sIconString.insert(0, Icons::GLOBE);
    }

    void drawText() override {
        // draw icon
        const float iconScale = 0.6f;
        McFont *iconFont = osu->getFontIcons();
        int iconWidth = 0;
        g->pushTransform();
        {
            const float scale = (this->getSize().y / iconFont->getHeight()) * iconScale;
            const float paddingLeft = scale * 15;

            iconWidth = paddingLeft + iconFont->getStringWidth(this->sIconString) * scale;

            g->scale(scale, scale);
            g->translate((int)(this->getPos().x + paddingLeft),
                         (int)(this->getPos().y + this->getSize().y / 2 + iconFont->getHeight() * scale / 2));
            g->setColor(0xffffffff);
            g->drawString(iconFont, this->sIconString);
        }
        g->popTransform();

        // draw text
        const float textScale = 0.4f;
        McFont *textFont = osu->getSongBrowserFont();
        g->pushTransform();
        {
            const float stringWidth = textFont->getStringWidth(this->sText);

            const float scale = ((this->getSize().x - iconWidth) / stringWidth) * textScale;

            g->scale(scale, scale);
            g->translate(
                (int)(this->getPos().x + iconWidth + (this->getSize().x - iconWidth) / 2 - stringWidth * scale / 2),
                (int)(this->getPos().y + this->getSize().y / 2 + textFont->getHeight() * scale / 2));
            g->setColor(0xff02c3e5);
            g->drawString(textFont, this->sText);
        }
        g->popTransform();
    }

   private:
    UString sIconString;
};

class NoRecordsSetElement final : public CBaseUILabel {
   public:
    NoRecordsSetElement(const UString &text) : CBaseUILabel(0, 0, 0, 0, "", text) {
        this->sIconString.insert(0, Icons::TROPHY);
    }

    void drawText() override {
        // draw icon
        const float iconScale = 0.6f;
        McFont *iconFont = osu->getFontIcons();
        int iconWidth = 0;
        g->pushTransform();
        {
            const float scale = (this->getSize().y / iconFont->getHeight()) * iconScale;
            const float paddingLeft = scale * 15;

            iconWidth = paddingLeft + iconFont->getStringWidth(this->sIconString) * scale;

            g->scale(scale, scale);
            g->translate((int)(this->getPos().x + paddingLeft),
                         (int)(this->getPos().y + this->getSize().y / 2 + iconFont->getHeight() * scale / 2));
            g->setColor(0xffffffff);
            g->drawString(iconFont, this->sIconString);
        }
        g->popTransform();

        // draw text
        const float textScale = 0.6f;
        McFont *textFont = osu->getSongBrowserFont();
        g->pushTransform();
        {
            const float stringWidth = textFont->getStringWidth(this->sText);

            const float scale = ((this->getSize().x - iconWidth) / stringWidth) * textScale;

            g->scale(scale, scale);
            g->translate(
                (int)(this->getPos().x + iconWidth + (this->getSize().x - iconWidth) / 2 - stringWidth * scale / 2),
                (int)(this->getPos().y + this->getSize().y / 2 + textFont->getHeight() * scale / 2));
            g->setColor(0xff02c3e5);
            g->drawString(textFont, this->sText);
        }
        g->popTransform();
    }

   private:
    UString sIconString;
};
}  // namespace

// used also by SongButton
bool SongBrowser::sort_by_difficulty(SongButton const *a, SongButton const *b) {
    const auto *aPtr = a->getDatabaseBeatmap(), *bPtr = b->getDatabaseBeatmap();
    if((aPtr == nullptr) || (bPtr == nullptr)) return (aPtr == nullptr) < (bPtr == nullptr);

    float stars1 = aPtr->getStarRating(StarPrecalc::active_idx);
    float stars2 = bPtr->getStarRating(StarPrecalc::active_idx);
    if(stars1 != stars2) return stars1 < stars2;

    float diff1 = (aPtr->getAR() + 1) * (aPtr->getCS() + 1) * (aPtr->getHP() + 1) * (aPtr->getOD() + 1) *
                  (std::max(aPtr->getMostCommonBPM(), 1));
    float diff2 = (bPtr->getAR() + 1) * (bPtr->getCS() + 1) * (bPtr->getHP() + 1) * (bPtr->getOD() + 1) *
                  (std::max(bPtr->getMostCommonBPM(), 1));

    if(diff1 == diff2) return false;
    return diff1 < diff2;
}

// not used anywhere else
bool SongBrowser::sort_by_artist(SongButton const *a, SongButton const *b) {
    const auto *aPtr = a->getDatabaseBeatmap(), *bPtr = b->getDatabaseBeatmap();
    if((aPtr == nullptr) || (bPtr == nullptr)) return (aPtr == nullptr) < (bPtr == nullptr);

    const auto &artistA{aPtr->getArtistLatin()};
    const auto &artistB{bPtr->getArtistLatin()};

    i32 cmp = strcasecmp(artistA.c_str(), artistB.c_str());
    if(cmp == 0) return sort_by_title(a, b);  // fall back to sort by title
    return cmp < 0;
}

bool SongBrowser::sort_by_bpm(SongButton const *a, SongButton const *b) {
    const auto *aPtr = a->getDatabaseBeatmap(), *bPtr = b->getDatabaseBeatmap();
    if((aPtr == nullptr) || (bPtr == nullptr)) return (aPtr == nullptr) < (bPtr == nullptr);

    int bpm1 = aPtr->getMostCommonBPM();
    int bpm2 = bPtr->getMostCommonBPM();
    if(bpm1 == bpm2) return sort_by_difficulty(a, b);
    return bpm1 < bpm2;
}

bool SongBrowser::sort_by_creator(SongButton const *a, SongButton const *b) {
    const auto *aPtr = a->getDatabaseBeatmap(), *bPtr = b->getDatabaseBeatmap();
    if((aPtr == nullptr) || (bPtr == nullptr)) return (aPtr == nullptr) < (bPtr == nullptr);

    const auto &creatorA{aPtr->getCreator()};
    const auto &creatorB{bPtr->getCreator()};

    i32 cmp = strcasecmp(creatorA.c_str(), creatorB.c_str());
    if(cmp == 0) return sort_by_difficulty(a, b);
    return cmp < 0;
}

bool SongBrowser::sort_by_date_added(SongButton const *a, SongButton const *b) {
    const auto *aPtr = a->getDatabaseBeatmap(), *bPtr = b->getDatabaseBeatmap();
    if((aPtr == nullptr) || (bPtr == nullptr)) return (aPtr == nullptr) < (bPtr == nullptr);

    i64 time1 = aPtr->last_modification_time;
    i64 time2 = bPtr->last_modification_time;
    if(time1 == time2) return sort_by_difficulty(a, b);
    return time1 > time2;
}

bool SongBrowser::sort_by_grade(SongButton const *a, SongButton const *b) {
    if(a->grade == b->grade) return sort_by_difficulty(a, b);
    return a->grade < b->grade;
}

bool SongBrowser::sort_by_length(SongButton const *a, SongButton const *b) {
    const auto *aPtr = a->getDatabaseBeatmap(), *bPtr = b->getDatabaseBeatmap();
    if((aPtr == nullptr) || (bPtr == nullptr)) return (aPtr == nullptr) < (bPtr == nullptr);

    u32 length1 = aPtr->getLengthMS();
    u32 length2 = bPtr->getLengthMS();
    if(length1 == length2) return sort_by_difficulty(a, b);
    return length1 < length2;
}

bool SongBrowser::sort_by_title(SongButton const *a, SongButton const *b) {
    const auto *aPtr = a->getDatabaseBeatmap(), *bPtr = b->getDatabaseBeatmap();
    if((aPtr == nullptr) || (bPtr == nullptr)) return (aPtr == nullptr) < (bPtr == nullptr);

    const auto &titleA{aPtr->getTitleLatin()};
    const auto &titleB{bPtr->getTitleLatin()};

    i32 cmp = strcasecmp(titleA.c_str(), titleB.c_str());
    if(cmp == 0) return sort_by_difficulty(a, b);
    return cmp < 0;
}

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

SongBrowser::SongBrowser() : ScreenBackable(), global_songbrowser_(this) {
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

    // vars
    this->bSongBrowserRightClickScrollCheck = false;
    this->bSongBrowserRightClickScrolling = false;
    this->bNextScrollToSongButtonJumpFixScheduled = false;
    this->bNextScrollToSongButtonJumpFixUseScrollSizeDelta = false;
    this->fNextScrollToSongButtonJumpFixOldScrollSizeY = 0.0f;
    this->fNextScrollToSongButtonJumpFixOldRelPosY = 0.0f;

    this->selectionPreviousSongButton = nullptr;
    this->selectionPreviousSongDiffButton = nullptr;
    this->selectionPreviousCollectionButton = nullptr;

    this->bF1Pressed = false;
    this->bF2Pressed = false;
    this->bF3Pressed = false;
    this->bShiftPressed = false;
    this->bLeft = false;
    this->bRight = false;

    this->bRandomBeatmapScheduled = false;

    // build topbar left
    this->topbarLeft = new CBaseUIContainer(0, 0, 0, 0, "");
    this->songInfo = new InfoLabel(0, 0, 0, 0, "");
    this->topbarLeft->addBaseUIElement(this->songInfo);

    this->filterScoresDropdown = new CBaseUIButton(0, 0, 0, 0, "", "Local");
    this->filterScoresDropdown->setClickCallback(SA::MakeDelegate<&SongBrowser::onFilterScoresClicked>(this));
    this->topbarLeft->addBaseUIElement(this->filterScoresDropdown);

    this->sortScoresDropdown = new CBaseUIButton(0, 0, 0, 0, "", "By score");
    this->sortScoresDropdown->setClickCallback(SA::MakeDelegate<&SongBrowser::onSortScoresClicked>(this));
    this->topbarLeft->addBaseUIElement(this->sortScoresDropdown);

    this->webButton = new CBaseUIButton(0, 0, 0, 0, "", "Web");
    this->webButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onWebClicked>(this));
    this->topbarLeft->addBaseUIElement(this->webButton);

    // build topbar right
    this->topbarRight = new CBaseUIContainer(0, 0, 0, 0, US_("SongBrowser::topbarRight"));
    {
        this->groupLabel = (new CBaseUILabel(0, 0, 0, 0, US_("SongBrowser::groupLabel"), ""))
                               ->setFont(osu->getSongBrowserFont())
                               ->setScale(0.75f)
                               ->setTextColor(rgba(200, 200, 255, 255))
                               ->setDrawTextShadow(true)
                               ->setDrawBackground(false)
                               ->setDrawFrame(false)
                               ->setText(US_("Group:"))  // setting text later so string metrics get applied...
                               ->setSizeToContent(-1, 0);
        this->groupLabel->setGrabClicks(true);
        this->topbarRight->addBaseUIElement(this->groupLabel);

        this->groupButton = new CBaseUIButton(0, 0, 0, 0, "", "No Grouping");
        this->groupButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onGroupClicked>(this));
        this->groupButton->setGrabClicks(true);
        this->topbarRight->addBaseUIElement(this->groupButton);

        this->sortLabel = (new CBaseUILabel(0, 0, 0, 0, US_("SongBrowser::sortLabel"), ""))
                              ->setFont(osu->getSongBrowserFont())
                              ->setScale(0.75f)
                              ->setTextColor(rgba(225, 255, 225, 255))
                              ->setDrawTextShadow(true)
                              ->setDrawBackground(false)
                              ->setDrawFrame(false)
                              ->setText(US_("Sort:"))
                              ->setSizeToContent(-1, 0);
        this->sortLabel->setGrabClicks(true);
        this->topbarRight->addBaseUIElement(this->sortLabel);

        this->sortButton = new CBaseUIButton(0, 0, 0, 0, "", "By Date Added");
        this->sortButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onSortClicked>(this));
        this->sortButton->setGrabClicks(true);
        this->topbarRight->addBaseUIElement(this->sortButton);

        // "hardcoded" grouping tabs
        this->groupByCollectionBtn = new CBaseUIButton(0, 0, 0, 0, "", "Collections");
        this->groupByCollectionBtn->setHandleRightMouse(true);
        this->groupByCollectionBtn->setClickCallback(SA::MakeDelegate<&SongBrowser::onQuickGroupClicked>(this));
        this->groupByCollectionBtn->setGrabClicks(true);
        this->topbarRight->addBaseUIElement(this->groupByCollectionBtn);
        this->groupByArtistBtn = new CBaseUIButton(0, 0, 0, 0, "", "By Artist");
        this->groupByArtistBtn->setHandleRightMouse(true);
        this->groupByArtistBtn->setClickCallback(SA::MakeDelegate<&SongBrowser::onQuickGroupClicked>(this));
        this->groupByArtistBtn->setGrabClicks(true);
        this->topbarRight->addBaseUIElement(this->groupByArtistBtn);
        this->groupByDifficultyBtn = new CBaseUIButton(0, 0, 0, 0, "", "By Difficulty");
        this->groupByDifficultyBtn->setHandleRightMouse(true);
        this->groupByDifficultyBtn->setClickCallback(SA::MakeDelegate<&SongBrowser::onQuickGroupClicked>(this));
        this->groupByDifficultyBtn->setGrabClicks(true);
        this->topbarRight->addBaseUIElement(this->groupByDifficultyBtn);
        this->groupByNothingBtn = new CBaseUIButton(0, 0, 0, 0, "", "No Grouping");
        this->groupByNothingBtn->setHandleRightMouse(true);
        this->groupByNothingBtn->setClickCallback(SA::MakeDelegate<&SongBrowser::onQuickGroupClicked>(this));
        this->groupByNothingBtn->setGrabClicks(true);
        this->topbarRight->addBaseUIElement(this->groupByNothingBtn);
    }

    // context menu
    this->contextMenu = new UIContextMenu(50, 50, 150, 0, "");
    this->contextMenu->setVisible(true);

    // build scorebrowser
    this->scoreBrowser = new CBaseUIScrollView(0, 0, 0, 0, "");
    this->scoreBrowser->setScrollbarOnLeft(true);
    this->scoreBrowser->setDrawBackground(false);
    this->scoreBrowser->setDrawFrame(false);
    this->scoreBrowser->setHorizontalScrolling(false);
    this->scoreBrowser->setScrollbarSizeMultiplier(0.25f);
    this->scoreBrowser->setScrollResistance(15);
    this->scoreBrowser->setHorizontalClipping(false);
    this->scoreBrowserScoresStillLoadingElement = new ScoresStillLoadingElement("Loading...");
    this->scoreBrowserNoRecordsSetElement = new NoRecordsSetElement("No records set!");
    this->scoreBrowser->container.addBaseUIElement(this->scoreBrowserNoRecordsSetElement);

    // NOTE: we don't add localBestContainer to the screen; we draw and update it manually so that
    //       it can be drawn under skins which overlay the scores list.
    this->localBestContainer = std::make_unique<CBaseUIContainer>(0.f, 0.f, 0.f, 0.f, "");
    this->localBestContainer->setVisible(false);
    this->localBestLabel = new CBaseUILabel(0, 0, 0, 0, "", "Personal Best (from local scores)");
    this->localBestLabel->setDrawBackground(false);
    this->localBestLabel->setDrawFrame(false);
    this->localBestLabel->setTextJustification(TEXT_JUSTIFICATION::CENTERED);

    this->thumbnailYRatio = cv::draw_songbrowser_thumbnails.getBool() ? 1.333333f : 0.f;

    // behaviour
    this->fPulseAnimation = 0.0f;
    this->fBackgroundFadeInTime = 0.0f;

    // search
    this->search = new UISearchOverlay(0, 0, 0, 0, "");
    this->search->setOffsetRight(10);
    this->fSearchWaitTime = 0.0f;
    this->bInSearch = (!cv::songbrowser_search_hardcoded_filter.getString().empty());
    this->updateLayout();
}

SongBrowser::~SongBrowser() {
    BatchDiffCalc::abort_calc();
    AsyncPPC::set_map(nullptr);
    VolNormalization::abort();
    this->checkHandleKillBackgroundSearchMatcher();

    this->hashToDiffButton->clear();
    for(auto &songButton : this->parentButtons) {
        delete songButton;
    }
    this->parentButtons.clear();

    this->collectionButtons.clear();

    this->artistCollectionButtons.clear();
    this->bpmCollectionButtons.clear();
    this->difficultyCollectionButtons.clear();
    this->creatorCollectionButtons.clear();
    this->dateaddedCollectionButtons.clear();
    this->lengthCollectionButtons.clear();
    this->titleCollectionButtons.clear();

    this->scoreBrowser->invalidate();
    for(ScoreButton *button : this->scoreButtonCache) {
        SAFE_DELETE(button);
    }
    this->scoreButtonCache.clear();

    this->localBestContainer->invalidate();  // contained elements freed manually below
    SAFE_DELETE(this->localBestButton);
    SAFE_DELETE(this->localBestLabel);
    SAFE_DELETE(this->scoreBrowserScoresStillLoadingElement);
    SAFE_DELETE(this->scoreBrowserNoRecordsSetElement);

    SAFE_DELETE(this->contextMenu);
    SAFE_DELETE(this->search);
    SAFE_DELETE(this->topbarLeft);
    SAFE_DELETE(this->topbarRight);
    SAFE_DELETE(this->scoreBrowser);
}

void SongBrowser::draw() {
    if(!this->bVisible) return;

    // draw background
    g->setColor(0xff000000);
    g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());

    // draw background image
    if(cv::draw_songbrowser_background_image.getBool()) {
        const DatabaseBeatmap *beatmap = osu->getMapInterface()->getBeatmap();
        const auto &bgHandler = osu->getBackgroundImageHandler();
        const Image *loadedImage = bgHandler->getLoadBackgroundImage(beatmap);

        float alpha = 1.0f;
        if(cv::songbrowser_background_fade_in_duration.getFloat() > 0.0f) {
            // handle fadein trigger after bgHandler is finished loading
            const bool ready = loadedImage && loadedImage->isReady();

            if(!ready)
                this->fBackgroundFadeInTime = engine->getTime();
            else if(this->fBackgroundFadeInTime > 0.0f && engine->getTime() > this->fBackgroundFadeInTime) {
                alpha = std::clamp<float>((engine->getTime() - this->fBackgroundFadeInTime) /
                                              cv::songbrowser_background_fade_in_duration.getFloat(),
                                          0.0f, 1.0f);
                alpha = 1.0f - (1.0f - alpha) * (1.0f - alpha);
            }
        }

        bgHandler->draw(loadedImage, alpha);
    } else if(cv::draw_songbrowser_menu_background_image.getBool()) {
        // menu-background
        Image *backgroundImage = osu->getSkin()->i_menu_bg;
        if(backgroundImage != nullptr && backgroundImage != MISSING_TEXTURE && backgroundImage->isReady()) {
            const float scale = Osu::getImageScaleToFillResolution(backgroundImage, osu->getVirtScreenSize());

            g->setColor(0xffffffff);
            g->pushTransform();
            {
                g->scale(scale, scale);
                g->translate(osu->getVirtScreenWidth() / 2, osu->getVirtScreenHeight() / 2);
                g->drawImage(backgroundImage);
            }
            g->popTransform();
        }
    }

    {
        f32 mode_osu_scale = SongBrowser::getSkinScale(osu->getSkin()->i_mode_osu);

        g->setColor(0xffffffff);
        if(cv::avoid_flashes.getBool()) {
            g->setAlpha(0.1f);
        } else {
            // XXX: Flash based on song BPM
            g->setAlpha(0.1f);
        }

        g->setBlendMode(DrawBlendMode::ADDITIVE);
        osu->getSkin()->i_mode_osu->drawRaw(vec2(osu->getVirtScreenWidth() / 2, osu->getVirtScreenHeight() / 2),
                                            mode_osu_scale, AnchorPoint::CENTER);
        g->setBlendMode(DrawBlendMode::ALPHA);
    }

    // draw score browser
    this->scoreBrowser->draw();
    this->localBestContainer->draw();

    if(cv::debug_osu.getBool()) {
        this->scoreBrowser->container.draw_debug();
    }

    // draw strain graph of currently selected beatmap
    if(cv::draw_songbrowser_strain_graph.getBool()) {
        const std::vector<f64> &aimStrains = osu->getMapInterface()->getWholeMapPPInfo().aimStrains;
        const std::vector<f64> &speedStrains = osu->getMapInterface()->getWholeMapPPInfo().speedStrains;
        const float speedMultiplier = osu->getMapInterface()->getSpeedMultiplier();

        if(aimStrains.size() > 0 && aimStrains.size() == speedStrains.size()) {
            const float strainStepMS = 400.0f * speedMultiplier;

            const u32 lengthMS = strainStepMS * aimStrains.size();

            // get highest strain values for normalization
            double highestAimStrain = 0.0;
            double highestSpeedStrain = 0.0;
            double highestStrain = 0.0;
            int highestStrainIndex = -1;
            for(int i = 0; i < aimStrains.size(); i++) {
                const double aimStrain = aimStrains[i];
                const double speedStrain = speedStrains[i];
                const double strain = aimStrain + speedStrain;

                if(strain > highestStrain) {
                    highestStrain = strain;
                    highestStrainIndex = i;
                }
                if(aimStrain > highestAimStrain) highestAimStrain = aimStrain;
                if(speedStrain > highestSpeedStrain) highestSpeedStrain = speedStrain;
            }

            // draw strain bar graph
            if(highestAimStrain > 0.0 && highestSpeedStrain > 0.0 && highestStrain > 0.0) {
                const float dpiScale = Osu::getUIScale();

                const float graphWidth = this->scoreBrowser->getSize().x;

                const float msPerPixel = (float)lengthMS / graphWidth;
                const float strainWidth = strainStepMS / msPerPixel;
                const float strainHeightMultiplier = cv::hud_scrubbing_timeline_strains_height.getFloat() * dpiScale;

                McRect graphRect(0, osu->getVirtScreenHeight() - (BottomBar::get_height() + strainHeightMultiplier),
                                 graphWidth, strainHeightMultiplier);

                const float alpha =
                    (graphRect.contains(mouse->getPos()) ? 1.0f : cv::hud_scrubbing_timeline_strains_alpha.getFloat());

                const Color aimStrainColor =
                    argb(alpha, cv::hud_scrubbing_timeline_strains_aim_color_r.getInt() / 255.0f,
                         cv::hud_scrubbing_timeline_strains_aim_color_g.getInt() / 255.0f,
                         cv::hud_scrubbing_timeline_strains_aim_color_b.getInt() / 255.0f);
                const Color speedStrainColor =
                    argb(alpha, cv::hud_scrubbing_timeline_strains_speed_color_r.getInt() / 255.0f,
                         cv::hud_scrubbing_timeline_strains_speed_color_g.getInt() / 255.0f,
                         cv::hud_scrubbing_timeline_strains_speed_color_b.getInt() / 255.0f);

                g->setDepthBuffer(true);
                for(int i = 0; i < aimStrains.size(); i++) {
                    const double aimStrain = (aimStrains[i]) / highestStrain;
                    const double speedStrain = (speedStrains[i]) / highestStrain;
                    // const double strain = (aimStrains[i] + speedStrains[i]) / highestStrain;

                    const double aimStrainHeight = aimStrain * strainHeightMultiplier;
                    const double speedStrainHeight = speedStrain * strainHeightMultiplier;
                    // const double strainHeight = strain * strainHeightMultiplier;

                    if(!keyboard->isShiftDown()) {
                        g->setColor(aimStrainColor);
                        g->fillRect(i * strainWidth,
                                    osu->getVirtScreenHeight() - (BottomBar::get_height() + aimStrainHeight),
                                    std::max(1.0f, std::round(strainWidth + 0.5f)), aimStrainHeight);
                    }

                    if(!keyboard->isControlDown()) {
                        g->setColor(speedStrainColor);
                        g->fillRect(i * strainWidth,
                                    osu->getVirtScreenHeight() -
                                        (BottomBar::get_height() +
                                         ((keyboard->isShiftDown() ? 0 : aimStrainHeight) - speedStrainHeight)),
                                    std::max(1.0f, std::round(strainWidth + 0.5f)), speedStrainHeight + 1);
                    }
                }
                g->setDepthBuffer(false);

                // highlight highest total strain value (+- section block)
                if(highestStrainIndex > -1) {
                    const double aimStrain = (aimStrains[highestStrainIndex]) / highestStrain;
                    const double speedStrain = (speedStrains[highestStrainIndex]) / highestStrain;
                    // const double strain = (aimStrains[i] + speedStrains[i]) / highestStrain;

                    const double aimStrainHeight = aimStrain * strainHeightMultiplier;
                    const double speedStrainHeight = speedStrain * strainHeightMultiplier;
                    // const double strainHeight = strain * strainHeightMultiplier;

                    vec2 topLeftCenter = vec2(
                        highestStrainIndex * strainWidth + strainWidth / 2.0f,
                        osu->getVirtScreenHeight() - (BottomBar::get_height() + aimStrainHeight + speedStrainHeight));

                    const float margin = 5.0f * dpiScale;

                    g->setColor(Color(0xffffffff).setA(alpha));

                    g->drawRect(topLeftCenter.x - margin * strainWidth, topLeftCenter.y - margin * strainWidth,
                                strainWidth * 2 * margin,
                                aimStrainHeight + speedStrainHeight + 2 * margin * strainWidth);
                    g->setAlpha(alpha * 0.5f);
                    g->drawRect(topLeftCenter.x - margin * strainWidth - 2, topLeftCenter.y - margin * strainWidth - 2,
                                strainWidth * 2 * margin + 4,
                                aimStrainHeight + speedStrainHeight + 2 * margin * strainWidth + 4);
                    g->setAlpha(alpha * 0.25f);
                    g->drawRect(topLeftCenter.x - margin * strainWidth - 4, topLeftCenter.y - margin * strainWidth - 4,
                                strainWidth * 2 * margin + 8,
                                aimStrainHeight + speedStrainHeight + 2 * margin * strainWidth + 8);
                }
            }
        }
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
    this->search->setSearchString(this->sSearchString, cv::songbrowser_search_hardcoded_filter.getString());
    this->search->setDrawNumResults(this->bInSearch);
    this->search->setNumFoundResults(this->currentVisibleSearchMatches);
    this->search->setSearching(this->searchHandle.valid() && !this->searchHandle.is_ready());
    this->search->draw();

    // NOTE: Intentionally not calling ScreenBackable::draw() here, since we're already drawing
    //       the back button in draw_bottombar().
    UIScreen::draw();

    // no beatmaps found (osu folder is probably invalid)
    if(db->getBeatmapSets().size() == 0) {
        UString errorMessage1 = "Invalid osu! folder (or no beatmaps found): ";
        errorMessage1.append(this->sLastOsuFolder);
        UString errorMessage2 = "Go to Options -> osu!folder";

        if constexpr(Env::cfg(OS::WASM)) {
            errorMessage1 = "Drop .osz beatmaps onto this window to import them";
            errorMessage2 = "Or click \"Online Beatmaps\" in the Main Menu";
        }

        g->setColor(Env::cfg(OS::WASM) ? 0xffffffff : 0xffff0000);
        g->pushTransform();
        {
            g->translate(
                (int)(osu->getVirtScreenWidth() / 2 - osu->getSubTitleFont()->getStringWidth(errorMessage1) / 2),
                (int)(osu->getVirtScreenHeight() / 2 + osu->getSubTitleFont()->getHeight()));
            g->drawString(osu->getSubTitleFont(), errorMessage1);
        }
        g->popTransform();

        g->setColor(Env::cfg(OS::WASM) ? 0xffffffff : 0xff00ff00);
        g->pushTransform();
        {
            g->translate(
                (int)(osu->getVirtScreenWidth() / 2 - osu->getSubTitleFont()->getStringWidth(errorMessage2) / 2),
                (int)(osu->getVirtScreenHeight() / 2 + osu->getSubTitleFont()->getHeight() * 2 + 15));
            g->drawString(osu->getSubTitleFont(), errorMessage2);
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

SongDifficultyButton *SongBrowser::getDiffButtonByHash(const MD5Hash &diff_hash) const {
    if(const auto &it = this->hashToDiffButton->find(diff_hash); it != this->hashToDiffButton->end()) {
        return it->second;
    }
    return nullptr;
}

// FIXME: this should not be a public function
bool SongBrowser::selectBeatmapset(i32 set_id) {
    if(db->isLoading()) {
        debugLog("Can't select a beatmapset while database is loading!");
        return false;
    }

    auto beatmapset = db->getBeatmapSet(set_id);
    if(beatmapset == nullptr) {
        // Pasted from Downloader::download_beatmap
        auto mapset_path = fmt::format(NEOMOD_MAPS_PATH "/{}/", set_id);
        beatmapset = db->addBeatmapSet(mapset_path);
        debugLog("Finished loading beatmapset {:d}.", set_id);
    }

    if(beatmapset == nullptr) {
        return false;
    }

    if(!this->bInitializedBeatmaps) {
        BeatmapInterface::loading_reselect_map = beatmapset->getDifficulties()[0]->getMD5();
        return false;
    }

    // Just picking the hardest diff for now
    DatabaseBeatmap *best_diff = nullptr;
    const auto &diffs = beatmapset->getDifficulties();
    for(auto &diff : diffs) {
        if(!best_diff ||
           diff->getStarRating(StarPrecalc::active_idx) > best_diff->getStarRating(StarPrecalc::active_idx)) {
            best_diff = diff.get();
        }
    }

    if(best_diff == nullptr) {
        ui->getNotificationOverlay()->addToast(US_("Beatmapset has no difficulties"), ERROR_TOAST);
        return false;
    } else {
        this->onSelectionChange((*this->hashToDiffButton)[best_diff->getMD5()], false);
        this->onDifficultySelected(best_diff, false);
        this->selectSelectedBeatmapSongButton();
        return true;
    }
}

void SongBrowser::update(CBaseUIEventCtx &c) {
    // flush diffcalc results to database
    // do this even if not visible, but not during gameplay
    if(!osu->isInGameplay()) {
        if(!BatchDiffCalc::update_mainthread()) {
            BatchDiffCalc::abort_calc();
            if(BatchDiffCalc::did_actual_work()) {
                this->lastDiffSortModIndex = 0xFF;  // force re-sort with final ratings
            }
        }

        // deferred batch calc for newly imported maps
        if(!BatchDiffCalc::running() && db->bPendingBatchDiffCalc) {
            db->bPendingBatchDiffCalc = false;
            BatchDiffCalc::start_calc();
        }
    }

    if(!this->bVisible) return;

    this->localBestContainer->update(c);
    ScreenBackable::update(c);

    // NOTE: This is placed before BottomBar::update(), otherwise the context menu would close
    //       on a bottombar selector click (yeah, a bit hacky)
    this->contextMenu->update(c);

    BottomBar::update(c);

    // handle changed mods resort
    if(this->lastDiffSortModIndex != StarPrecalc::active_idx) {
        this->onSortChange(cv::songbrowser_sortingtype.getString());
        this->lastDiffSortModIndex = StarPrecalc::active_idx;
    }

    // auto-download
    if(this->map_autodl) {
        auto beatmap = Downloader::download_beatmap(this->map_autodl, this->set_autodl, this->map_dl);
        if(this->map_dl.failed()) {
            auto error_str = fmt::format("Failed to download Beatmap #{:d} :(", this->map_autodl);
            ui->getNotificationOverlay()->addToast(error_str, ERROR_TOAST);
            this->map_autodl = 0;
            this->set_autodl = 0;
            this->map_dl.reset();
        } else if(beatmap != nullptr) {
            this->onDifficultySelected(beatmap, false);
            this->selectSelectedBeatmapSongButton();
            this->map_autodl = 0;
            this->set_autodl = 0;
            this->map_dl.reset();
        } else {
            // TODO @kiwec: this notification format is jank & laggy
            auto text = fmt::format("Downloading... {:.2f}%", this->map_dl.progress() * 100.f);
            ui->getNotificationOverlay()->addNotification(text);
        }
    } else if(this->set_autodl) {
        if(this->selectBeatmapset(this->set_autodl)) {
            this->map_autodl = 0;
            this->set_autodl = 0;
            this->set_dl.reset();
        } else {
            bool ready = Downloader::download_beatmapset(this->set_autodl, this->set_dl);
            if(ready) {
                this->selectBeatmapset(this->set_autodl);

                this->map_autodl = 0;
                this->set_autodl = 0;
                this->set_dl.reset();
            } else if(this->set_dl.failed()) {
                auto error_str = fmt::format("Failed to download Beatmapset #{:d} :(", this->set_autodl);
                ui->getNotificationOverlay()->addToast(error_str, ERROR_TOAST);
                this->map_autodl = 0;
                this->set_autodl = 0;
                this->set_dl.reset();
            } else {
                // TODO @kiwec: this notification format is jank & laggy
                auto text = fmt::format("Downloading... {:.2f}%", this->set_dl.progress() * 100.f);
                ui->getNotificationOverlay()->addNotification(text);
            }
        }
    }

    if(this->score_resort_scheduled) {
        this->rebuildScoreButtons();
        this->score_resort_scheduled = false;
    }

    // update and focus handling
    this->topbarRight->update(c);
    if(this->localBestButton) this->localBestButton->update(c);
    this->scoreBrowser->update(c);
    this->topbarLeft->update(c);

    this->carousel->update(c);

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
       mouse->getPos().x < osu->getVirtScreenWidth() * 0.1f && !this->contextMenu->isVisible()) {
        this->scheduled_scroll_to_selected_button = true;
    }

    // handle searching
    if(this->fSearchWaitTime != 0.0f && engine->getTime() > this->fSearchWaitTime) {
        this->fSearchWaitTime = 0.0f;
        this->onSearchUpdate();
    }

    // handle background search matcher
    {
        if(this->searchHandle.valid() && this->searchHandle.is_ready()) {
            this->searchHandle.get();
            this->rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(true);
        }
        if(!this->searchHandle.valid()) {
            if(this->scheduled_scroll_to_selected_button) {
                this->scheduled_scroll_to_selected_button = false;
                this->scrollToBestButton();
            }
        }
    }
}

void SongBrowser::onKeyDown(KeyboardEvent &key) {
    UIScreen::onKeyDown(key);  // only used for options menu
    if(!this->bVisible || key.isConsumed()) return;

    // context menu
    this->contextMenu->onKeyDown(key);
    if(key.isConsumed()) return;

    // searching text delete & escape key handling
    UString uSearch{this->sSearchString};

    if(!uSearch.isEmpty()) {
        switch(key.getScanCode()) {
            case KEY_DELETE:
            case KEY_BACKSPACE:
                key.consume();
                if(!uSearch.isEmpty()) {
                    if(keyboard->isControlDown()) {
                        // delete everything from the current caret position to the left, until after the first
                        // non-space character (but including it)
                        bool foundNonSpaceChar = false;
                        while(!uSearch.isEmpty()) {
                            const auto &curChar = uSearch.back();

                            const bool whitespace = std::iswspace(static_cast<wint_t>(curChar)) != 0;
                            if(foundNonSpaceChar && whitespace) break;

                            if(!whitespace) foundNonSpaceChar = true;

                            uSearch.pop_back();
                        }
                    } else {
                        uSearch.pop_back();
                    }

                    this->sSearchString = uSearch.utf8View();
                    this->scheduleSearchUpdate(uSearch.length() == 0);
                }
                break;

            case KEY_ESCAPE:
                key.consume();
                uSearch.clear();
                this->sSearchString.clear();
                this->scheduleSearchUpdate(true);
                break;
        }
    } else if(!this->contextMenu->isVisible()) {
        if(key == KEY_ESCAPE)  // can't support GAME_PAUSE hotkey here because of text searching
            this->onBack();
    }

    // paste clipboard support
    if(key == KEY_V) {
        if(keyboard->isControlDown()) {
            const auto &clipstring = env->getClipBoardText();
            if(!clipstring.isEmpty()) {
                uSearch.append(clipstring);
                this->sSearchString = uSearch.utf8View();
                this->scheduleSearchUpdate(false);
            }
        }
    }

    if(key == KEY_LSHIFT || key == KEY_RSHIFT) this->bShiftPressed = true;

    // function hotkeys
    if((key == KEY_F1 || key == cv::TOGGLE_MODSELECT.getVal<SCANCODE>()) && !this->bF1Pressed) {
        this->bF1Pressed = true;
        BottomBar::press_button(BottomBar::MODS);
    }
    if((key == KEY_F2 || key == cv::RANDOM_BEATMAP.getVal<SCANCODE>()) && !this->bF2Pressed) {
        this->bF2Pressed = true;
        BottomBar::press_button(BottomBar::RANDOM);
    }
    if(key == KEY_F3 && !this->bF3Pressed) {
        this->bF3Pressed = true;
        BottomBar::press_button(BottomBar::OPTIONS);
    }

    if(key == KEY_F5) this->refreshBeatmaps();

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

    if(key == KEY_LSHIFT || key == KEY_RSHIFT) this->bShiftPressed = false;
    if(key == KEY_LEFT) this->bLeft = false;
    if(key == KEY_RIGHT) this->bRight = false;

    if(key == KEY_F1 || key == cv::TOGGLE_MODSELECT.getVal<SCANCODE>()) this->bF1Pressed = false;
    if(key == KEY_F2 || key == cv::RANDOM_BEATMAP.getVal<SCANCODE>()) this->bF2Pressed = false;
    if(key == KEY_F3) this->bF3Pressed = false;
}

void SongBrowser::onChar(KeyboardEvent &e) {
    // context menu
    this->contextMenu->onChar(e);
    if(e.isConsumed()) return;

    if(e.getCharCode() < 32 || !this->bVisible || (keyboard->isControlDown() && !keyboard->isAltDown())) return;
    if(this->bF1Pressed || this->bF2Pressed || this->bF3Pressed) return;

    // handle searching
    UString uChar;
    uChar.append(e.getCharCode());
    this->sSearchString.append(uChar.utf8View());

    this->scheduleSearchUpdate();
}

void SongBrowser::onResolutionChange(vec2 newResolution) { ScreenBackable::onResolutionChange(newResolution); }

CBaseUIContainer *SongBrowser::setVisible(bool visible) {
    if(BanchoState::spectating && visible) return this;  // don't allow song browser to be visible while spectating
    if(visible == this->bVisible) return this;

    // Load DB if we haven't attempted yet
    if(visible && this->parentButtons.size() == 0 && !db->isFinished()) {
        this->refreshBeatmaps();
        return this;
    }

    this->bVisible = visible;
    this->bShiftPressed = false;  // seems to get stuck sometimes otherwise

    if(this->bVisible) {
        soundEngine->play(osu->getSkin()->s_expand);

        if(this->bSongButtonsNeedSorting) {
            this->rebuildAfterGroupOrSortChange(this->curGroup);
        }

        this->updateLayout();

        // we have to re-select the current beatmap to start playing music again
        osu->getMapInterface()->selectBeatmap();

        // update user name/stats
        osu->onUserCardChange(BanchoState::get_username().c_str());

        // HACKHACK: workaround for BaseUI framework deficiency (missing mouse events. if a mouse button is being held,
        // and then suddenly a BaseUIElement gets put under it and set visible, and then the mouse button is released,
        // that "incorrectly" fires onMouseUpInside/onClicked/etc.)
        mouse->onButtonChange({Timing::getTicksNS(), MouseButtonFlags::MF_LEFT, false});
        mouse->onButtonChange({Timing::getTicksNS(), MouseButtonFlags::MF_RIGHT, false});

        // For multiplayer: if the host exits song selection without selecting a song, we want to be able to revert
        // to that previous song.
        this->lastSelectedBeatmap = osu->getMapInterface()->getBeatmap();

        // Select button matching current song preview
        this->selectSelectedBeatmapSongButton();

        // re-enable looping, since exiting to the main menu disables it
        if(Sound *music = osu->getMapInterface()->getMusic()) {
            // make sure we loop the music, since if we're carrying over from main menu it was set to not-loop
            music->setLoop(cv::beatmap_preview_music_loop.getBool());
        }

        RichPresence::onSongBrowser();
    } else {
        this->contextMenu->setVisible2(false);
    }

    ui->getChat()->updateVisibility();
    return this;
}

void SongBrowser::selectSelectedBeatmapSongButton() {
    DatabaseBeatmap *map = nullptr;
    if(this->hashToDiffButton->empty() || !(map = osu->getMapInterface()->getBeatmap())) return;

    auto it = this->hashToDiffButton->find(map->getMD5());
    if(it == this->hashToDiffButton->end()) {
        debugLog("No song button found for currently selected beatmap...");
        return;
    }

    auto btn = it->second;
    if(btn->getDatabaseBeatmap() != map) {
        debugLog("Found matching beatmap, but not matching difficulty.");
        return;
    }

    btn->deselect();  // if we select() it when already selected, it would start playing!
    btn->select();
}

void SongBrowser::onPlayEnd(bool quit) {
    // update score displays
    if(!quit) {
        this->rebuildScoreButtons();

        auto *selectedSongDiffButton = this->selectedButton->as<SongDifficultyButton>();
        if(selectedSongDiffButton != nullptr) selectedSongDiffButton->updateGrade();
    }

    // update song info
    if(auto *bm = osu->getMapInterface()->getBeatmap()) {
        // also fix up modification time for improperly stored beatmaps while we're here
        // should be cheap
        // TODO: don't do this here, probably inherently racy somehow
        // NOTE: all modification times need to be accurate to support sorting/grouping by date added
        if(bm->last_modification_time <= 0 && !bm->sFilePath.empty()) {
            struct stat64 attr;
            if(File::stat_c(bm->sFilePath.c_str(), &attr) == 0) {
                bm->last_modification_time = attr.st_mtime;
            }
        }
        this->songInfo->setFromBeatmap(bm);
    }
}

void SongBrowser::onSelectionChange(CarouselButton *button, bool rebuild) {
    const CarouselButton *lastSelected = this->selectedButton;
    this->selectedButton = button;
    if(button == nullptr) return;

    this->contextMenu->setVisible2(false);
    if((lastSelected == button) && !rebuild) return;

    // keep track and update all selection states
    // I'm still not happy with this, but at least all state update logic is localized in this function instead of
    // spread across all buttons

    auto *prevSelDiffBtn = this->selectionPreviousSongDiffButton;
    auto *prevSelSongBtn = this->selectionPreviousSongButton;
    auto *prevSelColBtn = this->selectionPreviousCollectionButton;

    auto *songButtonPtr = button->as<SongButton>();
    auto *diffBtnPtr = button->as<SongDifficultyButton>();
    auto *colBtnPtr = button->as<CollectionButton>();

    if(diffBtnPtr) {
        if(prevSelDiffBtn && prevSelDiffBtn != diffBtnPtr) {
            prevSelDiffBtn->deselect();
        }

        // support individual diffs independent from their parent song button container
        {
            SongButton *parentSongButton = diffBtnPtr->getParentSongButton();
            // if the new diff has a parent song button, then update its selection state (select it to stay consistent)
            if(!parentSongButton->isSelected()) {
                parentSongButton->sortChildren();  // NOTE: workaround for disabled callback firing in select()
                parentSongButton->select({.noCallbacks = true});
                this->onSelectionChange(parentSongButton, false);  // NOTE: recursive call

                // reset last-selected button to the diff button after recursion
                // instead of the parent (which will be hidden)
                this->selectedButton = diffBtnPtr;
            }
        }

        this->selectionPreviousSongDiffButton = diffBtnPtr;
    } else if(songButtonPtr) {
        if(prevSelSongBtn && prevSelSongBtn != songButtonPtr) prevSelSongBtn->deselect();
        if(prevSelDiffBtn) prevSelDiffBtn->deselect();

        this->selectionPreviousSongButton = songButtonPtr;
    } else if(colBtnPtr) {
        // TODO: maybe expand this logic with per-group-type last-open-collection memory

        // logic for allowing collections to be deselected by clicking on the same button (contrary to how beatmaps
        // work)
        const bool isTogglingCollection = (prevSelColBtn != nullptr && prevSelColBtn == colBtnPtr);

        if(prevSelColBtn != nullptr) prevSelColBtn->deselect();

        this->selectionPreviousCollectionButton = colBtnPtr;

        if(isTogglingCollection) this->selectionPreviousCollectionButton = nullptr;
    }

    // try to avoid rebuilding by going through some cases we know can be skipped,
    // in increasingly expensive order (as long as it's still worth it vs rebuilding)
    bool doUpdateLayout = false;

    int once = 0;
    while(!once++ && rebuild) {
        if(colBtnPtr) break;  // no logic for collection buttons, it's not worth it
        // check old and new difficulty selections
        if(prevSelDiffBtn && diffBtnPtr) {
            if(prevSelDiffBtn == diffBtnPtr) {
                rebuild = false;
                break;
            }
            const auto &prevSiblings = prevSelDiffBtn->getSiblingsAndSelf();
            const auto &newSiblings = diffBtnPtr->getSiblingsAndSelf();
            if(&prevSiblings == &newSiblings) {
                // NOTE: pointer comparison
                // skip rebuilding if we merely selected a sibling difficulty button
                rebuild = false;
                break;
            }
            if(prevSiblings.size() == 1 && prevSiblings.size() == newSiblings.size()) {
                // if the new and old diffs are both single-children, no rebuild necessary
                rebuild = false;
                break;
            }
        }
        // check old and new parent selections
        // NOTE: using previously selected difficulty because previously selected song button has already been updated at this point...
        if(prevSelDiffBtn && songButtonPtr) {
            SongButton *oldParentPtr = prevSelDiffBtn->getParentSongButton();
            SongButton *newParentPtr = !diffBtnPtr ? songButtonPtr : diffBtnPtr->getParentSongButton();

            if(oldParentPtr == newParentPtr) {
                rebuild = false;
                break;
            }

            if(&oldParentPtr->getChildren() == &newParentPtr->getChildren()) {
                rebuild = false;
                break;
            }

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
                break;
            }
        }
    }

    if(rebuild) {
        this->rebuildSongButtons();
    } else if(doUpdateLayout) {
        this->updateSongButtonLayout();
    }
}

void SongBrowser::onDifficultySelected(DatabaseBeatmap *map, bool play) {
    // deselect = unload
    osu->getMapInterface()->deselectBeatmap();

    // select = play preview music
    osu->getMapInterface()->selectBeatmap(map);

    // update song info
    if(map) {
        this->songInfo->setFromBeatmap(map);

        // start playing
        if(play) {
            if(BanchoState::is_in_a_multi_room()) {
                ui->getRoom()->set_current_map(map);
                ui->setScreen(ui->getRoom());
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
    anim::moveLinear(&this->fPulseAnimation, 0.0f, 0.55f, true);

    // update score display
    this->rebuildScoreButtons();

    // update web button
    this->webButton->setVisible(this->songInfo->getBeatmapID() > 0);
}

void SongBrowser::refreshBeatmaps() { return this->refreshBeatmaps(this); }

void SongBrowser::refreshBeatmaps(UIScreen *next_screen) {
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
    if(BeatmapDifficulty *map = osu->getMapInterface()->getBeatmap();
       !!map && map->getMD5() != MD5Hash::sentinel && !map->getMD5().is_suspicious()) {
        BeatmapInterface::loading_reselect_map = map->getMD5();
    }

    // reset
    this->checkHandleKillBackgroundSearchMatcher();

    // clear beatmap interface to lose any potential stale references
    osu->reloadMapInterface();
    ui->getMainMenu()->clearPreloadedMaps();

    this->selectedButton = nullptr;
    this->selectionPreviousSongButton = nullptr;
    this->selectionPreviousSongDiffButton = nullptr;
    this->selectionPreviousCollectionButton = nullptr;

    // delete local database and UI
    this->carousel->invalidate();

    this->hashToDiffButton->clear();
    for(auto &songButton : this->parentButtons) {
        delete songButton;
    }
    this->parentButtons.clear();

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

    this->contextMenu->setVisible2(false);

    // clear potentially active search
    this->bInSearch = false;
    this->sSearchString.clear();
    this->sPrevSearchString.clear();
    this->fSearchWaitTime = 0.0f;
    this->searchPrevGroup = std::nullopt;

    // force no grouping (TODO: is this even necessary? why is it here?)
    // if(this->curGroup != GroupType::NO_GROUPING) {
    //     this->onGroupChange("", GroupType::NO_GROUPING);
    // } else {
    //     this->groupByNothingBtn->setTextBrightColor(highlightColor);
    // }

    auto loading_screen = std::make_unique<LoadingScreen>(
        next_screen,
        (LoadingProgressFn)[](LoadingScreen * /*ldscr*/) {
            if(!db->isFinished()) {
                db->update();  // raw load logic
            }
            return db->getProgress();
        },
        (LoadingFinishedFn)[](LoadingScreen * ldscr) {
            if(!db->isFinished()) {
                db->cancel();
            }

            assert(db->isFinished());

            SongBrowser *sb = ui->getSongBrowser();
            assert(sb && sb->loadingOverlay &&
                   "LoadingScreen::cancel_fn: canceled while SongBrowser was in an invalid state");

            sb->loadingOverlay = nullptr;

            // finish loading
            sb->onDatabaseLoadingFinished();

            // kill ourselves
            ui->popOverlay(ldscr);
        });

    this->loadingOverlay = ui->pushOverlay(std::move(loading_screen));

    // start loading
    db->load();

    // make sure whatever was visible is hidden until loading finishes
    ui->hide();
}

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
    auto *parentButton = new SongButton(250.f, 250.f + db->getBeatmapSets().size() * 50.f, 200.f, 50.f, mapset);
    this->parentButtons.push_back(parentButton);

    // add mapset to all necessary groups
    this->addSongButtonToAlphanumericGroup(parentButton, this->artistCollectionButtons, mapset->getArtistLatin());
    this->addSongButtonToAlphanumericGroup(parentButton, this->creatorCollectionButtons, mapset->getCreator());
    this->addSongButtonToAlphanumericGroup(parentButton, this->titleCollectionButtons, mapset->getTitleLatin());

    // use parent's children for grouping
    const auto &tempChildrenForGroups =
        reinterpret_cast<const std::vector<SongDifficultyButton *> &>(parentButton->getChildren());

    for(SongDifficultyButton *diff_btn : tempChildrenForGroups) {
        DatabaseBeatmap *diff = diff_btn->getDatabaseBeatmap();
        assert(diff);  // we just added it

        // map each difficulty hash to its button
        this->hashToDiffButton->emplace(diff->getMD5(), diff_btn);

        if(doDiffCollBtns) {
            const float stars_tmp = diff->getStarRating(StarPrecalc::active_idx);
            const int index = std::clamp<int>(
                (std::isfinite(stars_tmp) && stars_tmp >= static_cast<float>(std::numeric_limits<int>::min()) &&
                 stars_tmp <= static_cast<float>(std::numeric_limits<int>::max()))
                    ? static_cast<int>(stars_tmp)
                    : 0,
                0, 11);
            this->difficultyCollectionButtons[index]->addChild(diff_btn);
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

            this->bpmCollectionButtons[index]->addChild(diff_btn);
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

            this->lengthCollectionButtons[btnIdx]->addChild(diff_btn);
        }
    }
}

void SongBrowser::addSongButtonToAlphanumericGroup(SongButton *sbtn, CollBtnContainer &group, std::string_view name) {
    if(group.size() != 28) {
        debugLog("Alphanumeric group wasn't initialized!");
        return;
    }

    const char firstChar = name.length() == 0 ? '#' : name[0];
    const bool isNumber = (firstChar >= '0' && firstChar <= '9');
    const bool isLowerCase = (firstChar >= 'a' && firstChar <= 'z');
    const bool isUpperCase = (firstChar >= 'A' && firstChar <= 'Z');

    CollectionButton *cbtn = nullptr;
    if(isNumber) {
        cbtn = group[0].get();
    } else if(isLowerCase || isUpperCase) {
        const int index = 1 + (25 - (isLowerCase ? 'z' - firstChar : 'Z' - firstChar));
        cbtn = group[index].get();
    } else {
        cbtn = group[27].get();
    }

    logIfCV(debug_osu, "Inserting {:s}", name);

    cbtn->addChild(sbtn);
}

void SongBrowser::requestNextScrollToSongButtonJumpFix(SongDifficultyButton *diffButton) {
    if(diffButton == nullptr) return;

    this->bNextScrollToSongButtonJumpFixScheduled = true;

    // use parent position only if the diff is NOT independent
    // (i.e., parent is actually visible and expanded in the carousel)
    this->fNextScrollToSongButtonJumpFixOldRelPosY =
        (diffButton->isIndependentDiffButton() ? diffButton->getRelPos().y
                                               : diffButton->getParentSongButton()->getRelPos().y);

    this->fNextScrollToSongButtonJumpFixOldScrollSizeY = this->carousel->getScrollSize().y;
}

bool SongBrowser::isButtonVisible(CarouselButton *songButton) const {
    bool ret = false;

    for(const auto &btn : this->visibleSongButtons) {
        if(btn == songButton) {
            ret = true;
            goto out;
        }
        for(const auto &child : btn->getChildren()) {
            if(child == songButton) {
                ret = true;
                goto out;
            }

            for(const auto &grandchild : child->getChildren()) {
                if(grandchild == songButton) {
                    ret = true;
                    goto out;
                }
            }
        }
    }

out:
    // if(songButton->getDatabaseBeatmap()) {
    //     debugLog("{}: {}", ret ? "VISIBLE" : "NOT VISIBLE", songButton->getDatabaseBeatmap()->getFilePath());
    // }

    return ret;
}

SongBrowser::CollBtnContainer *SongBrowser::getCollectionButtonsForGroup(GroupType group) {
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
    SongDifficultyButton *selDiff =
        this->selectionPreviousSongDiffButton && this->selectionPreviousSongDiffButton->isSelected()
            ? this->selectionPreviousSongDiffButton
            : nullptr;

    SongButton *selParent = this->selectionPreviousSongButton && this->selectionPreviousSongButton->isSelected()
                                ? this->selectionPreviousSongButton
                                : nullptr;

    CollectionButton *selCollection =
        this->selectionPreviousCollectionButton && this->selectionPreviousCollectionButton->isSelected()
            ? this->selectionPreviousCollectionButton
            : nullptr;

    CarouselButton *best = selDiff ? selDiff : selParent ? static_cast<CarouselButton *>(selParent) : selCollection;

    if(!best) {
        this->carousel->scrollToTop();
        return true;
    }

    if(this->curGroup == GroupType::NO_GROUPING) {
        // trivial case
        if(best->isSearchMatch()) this->scrollToSongButton(best, false, true);
        return best->isSearchMatch();
    } else {
        if(best->isType<CollectionButton>()) {
            // nothing better to do
            if(best->isSearchMatch()) {
                this->scrollToSongButton(best);
            }
            return best->isSearchMatch();
        }

        auto *curCollBtnsUnprioritized = this->getCollectionButtonsForGroup(this->curGroup);
        if(!curCollBtnsUnprioritized) {
            this->carousel->scrollToTop();
            return true;
        }

        // FIXME: SHIT, the logic falls apart in GROUP_COLLECTIONS, since parents may or may not be collapsed depending on if the full beatmapset
        // was added to a certain collection or not! need to rework this garbage again
        const bool isAlphanumeric = this->isInAlphanumericCollection();

        CollectionButton *selectedCollection = nullptr;
        CollectionButton *collectionContainingTarget = nullptr;
        CarouselButton *target = nullptr;

        // XXX: special cases for collection grouping (same beatmap can be under multiple collection buttons!)
        // basically: if there is already a collection open, don't open another collection button with the same beatmap
        // a better way would be to build "candidates" no matter what group we're in and not just scroll to the first occurrence
        // AGHH this is so slow and ugly JUST SHOOT ME!
        std::vector<CollectionButton *> curCollBtnsCopyPrioritized;
        curCollBtnsCopyPrioritized.reserve(curCollBtnsUnprioritized->size());
        if(this->curGroup == GroupType::COLLECTIONS && selCollection) {
            curCollBtnsCopyPrioritized.push_back(selCollection);
        }
        for(const auto &colBtn : *curCollBtnsUnprioritized) {
            curCollBtnsCopyPrioritized.push_back(colBtn.get());
        }

        for(CollectionButton *curColBtn : curCollBtnsCopyPrioritized) {
            if(curColBtn->isSelected()) {
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

            const auto &collChildren = curColBtn->getChildren();
            // alphanumeric grouping contains parent song buttons instead of difficulties directly
            // XXX: more special cases for collection grouping (they can be grouped in multiple ways)
            if(isAlphanumeric || this->curGroup == GroupType::COLLECTIONS) {
                if(const auto &songit = std::ranges::find(collChildren, selParent); songit != collChildren.end()) {
                    collectionContainingTarget = curColBtn;
                    if(!selDiff) {
                        target = *songit;
                        // no selected difficulty, scroll to parent
                        break;
                    }
                    const auto &songChildren =
                        reinterpret_cast<const std::vector<SongDifficultyButton *> &>((*songit)->getChildren());
                    if(const auto &diffit = std::ranges::find(songChildren, selDiff); diffit != songChildren.end()) {
                        // found difficulty
                        target = *diffit;
                        break;
                    }
                }
            }

            if(!target && (!isAlphanumeric || this->curGroup == GroupType::COLLECTIONS)) {
                const auto &songChildren = reinterpret_cast<const std::vector<SongDifficultyButton *> &>(collChildren);
                if(const auto &diffit = std::ranges::find(songChildren, selDiff); diffit != songChildren.end()) {
                    collectionContainingTarget = curColBtn;
                    // found difficulty
                    target = *diffit;
                    break;
                }
            }
        }

        if(target) {
            if(target->isSearchMatch()) {
                bool selectedOrDeselected = false;
                // deselect previous
                if(selectedCollection && collectionContainingTarget &&
                   selectedCollection != collectionContainingTarget) {
                    selectedCollection->deselect();
                    selectedOrDeselected = true;
                }
                if(collectionContainingTarget && !collectionContainingTarget->isSelected()) {
                    collectionContainingTarget->select();
                    selectedOrDeselected = true;
                }
                if(auto *targetAsDiff = target->as<SongDifficultyButton>(); targetAsDiff && selectedOrDeselected) {
                    this->requestNextScrollToSongButtonJumpFix(targetAsDiff);
                }
                this->scrollToSongButton(target, false, true);
            }
            return target->isSearchMatch();
        } else {
            // TODO: only scroll to top if we literally don't see any buttons on screen
            this->carousel->scrollToTop();
            return true;
        }
    }
}

void SongBrowser::scrollToSongButton(CarouselButton *songButton, bool alignOnTop, bool knownVisible) {
    if(songButton == nullptr || (!knownVisible && !this->isButtonVisible(songButton))) {
        return;
    }

    // NOTE: compensate potential scroll jump due to added/removed elements (feels a lot better this way, also easier on
    // the eyes)
    if(this->bNextScrollToSongButtonJumpFixScheduled) {
        this->bNextScrollToSongButtonJumpFixScheduled = false;

        float delta = 0.0f;
        {
            if(!this->bNextScrollToSongButtonJumpFixUseScrollSizeDelta)
                delta = (songButton->getRelPos().y - this->fNextScrollToSongButtonJumpFixOldRelPosY);  // (default case)
            else
                delta = this->carousel->getScrollSize().y -
                        this->fNextScrollToSongButtonJumpFixOldScrollSizeY;  // technically not correct but feels a
                                                                             // lot better for KEY_LEFT navigation
        }
        this->carousel->scrollToY(this->carousel->getRelPosY() - delta, false);
    }

    this->carousel->scrollToY(-songButton->getRelPos().y +
                              (alignOnTop ? (0) : (this->carousel->getSize().y / 2 - songButton->getSize().y / 2)));
}

void SongBrowser::rebuildSongButtons() {
    this->carousel->invalidate();

    // NOTE: currently supports 3 depth layers (collection > beatmap > diffs)
    for(CarouselButton *button : this->visibleSongButtons) {
        if(!(button->isSelected() && button->isHiddenIfSelected())) this->carousel->container.addBaseUIElement(button);

        button->resetAnimations();

        // if it's a collection button, recount the number of search-matching children
        // to use as a label
        if(auto *collBtn = button->as<CollectionButton>(); !!collBtn) {
            i32 numVisibleDescendants = 0;
            for(const auto *c : collBtn->getChildren()) {
                const auto &childrenChildren = c->getChildren();
                if(childrenChildren.size() > 0) {
                    for(auto cc : childrenChildren) {
                        if(cc->isSearchMatch()) numVisibleDescendants++;
                    }
                } else if(c->isSearchMatch())
                    numVisibleDescendants++;
            }
            collBtn->setNumVisibleChildren(numVisibleDescendants);
        }

        // children
        if(button->isSelected()) {
            const auto &children = button->getChildren();
            for(auto button2 : children) {
                bool isButton2SearchMatch = false;
                if(button2->getChildren().size() > 0) {
                    const auto &children2 = button2->getChildren();
                    for(auto button3 : children2) {
                        if(button3->isSearchMatch()) {
                            isButton2SearchMatch = true;
                            break;
                        }
                    }
                } else
                    isButton2SearchMatch = button2->isSearchMatch();

                if(this->bInSearch && !isButton2SearchMatch) continue;

                bool addedSingleChild = false;

                if(!(button2->isSelected() && button2->isHiddenIfSelected())) {
                    if(this->getSetVisibility(button2) == SetVisibility::SINGLE_CHILD) {
                        for(auto *child : button2->getChildren()) {
                            if(child->isSearchMatch()) {
                                addedSingleChild = true;
                                this->carousel->container.addBaseUIElement(child);

                                child->resetAnimations();
                                break;  // only one visible child
                            }
                        }
                    } else {
                        this->carousel->container.addBaseUIElement(button2);
                    }
                }

                button2->resetAnimations();

                // child children
                if(!addedSingleChild && button2->isSelected()) {
                    const auto &children2 = button2->getChildren();
                    for(auto button3 : children2) {
                        if(this->bInSearch && !button3->isSearchMatch()) continue;

                        if(!(button3->isSelected() && button3->isHiddenIfSelected()))
                            this->carousel->container.addBaseUIElement(button3);

                        button3->resetAnimations();
                    }
                }
            }
        }
    }

    this->updateSongButtonLayout();
}

void SongBrowser::updateSongButtonLayout() {
    // this rebuilds the entire songButton layout (songButtons in relation to others)
    // only the y axis is set, because the x axis is constantly animated and handled within the button classes
    // themselves

    // all elements must be CarouselButtons, at least
    const auto &elements{this->carousel->container.getElements<CarouselButton>()};

    int yCounter = this->carousel->getSize().y / 4;
    if(elements.size() <= 1) yCounter = this->carousel->getSize().y / 2;

    bool isSelected = false;
    bool inOpenCollection = false;
    for(auto *carouselButton : elements) {
        const auto *diffButtonPointer = carouselButton->as<SongDifficultyButton>();

        // depending on the object type, layout differently
        const bool isDiffButton = diffButtonPointer != nullptr;
        const bool isCollectionButton = !isDiffButton && carouselButton->isType<CollectionButton>();
        const bool isIndependentDiffButton = isDiffButton && diffButtonPointer->isIndependentDiffButton();

        // give selected items & diffs a bit more spacing, to make them stand out
        if(((carouselButton->isSelected() && !isCollectionButton) || isSelected ||
            (isDiffButton && !isIndependentDiffButton)))
            yCounter += carouselButton->getSize().y * 0.1f;

        isSelected = carouselButton->isSelected() || (isDiffButton && !isIndependentDiffButton);

        // give collections a bit more spacing at start & end
        if((carouselButton->isSelected() && isCollectionButton)) yCounter += carouselButton->getSize().y * 0.2f;
        if(inOpenCollection && isCollectionButton && !carouselButton->isSelected())
            yCounter += carouselButton->getSize().y * 0.2f;
        if(isCollectionButton) {
            if(carouselButton->isSelected())
                inOpenCollection = true;
            else
                inOpenCollection = false;
        }

        carouselButton->setTargetRelPosY(yCounter);
        carouselButton->updateLayoutEx();

        yCounter += carouselButton->getActualSize().y;
    }
    this->carousel->setScrollSizeToContent(this->carousel->getSize().y / 2);
}

SongBrowser::SetVisibility SongBrowser::getSetVisibility(const SongButton *parent) const {
    if(parent == nullptr) return SetVisibility::HIDDEN;

    // count visible children
    int visibleCount = 0;
    for(const auto *child : parent->getChildren()) {
        if(child->isSearchMatch()) {
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
    const int margin = 5 * dpiScale;

    // topbar left
    this->topbarLeft->setSize(SongBrowser::getUIScale(390.f), SongBrowser::getUIScale(145.f));

    this->songInfo->onResized();  // force update metrics
    this->songInfo->setRelPos(margin, margin);
    this->songInfo->setSize(
        this->topbarLeft->getSize().x - margin,
        std::max(this->topbarLeft->getSize().y * 0.75f, this->songInfo->getMinimumHeight() + margin));

    const int topbarLeftButtonMargin = 5 * dpiScale;
    const int topbarLeftButtonHeight = 30 * dpiScale;
    const int topbarLeftButtonWidth = 55 * dpiScale;

    const int dropdowns_width =
        this->topbarLeft->getSize().x - 3 * topbarLeftButtonMargin - (topbarLeftButtonWidth + topbarLeftButtonMargin);
    const int dropdowns_y = this->topbarLeft->getSize().y - topbarLeftButtonHeight;

    this->webButton->setSize(topbarLeftButtonWidth, topbarLeftButtonHeight);
    this->webButton->onResized();  // HACKHACK: framework bug (should update string metrics on setSize())
    this->webButton->setRelPos(this->topbarLeft->getSize().x - (topbarLeftButtonMargin + topbarLeftButtonWidth),
                               dropdowns_y);

    this->filterScoresDropdown->setSize(dropdowns_width / 2, topbarLeftButtonHeight);
    this->filterScoresDropdown->onResized();  // HACKHACK: framework bug (should update string metrics on setSize())
    this->filterScoresDropdown->setRelPos(topbarLeftButtonMargin, dropdowns_y);

    this->sortScoresDropdown->setSize(dropdowns_width / 2, topbarLeftButtonHeight);
    this->sortScoresDropdown->onResized();  // HACKHACK: framework bug (should update string metrics on setSize())
    this->sortScoresDropdown->setRelPos(topbarLeftButtonMargin + (dropdowns_width / 2), dropdowns_y);

    this->topbarLeft->update_pos();

    // topbar right
    this->topbarRight->setPosX(osu->getVirtScreenWidth() / 2);
    this->topbarRight->setSize(osu->getVirtScreenWidth() - this->topbarRight->getPos().x,
                               SongBrowser::getUIScale(80.f));

    // horizontal positioning by DPI makes no sense here if we want them to ever have a chance of lining up with skin elements
    this->sortButton->setSize(200.f, 30.f * dpiScale);
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
    const i32 group_btn_width =
        std::clamp<i32>((this->topbarRight->getSize().x - 2 * group_margin) / 4, 0, 200 * dpiScale);
    this->groupByCollectionBtn->setSize(group_btn_width, 30 * dpiScale);
    this->groupByCollectionBtn->setRelPos(this->topbarRight->getSize().x - (group_margin + (4 * group_btn_width)),
                                          this->topbarRight->getSize().y - 30 * dpiScale);
    this->groupByArtistBtn->setSize(group_btn_width, 30 * dpiScale);
    this->groupByArtistBtn->setRelPos(this->topbarRight->getSize().x - (group_margin + (3 * group_btn_width)),
                                      this->topbarRight->getSize().y - 30 * dpiScale);
    this->groupByDifficultyBtn->setSize(group_btn_width, 30 * dpiScale);
    this->groupByDifficultyBtn->setRelPos(this->topbarRight->getSize().x - (group_margin + (2 * group_btn_width)),
                                          this->topbarRight->getSize().y - 30 * dpiScale);
    this->groupByNothingBtn->setSize(group_btn_width, 30 * dpiScale);
    this->groupByNothingBtn->setRelPos(this->topbarRight->getSize().x - (group_margin + (1 * group_btn_width)),
                                       this->topbarRight->getSize().y - 30 * dpiScale);

    this->topbarRight->update_pos();

    // score browser
    this->updateScoreBrowserLayout();

    // song browser
    this->carousel->setPos(this->topbarLeft->getPos().x + this->topbarLeft->getSize().x + 1, 0);
    this->carousel->setSize(osu->getVirtScreenWidth() - (this->topbarLeft->getPos().x + this->topbarLeft->getSize().x),
                            osu->getVirtScreenHeight());
    this->updateSongButtonLayout();

    this->search->setPos(osu->getVirtScreenWidth() / 2, this->topbarRight->getSize().y + 8 * dpiScale);
    this->search->setSize(osu->getVirtScreenWidth() / 2, 20 * dpiScale);
}

void SongBrowser::onBack() {
    if(BanchoState::is_in_a_multi_room()) {
        ui->setScreen(ui->getRoom());

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

            ui->getRoom()->on_map_change();
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

    const int scoreButtonWidthMax = this->topbarLeft->getSize().x;

    f32 browserHeight = osu->getVirtScreenHeight() -
                        (BottomBar::get_height() + (this->topbarLeft->getPos().y + this->topbarLeft->getSize().y)) +
                        2 * dpiScale;
    this->scoreBrowser->setPos(this->topbarLeft->getPos().x + 2 * dpiScale,
                               this->topbarLeft->getPos().y + this->topbarLeft->getSize().y + 4 * dpiScale);
    this->scoreBrowser->setSize(scoreButtonWidthMax, browserHeight);
    const i32 scoreHeight = SongBrowser::getUIScale(53.f);

    // In stable, even when looking at local scores, there is space where the "local best" would be.
    f32 local_best_size = scoreHeight + SongBrowser::getUIScale(61);
    browserHeight -= local_best_size;
    this->scoreBrowser->setSize(this->scoreBrowser->getSize().x, browserHeight);
    this->scoreBrowser->setScrollSizeToContent();

    if(this->localBestContainer->isVisible()) {
        this->localBestContainer->setPos(this->scoreBrowser->getPos().x,
                                         this->scoreBrowser->getPos().y + this->scoreBrowser->getSize().y);
        this->localBestContainer->setSize(this->scoreBrowser->getPos().x, local_best_size);
        this->localBestLabel->setRelPos(this->scoreBrowser->getPos().x, 0);
        this->localBestLabel->setSize(this->scoreBrowser->getSize().x, 40);
        if(this->localBestButton) {
            this->localBestButton->setRelPos(this->scoreBrowser->getPos().x, 40);
            this->localBestButton->setSize(this->scoreBrowser->getSize().x, scoreHeight);
        }
    }

    const std::vector<CBaseUIElement *> &elements = this->scoreBrowser->container.getElements();
    for(size_t i = 0; i < elements.size(); i++) {
        CBaseUIElement *scoreButton = elements[i];
        scoreButton->setSize(this->scoreBrowser->getSize().x, scoreHeight);
        scoreButton->setRelPos(0, i * scoreButton->getSize().y);
    }
    this->scoreBrowserScoresStillLoadingElement->setSize(this->scoreBrowser->getSize().x * 0.9f, scoreHeight * 0.75f);
    this->scoreBrowserScoresStillLoadingElement->setRelPos(
        this->scoreBrowser->getSize().x / 2 - this->scoreBrowserScoresStillLoadingElement->getSize().x / 2,
        (browserHeight / 2) * 0.65f - this->scoreBrowserScoresStillLoadingElement->getSize().y / 2);
    this->scoreBrowserNoRecordsSetElement->setSize(this->scoreBrowser->getSize().x * 0.9f, scoreHeight * 0.75f);
    if(elements[0] == this->scoreBrowserNoRecordsSetElement) {
        this->scoreBrowserNoRecordsSetElement->setRelPos(
            this->scoreBrowser->getSize().x / 2 - this->scoreBrowserScoresStillLoadingElement->getSize().x / 2,
            (browserHeight / 2) * 0.65f - this->scoreBrowserScoresStillLoadingElement->getSize().y / 2);
    } else {
        this->scoreBrowserNoRecordsSetElement->setRelPos(
            this->scoreBrowser->getSize().x / 2 - this->scoreBrowserScoresStillLoadingElement->getSize().x / 2, 45);
    }
    this->localBestContainer->update_pos();
    this->scoreBrowser->container.update_pos();
    this->scoreBrowser->setScrollSizeToContent();
}

void SongBrowser::onGotNewLeaderboard(const MD5Hash &lbHash) {
    if(!this->isVisible()) return;
    assert(BanchoState::is_online());

    auto *map = osu->getMapInterface()->getBeatmap();
    if(!map) return;
    // skip rebuild requests if we have switched off of the map in the meantime
    if(map->getMD5() != lbHash) return;

    this->rebuildScoreButtons();
}

void SongBrowser::rebuildScoreButtons() {
    if(!this->isVisible()) return;

    // XXX: When online, it would be nice to scroll to the current user's highscore

    // reset
    this->scoreBrowser->invalidate();
    this->localBestContainer->invalidate();
    this->localBestContainer->setVisible(false);

    auto *map = osu->getMapInterface()->getBeatmap();
    const bool validBeatmap = !!map;
    const MD5Hash mapHash = validBeatmap ? map->getMD5() : MD5Hash{};

    bool is_online = (BanchoState::is_online() || BanchoState::is_logging_in()) &&
                     cv::songbrowser_scores_filteringtype.getString() != "Local";

    std::vector<FinishedScore> scores;
    if(validBeatmap) {
        Sync::shared_lock lock(db->scores_mtx);

        const FinishedScore *local_best = nullptr;
        const auto &local_scores = db->getScores().find(mapHash);

        if(is_online) {
            if(local_scores != db->getScores().end()) {
                if(const auto &elem = std::ranges::max_element(
                       local_scores->second,
                       [](const FinishedScore &a, const FinishedScore &b) { return a.score < b.score; });
                   elem != local_scores->second.end()) {
                    local_best = &(*elem);
                }
            }

            const auto &search = db->getOnlineScores().find(mapHash);
            if(search != db->getOnlineScores().end()) {
                scores = search->second;

                if(!local_best) {
                    if(!scores.empty()) {
                        // We only want to display "No scores" if there are online scores present
                        // Otherwise, it would be displayed twice
                        SAFE_DELETE(this->localBestButton);
                        this->localBestContainer->addBaseUIElement(this->localBestLabel);
                        this->localBestContainer->addBaseUIElement(this->scoreBrowserNoRecordsSetElement);
                        this->localBestContainer->setVisible(true);
                    }
                } else {
                    SAFE_DELETE(this->localBestButton);
                    this->localBestButton = new ScoreButton(this->contextMenu, 0, 0, 0, 0);
                    this->localBestButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onScoreClicked>(this));
                    this->localBestButton->setScore(*local_best, map);
                    this->localBestButton->resetHighlight();
                    this->localBestButton->setGrabClicks(true);
                    this->localBestContainer->addBaseUIElement(this->localBestLabel);
                    this->localBestContainer->addBaseUIElement(this->localBestButton);
                    this->localBestContainer->setVisible(true);
                }

                // We have already fetched the scores so there's no point in showing "Loading...".
                // When there are no online scores for this map, let's treat it as if we are
                // offline in order to show "No records set!" instead.
                is_online = false;
            } else {
                // We haven't fetched the scores yet, do so now
                BANCHO::Leaderboard::fetch_online_scores(map);

                // Display local best while scores are loading
                if(local_best) {
                    SAFE_DELETE(this->localBestButton);
                    this->localBestButton = new ScoreButton(this->contextMenu, 0, 0, 0, 0);
                    this->localBestButton->setClickCallback(SA::MakeDelegate<&SongBrowser::onScoreClicked>(this));
                    this->localBestButton->setScore(*local_best, map);
                    this->localBestButton->resetHighlight();
                    this->localBestButton->setGrabClicks(true);
                    this->localBestContainer->addBaseUIElement(this->localBestLabel);
                    this->localBestContainer->addBaseUIElement(this->localBestButton);
                    this->localBestContainer->setVisible(true);
                }
            }
        } else {
            if(local_scores != db->getScores().end()) {
                scores = local_scores->second;
            }
        }
    }

    const int numScores = scores.size();

    if(numScores > 0) {
        // sort
        Database::sortScoresInPlace(scores);
    }

    // top up cache as necessary
    if(numScores > this->scoreButtonCache.size()) {
        const int numNewButtons = numScores - this->scoreButtonCache.size();
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
        CBaseUIElement *toAdd = validBeatmap && is_online ? this->scoreBrowserScoresStillLoadingElement
                                                          : this->scoreBrowserNoRecordsSetElement;
        this->scoreBrowser->container.addBaseUIElement(toAdd, this->scoreBrowserScoresStillLoadingElement->getRelPos());
    } else {
        // build
        std::vector<ScoreButton *> scoreButtons;
        for(int i = 0; i < numScores; i++) {
            ScoreButton *button = this->scoreButtonCache[i];
            button->setScore(scores[i], map, i + 1);
            scoreButtons.push_back(button);
        }

        // add
        for(int i = 0; i < numScores; i++) {
            scoreButtons[i]->setIndex(i + 1);
            this->scoreBrowser->container.addBaseUIElement(scoreButtons[i]);
        }

        // reset
        for(auto &scoreButton : scoreButtons) {
            scoreButton->resetHighlight();
        }
    }

    // layout
    this->updateScoreBrowserLayout();

    // update grade of difficulty button for current map
    // (weird place for this to be, i think the intent is to update them after you set a score)
    if(!validBeatmap) return;
    if(const auto &it = this->hashToDiffButton->find(mapHash); it != this->hashToDiffButton->end()) {
        it->second->updateGrade();
    }
}

void SongBrowser::scheduleSearchUpdate(bool immediately) {
    this->fSearchWaitTime = engine->getTime() + (immediately ? 0.0f : cv::songbrowser_search_delay.getFloat());
}

void SongBrowser::checkHandleKillBackgroundSearchMatcher() {
    this->searchHandle.cancel();
    if(this->searchHandle.valid()) this->searchHandle.wait();
}

void SongBrowser::initializeGroupingButtons() {
#define MKCBTN(name__) \
    std::make_unique<CollectionButton>(250.f, 250.f, 200.f, 50.f, fmt::format("cbtn-{}", name__), name__)
    // artist, title, creator
    for(auto *coll : {&this->artistCollectionButtons, &this->titleCollectionButtons, &this->creatorCollectionButtons}) {
        if(coll->size() == 28) continue;
        coll->resize(28);

        // 0-9
        {
            coll->at(0) = MKCBTN(US_("0-9"));
        }

        // A-Z
        for(size_t i = 0; i < 26; i++) {
            coll->at(i + 1) = MKCBTN(fmt::format("{:c}", 'A' + i));
        }

        // Other
        {
            coll->at(27) = MKCBTN(US_("Other"));
        }
    }

    // difficulty
    if(auto &diffbtns = this->difficultyCollectionButtons; diffbtns.size() != 12) {
        diffbtns.resize(12);
        for(size_t i = 0; i < 12; i++) {
            UString difficultyCollectionName;
            if(i < 1)
                difficultyCollectionName = US_("Below 1 star");
            else if(i > 10)
                difficultyCollectionName = US_("Above 10 stars");
            else
                difficultyCollectionName = fmt::format("{:d} star{:s}", i, i == 1 ? "" : "s");

            diffbtns[i] = MKCBTN(difficultyCollectionName);
        }
    }

    // bpm
    if(auto &bpmbtns = this->bpmCollectionButtons; bpmbtns.size() != 6) {
        bpmbtns.resize(6);
        bpmbtns[0] = MKCBTN(US_("Under 60 BPM"));
        bpmbtns[1] = MKCBTN(US_("Under 120 BPM"));
        bpmbtns[2] = MKCBTN(US_("Under 180 BPM"));
        bpmbtns[3] = MKCBTN(US_("Under 240 BPM"));
        bpmbtns[4] = MKCBTN(US_("Under 300 BPM"));
        bpmbtns[5] = MKCBTN(US_("Over 300 BPM"));
    }

    // dateadded
    {
        // TODO: annoying
    }

    // length
    if(auto &lenbtns = this->lengthCollectionButtons; lenbtns.size() != 7) {
        lenbtns.resize(7);
        lenbtns[0] = MKCBTN(US_("1 minute or less"));
        lenbtns[1] = MKCBTN(US_("2 minutes or less"));
        lenbtns[2] = MKCBTN(US_("3 minutes or less"));
        lenbtns[3] = MKCBTN(US_("4 minutes or less"));
        lenbtns[4] = MKCBTN(US_("5 minutes or less"));
        lenbtns[5] = MKCBTN(US_("10 minutes or less"));
        lenbtns[6] = MKCBTN(US_("Over 10 minutes"));
    }
#undef MKCBTN
}

void SongBrowser::onDatabaseLoadingFinished() {
    // Extract oszs from neomod's maps/ directory now
    auto oszs = env->getFilesInFolder(NEOMOD_MAPS_PATH "/");
    for(const auto &file : oszs) {
        if(env->getFileExtensionFromFilePath(file) != "osz") continue;
        auto path = NEOMOD_MAPS_PATH "/" + file;
        bool extracted = env->getEnvInterop().handle_osz(path.c_str());
        if(extracted) env->deleteFile(path);
    }

    Timer t;
    t.start();

    debugLog("Loading {} beatmapsets from database.", db->getBeatmapSets().size());

    // initialize all collection (grouped) buttons
    this->initializeGroupingButtons();

    // add all beatmaps (build buttons)
    {
        const size_t numSets = db->getBeatmapSets().size();
        size_t numDiffs = 0;
        for(const auto &mapset : db->getBeatmapSets()) {
            numDiffs += mapset->getDifficulties().size();
        }
        this->parentButtons.reserve(numSets);
        this->hashToDiffButton->reserve(numDiffs);
        for(const auto &mapset : db->getBeatmapSets()) {
            this->addBeatmapSet(mapset.get(), true /* initial songbrowser load flag (skip some checks) */);
        }
        this->parentButtons.shrink_to_fit();
    }

    // build collections
    this->recreateCollectionsButtons();

    this->bInitializedBeatmaps = true;
    this->bSongButtonsNeedSorting = true;

    this->onSortChange(cv::songbrowser_sortingtype.getString());
    this->onGroupChange("", this->curGroup);  // does nothing besides re-highlight the buttons

    this->onSortScoresChange(cv::songbrowser_scores_sortingtype.getString());

    // update rich presence (discord total pp)
    RichPresence::onSongBrowser();

    // update user name/stats
    osu->onUserCardChange(BanchoState::get_username());

    if(cv::songbrowser_search_hardcoded_filter.getString().length() > 0) {
        this->onSearchUpdate();
    }

    // ugly hack to transition from preloaded main menu beatmap to database-loaded beatmap without pausing music
    {
        DatabaseBeatmap *reselectMap = nullptr;
        if(BeatmapInterface::loading_reselect_map != MD5Hash{}) {
            reselectMap = db->getBeatmapDifficulty(BeatmapInterface::loading_reselect_map);
            if(!reselectMap) {
                // FIXME: why does this happen? every time i see this log, the md5hash is from a map that we never load in
                // from the database... where are we loading these stray .osu files from
                debugLog("failed to get reselect map for {}", BeatmapInterface::loading_reselect_map);
            }
        }

        this->onDifficultySelected(reselectMap, false);  // select even if null (clear existing)
        if(reselectMap) {
            this->selectSelectedBeatmapSongButton();
        }

        // the reselect map is cleared when the preview starts playing
    }

    // ok, if we still haven't selected a song, do so now
    if(osu->getMapInterface()->getBeatmap() == nullptr) {
        this->selectRandomBeatmap();
    }

    if(Sound *music = osu->getMapInterface()->getMusic()) {
        // make sure we loop the music, since if we're carrying over from main menu it was set to not-loop
        music->setLoop(cv::beatmap_preview_music_loop.getBool());
    }

    t.update();
    debugLog("Took {} seconds.", t.getElapsedTime());

    // Watch for new maps now
    directoryWatcher->watch_directory(NEOMOD_MAPS_PATH "/", [](const FileChangeEvent &ev) {
        if(ev.type != FileChangeType::CREATED) return;
        logRaw("[DirectoryWatcher] Importing new beatmap {}: type {}", ev.path, (u32)ev.type);
        if(env->getFileExtensionFromFilePath(ev.path) != "osz") return;
        const bool extracted = env->getEnvInterop().handle_osz(ev.path.c_str());
        if(extracted) env->deleteFile(ev.path);
    });
}

void SongBrowser::onSearchUpdate() {
    const std::string hardcodedFilterString{cv::songbrowser_search_hardcoded_filter.getString()};  // NOLINT
    const bool hasHardcodedSearchStringChanged = (this->sPrevHardcodedSearchString != hardcodedFilterString);
    const bool hasSearchStringChanged = (this->sPrevSearchString != this->sSearchString);

    const bool prevInSearch = this->bInSearch;
    this->bInSearch = (!this->sSearchString.empty() || !hardcodedFilterString.empty());
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
            // stop potentially running async search
            this->checkHandleKillBackgroundSearchMatcher();

            this->searchHandle = AsyncSongButtonMatcher::submitSearchMatch(
                this->parentButtons, this->sSearchString, hardcodedFilterString,
                osu->getMapInterface()->getSpeedMultiplier());
        } else
            this->rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(true, true);

        // (results are handled in update() once available)
    } else  // exit search
    {
        // exiting the search does not need any async work, so we can just directly do it in here

        // stop potentially running async search
        this->checkHandleKillBackgroundSearchMatcher();

        // reset container and visible buttons list
        this->carousel->invalidate();
        this->visibleSongButtons.clear();

        // reset all search flags
        for(auto &songButton : this->parentButtons) {
            for(auto &c : songButton->getChildren()) {
                c->setIsSearchMatch(true);
            }
        }

        // remember which tab was selected, instead of defaulting back to no grouping
        // this also rebuilds the visible buttons list
        if(this->searchPrevGroup.has_value()) {
            auto prevgid = this->searchPrevGroup.value();
            this->onGroupChange(GROUP_NAMES[prevgid], prevgid);
        }
    }

    this->sPrevSearchString = this->sSearchString;
    this->sPrevHardcodedSearchString = cv::songbrowser_search_hardcoded_filter.getString();
}

void SongBrowser::rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(bool scrollToTop,
                                                                                bool doRebuildSongButtons) {
    // reset container and visible buttons list
    this->carousel->invalidate();
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
        for(auto *parentButton : this->parentButtons) {
            const SetVisibility visibility = this->getSetVisibility(parentButton);

            switch(visibility) {
                case SetVisibility::HIDDEN:
                    // don't add to carousel
                    break;

                case SetVisibility::SINGLE_CHILD:
                    // add only the visible child
                    for(auto *child : parentButton->getChildren()) {
                        if(child->isSearchMatch()) {
                            if(recountMatches) this->currentVisibleSearchMatches++;
                            this->visibleSongButtons.push_back(child);
                            break;  // only one visible child
                        }
                    }
                    break;

                case SetVisibility::SHOW_PARENT:
                    // add parent (which will expand to show children if selected)
                    if(recountMatches) {
                        for(const auto *child : parentButton->getChildren()) {
                            if(child->isSearchMatch()) {
                                this->currentVisibleSearchMatches++;
                            }
                        }
                    }
                    this->visibleSongButtons.push_back(parentButton);
                    break;
            }
        }
    } else {
        if(auto *groupButtons = this->getCollectionButtonsForGroup(this->curGroup); !!groupButtons) {
            for(const auto &groupButton : *groupButtons) {
                bool isAnyMatchInGroup = false;

                const auto &children = groupButton->getChildren();
                for(const auto &c : children) {
                    const auto &childrenChildren = c->getChildren();
                    if(childrenChildren.size() > 0) {
                        for(const auto &cc : childrenChildren) {
                            if(cc->isSearchMatch()) {
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
                    } else if(c->isSearchMatch()) {
                        isAnyMatchInGroup = true;
                        if(canBreakEarly) {
                            break;
                        } else {
                            this->currentVisibleSearchMatches++;
                        }
                    }
                }

                if(isAnyMatchInGroup || !this->bInSearch) {
                    this->visibleSongButtons.push_back(groupButton.get());
                }
            }
        }
    }

    if(doRebuildSongButtons) this->rebuildSongButtons();

    // scroll to top search result, or auto select the only result
    if(scrollToTop) {
        if(this->visibleSongButtons.size() > 1) {
            // scroll to the currently selected button if it's a search match, otherwise the first one
            if(!this->scrollToBestButton()) {
                this->scrollToSongButton(this->visibleSongButtons[0]);
            }
        } else if(this->visibleSongButtons.size() > 0) {
            this->selectSongButton(this->visibleSongButtons[0]);
        }
    }
}

void SongBrowser::onFilterScoresClicked(CBaseUIButton *button) {
    static const std::array filters{US_("Local"),   US_("Global"),  US_("Selected mods"),
                                    US_("Country"), US_("Friends"), US_("Team")};

    this->contextMenu->setPos(button->getPos());
    this->contextMenu->setRelPos(button->getRelPos());
    this->contextMenu->begin(button->getSize().x);
    {
        if(BanchoState::is_online()) {
            for(const auto &filter : filters) {
                CBaseUIButton *button = this->contextMenu->addButton(filter);
                if(filter == cv::songbrowser_scores_filteringtype.getString()) {
                    button->setTextBrightColor(0xff00ff00);
                }
            }
        } else {
            CBaseUIButton *button = this->contextMenu->addButton(US_("Local"));
            button->setTextBrightColor(0xff00ff00);
        }
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onFilterScoresChange>(this));
}

void SongBrowser::onSortScoresClicked(CBaseUIButton *button) {
    this->contextMenu->setPos(button->getPos());
    this->contextMenu->setRelPos(button->getRelPos());
    this->contextMenu->begin(button->getSize().x);
    {
        for(const auto &scoreSortingMethod : Database::SCORE_SORTING_METHODS) {
            CBaseUIButton *button = this->contextMenu->addButton(UString{scoreSortingMethod.name});
            if(scoreSortingMethod.name == cv::songbrowser_scores_sortingtype.getString())
                button->setTextBrightColor(0xff00ff00);
        }
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onSortScoresChange>(this));
}

void SongBrowser::onFilterScoresChange(const UString &text, int id) {
    UString text_to_set{text};
    const auto &type_cv = &cv::songbrowser_scores_filteringtype;
    const auto &manual_type_cv = &cv::songbrowser_scores_filteringtype_manual;

    // abusing "id" to determine whether it was a click callback or due to login
    if(id != LOGIN_STATE_FILTER_ID) {
        manual_type_cv->setValue(text);
    }

    // always change for manual setting, otherwise allow login state to affect filtering (if it was never manually set)
    const bool should_change = id != LOGIN_STATE_FILTER_ID || (manual_type_cv->isDefault());
    if(!should_change) {
        text_to_set = UString{manual_type_cv->getString()};
    }
    type_cv->setValue(text_to_set);  // NOTE: remember

    this->filterScoresDropdown->setText(text_to_set);
    db->getOnlineScores().clear();
    this->rebuildScoreButtons();
    this->scoreBrowser->scrollToTop();
}

void SongBrowser::onSortScoresChange(const UString &text, int /*id*/) {
    cv::songbrowser_scores_sortingtype.setValue(text);  // NOTE: remember
    this->sortScoresDropdown->setText(text);
    this->rebuildScoreButtons();
    this->scoreBrowser->scrollToTop();
    ui->getHUD()->updateScoringMetric();
}

void SongBrowser::onWebClicked(CBaseUIButton * /*button*/) {
    if(this->songInfo->getBeatmapID() > 0) {
        auto scheme = cv::use_https.getBool() ? "https://" : "http://";
        auto endpoint = BanchoState::is_online() ? BanchoState::endpoint : "ppy.sh";
        env->openURLInDefaultBrowser(fmt::format("{}osu.{}/b/{}", scheme, endpoint, this->songInfo->getBeatmapID()));
        ui->getNotificationOverlay()->addNotification("Opening browser, please wait ...", 0xffffffff, false, 0.75f);
    }
}

void SongBrowser::onQuickGroupClicked(CBaseUIButton *button, bool /*left*/, bool right) {
    if(right || button->getText().isEmpty()) return;
    const std::string_view btntxt = button->getText().utf8View();

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
    this->contextMenu->begin(button->getSize().x);
    {
        for(int gid = -1; const auto &gname : GROUP_NAMES) {
            ++gid;
            CBaseUIButton *button = this->contextMenu->addButton(gname, gid);
            if(gid == this->curGroup) button->setTextBrightColor(0xff00ff00);
        }
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onGroupChange>(this));
}

void SongBrowser::onGroupChange(const UString &text, int id) {
    auto group_id = this->curGroup;
    if(id >= 0 && id < GroupType::MAX) {
        group_id = (GroupType)id;
    } else if(!text.isEmpty()) {
        for(int gid = -1; const auto &gname : GROUP_NAMES) {
            ++gid;
            if(text.utf8View() == gname) {
                group_id = (GroupType)gid;
                break;
            }
        }
    }

    // update group combobox button text
    this->groupButton->setText(GROUP_NAMES[group_id]);

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
    this->contextMenu->begin(button->getSize().x);
    {
        for(int stype = -1; const auto &sortmeth : SORTING_METHODS) {
            ++stype;
            CBaseUIButton *button = this->contextMenu->addButton(sortmeth.name, stype);
            if(stype == this->curSortMethod) button->setTextBrightColor(0xff00ff00);
        }
    }
    this->contextMenu->end(false, false);
    this->contextMenu->setClickCallback(SA::MakeDelegate<&SongBrowser::onSortChange>(this));
}

void SongBrowser::onSortChange(const UString &text, int id) {
    auto sort_id = this->curSortMethod;
    if(id >= 0 && id < SortType::MAX) {
        sort_id = (SortType)id;
    } else if(!text.isEmpty()) {
        for(int sid = -1; const auto &sortmeth : SORTING_METHODS) {
            ++sid;
            if(text.utf8View() == sortmeth.name) {
                sort_id = (SortType)sid;
                break;
            }
        }
    }

    SORTING_METHOD newMethod = SORTING_METHODS[sort_id];

    const bool sortChanged = sort_id != this->curSortMethod;

    this->sortButton->setText(newMethod.name);
    cv::songbrowser_sortingtype.setValue(newMethod.name);

    // reuse the group update logic instead of duplicating it
    if(!sortChanged) {
        this->rebuildAfterGroupOrSortChange(this->curGroup);
    } else {
        this->rebuildAfterGroupOrSortChange(this->curGroup, sort_id);
    }
}

void SongBrowser::rebucketDifficultyCollections() {
    if(this->difficultyCollectionButtons.size() != 12) return;

    for(auto &btn : this->difficultyCollectionButtons) btn->setChildren({});

    for(auto *parentBtn : this->parentButtons) {
        for(auto *child : parentBtn->getChildren()) {
            auto *diff = child->getDatabaseBeatmap();
            if(!diff) continue;
            const float stars = diff->getStarRating(StarPrecalc::active_idx);
            const int idx =
                std::clamp<int>((std::isfinite(stars) && stars >= static_cast<float>(std::numeric_limits<int>::min()) &&
                                 stars <= static_cast<float>(std::numeric_limits<int>::max()))
                                    ? static_cast<int>(stars)
                                    : 0,
                                0, 11);
            this->difficultyCollectionButtons[idx]->addChild(child);
        }
    }
}

void SongBrowser::rebuildAfterGroupOrSortChange(GroupType group, const std::optional<SortType> &sortMethod) {
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

    if(this->bSongButtonsNeedSorting || sortingChanged || diffSortChanged) {
        // lazy update grade
        if(this->curSortMethod == SortType::RANKACHIEVED) {
            for(SongButton *songButton : this->parentButtons) {
                for(SongDifficultyButton *diffButton :
                    reinterpret_cast<const std::vector<SongDifficultyButton *> &>(songButton->getChildren())) {
                    diffButton->maybeUpdateGrade();
                }
            }
        }
        // the master button list should be sorted for all groupings
        srt::pdqsort(this->parentButtons, SORTING_METHODS[this->curSortMethod].comparator);
        this->bSongButtonsNeedSorting = false;
    }

    this->visibleSongButtons.clear();

    if(group == GroupType::NO_GROUPING) {
        this->visibleSongButtons.reserve(this->parentButtons.size());

        // apply visibility logic
        for(auto *parentButton : this->parentButtons) {
            const SetVisibility visibility = this->getSetVisibility(parentButton);

            switch(visibility) {
                case SetVisibility::HIDDEN:
                    break;

                case SetVisibility::SINGLE_CHILD:
                    for(auto *child : parentButton->getChildren()) {
                        if(child->isSearchMatch()) {
                            this->visibleSongButtons.push_back(child);
                            break;
                        }
                    }
                    break;

                case SetVisibility::SHOW_PARENT:
                    this->visibleSongButtons.push_back(parentButton);
                    break;
            }
        }
    } else {
        if(auto *collBtns = this->getCollectionButtonsForGroup(group); !!collBtns) {
            if(group == GroupType::DIFFICULTY) {
                this->rebucketDifficultyCollections();
            }

            this->visibleSongButtons.reserve(collBtns->size());
            for(const auto &unq : *collBtns) {
                this->visibleSongButtons.push_back(unq.get());
            }

            if(groupingChanged || sortingChanged || diffSortChanged) {
                for(const auto &button : *collBtns) {
                    auto &children = button->getChildren();
                    if(!children.empty()) {
                        srt::pdqsort(children, SORTING_METHODS[this->curSortMethod].comparator);
                        button->setChildren(children);
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
        ui->getNotificationOverlay()->addToast(US_("Disabled FPoSu mode."), INFO_TOAST);
    } else {
        cv::mod_fposu.setValue(true);
        ui->getNotificationOverlay()->addToast(US_("Enabled FPoSu mode."), SUCCESS_TOAST);
    }
}

void SongBrowser::onSelectionMods() {
    ui->setScreen(ui->getModSelector());
    soundEngine->play(osu->getSkin()->s_expand);
}

void SongBrowser::onSelectionRandom() {
    soundEngine->play(osu->getSkin()->s_click_button);
    if(this->bShiftPressed)
        this->selectPreviousRandomBeatmap();
    else
        this->bRandomBeatmapScheduled = true;
}

void SongBrowser::onSelectionOptions() {
    soundEngine->play(osu->getSkin()->s_click_button);

    if(this->selectedButton != nullptr) {
        this->scrollToSongButton(this->selectedButton);

        const vec2 heuristicSongButtonPositionAfterSmoothScrollFinishes =
            (this->carousel->getPos() + this->carousel->getSize() / 2.f);

        auto *songButtonPointer = this->selectedButton->as<SongButton>();
        auto *collectionButtonPointer = this->selectedButton->as<CollectionButton>();
        if(songButtonPointer != nullptr) {
            songButtonPointer->triggerContextMenu(heuristicSongButtonPositionAfterSmoothScrollFinishes);
        } else if(collectionButtonPointer != nullptr) {
            collectionButtonPointer->triggerContextMenu(heuristicSongButtonPositionAfterSmoothScrollFinishes);
        }
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

void SongBrowser::onSongButtonContextMenu(SongButton *songButton, const UString &text, int id) {
    // debugLog("SongBrowser::onSongButtonContextMenu({:p}, {:s}, {:d})", songButton, text.toUtf8(), id);

    struct CollectionManagementHelper {
        static std::vector<MD5Hash> getBeatmapSetHashesForSongButton(SongButton *songButton) {
            std::vector<MD5Hash> beatmapSetHashes;
            {
                const auto &songButtonChildren = songButton->getChildren();
                if(songButtonChildren.size() > 0) {
                    for(auto i : songButtonChildren) {
                        beatmapSetHashes.push_back(i->getDatabaseBeatmap()->getMD5());
                    }
                } else {
                    const BeatmapSet *mapset = db->getBeatmapSet(songButton->getDatabaseBeatmap()->getSetID());
                    if(mapset != nullptr) {
                        const auto &diffs = mapset->getDifficulties();
                        for(const auto &diff : diffs) {
                            beatmapSetHashes.push_back(diff->getMD5());
                        }
                    }
                }
            }
            return beatmapSetHashes;
        }
    };

    bool updateUI = false;
    {
        if(id == 1) {
            // add diff to collection
            std::string name = text.toUtf8();
            auto &collection = Collections::get_or_create_collection(name);
            collection.add_map(songButton->getDatabaseBeatmap()->getMD5());
            Collections::save_collections();
            updateUI = true;
        } else if(id == 2) {
            // add set to collection
            std::string name = text.toUtf8();
            auto &collection = Collections::get_or_create_collection(name);
            const std::vector<MD5Hash> beatmapSetHashes =
                CollectionManagementHelper::getBeatmapSetHashesForSongButton(songButton);
            for(const auto &hash : beatmapSetHashes) {
                collection.add_map(hash);
            }
            Collections::save_collections();
            updateUI = true;
        } else if(id == 3) {
            // remove diff from collection

            // get collection name by selection
            std::string collectionName;
            {
                for(auto &collectionButton : this->collectionButtons) {
                    if(collectionButton->isSelected()) {
                        collectionName = collectionButton->getCollectionName();
                        break;
                    }
                }
            }

            auto &collection = Collections::get_or_create_collection(collectionName);
            collection.remove_map(songButton->getDatabaseBeatmap()->getMD5());
            Collections::save_collections();
            updateUI = true;
        } else if(id == 4) {
            // remove entire set from collection

            // get collection name by selection
            std::string collectionName;
            {
                for(auto &collectionButton : this->collectionButtons) {
                    if(collectionButton->isSelected()) {
                        collectionName = collectionButton->getCollectionName();
                        break;
                    }
                }
            }

            auto &collection = Collections::get_or_create_collection(collectionName);
            const std::vector<MD5Hash> beatmapSetHashes =
                CollectionManagementHelper::getBeatmapSetHashesForSongButton(songButton);
            for(const auto &hash : beatmapSetHashes) {
                collection.remove_map(hash);
            }
            Collections::save_collections();
            updateUI = true;
        } else if(id == -2 || id == -4) {
            // add beatmap(set) to new collection
            std::string name = text.toUtf8();
            auto &collection = Collections::get_or_create_collection(name);

            if(id == -2) {
                // id == -2 means beatmap
                collection.add_map(songButton->getDatabaseBeatmap()->getMD5());
                updateUI = true;
            } else if(id == -4) {
                // id == -4 means beatmapset
                const std::vector<MD5Hash> beatmapSetHashes =
                    CollectionManagementHelper::getBeatmapSetHashesForSongButton(songButton);
                for(const auto &hash : beatmapSetHashes) {
                    collection.add_map(hash);
                }
                updateUI = true;
            }

            Collections::save_collections();
        } else if(id == 5) {  // export beatmapset
            assert(songButton->getDatabaseBeatmap());
            std::string folder = songButton->getDatabaseBeatmap()->getFolder();
            auto ctx = MapExporter::ExportContext{{folder}, "", BottomBar::update_export_progress_cb};
            MapExporter::export_paths(std::move(ctx));
        }
    }

    if(updateUI) {
        const float prevScrollPosY = this->carousel->getRelPosY();  // usability
        const auto previouslySelectedCollectionName =
            (this->selectionPreviousCollectionButton != nullptr
                 ? this->selectionPreviousCollectionButton->getCollectionName()
                 : "");  // usability
        {
            this->recreateCollectionsButtons();
            this->rebuildSongButtonsAndVisibleSongButtonsWithSearchMatchSupport(
                false, false);  // (last false = skipping rebuildSongButtons() here)
            this->bSongButtonsNeedSorting = true;
            this->onSortChange(
                cv::songbrowser_sortingtype.getString());  // (because this does the rebuildSongButtons())
        }
        if(previouslySelectedCollectionName.length() > 0) {
            for(auto &collectionButton : this->collectionButtons) {
                if(collectionButton->getCollectionName() == previouslySelectedCollectionName) {
                    collectionButton->select();
                    this->carousel->scrollToY(prevScrollPosY, false);
                    break;
                }
            }
        }
    }
}

void SongBrowser::onCollectionButtonContextMenu(CollectionButton *collectionButton, const UString &text, int id) {
    std::string collection_name = text.toUtf8();

    if(id == 2) {  // delete collection
        if(const auto &it =
               std::ranges::find(this->collectionButtons, collection_name,
                                 [](const auto &collBtn) -> std::string { return collBtn->getCollectionName(); });
           it != this->collectionButtons.end() && Collections::delete_collection(collection_name)) {
            Collections::save_collections();

            // delete UI
            this->collectionButtons.erase(it);

            // reset UI state
            this->selectionPreviousCollectionButton = nullptr;

            // update UI
            this->bSongButtonsNeedSorting = true;
            this->rebuildAfterGroupOrSortChange(GroupType::COLLECTIONS);
        }
    } else if(id == 3) {  // rename collection
        const std::string &currentButtonName = collectionButton->getCollectionName();
        if(!currentButtonName.empty()) {
            auto &existingCollection = Collections::get_or_create_collection(currentButtonName);

            if(existingCollection.rename_to(collection_name)) {
                Collections::save_collections();

                // rename button
                if(const auto &it = std::ranges::find(this->collectionButtons, collectionButton,
                                                      [](const auto &unq) -> auto * { return unq.get(); });
                   it != this->collectionButtons.end()) {
                    (*it)->setCollectionName(collection_name);

                    // resort collection buttons
                    std::ranges::stable_sort(
                        this->collectionButtons,
                        [](const char *s1, const char *s2) -> bool { return strcasecmp(s1, s2) < 0; },
                        [](const auto &colBtn) -> const char * { return colBtn->getCollectionName().c_str(); });
                }

                // update UI
                this->onSortChange(cv::songbrowser_sortingtype.getString());
            }
        }
    } else if(id == 5) {  // export collection
        std::vector<std::string> pathsToExport;
        auto &existingCollection = Collections::get_or_create_collection(collection_name);
        for(auto &mapHash : existingCollection.get_maps()) {
            if(auto *diff = db->getBeatmapDifficulty(mapHash); diff) {
                pathsToExport.push_back(diff->getFolder());
            }
        }

        // uber sanity
        std::string colNameSanitized = collection_name;
        if(colNameSanitized.empty()) {
            colNameSanitized = fmt::format("Untitled-Collection-{:%F-%H-%M-%S}", fmt::gmtime(std::time(nullptr)));
        } else {
            std::ranges::replace(colNameSanitized, '\\', '_');
            std::ranges::replace(colNameSanitized, '/', '_');
        }

        // TODO: custom export name maybe
        Environment::createDirectory(cv::export_folder.getString() + "/collections");

        auto ctx = MapExporter::ExportContext{
            pathsToExport, fmt::format("collections/{}", colNameSanitized),
            [namestr = std::string{collection_name}](f32 progress, std::string entry) -> void {
                return BottomBar::update_export_progress(progress, std::move(entry), namestr);
            }};
        MapExporter::export_paths(std::move(ctx));
    }
}

void SongBrowser::highlightScore(const FinishedScore &scoreToHighlight) {
    if(auto cachedit =
           std::ranges::find(this->scoreButtonCache, scoreToHighlight, [](const auto &btn) { return btn->getScore(); });
       cachedit != this->scoreButtonCache.end()) {
        this->scoreBrowser->scrollToElement(*cachedit, 0, 10);
        (*cachedit)->highlight();
    }
}

void SongBrowser::selectSongButton(CarouselButton *songButton) {
    if(songButton != nullptr && !songButton->isSelected()) {
        this->contextMenu->setVisible2(false);
        songButton->select();
    }
}

void SongBrowser::selectRandomBeatmap() {
    // filter songbuttons or independent diffs
    const auto &elements{this->carousel->container.getElements<CarouselButton>()};

    std::vector<SongButton *> songButtons;
    for(auto element : elements) {
        auto *songButtonPointer = element->as<SongButton>();
        auto *songDifficultyButtonPointer = element->as<SongDifficultyButton>();

        if(songButtonPointer != nullptr &&
           (songDifficultyButtonPointer == nullptr ||
            songDifficultyButtonPointer->isIndependentDiffButton()))  // only allow songbuttons or independent diffs
            songButtons.push_back(songButtonPointer);
    }

    if(songButtons.size() < 1) return;

    // remember previous
    if(osu->getMapInterface()->getBeatmap() != nullptr && !osu->getMapInterface()->getBeatmap()->do_not_store) {
        this->previousRandomBeatmaps.push_back(osu->getMapInterface()->getBeatmap());
    }

    size_t randomIndex = songButtons.size() == 1 ? 0 : (prand() % (songButtons.size() - 1));

    auto *songButton = songButtons[randomIndex]->as<SongButton>();
    this->selectSongButton(songButton);
}

void SongBrowser::selectPreviousRandomBeatmap() {
    if(this->previousRandomBeatmaps.size() > 0) {
        const auto *currentRandomBeatmap = this->previousRandomBeatmaps.back();
        if(this->previousRandomBeatmaps.size() > 1 && currentRandomBeatmap == osu->getMapInterface()->getBeatmap())
            this->previousRandomBeatmaps.pop_back();  // deletes the current beatmap which may also be at the top (so
                                                      // we don't switch to ourself)

        // filter songbuttons
        const auto &elements{this->carousel->container.getElements<CarouselButton>()};

        std::vector<SongButton *> songButtons;
        for(auto *element : elements) {
            auto *songButtonPointer = element->as<SongButton>();

            if(songButtonPointer != nullptr)  // allow ALL songbuttons
                songButtons.push_back(songButtonPointer);
        }

        // select it, if we can find it (and remove it from memory)
        bool foundIt = false;
        const DatabaseBeatmap *previousRandomBeatmap = this->previousRandomBeatmaps.back();
        for(auto *songButton : songButtons) {
            if(songButton->getDatabaseBeatmap() != nullptr &&
               songButton->getDatabaseBeatmap() == previousRandomBeatmap) {
                this->previousRandomBeatmaps.pop_back();
                this->selectSongButton(songButton);
                foundIt = true;
                break;
            }

            const auto &children = songButton->getChildren();
            for(auto *c : children) {
                if(c->getDatabaseBeatmap() == previousRandomBeatmap) {
                    this->previousRandomBeatmaps.pop_back();
                    this->selectSongButton(c);
                    foundIt = true;
                    break;
                }
            }

            if(foundIt) break;
        }

        // if we didn't find it then restore the current random beatmap, which got pop_back()'d above (shit logic)
        if(!foundIt) this->previousRandomBeatmaps.push_back(currentRandomBeatmap);
    }
}

void SongBrowser::playSelectedDifficulty() {
    const auto &elements{this->carousel->container.getElements<CarouselButton>()};

    for(auto *element : elements) {
        if(auto *songDifficultyButton = element->as<SongDifficultyButton>();
           songDifficultyButton && songDifficultyButton->isSelected()) {
            songDifficultyButton->select();
            break;
        }
    }
}

void SongBrowser::recreateCollectionsButtons() {
    // reset
    {
        // sanity
        if(this->curGroup == GroupType::COLLECTIONS) {
            this->carousel->invalidate();
            this->visibleSongButtons.clear();
        }

        this->selectionPreviousCollectionButton = nullptr;
        this->collectionButtons.clear();
    }

    Timer t;
    t.start();

    for(const auto &collection : Collections::get_loaded()) {
        const auto &coll_maps = collection.get_maps();
        if(coll_maps.empty()) continue;

        std::vector<SongButton *> folder;
        Hash::flat::set<u32> matched_sets;
        std::vector<SongDifficultyButton *> matching_diffs;

        for(const auto &map_hash : coll_maps) {
            auto it = this->hashToDiffButton->find(map_hash);
            if(it == this->hashToDiffButton->end()) continue;

            SongDifficultyButton *diff_btn = it->second;

            // get parent button and siblings
            SongButton *parent = diff_btn->getParentSongButton();
            const std::vector<SongDifficultyButton *> &diff_btn_group = diff_btn->getSiblingsAndSelf();

            matching_diffs.clear();

            // filter to only diffs in this collection
            for(SongDifficultyButton *sbc : diff_btn_group) {
                if(coll_maps.contains(sbc->getDatabaseBeatmap()->getMD5())) {
                    matching_diffs.push_back(sbc);
                }
            }

            const i32 set_id = diff_btn->getDatabaseBeatmap()->getSetID();

            if(auto [_, inserted] = matched_sets.insert(set_id); !inserted) {
                // We already added the maps from this set to the collection!
                continue;
            }

            if(diff_btn_group.size() == matching_diffs.size()) {
                // all diffs match: add the set button (user added all diffs of beatmap into collection)
                folder.push_back(parent);
            } else {
                // only add matched diff buttons
                Mc::append_range(folder, matching_diffs);
            }
        }

        if(!folder.empty()) {
            this->collectionButtons.emplace_back(new CollectionButton(250.f, 250.f + db->getBeatmapSets().size() * 50.f,
                                                                      200.f, 50.f, "", collection.get_name(), folder));
        }
    }

    // sort buttons by name
    std::ranges::stable_sort(
        this->collectionButtons, [](const char *s1, const char *s2) -> bool { return strcasecmp(s1, s2) < 0; },
        [](const auto &colBtn) -> const char * { return colBtn->getCollectionName().c_str(); });

    t.update();
    debugLog("recreateCollectionsButtons(): {:f} seconds", t.getElapsedTime());
}
