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
#include "CBaseUIElement.h"
#include "CBaseUILabel.h"
#include "CBaseUIScrollView.h"
#include "CBaseUITextbox.h"
#include "Database.h"
#include "Downloader.h"
#include "Engine.h"
#include "Environment.h"
#include "Paths.h"
#include "Font.h"
#include "Graphics.h"
#include "i18n.h"
#include "Icons.h"
#include "KeyBindings.h"
#include "KeyboardEvent.h"
#include "Logging.h"
#include "MainMenu.h"
#include "MakeDelegateWrapper.h"
#include "Mouse.h"
#include "NetworkHandler.h"
#include "NotificationOverlay.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "OsuKeyBinds.h"
#include "PreviewTrackManager.h"
#include "RoomScreen.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "SoundEngine.h"
#include "SString.h"
#include "TooltipOverlay.h"
#include "UI.h"
#include "UIButton.h"
#include "UIIcon.h"
#include "UniString.h"
#include "DatabaseBeatmap.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>

namespace {

// preview panel geometry, in virtual-screen pixels at scale=1 (multiplied by Osu::getUIScale())
constexpr f32 DEF_PREVIEW_WIDTH{320.f};
constexpr f32 DEF_PREVIEW_MAX_HEIGHT{720.f};
constexpr f32 DEF_PREVIEW_MARGIN{16.f};  // to the right edge of the screen, and to the results list
constexpr f32 DEF_PREVIEW_PAD{8.f};
constexpr f32 DEF_PREVIEW_BUTTON_HEIGHT{36.f};
constexpr f32 DEF_PREVIEW_BUTTON_GAP{6.f};
constexpr f32 DEF_PREVIEW_MIN_THUMB_HEIGHT{64.f};    // below this, short screens go without the thumbnail
constexpr f32 PREVIEW_MAX_SCREEN_WIDTH_RATIO{0.3f};  // leaves small windows room for the results list
constexpr f32 PREVIEW_LINE_SPACING{1.5f};            // times the font height
constexpr uSz PREVIEW_MIN_DIFFICULTY_LINES{3};       // kept free before the thumbnail gets any room

// the listings show the small thumbnails, the preview the large ("l") ones, which get a cache directory of their own
// since the thumbnail cache only keeps track of id-named files
[[nodiscard]] ThumbIdentifier thumb_id_for(i32 set_id, bool large) {
    return {.save_path = fmt::format("{}/thumbs/{}/{}{}", Mc::Paths::cache(), BanchoState::endpoint,
                                     large ? "large/" : "", set_id),
            .download_url = fmt::format("b.{}/thumb/{:d}{}.jpg", BanchoState::endpoint, set_id, large ? "l" : ""),
            .id = set_id};
}

void open_beatmap_page(i32 set_id) {
    const auto scheme = cv::use_https.getBool() ? "https://"sv : "http://"sv;
    const auto url = fmt::format("{}osu.{}/s/{}", scheme, BanchoState::endpoint, set_id);
    debugLog("opening map link {:s}", url);
    ui->getNotificationOverlay()->addNotification(_("Opening browser, please wait ..."), 0xffffffff, false, 0.75f);
    env->openURLInDefaultBrowser(url, /*preventFocusSteal=*/true);
}

// the server takes these queries as keywords, so they must not be translated
constexpr std::string_view NEWEST_QUERY{"Newest"};
constexpr std::string_view TOP_RATED_QUERY{"Top Rated"};

// install state of one online beatmapset, poll() it once per tick
struct SetInstallState {
    BeatmapInstaller::State download;
    f64 last_database_check_time{0.};
    bool installed{false};

    void poll(i32 set_id);

    [[nodiscard]] bool in_flight() const {
        using enum MapInstallStage;
        using namespace flags::operators;
        return !this->installed && !!(this->download.stage & (Queued | Downloading | Extracting | Installing));
    }
};

void SetInstallState::poll(i32 set_id) {
    if(this->installed) return;

    // throttle database checks (beatmaps may have been installed through means other than
    // downloading it, so we have to check the db to reconcile the state)
    if(const f64 now = engine->getTime(); this->last_database_check_time + 1. < now) {
        this->last_database_check_time = now;
        this->installed = !!db->getBeatmapSet(set_id);
        if(this->installed) return;
    }

    using enum MapInstallStage;
    using namespace flags::operators;
    this->download = osu->getBeatmapInstaller()->get_state(set_id);
    if(this->download.stage == Done) {
        this->installed = true;
    } else if(this->download.stage != Failed &&
              (this->download.progress == 1. || !!(this->download.stage & (Extracting | Installing)))) {
        // poll for database presence immediately
        this->last_database_check_time = 0.;
    }
}

// over the thumbnail in the preview panel: shows how the beatmapset's audio preview is doing, and plays or stops it
class PreviewAudioToggle final : public CBaseUIElement {
    NOCOPY_NOMOVE(PreviewAudioToggle)
   public:
    PreviewAudioToggle()
        : CBaseUIElement(0, 0, 0, 0, "direct_preview_audio"),
          play_glyph(UniString::to_utf8(std::u32string_view{&Icons::PLAY, 1})),
          stop_glyph(UniString::to_utf8(std::u32string_view{&Icons::STOP, 1})) {}
    ~PreviewAudioToggle() override = default;

    i32 set_id{0};  // 0 while there's nothing to play (an installed set plays its own music instead)

    void draw() override;

   protected:
    void onMouseUpInside(bool left = true, bool right = false) override;

   private:
    const std::string play_glyph;
    const std::string stop_glyph;
};

}  // namespace

// the details of the beatmapset last clicked in the results list and what can be done with it, or a hint on how to
// get there while there is none
class OnlineMapPreview final : public CBaseUIContainer {
    NOCOPY_NOMOVE(OnlineMapPreview)
   public:
    OnlineMapPreview();
    ~OnlineMapPreview() override;

