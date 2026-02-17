#include "OsuDirectScreen.h"

#include "AnimationHandler.h"
#include "ThumbnailManager.h"
#include "BackgroundImageHandler.h"
#include "BanchoApi.h"
#include "Bancho.h"
#include "BeatmapInterface.h"
#include "CBaseUICheckbox.h"
#include "CBaseUILabel.h"
#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"
#include "Database.h"
#include "Downloader.h"
#include "Engine.h"
#include "Environment.h"
#include "Font.h"
#include "Graphics.h"
#include "Icons.h"
#include "Logging.h"
#include "MainMenu.h"
#include "MakeDelegateWrapper.h"
#include "Mouse.h"
#include "NetworkHandler.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "SString.h"
#include "UI.h"
#include "UIButton.h"
#include "UIIcon.h"

#include <charconv>

// represents: one single beatmapset element inside the OsuDirectScreen scrollview
// it's a container because it contains UIIcons with tooltips for difficulties
class OnlineMapListing : public CBaseUIContainer {
    NOCOPY_NOMOVE(OnlineMapListing)
   public:
    OnlineMapListing(OsuDirectScreen* parent, Downloader::BeatmapSetMetadata meta);
    ~OnlineMapListing() override;

    void update(CBaseUIEventCtx& c) override;
    void draw() override;

    // NOT inherited, called manually
    void onResolutionChange(vec2 newResolution);

    // Overriding click detection because buttons don't work well in scrollviews
    // Our custom behavior is "if clicked and cursor moved less than 5px"
    void onMouseDownInside(bool left = true, bool right = false) override;
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseUpOutside(bool left = true, bool right = false) override;
    void onMouseInside() override;
    void onMouseOutside() override;

   private:
    OsuDirectScreen* directScreen;
    McFont* font;
    Downloader::BeatmapSetMetadata meta;

    vec2 mousedown_coords{-999.f, -999.f};
    f32 hover_anim{0.f};
    f32 click_anim{0.f};

    // Cache
    std::string full_title;
    ThumbIdentifier thumb_id;
    Downloader::DownloadHandle dl_handle;

    f32 creator_width{0.f};
    f32 download_progress{0.f};

    bool installed;
    bool downloading{false};
    bool download_failed{false};
};

// "b.{}/thumb/{:d}.jpg";
OnlineMapListing::OnlineMapListing(OsuDirectScreen* parent, Downloader::BeatmapSetMetadata meta)
    : directScreen(parent),
      font(engine->getDefaultFont()),
      meta(std::move(meta)),
      thumb_id(
          {.save_path = fmt::format("{}/thumbs/{}/{}", env->getCacheDir(), BanchoState::endpoint, this->meta.set_id),
           .download_url =
               fmt::format("b.{}/thumb/{:d}.jpg", BanchoState::endpoint,
                           this->meta.set_id),  // Also valid: "b.{}/thumb/{:d}l.jpg" ("l" stands for "large")
           .id = this->meta.set_id}) {
    if(this->meta.beatmaps.size() > 1) {
        // reverse
        std::ranges::sort(this->meta.beatmaps, std::ranges::greater{}, [](const auto& bm) { return bm.star_rating; });
    }

    this->installed = db->getBeatmapSet(this->meta.set_id) != nullptr;
    this->onResolutionChange(osu->getVirtScreenSize());

    osu->getThumbnailManager()->request_image(this->thumb_id);
}

OnlineMapListing::~OnlineMapListing() {
    osu->getThumbnailManager()->discard_image(this->thumb_id);

    anim::deleteExistingAnimation(&this->click_anim);
    anim::deleteExistingAnimation(&this->hover_anim);
}

void OnlineMapListing::onMouseDownInside(bool /*left*/, bool /*right*/) { this->mousedown_coords = mouse->getPos(); }

