#include "LoadingScreen.h"

#include "ConVar.h"
#include "Engine.h"
#include "Font.h"
#include "KeyBindings.h"
#include "Graphics.h"
#include "Osu.h"
#include "OsuConVarDefs.h"
#include "Skin.h"
#include "UI.h"

#include <utility>

void LoadingScreen::update(CBaseUIEventCtx& /*c*/) {
    if(!this->isVisible()) {
        return;
    }

    this->progress = this->get_progress_fn(this);
    if(this->progress >= 1.f) {
        this->finish();
    }
}

void LoadingScreen::draw() {
    if(!this->isVisible()) return;

    // background
    g->setColor(0xff000000);
    g->fillRect(0, 0, osu->getVirtScreenWidth(), osu->getVirtScreenHeight());

    // progress message
    g->setColor(0xffffffff);
    UString loadingMessage = fmt::format("Loading ... ({} %)", (int)(this->progress * 100.0f));
    g->pushTransform();
    {
        g->translate((int)(osu->getVirtScreenWidth() / 2 - osu->getSubTitleFont()->getStringWidth(loadingMessage) / 2),
                     osu->getVirtScreenHeight() - 15);
        g->drawString(osu->getSubTitleFont(), loadingMessage);
    }
    g->popTransform();

    // spinner
    const float scale = Osu::getImageScale(osu->getSkin()->i_beatmap_import_spinner, 100);
    g->pushTransform();
    {
        g->rotate(engine->getTime() * 180, 0, 0, 1);
        g->scale(scale, scale);
        g->translate(osu->getVirtScreenWidth() / 2, osu->getVirtScreenHeight() / 2);
        g->drawImage(osu->getSkin()->i_beatmap_import_spinner);
    }
    g->popTransform();
}

void LoadingScreen::onKeyDown(KeyboardEvent& e) {
    if(this->isFinished()) return;
    if(e.isConsumed()) return;

    if(e == KEY_ESCAPE || e == cv::GAME_PAUSE.getVal<SCANCODE>()) {
        e.consume();
        this->finish();
    }
}

void LoadingScreen::finish() {
    if(!this->on_finished_fn) return;
    auto finishfunc = std::move(this->on_finished_fn);

    this->progress = 1.f;
    this->get_progress_fn = {};
    this->on_finished_fn = {};

    finishfunc(this);
}