    void show(const Downloader::BeatmapSetMetadata& meta);
    void clear();
    [[nodiscard]] bool is_showing(i32 set_id) const { return this->meta && this->meta->set_id == set_id; }

    // goes to the set in the song browser if it's installed, otherwise downloads it (unless that's already underway)
    void activate();

    void tick() override;
    void draw() override;
    void onKeyDown(KeyboardEvent& e) override;

    // NOT inherited, called manually
    void onResolutionChange(vec2 newResolution);

   private:
    enum class PrimaryAction : u8 { NONE, DOWNLOAD, CANCEL_DOWNLOAD, GO_TO_BEATMAP };

    // text drawn by draw(), positioned by layout()
    struct TextRun {
        McFont* font;
        std::string text;
        vec2 relpos;  // start of the baseline
        Color color{rgb(255, 255, 255)};
        Color outline{rgb(40, 40, 40)};
        f32 scale{1.f};
    };

    void layout();
    void update_primary_button();
    void select_installed();

    std::optional<Downloader::BeatmapSetMetadata> meta;
    ThumbIdentifier thumb_id;
    ThumbIdentifier small_thumb_id;  // the listing's, drawn until the large one is there
    SetInstallState install;

    UIButton* primary_button;
    UIButton* page_button;
    UIButton* close_button;
    PreviewAudioToggle* audio_toggle;
    PrimaryAction primary_action{PrimaryAction::NONE};
    f64 last_primary_click_time{-HUGE_VAL};

    McRect thumb_relrect;
    std::vector<TextRun> text_runs;
    std::vector<f32> divider_ys;  // panel-relative
};

// represents: one single beatmapset element inside the OsuDirectScreen scrollview
// it's a container because it contains UIIcons with tooltips for difficulties
class OnlineMapListing : public CBaseUIContainer {
    NOCOPY_NOMOVE(OnlineMapListing)
   public:
    OnlineMapListing(Downloader::BeatmapSetMetadata meta, OnlineMapPreview& preview);
    ~OnlineMapListing() override;

    void tick() override;
    void draw() override;
    void updateInput(CBaseUIEventCtx& c) override;

    // NOT inherited, called manually
    void onResolutionChange(vec2 newResolution);

   protected:
    void onMouseUpInside(bool left = true, bool right = false) override;
    void onMouseInside() override;
    void onMouseOutside() override;

   private:
    McFont* font;
    Downloader::BeatmapSetMetadata meta;
    OnlineMapPreview& preview;

    AnimFloat hover_anim;
    AnimFloat click_anim;

    // Cache
    std::string full_title;
    ThumbIdentifier thumb_id;

    // "+N more" overflow indicator (drawn by the card itself, not a child element)
    std::string overflow_indicator_str;               // empty => no overflow
    std::vector<std::string> overflow_tooltip_lines;  // hidden diff names (icon-tooltip format)
    McRect overflow_indicator_relrect;                // card-relative draw + hit rect

    f32 creator_width{0.f};
    SetInstallState install;
};

OnlineMapListing::OnlineMapListing(Downloader::BeatmapSetMetadata meta, OnlineMapPreview& preview)
    : font(engine->getDefaultFont()),
      meta(std::move(meta)),
      preview(preview),
      thumb_id(thumb_id_for(this->meta.set_id, false)) {
    // the card itself is the click surface: opt back into hit candidacy (container-self is
    // click-through by default since the single-target dispatch)
    this->bClickThroughSelf = false;

    // right click: open browser to web
    this->setHandleRightMouse(true);

    if(this->meta.beatmaps.size() > 1) {
        // reverse
        std::ranges::sort(this->meta.beatmaps, std::ranges::greater{}, [](const auto& bm) { return bm.star_rating; });
    }

    this->onResolutionChange(osu->getVirtScreenSize());

    osu->getThumbnailManager()->request_image(this->thumb_id);
}

OnlineMapListing::~OnlineMapListing() { osu->getThumbnailManager()->discard_image(this->thumb_id); }

// a left press that turns into a drag never gets here: the scrollview takes it over (cancelling it) past its resistance
void OnlineMapListing::onMouseUpInside(bool left, bool /*right*/) {
    this->click_anim = 1.f;
    this->click_anim.set(0.0f, 0.15f, anim::QuadInOut);

    if(!left) return open_beatmap_page(this->meta.set_id);

    // the first click shows the set in the preview, another one (e.g. the second half of a double click) acts on it
    if(this->preview.is_showing(this->meta.set_id)) {
        this->preview.activate();
    } else {
        this->preview.show(this->meta);
    }
}

void OnlineMapListing::onMouseInside() { this->hover_anim.set(0.25f, 0.15f, anim::QuadInOut); }
void OnlineMapListing::onMouseOutside() { this->hover_anim.set(0.f, 0.15f, anim::QuadInOut); }

void OnlineMapListing::tick() {
    CBaseUIContainer::tick();
    if(!this->isVisible()) return;

    this->install.poll(this->meta.set_id);
}

void OnlineMapListing::updateInput(CBaseUIEventCtx& c) {
    CBaseUIContainer::updateInput(c);

    // the "+N more" indicator is drawn by the card (not a child), so handle its hover tooltip here
    if(this->overflow_indicator_str.empty() || !this->isMouseInside()) return;

    const McRect r(this->getPos() + this->overflow_indicator_relrect.getPos(),
                   this->overflow_indicator_relrect.getSize());
    if(!r.contains(mouse->getPos())) return;

    auto* tt = ui->getTooltipOverlay();
    tt->begin();
    for(const auto& line : this->overflow_tooltip_lines) tt->addLine(line);
    tt->end();
}

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

[[nodiscard]] Color get_difficulty_color(f32 star_rating) {
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

}  // namespace