void OnlineMapListing::onMouseUpInside(bool /*left*/, bool /*right*/) {
    const f32 distance = vec::distance(mouse->getPos(), this->mousedown_coords);
    if(distance < 5.f) {
        this->click_anim = 1.f;
        anim::moveQuadInOut(&this->click_anim, 0.0f, 0.15f, 0.0f, true);

        if(this->installed) {
            // Select map, or go to song browser if already selected
            if(osu->getMapInterface()->getBeatmap()->getSetID() == this->meta.set_id) {
                ui->setScreen(ui->getSongBrowser());
            } else {
                const auto set = db->getBeatmapSet(this->meta.set_id);
                if(!set) return;  // probably unreachable
                const auto& diffs = set->getDifficulties();
                if(diffs.empty()) return;  // surely unreachable
                ui->getSongBrowser()->onDifficultySelected(diffs[0].get(), false);
            }
        } else {
            this->downloading = !this->downloading;
            if(this->downloading) {
                this->directScreen->auto_select_set = this->meta.set_id;
            }
        }
    }

    this->mousedown_coords = {-999.f, -999.f};
}

void OnlineMapListing::onMouseUpOutside(bool /*left*/, bool /*right*/) { this->mousedown_coords = {-999.f, -999.f}; }

void OnlineMapListing::onMouseInside() { anim::moveQuadInOut(&this->hover_anim, 0.25f, 0.15f, 0.0f, true); }
void OnlineMapListing::onMouseOutside() { anim::moveQuadInOut(&this->hover_anim, 0.f, 0.15f, 0.0f, true); }

void OnlineMapListing::onResolutionChange(vec2 /*newResolution*/) {
    this->full_title = fmt::format("{} - {}", this->meta.artist, this->meta.title);
    this->creator_width = this->font->getStringWidth(this->meta.creator);

    const f32 scale = Osu::getUIScale();
    vec2 pos_counter = this->getSize() - (40.f * scale);

    auto icon_elems_copy = reinterpret_cast<std::vector<UIIcon*>&>(this->vElements);
    this->invalidate();  // clear this->vElements and rebuild

    for(sSz added_elem_i = 0; const auto& diff : this->meta.beatmaps) {
        if(diff.mode != 0) continue;

        UIIcon* icon = nullptr;
        if(added_elem_i < icon_elems_copy.size()) {
            icon = icon_elems_copy[added_elem_i];
            icon_elems_copy[added_elem_i] = nullptr;  // set to null so we don't delete it later
        } else {
            icon = new UIIcon(Icons::CIRCLE);
        }

        icon->setPos(pos_counter);
        icon->setSize(30.f * scale, 30.f * scale);
        icon->setTooltipText(fmt::format("{}{}", diff.diffname,
                                         diff.star_rating > 0.f ? fmt::format(" ({:.2f} ★)", diff.star_rating) : ""));

        this->addBaseUIElement(icon);

        pos_counter.x -= 40.f * scale;

        ++added_elem_i;
    }

    // delete any excess items in the container (if we somehow rebuilt with fewer than we originally had)
    // we set the element to NULL if we put it back into the container, so SAFE_DELETE won't delete those
    for(auto* icon : icon_elems_copy) {
        SAFE_DELETE(icon);
    }
}

void OnlineMapListing::update(CBaseUIEventCtx& c) {
    // TODO:
    //   - continue downloading even if clipped

    CBaseUIContainer::update(c);

    this->download_progress = 0.f;
    if(this->downloading) {
        // TODO: downloads will not finish if player leaves this screen
        bool ready = Downloader::download_beatmapset(this->meta.set_id, this->dl_handle);
        if(ready) {
            this->downloading = false;

            std::string mapset_path = fmt::format(NEOMOD_MAPS_PATH "/{}/", this->meta.set_id);
            const auto set = db->addBeatmapSet(mapset_path, this->meta.set_id);
            if(set) {
                this->installed = true;

                if(this->directScreen->auto_select_set == this->meta.set_id) {
                    const auto& diffs = set->getDifficulties();
                    if(diffs.empty()) return;  // surely unreachable
                    ui->getSongBrowser()->onDifficultySelected(diffs[0].get(), false);
                }
            } else {
                this->download_failed = true;
            }
        } else if(this->dl_handle.failed()) {
            // TODO: display error toast
            this->downloading = false;
            this->download_failed = true;
        } else {
            // To show we're downloading, always draw at least 5%
            this->download_progress = std::max(0.05f, this->dl_handle.progress());
        }
    }
}

