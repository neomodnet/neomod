// Copyright (c) 2026, WH, All rights reserved.

#include "BeatmapInstallOverlay.h"

#include "CBaseUIButton.h"
#include "Engine.h"
#include "Font.h"
#include "Graphics.h"
#include "Logging.h"
#include "Osu.h"
#include "OsuConVars.h"
#include "i18n.h"

#include "BeatmapInstaller.h"

namespace {

// virtual-screen pixels at scale=1; everything is multiplied by Osu::getUIScale() at draw/update time
constexpr f32 DEF_ROW_WIDTH{480.f};
constexpr f32 DEF_ROW_HEIGHT{30.f};
constexpr f32 DEF_ROW_PAD_X{8.f};
constexpr f32 DEF_PROGRESS_BAR_H{3.f};
constexpr f32 DEF_X_BTN_SIZE{16.f};
constexpr f32 DEF_X_BTN_PAD_RIGHT{4.f};
constexpr f32 DEF_PANEL_MARGIN_X{16.f};
constexpr f32 DEF_PANEL_MARGIN_BOTTOM{80.f};  // clear the (nearly-global) back button image
constexpr f32 DEF_PANEL_INNER_PAD{4.f};
// fixed-width status zone (right-aligned text) so digit-count changes in "73%" don't reflow the title;
// sized to fit the widest status string we'll show ("Installing...").
constexpr f32 DEF_STATUS_ZONE_W{96.f};
constexpr f32 DEF_TITLE_STATUS_GAP{8.f};

Color color_for_progress(MapInstallStage stage) {
    using enum MapInstallStage;
    switch(stage) {
        case Failed:
            return rgb(220, 80, 80);
        case Extracting:
        case Installing:
            return rgb(255, 220, 100);
        case Done:  // shouldn't appear; defensive
        case Downloading:
            return rgb(150, 255, 150);
        case Queued:
        case None:
        default:
            return rgb(150, 150, 150);
    }
}

class InstallRow final : public CBaseUIContainer {
    NOCOPY_NOMOVE(InstallRow)
   public:
    explicit InstallRow(u32 uid);
    ~InstallRow() override = default;

    void draw() override;

    // set this InstallRow element to an EntryView's state
    // maybe TODO: should BeatmapInstallOverlay be more tightly coupled to BeatmapInstaller to avoid the need
    // to duplicate and reconcile state between them?
    void apply(const BeatmapInstaller::EntryView& v);

    [[nodiscard]] u32 uid() const { return this->entry_uid; }
    [[nodiscard]] CBaseUIButton* xButton() const { return this->x_btn; }

   private:
    u32 entry_uid;
    i32 mapset_id{-1};
    MapInstallStage stage{MapInstallStage::None};
    f32 progress{0.f};
    std::string display_name;
    std::string cached_title;   // "Beatmap #N" or display_name
    std::string cached_status;  // "73%" / "Queued" / "Installing..." / "Failed"

    CBaseUIButton* x_btn{nullptr};
    McFont* font;