void OnlineMapListing::onResolutionChange(vec2 /*newResolution*/) {
    this->full_title = fmt::format("{:s} - {:s}", this->meta.artist, this->meta.title);
    this->creator_width = this->font->getStringWidth(this->meta.creator);

    const f32 scale = Osu::getUIScale();
    const f32 ICON = 30.f * scale;
    const f32 STEP = 40.f * scale;
    const f32 GAP = STEP - ICON;  // natural inter-icon spacing (10*scale)

    const vec2 card = this->getSize();
    const f32 thumb_w = card.y * (4.f / 3.f);  // matches draw()'s map_bg_size.x
    const f32 left_limit = thumb_w + GAP;      // icons/indicator must stay right of the thumbnail
    const f32 row_y = card.y - STEP;           // top of the icon row
    const f32 icon_region = card.x - left_limit;
    const sSz max_slots = icon_region >= STEP ? (sSz)(icon_region / STEP) : 0;

    // only standard-mode diffs get an icon; meta.beatmaps is sorted descending by star rating
    const sSz n_std = std::ranges::count_if(this->meta.beatmaps, [](const auto& bm) { return bm.mode == 0; });

    // if there are too many to fit, reserve room on the left for a "+N more" indicator and
    // collapse the lowest-star tail into it
    sSz visible_count = n_std;
    if(n_std > max_slots) {
        const f32 probe_w = this->font->getStringWidth(tformat("+{:d} more", n_std));  // worst-case digits
        const sSz chip_slots = std::max<sSz>(1, (sSz)std::ceil((probe_w + GAP) / STEP));
        visible_count = std::max<sSz>(0, max_slots - chip_slots);
    }

    this->overflow_indicator_str.clear();
    this->overflow_tooltip_lines.clear();
    if(visible_count < n_std) {
        const sSz hidden = n_std - visible_count;
        this->overflow_indicator_str = tformat("+{:d} more", hidden);
        const f32 chip_w = this->font->getStringWidth(this->overflow_indicator_str);
        // right-anchored just left of the leftmost visible icon, clamped to never cross the thumbnail
        const f32 chip_left = std::max(left_limit, card.x - (f32)visible_count * STEP - GAP - chip_w);
        this->overflow_indicator_relrect = McRect(vec2(chip_left, row_y), vec2(chip_w, ICON));
    }

    vec2 pos_counter = {card.x - STEP, row_y};

    auto icon_elems_copy = reinterpret_cast<std::vector<UIIcon*>&>(this->vElements);
    this->invalidate();  // clear this->vElements and rebuild

    for(sSz si = 0; const auto& diff : this->meta.beatmaps) {
        if(diff.mode != 0) continue;

        std::string label = diff.star_rating > 0.f ? fmt::format("{:s} ({:.2f} ⭐)", diff.diffname, diff.star_rating)
                                                   : std::string{diff.diffname};

        if(si >= visible_count) {
            // overflowed: fold the lowest-star tail into the "+N more" tooltip
            this->overflow_tooltip_lines.push_back(std::move(label));
            ++si;
            continue;
        }

        UIIcon* icon = nullptr;
        if(si < icon_elems_copy.size()) {
            icon = icon_elems_copy[si];
            icon_elems_copy[si] = nullptr;  // set to null so we don't delete it later
        } else {
            icon = new UIIcon(Icons::CIRCLE);
        }

        icon->setPos(pos_counter);
        icon->setSize(ICON, ICON);
        icon->setDrawTextShadow(false);  // only outline
        icon->setAutoscaleFX(false);     // use osu scaling

        const Color text_color = diff.star_rating > 0.f ? get_difficulty_color(diff.star_rating) : (Color)-1;
        icon->setTooltipText(label);
        icon->setTextFX({.col_text = text_color,
                         .col_shadow = 0,
                         .col_outline = Colors::invert(text_color),  // TODO: this is kind of ugly
                         .outline_px = 1.f * scale,
                         .shadow_softness_px = 0.5f * scale});

        this->addBaseUIElement(icon);

        pos_counter.x -= STEP;

        ++si;
    }

    // delete any excess items in the container (if we rebuilt with fewer than we originally had)
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

    f32 download_progress = 0.f;
    const bool installed = this->install.installed;
    const bool failed = !installed && this->install.download.stage == MapInstallStage::Failed;
    if(this->install.in_flight()) {
        // To show we're downloading, always draw at least 5%
        download_progress = std::max(0.05f, this->install.download.progress);
    }

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

        // ellipsize the title so it stops before the right-aligned creator name
        const f32 title_max_w = std::max(0.f, progress_size.x - this->creator_width - 4.f * padding);

        g->pushTransform();
        {
            g->translate(pos_counter.x + padding, pos_counter.y + padding + this->font->getHeight());
            g->drawString(this->font, this->font->ellipsize(this->full_title, title_max_w), string_style);
        }
        g->popTransform();

        g->pushTransform();
        {
            g->translate(pos_counter.x + progress_size.x - (this->creator_width + 2 * padding),
                         pos_counter.y + padding + this->font->getHeight());
            g->drawString(this->font, this->meta.creator, string_style);
        }
        g->popTransform();

        if(!this->overflow_indicator_str.empty()) {
            const McRect r(this->getPos() + this->overflow_indicator_relrect.getPos(),
                           this->overflow_indicator_relrect.getSize());
            g->pushTransform();
            {
                g->translate(r.getMinX(), r.getCenter().y + this->font->getHeight() / 2.f);
                g->drawString(this->font, this->overflow_indicator_str, string_style);
            }
            g->popTransform();
        }
    }
    g->popClipRect();

    // tie the card to the preview showing it
    if(this->preview.is_showing(this->meta.set_id)) {
        const f32 thickness = 2.f * Osu::getUIScale();
        g->setColor(rgb(255, 255, 255));
        g->drawRectf(Graphics::RectOptions{
            .x = this->getPos().x + thickness / 2.f,
            .y = this->getPos().y + thickness / 2.f,
            .width = this->getSize().x - thickness,
            .height = this->getSize().y - thickness,
            .lineThickness = thickness,
            .withColor = false,
        });
    }
}