void OnlineMapListing::draw() {
    // XXX: laggy/slow

    CBaseUIContainer::draw();

    // use a 4:3 aspect ratio
    const vec2 map_bg_size = {this->getSize().y * (4.f / 3.f), this->getSize().y};
    vec2 pos_counter = this->getPos();

    if(auto* map_thumbnail = osu->getThumbnailManager()->try_get_image(this->thumb_id)) {
        // Map thumbnail
        const f32 scale = Osu::getImageScaleToFillResolution(map_thumbnail, map_bg_size);
        g->pushTransform();
        g->setColor(Color(0xffffffff));
        g->scale(scale, scale);

        g->translate(pos_counter);  // needs to be *after* scale
        g->drawImage(map_thumbnail, AnchorPoint::TOP_LEFT);
        g->popTransform();
    } else {
        // Map thumbnail placeholder
        g->setColor(Color(0x55000000));
        g->fillRect(pos_counter, map_bg_size);
    }

    pos_counter.x += map_bg_size.x;

    const f32 padding = 5.f;
    const vec2 progress_size = {this->getSize().x - map_bg_size.x, this->getSize().y};
    const f32 alpha = std::min(0.25f + this->hover_anim + this->click_anim, 1.f);

    g->pushClipRect(McRect(pos_counter, progress_size));
    {
        // Download progress
        const f32 download_width = progress_size.x * this->download_progress;
        g->setColor(rgb(150, 255, 150));
        g->setAlpha(alpha);
        g->fillRect(pos_counter, {download_width, progress_size.y});

        // Background
        Color color = rgb(255, 255, 255);
        if(this->installed)
            color = rgb(0, 150, 0);
        else if(this->download_failed)
            color = rgb(200, 0, 0);

        color.setA(alpha);

        g->setColor(color);
        g->fillRect(pos_counter.x + download_width, pos_counter.y, progress_size.x - download_width, progress_size.y);

        g->pushTransform();
        {
            g->setColor(0xffffffff);
            g->translate(pos_counter.x + padding, pos_counter.y + padding + this->font->getHeight());
            g->drawString(this->font, this->full_title);
        }
        g->popTransform();

        g->pushTransform();
        {
            g->setColor(0xffffffff);
            g->translate(pos_counter.x + progress_size.x - (this->creator_width + 2 * padding),
                         pos_counter.y + padding + this->font->getHeight());
            g->drawString(this->font, this->meta.creator);
        }
        g->popTransform();
    }
    g->popClipRect();
}

OsuDirectScreen::OsuDirectScreen() {
    this->title = new CBaseUILabel(0, 0, 0, 0, "", "Online Beatmaps");
    this->title->setDrawFrame(false);
    this->title->setDrawBackground(false);
    this->addBaseUIElement(this->title);

    this->search_bar = new CBaseUITextbox();
    this->search_bar->setBackgroundColor(0xaa000000);
    this->addBaseUIElement(this->search_bar);

    this->newest_btn = new UIButton(0, 0, 0, 0, "", "Newest maps");
    this->newest_btn->setColor(0xff88FF00);
    this->newest_btn->setClickCallback([this]() {
        this->reset();
        this->search("Newest");
    });
    this->addBaseUIElement(this->newest_btn);

    this->best_rated_btn = new UIButton(0, 0, 0, 0, "", "Best maps");
    this->best_rated_btn->setColor(0xffFF006A);
    this->best_rated_btn->setClickCallback([this]() {
        this->reset();
        this->search("Top Rated");
    });
    this->addBaseUIElement(this->best_rated_btn);

    this->ranked_only = new CBaseUICheckbox(0, 0, 0, 0, "", "Only show ranked beatmaps");
    this->ranked_only->setDrawFrame(false);
    this->ranked_only->setDrawBackground(false);
    this->ranked_only->setChecked(cv::direct_ranking_status_filter.getInt() == RankingStatusFilter::RANKED);
    this->ranked_only->setChangeCallback(SA::MakeDelegate<&OsuDirectScreen::onRankedCheckboxChange>(this));
    this->addBaseUIElement(this->ranked_only);

    this->results = new CBaseUIScrollView();
    this->results->setBackgroundColor(0xaa000000);
    this->results->setHorizontalScrolling(false);
    this->results->setVerticalScrolling(true);
    this->results->setGrabClicks(false);
    this->addBaseUIElement(this->results);
}

