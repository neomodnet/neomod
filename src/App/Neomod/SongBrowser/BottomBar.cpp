// Copyright (c) 2025, kiwec, All rights reserved.
#include "BottomBar.h"

#include "AnimationHandler.h"
#include "OsuConVars.h"
#include "Engine.h"
#include "LoudnessCalcThread.h"
#include "BatchDiffCalc.h"
#include "Mouse.h"
#include "OptionsOverlay.h"
#include "UIBackButton.h"
#include "Osu.h"
#include "MapExporter.h"
#include "ResourceManager.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SongBrowser.h"
#include "UI.h"
#include "UserCard.h"
#include "Font.h"
#include "SyncMutex.h"

#include <atomic>
#include <cassert>

namespace BottomBar {
using namespace neomod::sbr;

namespace {

Sync::mutex export_progress_mtx;
std::atomic<float> export_progress{-1.f};
std::string export_entry;
std::string export_collection;

struct ButtonState {
    McRect rect{};
    f32 alpha{0.f};  // hover alpha
};
std::array<ButtonState, BTN_MAX> btns{};
Button hovered_btn = BTN_NONE;

}  // namespace

void update_export_progress(float progress, std::string entry_being_processed, const std::string& collection) {
    export_collection = collection;
    export_progress.store(progress, std::memory_order_relaxed);
    if(progress > 0.f) {
        Sync::scoped_lock lk(export_progress_mtx);
        export_entry = std::move(entry_being_processed);
    }
}

void press_button(Button btn_index) {
    assert(btn_index < BTN_MAX);

    if(hovered_btn != btn_index) {
        btns[btn_index].alpha = 1.f;
        anim::moveLinear(&btns[btn_index].alpha, 0.0f, 0.1f, 0.05f, true);
    }

    switch(btn_index) {
        case MODE:
            return g_songbrowser->onSelectionMode();
        case MODS:
            return g_songbrowser->onSelectionMods();
        case RANDOM:
            return g_songbrowser->onSelectionRandom();
        case OPTIONS:
            return g_songbrowser->onSelectionOptions();
        default:
            std::unreachable();
            break;
    }
    std::unreachable();
}

f32 get_min_height() { return SongBrowser::getUIScale() * 101.f; }

f32 get_height() {
    f32 max = 0.f;
    for(const auto& [rect, _] : btns) {
        if(rect.getHeight() > max) {
            max = rect.getHeight();
        }
    }

    return std::max(get_min_height(), max);
}

void update(CBaseUIEventCtx& c) {
    static bool mouse_was_down = false;

    auto mousePos = mouse->getPos();
    bool clicked = !mouse_was_down && mouse->isLeftDown();
    mouse_was_down = mouse->isLeftDown();

    const auto* skin = osu->getSkin();
    const vec2 screen = osu->getVirtScreenSize();
    bool is_widescreen = (screen.x / screen.y) > (4.f / 3.f);

    const SkinImage* mode_img = skin->i_sel_mode_over;
    btns[MODE].rect.setSize(SongBrowser::getSkinDimensions(mode_img));
    btns[MODE].rect.setPos({Osu::getUIScale(is_widescreen ? 140.0f : 120.0f), screen.y - btns[MODE].rect.getHeight()});

    const SkinImage* mods_img = skin->i_sel_mods_over;
    btns[MODS].rect.setSize(SongBrowser::getSkinDimensions(mods_img));
    btns[MODS].rect.setPos(
        {btns[MODE].rect.getX() + SongBrowser::getUIScale(92.5f), screen.y - btns[MODS].rect.getHeight()});

    const SkinImage* random_img = skin->i_sel_random_over;
    btns[RANDOM].rect.setSize(SongBrowser::getSkinDimensions(random_img));
    btns[RANDOM].rect.setPos(
        {btns[MODS].rect.getX() + SongBrowser::getUIScale(77.5f), screen.y - btns[RANDOM].rect.getHeight()});

    const SkinImage* options_img = skin->i_sel_options_over;
    btns[OPTIONS].rect.setSize(SongBrowser::getSkinDimensions(options_img));
    btns[OPTIONS].rect.setPos(
        {btns[RANDOM].rect.getX() + SongBrowser::getUIScale(77.5f), screen.y - btns[OPTIONS].rect.getHeight()});

    osu->getUserButton()->setSize(SongBrowser::getUIScale(320.f), SongBrowser::getUIScale(75.f));
    osu->getUserButton()->setPos(btns[OPTIONS].rect.getX() + SongBrowser::getUIScale(160.f),
                                 osu->getVirtScreenHeight() - osu->getUserButton()->getSize().y);
    osu->getUserButton()->update(c);

    // Yes, the order looks whack. That's the correct order.
    Button new_hover = BTN_NONE;
    if(btns[OPTIONS].rect.contains(mousePos)) {
        new_hover = OPTIONS;
    } else if(btns[MODE].rect.contains(mousePos)) {
        new_hover = MODE;
    } else if(btns[MODS].rect.contains(mousePos)) {
        new_hover = MODS;
    } else if(btns[RANDOM].rect.contains(mousePos)) {
        new_hover = RANDOM;
    } else {
        clicked = false;
    }

    if(hovered_btn != new_hover) {
        if(hovered_btn != BTN_NONE) {
            anim::moveLinear(&btns[hovered_btn].alpha, 0.0f, btns[hovered_btn].alpha * 0.1f, true);
        }
        if(new_hover != BTN_NONE) {
            anim::moveLinear(&btns[new_hover].alpha, 1.f, 0.1f, true);
        }

        hovered_btn = new_hover;
    }

    if(clicked && c.propagate_clicks) {
        c.propagate_clicks = false;
        c.propagate_hover = false;
        press_button(hovered_btn);
    }

    // TODO @kiwec: do buttons make .alphasound?
}

void draw() {
    const auto* skin = osu->getSkin();
    const vec2 screen_size = osu->getVirtScreenSize();

    g->pushTransform();
    {
        const f32 bar_height = get_min_height();
        const Image* img = skin->i_songselect_bot;
        g->setColor(0xffffffff);
        g->scale((f32)screen_size.x / (f32)img->getWidth(), bar_height / (f32)img->getHeight());
        g->translate(0, screen_size.y - bar_height);
        g->drawImage(img, AnchorPoint::TOP_LEFT);
    }
    g->popTransform();

    // don't double-draw the back button
    if(!ui->getOptionsOverlay()->isVisible()) {
        g_songbrowser->backButton->draw();
    }

    // Draw the user card under selection elements, which can cover it for fancy effects
    // (we don't match stable perfectly, but close enough)
    osu->getUserButton()->draw();

    g->setColor(0xffffffff);

    // Careful, these buttons are often used as overlays
    // eg. selection-mode usually covers the whole screen, drawing topbar, bottom right osu cookie etc
    const std::array btn_imgs{std::pair{&btns[MODE], skin->i_sel_mode},         //
                              std::pair{&btns[MODS], skin->i_sel_mods},         //
                              std::pair{&btns[RANDOM], skin->i_sel_random},     //
                              std::pair{&btns[OPTIONS], skin->i_sel_options}};  //
    for(const auto& [btn, img] : btn_imgs) {
        if(img == nullptr) continue;

        const f32 scale = SongBrowser::getSkinScale(img);
        img->drawRaw(vec2(btn->rect.getX(), screen_size.y), scale, AnchorPoint::BOTTOM_LEFT);
    }

    // Ok, and now for the hover images... which are drawn in a weird order, same as update_bottombar()
    const std::array btn_hoverimgs{std::pair{&btns[RANDOM], skin->i_sel_random_over},     //
                                   std::pair{&btns[MODS], skin->i_sel_mods_over},         //
                                   std::pair{&btns[MODE], skin->i_sel_mode_over},         //
                                   std::pair{&btns[OPTIONS], skin->i_sel_options_over}};  //
    for(const auto& [btn, img] : btn_hoverimgs) {
        if(img == nullptr) continue;

        g->setAlpha(btn->alpha);
        const f32 scale = SongBrowser::getSkinScale(img);
        img->drawRaw(vec2(btn->rect.getX(), screen_size.y), scale, AnchorPoint::BOTTOM_LEFT);
    }

    // mode-osu-small (often used as overlay)
    const SkinImage* mos_img = skin->i_mode_osu_small;
    if(mos_img != nullptr) {
        f32 mos_scale = SongBrowser::getSkinScale(mos_img);
        g->setBlendMode(DrawBlendMode::ADDITIVE);
        g->setColor(0xffffffff);
        mos_img->drawRaw(vec2(btns[MODE].rect.getX() + (btns[MODS].rect.getX() - btns[MODE].rect.getX()) / 2.f,
                              screen_size.y - SongBrowser::getUIScale(56.f)),
                         mos_scale, AnchorPoint::CENTER);
        g->setBlendMode(DrawBlendMode::ALPHA);
    }

    // background task busy notification
    g->setColor(0xff333333);

    // XXX: move this to permanent toasts
    McFont* font = engine->getDefaultFont();
    i32 calcx = osu->getUserButton()->getPos().x + osu->getUserButton()->getSize().x + 20;
    i32 calcy = osu->getUserButton()->getPos().y + 15;
    if(BatchDiffCalc::get_maps_total() > 0) {
        UString msg = fmt::format("Calculating stars ({}/{}) ...", BatchDiffCalc::get_maps_processed(),
                                  BatchDiffCalc::get_maps_total());
        g->pushTransform();
        g->translate(calcx, calcy);
        g->drawString(font, msg);
        g->popTransform();
        calcy += font->getHeight() + 10;
    }
    if(float progress = export_progress.load(std::memory_order_relaxed); progress > 0.f && progress < 1.f) {
        std::string export_entry_copy;
        {
            Sync::scoped_lock lk(export_progress_mtx);
            export_entry_copy = export_entry;
        }
        UString msg1 = fmt::format("Exporting {} {:.2f}%", !export_collection.empty() ? export_collection : "mapset",
                                   progress * 100.f);
        UString msg2 = fmt::format(" {:s}", export_entry_copy);
        g->pushTransform();
        g->translate(calcx, calcy);
        g->drawString(font, msg1);
        g->translate(0, font->getHeight() + 10);
        g->drawString(font, msg2);
        g->popTransform();
        calcy += font->getHeight() + 20;
    }
    if(cv::normalize_loudness.getBool() && VolNormalization::get_total() > 0 &&
       VolNormalization::get_computed() < VolNormalization::get_total()) {
        UString msg = fmt::format("Computing loudness ({}/{}) ...", VolNormalization::get_computed(),
                                  VolNormalization::get_total());
        g->pushTransform();
        g->translate(calcx, calcy);
        g->drawString(font, msg);
        g->popTransform();
        calcy += font->getHeight() + 10;
    }
    const auto calc_total = BatchDiffCalc::get_scores_total();
    const auto calc_computed = BatchDiffCalc::get_scores_processed();
    if(calc_total > 0 && calc_computed < calc_total) {
        UString msg = fmt::format("Converting scores ({}/{}) ...", calc_computed, calc_total);
        g->pushTransform();
        g->translate(calcx, calcy);
        g->drawString(font, msg);
        g->popTransform();
        calcy += font->getHeight() + 10;
    }
}

// TODO @kiwec: default icon for mode-osu-small
}  // namespace BottomBar