namespace {

void PreviewAudioToggle::draw() {
    if(!this->isVisible() || this->set_id == 0) return;

    using State = PreviewTrackManager::State;
    const PreviewTrackManager* previews = osu->getPreviewTrackManager();
    const State state = previews->get_state(this->set_id);
    const McRect& rect = this->getRect();
    const f32 scale = Osu::getUIScale();

    // a bar along the bottom edge, with the progress while it plays and a segment sweeping across while it loads
    if(state == State::LOADING || state == State::PLAYING) {
        const f32 bar_height = std::round(3.f * scale);
        const McRect bar(rect.getX(), rect.getMaxY() - bar_height, rect.getWidth(), bar_height);
        g->setColor(rgb(0, 0, 0).setA(0.5f));
        g->fillRectf(bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight());
        g->setColor(rgb(255, 255, 255));
        if(state == State::PLAYING) {
            g->fillRectf(bar.getX(), bar.getY(), bar.getWidth() * previews->get_progress(), bar.getHeight());
        } else {
            const f32 segment_width = bar.getWidth() / 4.f;
            const f32 sweep = (f32)std::fmod(engine->getTime(), 1.);
            g->pushClipRect(bar);
            g->fillRectf(bar.getX() - segment_width + (bar.getWidth() + segment_width) * sweep, bar.getY(),
                         segment_width, bar.getHeight());
            g->popClipRect();
        }
    }

    // what a click would do
    if(!this->isMouseInside() || state == State::UNAVAILABLE) return;

    g->setColor(rgb(0, 0, 0).setA(0.4f));
    g->fillRectf(rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight());

    const std::string_view glyph = state == State::NONE ? this->play_glyph : this->stop_glyph;

    McFont* icon_font = osu->getFontIcons();
    const f32 glyph_scale = rect.getHeight() * 0.3f / icon_font->getHeight();
    g->pushTransform();
    {
        g->scale(glyph_scale, glyph_scale);
        g->translate(std::round(rect.getCenter().x - icon_font->getStringWidth(glyph) * glyph_scale / 2.f),
                     std::round(rect.getCenter().y + icon_font->getHeight() * glyph_scale / 2.f));
        g->drawString(icon_font, glyph,
                      TextFX{.col_text = rgb(255, 255, 255),
                             .col_shadow = 0,
                             .col_outline = rgb(40, 40, 40),
                             .outline_px = 1.f * scale,
                             .shadow_softness_px = 0.5f * scale});
    }
    g->popTransform();
}

void PreviewAudioToggle::onMouseUpInside(bool left, bool /*right*/) {
    if(!left || this->set_id == 0) return;

    auto* previews = osu->getPreviewTrackManager();
    if(previews->get_state(this->set_id) == PreviewTrackManager::State::NONE) {
        previews->play(this->set_id);
    } else {
        previews->stop();
    }
}

}  // namespace

OnlineMapPreview::OnlineMapPreview() : CBaseUIContainer(0, 0, 0, 0, "direct_preview") {
    this->audio_toggle = new PreviewAudioToggle();
    this->addBaseUIElement(this->audio_toggle);

    this->primary_button = new UIButton(0, 0, 0, 0, "direct_preview_primary", "");
    this->primary_button->setClickCallback([this]() {
        // a double click on "Download" must not cancel the download its first click started
        const f64 now = engine->getTime();
        if(now < this->last_primary_click_time + 0.5) return;
        this->last_primary_click_time = now;

        if(this->install.in_flight()) {
            osu->getBeatmapInstaller()->cancel(this->meta->set_id);
        } else {
            this->activate();
        }
    });
    this->addBaseUIElement(this->primary_button);

    this->page_button = new UIButton(0, 0, 0, 0, "direct_preview_page", _("View beatmap page"));
    this->page_button->setColor(0xff0c7c99);
    this->page_button->setClickCallback([this]() { open_beatmap_page(this->meta->set_id); });
    this->addBaseUIElement(this->page_button);

    this->close_button = new UIButton(0, 0, 0, 0, "direct_preview_close", _("Close"));
    this->close_button->setColor(0xff636363);
    this->close_button->setClickCallback([this]() { this->clear(); });
    this->addBaseUIElement(this->close_button);
}

OnlineMapPreview::~OnlineMapPreview() {
    if(this->meta) osu->getThumbnailManager()->discard_image(this->thumb_id);
}

void OnlineMapPreview::show(const Downloader::BeatmapSetMetadata& meta) {
    auto* thumbnails = osu->getThumbnailManager();
    if(this->meta) thumbnails->discard_image(this->thumb_id);

    this->meta = meta;
    this->thumb_id = thumb_id_for(meta.set_id, true);
    this->small_thumb_id = thumb_id_for(meta.set_id, false);
    thumbnails->request_image(this->thumb_id);

    this->install = {};
    this->install.poll(meta.set_id);
    this->audio_toggle->set_id = this->install.installed ? 0 : meta.set_id;
    auto* previews = osu->getPreviewTrackManager();
    if(this->install.installed) {
        // it plays its own music instead (selected first, so that stopping the preview doesn't resume the music it
        // replaces in between)
        this->select_installed();
        previews->stop();
    } else if(cv::direct_autoplay_preview.getBool()) {
        previews->play(meta.set_id);
    } else {
        // (the previous set's preview stops, this one waits for a click on the thumbnail)
        previews->stop();
    }

    this->update_primary_button();
    this->layout();
}

void OnlineMapPreview::clear() {
    if(!this->meta) return;

    osu->getPreviewTrackManager()->stop();
    osu->getThumbnailManager()->discard_image(this->thumb_id);
    this->meta.reset();

    this->update_primary_button();
    this->layout();
}