OsuDirectScreen::~OsuDirectScreen() = default;

void OsuDirectScreen::onRankedCheckboxChange(CBaseUICheckbox* checkbox) {
    cv::direct_ranking_status_filter.setValue(checkbox->isChecked() ? RankingStatusFilter::RANKED
                                                                    : RankingStatusFilter::ALL);
    this->reset();
    this->search(this->current_query);
}

CBaseUIContainer* OsuDirectScreen::setVisible(bool visible) {
    if(visible) {
        if(!db->isFinished() || db->isCancelled()) {
            // Ensure database is loaded (same as Lobby screen)
            ui->getSongBrowser()->refreshBeatmaps(this);
            return this;
        }
    }

    ScreenBackable::setVisible(visible);

    if(visible) {
        this->onResolutionChange(osu->getVirtScreenSize());

        this->search_bar->clear();
        this->search_bar->focus();
    } else {
        // clear search results if we're setting invisible
        this->reset();
    }

    return this;
}

void OsuDirectScreen::draw() {
    if(!this->isVisible()) return;

    osu->getBackgroundImageHandler()->draw(osu->getMapInterface()->getBeatmap());
    ScreenBackable::draw();

    if(this->loading) {
        const f32 spinner_size = (40.f * Osu::getUIScale());
        const f32 scale = spinner_size / osu->getSkin()->i_loading_spinner->getSize().y;
        g->setColor(0xffffffff);
        g->pushTransform();
        g->rotate(engine->getTime() * 180, 0, 0, 1);
        g->scale(scale, scale);
        g->translate(this->spinner_pos.x, this->spinner_pos.y);
        g->drawImage(osu->getSkin()->i_loading_spinner);
        g->popTransform();
    }

    // TODO: message if no maps were found or server errored
}

void OsuDirectScreen::update(CBaseUIEventCtx& c) {
    if(!this->isVisible()) return;
    if(!BanchoState::is_online() || !db->isFinished() || db->isCancelled()) return this->onBack();
    ScreenBackable::update(c);

    if(this->search_bar->hitEnter()) {
        if(this->current_query == this->search_bar->getText().utf8View() && this->loading) {
            // We're already searching for the current query, don't cancel the request
            return;
        }

        this->reset();
        this->search(this->search_bar->getText().utf8View());
        return;
    }

    // Fetch next results once we reached the bottom
    if(this->results->isAtBottom() && this->last_search_time + 1.0 < engine->getTime()) {
        this->search(this->current_query);
    }
}

void OsuDirectScreen::onBack() { ui->setScreen(ui->getMainMenu()); }