    void update_cached_strings();
};

InstallRow::InstallRow(u32 uid)
    : CBaseUIContainer(0, 0, 0, 0, fmt::format("install-row-{}", uid)), entry_uid(uid), font(engine->getDefaultFont()) {
    this->x_btn = new CBaseUIButton(0, 0, 0, 0, "x", "✖"s);
    this->x_btn->setDrawFrame(false);
    this->x_btn->setDrawBackground(false);
    this->x_btn->setClickCallback([uid]() { osu->getBeatmapInstaller()->cancel_entry(uid); });
    this->addBaseUIElement(this->x_btn);

    this->update_cached_strings();
}

void InstallRow::apply(const BeatmapInstaller::EntryView& v) {
    bool dirty = false;
    if(v.set_id != this->mapset_id) {  // local imports resolve theirs mid-flight
        this->mapset_id = v.set_id;
        dirty = true;
    }
    if(v.stage != this->stage) {
        this->stage = v.stage;
        dirty = true;
    }
    if(v.progress != this->progress) {
        this->progress = v.progress;
        dirty = true;
    }
    if(v.display_name != this->display_name) {
        this->display_name = v.display_name;
        dirty = true;
    }
    if(dirty) this->update_cached_strings();
}

void InstallRow::update_cached_strings() {
    this->cached_title = this->display_name.empty() ? tformat("Beatmap #{:d}", this->mapset_id) : this->display_name;

    switch(this->stage) {
        using enum MapInstallStage;
        case Failed:
            this->cached_status = _("Failed");
            break;
        case Installing:
            this->cached_status = _("Installing...");
            break;
        case Extracting:
            this->cached_status = _("Extracting...");
            break;
        case Downloading:
            this->cached_status = fmt::format("{:d}%", static_cast<i32>(this->progress * 100.f));
            break;
        case Queued:
        case Done:
        case None:
        default:
            this->cached_status = _("Queued");
            break;
    }
}

void InstallRow::draw() {
    const f32 scale = Osu::getUIScale();
    const vec2 pos = this->getPos();
    const vec2 size = this->getSize();

    // progress fill across the bottom (clamp to 5% minimum while in flight, mirrors OsuDirect)
    using enum MapInstallStage;
    using namespace flags::operators;
    const bool active = !!(this->stage & (Queued | Downloading | Extracting | Installing));
    const f32 raw_p = !!(this->stage & (Extracting | Installing)) ? 1.f : this->progress;
    const f32 fill_p = (this->stage == Failed) ? 1.f : (active ? std::max(0.05f, raw_p) : raw_p);
    const f32 bar_h = DEF_PROGRESS_BAR_H * scale;

    g->setColor(color_for_progress(this->stage).setA(0.85f));
    g->fillRect(static_cast<int>(pos.x), static_cast<int>(pos.y + size.y - bar_h), static_cast<int>(size.x * fill_p),
                static_cast<int>(bar_h));

    // text: outlined to match OsuDirect's TextFX usage
    const TextFX fx{
        .col_text = rgb(255, 255, 255),
        .col_shadow = 0,
        .col_outline = rgb(40, 40, 40),
        .outline_px = 1.f * scale,
        .shadow_softness_px = 0.5f * scale,
    };

    const f32 text_y = pos.y + (size.y - bar_h) / 2.f + static_cast<f32>(this->font->getHeight()) / 2.f;
    const f32 pad_x = DEF_ROW_PAD_X * scale;
    const f32 x_btn_zone_w = (DEF_X_BTN_SIZE + DEF_X_BTN_PAD_RIGHT) * scale + pad_x;
    const f32 status_zone_w = DEF_STATUS_ZONE_W * scale;
    const f32 gap = DEF_TITLE_STATUS_GAP * scale;

    // right: status text, right-aligned within a fixed-width zone so digit-count changes
    // don't shift the title's ellipsization budget every frame.
    const f32 status_zone_right = pos.x + size.x - x_btn_zone_w;
    const f32 status_text_x = status_zone_right - static_cast<f32>(this->font->getStringWidth(this->cached_status));
    g->pushTransform();
    {
        g->translate(static_cast<f32>(static_cast<i32>(status_text_x)), static_cast<f32>(static_cast<i32>(text_y)));
        g->drawString(this->font, this->cached_status, fx);
    }
    g->popTransform();

    // left: title, ellipsized to whatever space is left before the status zone
    const f32 title_max_w = (status_zone_right - status_zone_w) - (pos.x + pad_x) - gap;
    const std::string title_disp = (static_cast<f32>(this->font->getStringWidth(this->cached_title)) > title_max_w)
                                       ? this->font->ellipsize(this->cached_title, title_max_w)
                                       : this->cached_title;
    g->pushTransform();
    {
        g->translate(static_cast<f32>(static_cast<i32>(pos.x + pad_x)), static_cast<f32>(static_cast<i32>(text_y)));
        g->drawString(this->font, title_disp, fx);
    }
    g->popTransform();

    CBaseUIContainer::draw();  // X button
}

}  // namespace

struct BeatmapInstallOverlay::BIOImpl {
    // temporary buffer for in-progress downloads
    std::vector<BeatmapInstaller::EntryView> entry_cache;

    // cached layout inputs; layout is recomputed only when any of these change
    uSz last_row_count{0};
    f32 last_scale{0.f};
    i32 last_virt_height{0};
};

BeatmapInstallOverlay::BeatmapInstallOverlay() : UIScreen(), m_impl() {}
BeatmapInstallOverlay::~BeatmapInstallOverlay() = default;

void BeatmapInstallOverlay::onResolutionChange(vec2 /*newResolution*/) {
    // invalidate cached layout inputs so update() recomputes positions next tick
    m_impl->last_scale = 0.f;
    m_impl->last_virt_height = 0;
}

void BeatmapInstallOverlay::updateInput(CBaseUIEventCtx& c) { CBaseUIContainer::updateInput(c); }

