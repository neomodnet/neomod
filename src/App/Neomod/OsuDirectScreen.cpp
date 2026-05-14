#include "OsuDirectScreen.h"

#include "AnimationHandler.h"
#include "Parsing.h"
#include "ThumbnailManager.h"
#include "BackgroundImageHandler.h"
#include "BanchoApi.h"
#include "Bancho.h"
#include "BeatmapInstaller.h"
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
#include "RoomScreen.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "SString.h"
#include "UI.h"
#include "UIButton.h"
#include "UIIcon.h"
#include "DatabaseBeatmap.h"

#include <charconv>
#include <cmath>

// represents: one single beatmapset element inside the OsuDirectScreen scrollview
// it's a container because it contains UIIcons with tooltips for difficulties
class OnlineMapListing : public CBaseUIContainer {
    NOCOPY_NOMOVE(OnlineMapListing)
   public:
    explicit OnlineMapListing(Downloader::BeatmapSetMetadata meta);
    ~OnlineMapListing() override;

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
    McFont* font;
    Downloader::BeatmapSetMetadata meta;

    vec2 mousedown_coords{-999.f, -999.f};
    AnimFloat hover_anim;
    AnimFloat click_anim;

    // Cache
    std::string full_title;
    ThumbIdentifier thumb_id;

    f32 creator_width{0.f};
};

OnlineMapListing::OnlineMapListing(Downloader::BeatmapSetMetadata meta)
    : font(engine->getDefaultFont()),
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

    this->onResolutionChange(osu->getVirtScreenSize());

    osu->getThumbnailManager()->request_image(this->thumb_id);
}

OnlineMapListing::~OnlineMapListing() { osu->getThumbnailManager()->discard_image(this->thumb_id); }

void OnlineMapListing::onMouseDownInside(bool /*left*/, bool /*right*/) { this->mousedown_coords = mouse->getPos(); }

void OnlineMapListing::onMouseUpInside(bool /*left*/, bool /*right*/) {
    const f32 distance = vec::distance(mouse->getPos(), this->mousedown_coords);
    if(distance < 5.f) {
        this->click_anim = 1.f;
        this->click_anim.set(0.0f, 0.15f, anim::QuadInOut);

        const bool installed = db->getBeatmapSet(this->meta.set_id) != nullptr;
        if(installed) {
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
            auto* installer = osu->getBeatmapInstaller();
            const auto state = installer->get_state(this->meta.set_id);
            // toggle: if already in flight (and not yet terminal), cancel; otherwise (re)enqueue
            using enum MapInstallStage;
            using namespace flags::operators;
            if(!!(state.stage & (Queued | Downloading | Installing))) {
                installer->cancel(this->meta.set_id);
            } else {
                installer->enqueue(this->meta.set_id, /*auto_select=*/true,
                                   fmt::format("{} - {}", this->meta.artist, this->meta.title));
            }
        }
    }

    this->mousedown_coords = {-999.f, -999.f};
}

void OnlineMapListing::onMouseUpOutside(bool /*left*/, bool /*right*/) { this->mousedown_coords = {-999.f, -999.f}; }

void OnlineMapListing::onMouseInside() { this->hover_anim.set(0.25f, 0.15f, anim::QuadInOut); }
void OnlineMapListing::onMouseOutside() { this->hover_anim.set(0.f, 0.15f, anim::QuadInOut); }