void OsuDirectScreen::onResolutionChange(vec2 newResolution) {
    this->setSize(osu->getVirtScreenSize());  // HACK: don't forget this or else nothing works!
    ScreenBackable::onResolutionChange(newResolution);

    const f32 scale = Osu::getUIScale();
    f32 x = 50.f;
    f32 y = 30.f;

    // Screen title
    this->title->setFont(osu->getTitleFont());
    this->title->setSizeToContent(0, 0);
    this->title->setRelPos(x, y);
    y += this->title->getSize().y;

    const f32 results_width = std::min(newResolution.x - 10.f * scale, 1024.f * scale);
    const f32 x_start = osu->getVirtScreenWidth() / 2.f - results_width / 2.f;
    x = x_start;
    y += 50.f * scale;

    // Search bar & buttons
    this->search_bar->setRelPos(x, y);
    this->search_bar->setSize(400.0 * scale, 40.0 * scale);
    x += this->search_bar->getSize().x;
    const f32 BUTTONS_MARGIN = 10.f * scale;
    x += BUTTONS_MARGIN;
    this->newest_btn->setRelPos(x, y);
    this->newest_btn->setSize(150.f * scale, this->search_bar->getSize().y);
    x += this->newest_btn->getSize().x + BUTTONS_MARGIN;
    this->best_rated_btn->setRelPos(x, y);
    this->best_rated_btn->setSize(150.f * scale, this->search_bar->getSize().y);
    x += this->best_rated_btn->getSize().x + BUTTONS_MARGIN;
    this->ranked_only->setRelPos(x, y);
    this->ranked_only->setSize(40.f * scale, 40.f * scale);
    y += this->search_bar->getSize().y;

    // Results list
    x = x_start;
    y += 10.f * scale;
    this->results->setRelPos(x, y);
    this->results->setSize(results_width, newResolution.y - (y + 100.f * scale));
    {
        const f32 LISTING_MARGIN = 10.f * scale;

        f32 y = LISTING_MARGIN;
        // We only put OnlineMapListings into the container of this->results
        for(auto* listing : this->results->container.getElements<OnlineMapListing>()) {
            listing->setRelPos(LISTING_MARGIN, y);
            listing->setSize(results_width - 2 * LISTING_MARGIN, 75.f * scale);
            y += listing->getSize().y + LISTING_MARGIN;

            // Update font stuff
            listing->onResolutionChange(newResolution);
        }
        this->results->setScrollSizeToContent();
        this->results->container.update_pos();  // sigh...
    }

    const f32 spinner_size = (40.f * scale);
    const f32 spinner_margin = spinner_size / 2.f;
    this->spinner_pos.x = x + this->results->getSize().x - (spinner_size / 2.f);
    this->spinner_pos.y = y + this->results->getSize().y + spinner_margin + (spinner_size / 2.f);

    this->update_pos();
}

void OsuDirectScreen::reset() {
    // "Cancel" current request and immediately allow a new one
    this->request_id++;
    this->loading = false;

    // Clear search results
    this->results->freeElements();

    // De-focus search bar (since we only reset() on user action)
    this->search_bar->stealFocus();
}

void OsuDirectScreen::search(std::string_view query) {
    if(this->loading) return;

    const i32 offset = this->results->container.getElements().size();
    const i32 filter = cv::direct_ranking_status_filter.getInt();
    std::string url = fmt::format("osu.{}/web/osu-search.php?m=0&r={}&q={}&p={}", BanchoState::endpoint, filter,
                                  Mc::Net::urlEncode(query), offset);
    BANCHO::Api::append_auth_params(url);

    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = 5,
        .connect_timeout = 5,
        .flags = Mc::Net::RequestOptions::FOLLOW_REDIRECTS,
    };

    debugLog("Searching for maps matching \"{}\" (offset {})", query, offset);
    const auto current_request_id = ++this->request_id;
    this->current_query = query;
    this->last_search_time = engine->getTime();
    this->loading = true;

    networkHandler->httpRequestAsync(
        url, std::move(options), [current_request_id, this](const Mc::Net::Response& response) {
            this->loading = false;

            if(current_request_id != this->request_id) {
                // Request was "cancelled"
                return;
            }

            if(response.success) {
                const auto set_lines = SString::split_newlines(response.body);

                i32 nb_results{0};
                auto [ptr, ec] =
                    std::from_chars(set_lines[0].data(), set_lines[0].data() + set_lines[0].size(), nb_results);

                if(nb_results <= 0 || ec != std::errc()) {
                    // HACK: reached end of results (or errored), prevent further requests
                    this->last_search_time = 9999999.9;

                    if(nb_results == -1 && set_lines.size() >= 2) {
                        // Relay server's error message to the player
                        ui->getNotificationOverlay()->addToast(set_lines[1], ERROR_TOAST);
                    }

                    return;
                }

                debugLog("Received {} maps", nb_results);
                for(i32 i = 1; i < set_lines.size(); i++) {
                    auto meta = Downloader::parse_beatmapset_metadata(set_lines[i]);
                    if(meta.set_id == 0) continue;

                    this->results->container.addBaseUIElement(new OnlineMapListing(this, std::move(meta)));
                }

                this->onResolutionChange(osu->getVirtScreenSize());
            } else {
                // TODO: handle failure
            }
        });
}