void OnlineMapPreview::activate() {
    if(!this->meta) return;

    if(this->install.installed) {
        this->select_installed();
        ui->setScreen(ui->getSongBrowser());
    } else if(!this->install.in_flight()) {
        osu->getBeatmapInstaller()->enqueue(this->meta->set_id, cv::direct_autoselect.getBool(),
                                            fmt::format("{} - {}", this->meta->artist, this->meta->title));
    }
}

// selects the installed set like a click on it in the song browser would (which plays its music), unless it already
// is the selected one
void OnlineMapPreview::select_installed() {
    if(const auto* current_map = osu->getMapInterface()->getBeatmap();
       current_map && current_map->getSetID() == this->meta->set_id) {
        return;
    }

    const auto* set = db->getBeatmapSet(this->meta->set_id);
    if(!set) return;
    const auto& diffs = set->getDifficulties();
    if(diffs.empty()) return;  // surely unreachable
    ui->getSongBrowser()->onDifficultySelected(diffs[0].get(), false);
}

void OnlineMapPreview::tick() {
    CBaseUIContainer::tick();
    if(!this->isVisible() || !this->meta) return;

    this->install.poll(this->meta->set_id);
    this->audio_toggle->set_id = this->install.installed ? 0 : this->meta->set_id;
    this->update_primary_button();
}

void OnlineMapPreview::update_primary_button() {
    using enum PrimaryAction;
    const PrimaryAction action = !this->meta                 ? NONE
                                 : this->install.installed   ? GO_TO_BEATMAP
                                 : this->install.in_flight() ? CANCEL_DOWNLOAD
                                                             : DOWNLOAD;
    if(action == this->primary_action) return;
    this->primary_action = action;

    switch(action) {
        case DOWNLOAD:
            this->primary_button->setText(_("Download"));
            this->primary_button->setColor(0xff3ca84c);
            break;
        case CANCEL_DOWNLOAD:
            this->primary_button->setText(_("Cancel download"));
            this->primary_button->setColor(0xffc62b00);
            break;
        case GO_TO_BEATMAP:
            this->primary_button->setText(_("Go to beatmap"));
            this->primary_button->setColor(0xff3ca84c);
            break;
        case NONE:
            break;
    }
}

void OnlineMapPreview::onKeyDown(KeyboardEvent& e) {
    CBaseUIContainer::onKeyDown(e);

    // checked after the search bar, which takes every key while it's focused
    if(e.isConsumed() || !this->meta) return;
    if(e == KEY_ESCAPE || e == binds::GAME_PAUSE) {
        soundEngine->play(osu->getSkin()->s_menu_back);
        this->clear();
        e.consume();
    }
}

void OnlineMapPreview::onResolutionChange(vec2 /*newResolution*/) { this->layout(); }