void BeatmapInstallOverlay::tick() {
    UIScreen::tick();

    // play mode is the broader visibility gate; the matching draw() call lives in UI::draw()'s
    // "not playing" branch. tick() runs before the input pass, so the bVisible computed here is
    // what gates the child X buttons' input in the same frame.
    if(!cv::draw_beatmap_install_overlay.getBool() || osu->isInPlayMode() || Osu::isKioskMode()) {
        this->bVisible = false;
        return;
    }

    auto* installer = osu->getBeatmapInstaller();

    installer->snapshot(m_impl->entry_cache);
    if(m_impl->entry_cache.empty()) {
        this->bVisible = false;
        return;
    }

    this->bVisible = true;

    // diff existing rows against snapshot, keyed by set_id
    bool topology_changed = false;
    {
        // collect-then-delete to avoid mutating vElements while iterating it
        std::vector<InstallRow*> stale;
        for(auto* row : this->getElementsAs<InstallRow>()) {
            bool found = false;
            for(const auto& v : m_impl->entry_cache) {
                if(v.uid == row->uid()) {
                    found = true;
                    break;
                }
            }
            if(!found) stale.push_back(row);
        }
        for(auto* row : stale) this->deleteBaseUIElement(row);
        if(!stale.empty()) topology_changed = true;
    }

    // add new rows + update existing ones. insertion order preserves "first seen first"
    // so a bottom-anchored vertical layout naturally puts newest at the bottom.
    for(const auto& v : m_impl->entry_cache) {
        InstallRow* row = nullptr;
        for(auto* r : this->getElementsAs<InstallRow>()) {
            if(r->uid() == v.uid) {
                row = r;
                break;
            }
        }
        if(!row) {
            row = new InstallRow(v.uid);
            this->addBaseUIElement(row);
            topology_changed = true;
        }
        row->apply(v);
    }

    m_impl->entry_cache.clear();  // unnecessary to keep this around for now

    // layout: bottom-left anchored panel, rows stacked top-to-bottom inside.
    // recompute only when row count, ui scale, or virtual screen height changed.
    const f32 scale = Osu::getUIScale();
    const i32 virt_h = osu->getVirtScreenHeight();
    const auto& rows = this->getElementsAs<InstallRow>();

    if(topology_changed || scale != m_impl->last_scale || virt_h != m_impl->last_virt_height ||
       rows.size() != m_impl->last_row_count) {
        const f32 row_w = DEF_ROW_WIDTH * scale;
        const f32 row_h = DEF_ROW_HEIGHT * scale;
        const f32 inner_pad = DEF_PANEL_INNER_PAD * scale;
        const f32 margin_x = DEF_PANEL_MARGIN_X * scale;
        const f32 margin_b = DEF_PANEL_MARGIN_BOTTOM * scale;
        const f32 x_btn_sz = DEF_X_BTN_SIZE * scale;
        const f32 x_btn_pad_r = DEF_X_BTN_PAD_RIGHT * scale;

        const f32 panel_w = row_w + 2.f * inner_pad;
        const f32 panel_h = static_cast<f32>(rows.size()) * row_h + 2.f * inner_pad;

        const vec2 panel_pos{margin_x, static_cast<f32>(virt_h) - margin_b - panel_h};
        this->setPos(panel_pos);
        this->setSize(panel_w, panel_h);

        for(uSz i = 0; i < rows.size(); ++i) {
            auto* row = rows[i];
            row->setRelPos(inner_pad, inner_pad + static_cast<f32>(i) * row_h);
            row->setSize(row_w, row_h);

            // X button: right-anchored inside the row, vertically centered above the progress bar
            const f32 bar_h = DEF_PROGRESS_BAR_H * scale;
            const f32 by = (row_h - bar_h - x_btn_sz) / 2.f;
            auto* btn = row->xButton();
            btn->setRelPos(row_w - x_btn_sz - x_btn_pad_r, by);
            btn->setSize(x_btn_sz, x_btn_sz);
        }
        this->update_pos();

        m_impl->last_scale = scale;
        m_impl->last_virt_height = virt_h;
        m_impl->last_row_count = rows.size();
    }
}

void BeatmapInstallOverlay::draw() {
    if(!this->isVisible()) return;

    const vec2 pos = this->getPos();
    const vec2 size = this->getSize();

    // panel background
    g->setColor(rgb(15, 15, 15).setA(0.85f));
    g->fillRect(static_cast<int>(pos.x), static_cast<int>(pos.y), static_cast<int>(size.x), static_cast<int>(size.y));

    // panel border (1 ui-pixel)
    const f32 thickness = Osu::getUIScale();
    g->setColor(rgb(80, 80, 80));
    g->drawRectf(Graphics::RectOptions{
        .x = pos.x + thickness / 2.f,
        .y = pos.y + thickness / 2.f,
        .width = size.x - thickness,
        .height = size.y - thickness,
        .lineThickness = thickness,
        .withColor = false,
    });

    // dividers between rows
    const auto& rows = this->getElementsAs<InstallRow>();
    if(rows.size() >= 2) {
        g->setColor(rgb(50, 50, 50).setA(0.85f));
        for(uSz i = 1; i < rows.size(); ++i) {
            const vec2 rp = rows[i]->getPos();
            g->fillRect(static_cast<int>(rp.x), static_cast<int>(rp.y), static_cast<int>(rows[i]->getSize().x), 1);
        }
    }

    CBaseUIContainer::draw();
}