namespace {
enum class RankingStatusFilter : u8 {
    RANKED = 0,
    APPROVED = 1,
    PENDING = 2,
    QUALIFIED = 3,
    ALL = 4,
    GRAVEYARD = 5,
    PLAYED = 7,
    LOVED = 8,
};

Color get_difficulty_color(f32 star_rating) {
    // using this: https://github.com/ppy/osu-web/blob/2d211ba69831f32bc4aed06d4f3bd0020c50b440/resources/js/utils/beatmap-helper.ts#L22
    // (maybe already outdated?)
    static constexpr const struct ColorMap {
        f32 domain;
        Color color;
    } tbl[] = {
        {0.10f, rgb(0x42, 0x90, 0xFB)},  //
        {1.25f, rgb(0x4F, 0xC0, 0xFF)},  //
        {2.00f, rgb(0x4F, 0xFF, 0xD5)},  //
        {2.50f, rgb(0x7C, 0xFF, 0x4F)},  //
        {3.30f, rgb(0xF6, 0xF0, 0x5C)},  //
        {4.20f, rgb(0xFF, 0x80, 0x68)},  //
        {4.90f, rgb(0xFF, 0x4E, 0x6F)},  //
        {5.80f, rgb(0xC6, 0x45, 0xB8)},  //
        {6.70f, rgb(0x65, 0x63, 0xDE)},  //
        {7.70f, rgb(0x18, 0x15, 0x8E)},  //
        {9.00f, rgb(0x00, 0x00, 0x00)},  //
    };

    // clamp bounds
    static constexpr uSz end_idx = (sizeof(tbl) / sizeof(tbl[0])) - 1;
    if(star_rating <= tbl[0].domain) return tbl[0].color;
    if(star_rating >= tbl[end_idx].domain) return tbl[end_idx].color;

    // find the segment [i-1, i] straddling star_rating (upper bound is guaranteed by the clamp above)
    sSz i = 1;
    while(star_rating > tbl[i].domain) ++i;

    const f32 t = (star_rating - tbl[i - 1].domain) / (tbl[i].domain - tbl[i - 1].domain);
    const Color a = tbl[i - 1].color;
    const Color b = tbl[i].color;

    // gamma-corrected lerp (d3.interpolateRgb.gamma(2.2)):
    // linearize each channel with c^gamma, lerp, then undo with the 1/gamma root
    constexpr f32 gamma = 2.2f;
    auto lerp_ch = [t](f32 ca, f32 cb) {
        const f32 ag = std::pow(ca, gamma);
        const f32 bg = std::pow(cb, gamma);
        return std::pow(ag + t * (bg - ag), 1.f / gamma);
    };

    return rgb(lerp_ch(a.Rf(), b.Rf()), lerp_ch(a.Gf(), b.Gf()), lerp_ch(a.Bf(), b.Bf()));
}

class DiffLabel final : public UIIcon {
   public:
    using UIIcon::UIIcon;
    // TODO: crap duplication (CBaseUILabel only supports shadows, TextFX was added later... should retrofit it there to avoid needing this)
    void drawText() override {
        if(!this->font || this->sText.empty()) {
            return;
        }

        g->pushTransform();
        {
            g->scale(this->fScale, this->fScale);
            g->translate((f32)(i32)(this->getPos().x),
                         (f32)(i32)(this->getPos().y + this->getSize().y / 2.f + this->fStringHeight / 2.f));

            const f32 outline_scale = Osu::getUIScale();
            const TextFX icon_fx{.col_text = this->textColor,
                                 .col_shadow = 0,                                 // no shadow
                                 .col_outline = Colors::invert(this->textColor),  // TODO: this is kind of ugly
                                 .outline_px = 1.f * outline_scale,
                                 .shadow_softness_px = 0.5f * outline_scale};

            g->drawString(this->font, this->sText, icon_fx);
        }
        g->popTransform();
    }
};

}  // namespace