void OnlineMapPreview::layout() {
    const f32 scale = Osu::getUIScale();
    const vec2 size = this->getSize();
    const f32 pad = DEF_PREVIEW_PAD * scale;
    const f32 inner_w = std::max(0.f, size.x - 2.f * pad);
    McFont* font = engine->getDefaultFont();

    // buttons, stacked at the bottom
    const f32 button_h = DEF_PREVIEW_BUTTON_HEIGHT * scale;
    const f32 button_gap = DEF_PREVIEW_BUTTON_GAP * scale;
    const f32 buttons_top = size.y - pad - 3.f * button_h - 2.f * button_gap;
    for(f32 button_y = buttons_top; auto* button : {this->primary_button, this->page_button, this->close_button}) {
        button->setVisible(this->meta.has_value());
        button->setRelPos(pad, button_y);
        button->setSize(inner_w, button_h);
        button_y += button_h + button_gap;
    }
    this->audio_toggle->setVisible(false);  // (put over the thumbnail below, if there is one)
    this->update_pos();

    this->text_runs.clear();
    this->divider_ys.clear();

    if(!this->meta) {
        const std::vector<std::string> hint =
            font->wrap(_("Click a beatmap to see its details.\nClick it again to download it."), inner_w);
        const f32 line_advance = font->getHeight() * PREVIEW_LINE_SPACING;
        f32 y = (size.y - (f32)hint.size() * line_advance) / 2.f;
        for(const auto& line : hint) {
            this->text_runs.push_back({.font = font,
                                       .text = line,
                                       .relpos = {(size.x - font->getStringWidth(line)) / 2.f, y + font->getHeight()},
                                       .color = rgb(180, 180, 180)});
            y += line_advance;
        }
        return;
    }

    const auto& meta = *this->meta;
    const f32 content_bottom = buttons_top - pad;

    // the text gets laid out from 0 first, since the thumbnail above it only gets the room that's left then
    f32 y = 0.f;

    // adds text wrapped to the inner width, the last of max_lines ellipsized if it doesn't fit
    const auto add_text = [&](McFont* line_font, std::string_view text, uSz max_lines) {
        std::vector<std::string> lines = line_font->wrap(text, inner_w);
        if(lines.size() > max_lines) {
            // the rest of the text, from where the last line starts (the lines may have dropped a space at the end)
            std::string_view rest = text;
            for(uSz i = 0; i + 1 < max_lines; ++i) {
                rest.remove_prefix(std::min(lines[i].size(), rest.size()));
                rest.remove_prefix(std::min(rest.find_first_not_of(' '), rest.size()));
            }
            lines.resize(max_lines);
            lines.back() = line_font->ellipsize(rest, inner_w);
        }
        for(auto& line : lines) {
            this->text_runs.push_back(
                {.font = line_font, .text = std::move(line), .relpos = {pad, y + line_font->getHeight()}});
            y += line_font->getHeight() * PREVIEW_LINE_SPACING;
        }
    };

    add_text(osu->getSubTitleFont(), meta.title, 2);
    if(!meta.artist.empty()) add_text(font, meta.artist, 1);
    if(!meta.creator.empty()) add_text(font, tformat("Mapped by {:s}", meta.creator), 1);

    std::vector<std::string> info;
    switch(meta.ranking_status) {
        case 0:
            info.emplace_back(_("Pending"));
            break;
        case 1:
            info.emplace_back(_("Ranked"));
            break;
        case 2:
            info.emplace_back(_("Approved"));
            break;
        case 4:
            info.emplace_back(_("Loved"));
            break;
        default:
            // 3 is ambiguous: qualified for stable and bancho.py, but the pending/graveyard ones for titanic
            break;
    }
    if(!meta.last_update.empty()) {
        // the date part of whatever datetime format the server uses
        info.push_back(tformat("Updated {:s}", meta.last_update.substr(0, meta.last_update.find_first_of("T "))));
    }
    if(meta.has_video) info.emplace_back(_("Video"));
    if(meta.has_storyboard) info.emplace_back(_("Storyboard"));
    if(!info.empty()) add_text(font, SString::join(info, " · "), 2);

    // difficulties (only the standard ones, like the listing's icons)
    std::vector<const Downloader::BeatmapMetadata*> diffs;
    for(const auto& diff : meta.beatmaps) {
        if(diff.mode == 0) diffs.push_back(&diff);
    }
    const f32 line_advance = font->getHeight() * PREVIEW_LINE_SPACING;

    // thumbnail (4:3 like the thumbnails themselves), as big as the width and the room left after the text and a
    // few difficulties allow
    const uSz reserved_lines = diffs.size() > PREVIEW_MIN_DIFFICULTY_LINES
                                   ? PREVIEW_MIN_DIFFICULTY_LINES + 1  // "+N more"
                                   : diffs.size();
    const f32 thumb_h =
        std::min(inner_w * 3.f / 4.f, content_bottom - y - 3.f * pad - (f32)reserved_lines * line_advance);
    f32 text_top = pad;
    if(thumb_h >= DEF_PREVIEW_MIN_THUMB_HEIGHT * scale) {
        this->thumb_relrect = McRect((size.x - thumb_h * 4.f / 3.f) / 2.f, pad, thumb_h * 4.f / 3.f, thumb_h);
        text_top += thumb_h + pad;

        this->audio_toggle->setVisible(true);
        this->audio_toggle->setRelPos(this->thumb_relrect.getPos());
        this->audio_toggle->setSize(this->thumb_relrect.getSize());
        this->update_pos();
    } else {
        this->thumb_relrect = {};
    }
    for(auto& run : this->text_runs) run.relpos.y += text_top;
    y += text_top;

    this->divider_ys.push_back(y);
    y += pad;

    // as many difficulties as fit above the buttons
    McFont* icon_font = osu->getFontIcons();
    const std::string icon = UniString::to_utf8(std::u32string_view{&Icons::CIRCLE, 1});
    const f32 icon_scale = font->getHeight() / icon_font->getHeight();
    const f32 text_indent = icon_font->getStringWidth(icon) * icon_scale + pad;

    const f32 space = content_bottom - y;
    const uSz fitting = space > 0.f ? (uSz)(space / line_advance) : 0;
    const uSz shown = diffs.size() <= fitting ? diffs.size() : (fitting > 0 ? fitting - 1 : 0);
    for(uSz i = 0; i < shown; ++i) {
        const auto& diff = *diffs[i];
        const Color color = diff.star_rating > 0.f ? get_difficulty_color(diff.star_rating) : rgb(255, 255, 255);
        const f32 baseline = y + font->getHeight();
        // same look as the listing's icons
        this->text_runs.push_back({.font = icon_font,
                                   .text = icon,
                                   .relpos = {pad, baseline},
                                   .color = color,
                                   .outline = Colors::invert(color),
                                   .scale = icon_scale});

        // the star rating is a column on the right, so only ever the name gets cut off
        f32 name_w = inner_w - text_indent;
        if(diff.star_rating > 0.f) {
            std::string stars = fmt::format("{:.2f} ⭐", diff.star_rating);
            const f32 stars_w = font->getStringWidth(stars);
            this->text_runs.push_back(
                {.font = font, .text = std::move(stars), .relpos = {pad + inner_w - stars_w, baseline}});
            name_w -= stars_w + pad;
        }
        this->text_runs.push_back(
            {.font = font, .text = font->ellipsize(diff.diffname, name_w), .relpos = {pad + text_indent, baseline}});
        y += line_advance;
    }
    if(shown < diffs.size() && fitting > 0) add_text(font, tformat("+{:d} more", diffs.size() - shown), 1);

    // on screens too short for even the text, what doesn't fit goes
    std::erase_if(this->text_runs, [content_bottom](const TextRun& run) { return run.relpos.y > content_bottom; });
    std::erase_if(this->divider_ys, [content_bottom](f32 divider_y) { return divider_y > content_bottom; });
    this->divider_ys.push_back(buttons_top - pad / 2.f);
}

void OnlineMapPreview::draw() {
    if(!this->isVisible()) return;

    const f32 scale = Osu::getUIScale();
    const vec2 pos = this->getPos();
    const vec2 size = this->getSize();

    // panel background and border, like the install overlay's
    g->setColor(rgb(15, 15, 15).setA(0.85f));
    g->fillRect(static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(size.x), static_cast<int>(size.y));
    g->setColor(rgb(80, 80, 80));
    g->drawRectf(Graphics::RectOptions{
        .x = pos.x + scale / 2.f,
        .y = pos.y + scale / 2.f,
        .width = size.x - scale,
        .height = size.y - scale,
        .lineThickness = scale,
        .withColor = false,
    });

    if(this->meta) {
        if(this->thumb_relrect.getWidth() > 0.f) {
            auto* thumbnails = osu->getThumbnailManager();
            const McRect thumb_rect(pos + this->thumb_relrect.getPos(), this->thumb_relrect.getSize());
            const Image* thumbnail = thumbnails->try_get_image(this->thumb_id);
            if(!thumbnail) thumbnail = thumbnails->try_get_image(this->small_thumb_id);

            if(thumbnail) {
                const f32 thumb_scale = Osu::getImageScaleToFillResolution(thumbnail, thumb_rect.getSize());
                g->pushClipRect(thumb_rect);
                g->pushTransform();
                g->setColor(0xffffffff);
                g->scale(thumb_scale, thumb_scale);
                g->translate(thumb_rect.getCenter());  // needs to be *after* scale
                g->drawImage(thumbnail);
                g->popTransform();
                g->popClipRect();
            } else {
                g->setColor(Color(0x55000000));
                g->fillRect(thumb_rect);
            }
        }

        g->setColor(rgb(50, 50, 50).setA(0.85f));
        const f32 pad = DEF_PREVIEW_PAD * scale;
        for(const f32 divider_y : this->divider_ys) {
            g->fillRect(static_cast<int>(pos.x + pad), static_cast<int>(pos.y + divider_y),
                        static_cast<int>(size.x - 2.f * pad), 1);
        }
    }

    for(const auto& run : this->text_runs) {
        g->pushTransform();
        {
            g->scale(run.scale, run.scale);
            g->translate(std::round(pos.x + run.relpos.x), std::round(pos.y + run.relpos.y));
            g->drawString(run.font, run.text,
                          TextFX{.col_text = run.color,
                                 .col_shadow = 0,
                                 .col_outline = run.outline,
                                 .outline_px = 1.f * scale,
                                 .shadow_softness_px = 0.5f * scale});
        }
        g->popTransform();
    }

    CBaseUIContainer::draw();  // buttons
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
        this->search(NEWEST_QUERY);
    });
    this->addBaseUIElement(this->newest_btn);

    this->best_rated_btn = new UIButton(0, 0, 0, 0, "", _("Best maps"));
    this->best_rated_btn->setColor(0xffFF006A);
    this->best_rated_btn->setClickCallback([this]() {
        this->reset();
        this->search(TOP_RATED_QUERY);
    });
    this->addBaseUIElement(this->best_rated_btn);

    this->ranked_only = new CBaseUICheckbox(0, 0, 0, 0, "direct_ranked_only", _("Only show ranked beatmaps"));
    this->ranked_only->setDrawFrame(false);
    this->ranked_only->setDrawBackground(false);
    this->ranked_only->setChecked(cv::direct_ranking_status_filter.getVal<RankingStatusFilter>() ==
                                  RankingStatusFilter::RANKED);
    this->ranked_only->setChangeCallback(SA::MakeDelegate<&OsuDirectScreen::onRankedCheckboxChange>(this));
    this->addBaseUIElement(this->ranked_only);

    this->auto_select = new CBaseUICheckbox(0, 0, 0, 0, "direct_autoselect", _("Auto-select downloaded beatmaps"));
    this->auto_select->setDrawFrame(false);
    this->auto_select->setDrawBackground(false);
    this->auto_select->setChecked(cv::direct_autoselect.getBool());
    this->auto_select->setChangeCallback(
        [](CBaseUICheckbox* checkbox) { cv::direct_autoselect.setValue(checkbox->isChecked()); });
    this->addBaseUIElement(this->auto_select);

    this->results = new CBaseUIScrollView();
    this->results->setBackgroundColor(0xaa000000);
    this->results->setHorizontalScrolling(false);
    this->results->setVerticalScrolling(true);
    this->addBaseUIElement(this->results);

    this->preview = new OnlineMapPreview();
    this->addBaseUIElement(this->preview);

    cv::direct_ranking_status_filter.setCallback(SA::MakeDelegate<&OsuDirectScreen::onRankedStatusCvarChange>(this));
    cv::direct_autoselect.setCallback(SA::MakeDelegate<&OsuDirectScreen::onAutoSelectCvarChange>(this));
}

OsuDirectScreen::~OsuDirectScreen() {
    // cancel any in-flight search so its callback can't fire against this destroyed screen
    this->search_cancel.request_stop();

    cv::direct_ranking_status_filter.removeAllCallbacks();
    cv::direct_autoselect.removeAllCallbacks();
}

void OsuDirectScreen::onRankedCheckboxChange(CBaseUICheckbox* checkbox) {
    cv::direct_ranking_status_filter.setValue(
        checkbox->isChecked() ? (u8)RankingStatusFilter::RANKED : (u8)RankingStatusFilter::ALL, false);

    this->reset();
    this->search(this->current_query);
}

void OsuDirectScreen::onRankedStatusCvarChange(float oldValue, float newValue) {
    const auto oldEnum = static_cast<RankingStatusFilter>(oldValue);
    const auto newEnum = static_cast<RankingStatusFilter>(newValue);
    using enum RankingStatusFilter;
    if(newEnum != RANKED && newEnum != ALL) {
        // set back to previous (valid) value
        cv::direct_ranking_status_filter.setValue((u8)oldEnum, false);
        return;
    }
    if((newEnum == RANKED) == this->ranked_only->isChecked()) return;
    if(!this->isVisible()) return;

    this->reset();
    this->search(this->current_query);
}

void OsuDirectScreen::onAutoSelectCvarChange() {
    this->auto_select->setChecked(cv::direct_autoselect.getBool(), false);
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

        // the query starts out empty again like the search box (and gets searched again, see reset())
        this->search_bar->clear();
        this->current_query.clear();
        this->search_bar->focus();
    } else {
        this->preview->clear();
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
        g->rotate((f32)std::fmod(engine->getTime(), 2.) * 180.f, 0, 0, 1);
        g->scale(scale, scale);
        g->translate(this->spinner_pos.x, this->spinner_pos.y);
        g->drawImage(osu->getSkin()->i_loading_spinner);
        g->popTransform();
    }

    // TODO: message if no maps were found or server errored
}