void OnlineMapListing::onResolutionChange(vec2 /*newResolution*/) {
    this->full_title = fmt::format("{:s} - {:s}", this->meta.artist, this->meta.title);
    this->creator_width = this->font->getStringWidth(this->meta.creator);

    const f32 scale = Osu::getUIScale();
    vec2 pos_counter = this->getSize() - (40.f * scale);

    auto icon_elems_copy = reinterpret_cast<std::vector<DiffLabel*>&>(this->vElements);
    this->invalidate();  // clear this->vElements and rebuild

    for(sSz added_elem_i = 0; const auto& diff : this->meta.beatmaps) {
        if(diff.mode != 0) continue;

        DiffLabel* icon = nullptr;
        if(added_elem_i < icon_elems_copy.size()) {
            icon = icon_elems_copy[added_elem_i];
            icon_elems_copy[added_elem_i] = nullptr;  // set to null so we don't delete it later
        } else {
            icon = new DiffLabel(Icons::CIRCLE);
        }

        icon->setPos(pos_counter);
        icon->setSize(30.f * scale, 30.f * scale);

        if(diff.star_rating > 0.f) {
            // has star rating
            icon->setTooltipText(fmt::format("{:s} ({:.2f} ⭐)", diff.diffname, diff.star_rating));
            icon->setTextColor(get_difficulty_color(diff.star_rating));
        } else {
            // didn't parse star rating for this difficulty
            icon->setTooltipText(diff.diffname);
            icon->setTextColor((Color)-1);
        }

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

void OnlineMapListing::draw() {
    CBaseUIContainer::draw();

    // use a 4:3 aspect ratio
    const vec2 map_bg_size = {this->getSize().y * (4.f / 3.f), this->getSize().y};
    vec2 pos_counter = this->getPos();

    if(const Image* map_thumbnail = osu->getThumbnailManager()->try_get_image(this->thumb_id)) {
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

    const bool installed = db->getBeatmapSet(this->meta.set_id) != nullptr;
    const auto install_state = osu->getBeatmapInstaller()->get_state(this->meta.set_id);
    const bool failed = !installed && install_state.stage == MapInstallStage::Failed;
    const bool downloading = !installed && !failed && [stg = install_state.stage]() -> bool {
        using enum MapInstallStage;
        using namespace flags::operators;
        return !!(stg & (Queued | Downloading | Installing));
    }();

    // To show we're downloading, always draw at least 5%
    const f32 download_progress = downloading ? std::max(0.05f, install_state.progress) : 0.f;

    g->pushClipRect(McRect(pos_counter, progress_size));
    {
        // Download progress
        const f32 download_width = progress_size.x * download_progress;
        g->setColor(rgb(150, 255, 150));
        g->setAlpha(alpha);
        g->fillRect(pos_counter, {download_width, progress_size.y});

        // Background
        Color color = rgb(255, 255, 255);
        if(installed)
            color = rgb(0, 150, 0);
        else if(failed)
            color = rgb(200, 0, 0);

        color.setA(alpha);

        g->setColor(color);
        g->fillRect(static_cast<int>(pos_counter.x + download_width), static_cast<int>(pos_counter.y),
                    static_cast<int>(progress_size.x - download_width), static_cast<int>(progress_size.y));

        const f32 outline_scale = Osu::getUIScale();
        const TextFX string_style{.col_text = rgb(255, 255, 255),
                                  .col_shadow = 0,  // no shadow
                                  .col_outline = rgb(50, 50, 50),
                                  .outline_px = 1.f * outline_scale,
                                  .shadow_softness_px = 0.5f * outline_scale};

        g->pushTransform();
        {
            g->translate(pos_counter.x + padding, pos_counter.y + padding + this->font->getHeight());
            g->drawString(this->font, this->full_title, string_style);
        }
        g->popTransform();

        g->pushTransform();
        {
            g->translate(pos_counter.x + progress_size.x - (this->creator_width + 2 * padding),
                         pos_counter.y + padding + this->font->getHeight());
            g->drawString(this->font, this->meta.creator, string_style);
        }
        g->popTransform();
    }
    g->popClipRect();
}

OsuDirectScreen::OsuDirectScreen() {
    this->title = new CBaseUILabel(0, 0, 0, 0, "", _("Online Beatmaps"));
    this->title->setDrawFrame(false);
    this->title->setDrawBackground(false);
    this->addBaseUIElement(this->title);

    this->search_bar = new CBaseUITextbox();
    this->search_bar->setBackgroundColor(0xaa000000);
    this->addBaseUIElement(this->search_bar);

    this->newest_btn = new UIButton(0, 0, 0, 0, "", _("Newest maps"));
    this->newest_btn->setColor(0xff88FF00);
    this->newest_btn->setClickCallback([this]() {
        this->reset();
        this->search(_("Newest"));
    });
    this->addBaseUIElement(this->newest_btn);

    this->best_rated_btn = new UIButton(0, 0, 0, 0, "", _("Best maps"));
    this->best_rated_btn->setColor(0xffFF006A);
    this->best_rated_btn->setClickCallback([this]() {
        this->reset();
        this->search(_("Top Rated"));
    });
    this->addBaseUIElement(this->best_rated_btn);

    this->ranked_only = new CBaseUICheckbox(0, 0, 0, 0, "", _("Only show ranked beatmaps"));
    this->ranked_only->setDrawFrame(false);
    this->ranked_only->setDrawBackground(false);
    this->ranked_only->setChecked(cv::direct_ranking_status_filter.getVal<RankingStatusFilter>() ==
                                  RankingStatusFilter::RANKED);
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
    cv::direct_ranking_status_filter.setValue(checkbox->isChecked() ? (u8)RankingStatusFilter::RANKED
                                                                    : (u8)RankingStatusFilter::ALL);
    this->reset();
    this->search(this->current_query);
}

CBaseUIContainer* OsuDirectScreen::setVisible(bool visible) {
    if(visible) {
        if(!db->isFinished() || db->isCancelled()) {
            // Ensure database is loaded (same as Lobby screen)
            ui->getSongBrowser()->refreshBeatmaps(/*next_screen=*/this);
            return this;
        }
    }

    ScreenBackable::setVisible(visible);

    if(visible) {
        this->onResolutionChange(osu->getVirtScreenSize());

        // clear previous search results
        // HACKHACK: we do this now instead of on setVisible(false), because this deletes map listings
        // ...and we can call setScreen() from inside of a map listing update loop
        // ...which deletes map listings WHILE we are iterating map listings
        this->reset();

        this->search_bar->clear();
        this->search_bar->focus();
    }

    return this;
}

void OsuDirectScreen::draw() {
    if(!this->isVisible()) return;

    osu->getBackgroundImageHandler()->draw(osu->getMapInterface()->getBeatmap());
    ScreenBackable::draw();

    if(this->loading) {
        const f32 spinner_size = (40.f * Osu::getUIScale());
        const f32 scale = spinner_size / (f32)osu->getSkin()->i_loading_spinner.getSize().y;
        g->setColor(0xffffffff);
        g->pushTransform();
        g->rotate((f32)std::fmod(engine->getTime(), PI * 2) * 180.f, 0, 0, 1);
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
        if(this->current_query == this->search_bar->getText() && this->loading) {
            // We're already searching for the current query, don't cancel the request
            return;
        }

        this->reset();
        this->search(this->search_bar->getText());
        return;
    }

    // Fetch next results once we reached the bottom
    if(this->results->isAtBottom() && this->last_search_time + 1.0 < engine->getTime()) {
        this->search(this->current_query);
    }
}

void OsuDirectScreen::onBack() {
    if(BanchoState::is_in_a_multi_room()) {
        ui->getRoom()->set_current_map(osu->getMapInterface()->getBeatmap());
        ui->setScreen(ui->getRoom());
    } else {
        ui->setScreen(ui->getMainMenu());
    }
}

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
    const f32 x_start = (f32)osu->getVirtScreenWidth() / 2.f - results_width / 2.f;
    x = x_start;
    y += 50.f * scale;

    // Search bar & buttons
    this->search_bar->setRelPos(x, y);
    this->search_bar->setSize(400.0f * scale, 40.0f * scale);
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

        f32 y2 = LISTING_MARGIN;
        // We only put OnlineMapListings into the container of this->results
        for(auto* listing : this->results->container.getElementsAs<OnlineMapListing>()) {
            listing->setRelPos(LISTING_MARGIN, y2);
            listing->setSize(results_width - 2 * LISTING_MARGIN, 75.f * scale);
            y2 += listing->getSize().y + LISTING_MARGIN;

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

    // TODO: show "approved" maps when ranked only is checked (how?)
    // NOTE: implemented server-side for neomod.net, other servers still won't work
    const uSz offset = this->results->container.getElements().size();
    const i32 filter = cv::direct_ranking_status_filter.getInt();
    std::string url = fmt::format("osu.{:s}/web/osu-search.php?m=0&r={:d}&q={:s}&p={:d}", BanchoState::endpoint, filter,
                                  Mc::Net::urlEncode(query), offset);
    BANCHO::Api::append_auth_params(url);

    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = 5,
        .connect_timeout = 5,
        .flags = Mc::Net::RequestOptions::FOLLOW_REDIRECTS,
    };

    debugLog("Searching for maps matching \"{:s}\" (offset {:d})", query, offset);
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
                const bool success = Parsing::strto_s(set_lines[0], nb_results);
                if(!success || nb_results <= 0) {
                    // HACK: reached end of results (or errored), prevent further requests
                    this->last_search_time = 9999999.9;

                    if(nb_results == -1 && set_lines.size() >= 2) {
                        // Relay server's error message to the player
                        ui->getNotificationOverlay()->addToast(std::string{set_lines[1]}, ERROR_TOAST);
                    }

                    return;
                }

                debugLog("Received {} maps", nb_results);
                for(i32 i = 1; i < set_lines.size(); i++) {
                    auto meta = Downloader::parse_beatmapset_metadata(set_lines[i]);
                    if(meta.set_id == 0) continue;

                    this->results->container.addBaseUIElement(new OnlineMapListing(std::move(meta)));
                }

                this->onResolutionChange(osu->getVirtScreenSize());
            } else {
                // TODO: handle failure
            }
        });
}