void OsuDirectScreen::updateInput(CBaseUIEventCtx& c) {
    if(!this->isVisible()) return;
    ScreenBackable::updateInput(c);
}

void OsuDirectScreen::tick() {
    ScreenBackable::tick();
    if(!this->isVisible()) return;
    if(!BanchoState::is_online() || !db->isFinished() || db->isCancelled()) return this->onBack();

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
        ui->getRoomScreen()->set_current_map(osu->getMapInterface()->getBeatmap());
        ui->setScreen(ui->getRoomScreen());
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

    // the results list stays centered unless that would put it under the preview's column
    const f32 preview_width = std::min(DEF_PREVIEW_WIDTH * scale, newResolution.x * PREVIEW_MAX_SCREEN_WIDTH_RATIO);
    const f32 preview_column = preview_width + 2.f * DEF_PREVIEW_MARGIN * scale;
    const f32 results_width = std::min(newResolution.x - 10.f * scale - preview_column, 1024.f * scale);
    const f32 x_start = std::min((f32)osu->getVirtScreenWidth() / 2.f - results_width / 2.f,
                                 newResolution.x - preview_column - results_width);
    x = x_start;
    y += 50.f * scale;

    // Search bar & buttons, as wide as the results list (the search bar gets what the buttons leave)
    const f32 BUTTONS_MARGIN = 10.f * scale;
    const f32 button_width = 150.f * scale;
    this->search_bar->setRelPos(x, y);
    this->search_bar->setSize(std::max(results_width - 2.f * (button_width + BUTTONS_MARGIN), button_width),
                              40.0f * scale);
    x += this->search_bar->getSize().x + BUTTONS_MARGIN;
    this->newest_btn->setRelPos(x, y);
    this->newest_btn->setSize(button_width, this->search_bar->getSize().y);
    x += this->newest_btn->getSize().x + BUTTONS_MARGIN;
    this->best_rated_btn->setRelPos(x, y);
    this->best_rated_btn->setSize(button_width, this->search_bar->getSize().y);
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

    // Preview panel, vertically centered on the results list
    const f32 preview_height = std::min(this->results->getSize().y, DEF_PREVIEW_MAX_HEIGHT * scale);
    this->preview->setRelPos(newResolution.x - DEF_PREVIEW_MARGIN * scale - preview_width,
                             y + (this->results->getSize().y - preview_height) / 2.f);
    this->preview->setSize(preview_width, preview_height);
    this->preview->onResolutionChange(newResolution);

    // Checkboxes, stacked below the right end of the results list
    const f32 checkbox_height = 40.f * scale;
    f32 checkboxes_width = 0.f;
    for(auto* checkbox : {this->ranked_only, this->auto_select}) {
        checkbox->setSizeY(checkbox_height);
        checkbox->setWidthToContent(0);
        checkboxes_width = std::max(checkboxes_width, checkbox->getSize().x);
    }
    const f32 checkboxes_x = x + results_width - checkboxes_width;
    const f32 checkboxes_y = y + this->results->getSize().y + 10.f * scale;
    for(f32 checkbox_y = checkboxes_y; auto* checkbox : {this->ranked_only, this->auto_select}) {
        checkbox->setRelPos(checkboxes_x, checkbox_y);
        checkbox->setSizeX(checkboxes_width);
        checkbox_y += checkbox_height;
    }

    // the loading spinner goes to their left, centered on the two of them
    const f32 spinner_size = (40.f * scale);
    this->spinner_pos.x = checkboxes_x - (spinner_size / 2.f);
    this->spinner_pos.y = checkboxes_y + checkbox_height;

    this->update_pos();
}

void OsuDirectScreen::reset() {
    // cancel the in-flight request (if any) and immediately allow a new one
    this->search_cancel.request_stop();
    this->loading = false;
    this->last_search_time = 0.0;  // (also if the old results ran out or failed, which stopped fetching more)

    // Clear search results
    this->preview->clear();
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
    // an empty search box shows the newest maps (like on stable)
    const std::string_view server_query = query.empty() ? NEWEST_QUERY : query;
    std::string url = fmt::format("osu.{:s}/web/osu-search.php?m=0&r={:d}&q={:s}&p={:d}", BanchoState::endpoint, filter,
                                  Mc::Net::urlEncode(server_query), offset);
    BANCHO::Api::append_auth_params(url);

    Mc::Net::RequestOptions options{
        .user_agent = "osu!",
        .timeout = 5,
        .connect_timeout = 5,
        .flags = Mc::Net::RequestOptions::FOLLOW_REDIRECTS,
    };

    debugLog("Searching for maps matching \"{:s}\" (offset {:d})", server_query, offset);
    this->search_cancel = {};
    options.cancel_token = this->search_cancel.get_token();
    this->current_query = query;
    this->last_search_time = engine->getTime();
    this->loading = true;

    networkHandler->httpRequestAsync(url, std::move(options), [this](const Mc::Net::Response& response) {
        // a cancelled request never reaches here, so a stale response can't clobber newer results
        this->loading = false;

        if(response.success) {
            const auto set_lines = SString::split_newlines(response.text());

            i32 nb_results{0};
            const bool success = Parsing::strto_s(set_lines[0], nb_results);
            if(!success || nb_results <= 0) {
                // HACK: reached end of results (or errored), prevent further requests
                this->last_search_time = HUGE_VAL;

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

                this->results->container.addBaseUIElement(new OnlineMapListing(std::move(meta), *this->preview));
            }

            this->onResolutionChange(osu->getVirtScreenSize());
        } else {
            // HACK: reached end of results (or errored), prevent further (polled) requests
            this->last_search_time = HUGE_VAL;
            debugLog("Server returned error fetching beatmaps: {}", response.error_msg);
            // TODO: handle failure (better)
        }
    });
}
