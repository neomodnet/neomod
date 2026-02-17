// Copyright (c) 2018, PG, All rights reserved.
#include "UIVolumeSlider.h"

#include <utility>

#include "AnimationHandler.h"
#include "Engine.h"
#include "Osu.h"
#include "ResourceManager.h"
#include "Font.h"
#include "Graphics.h"
#include "Image.h"

UIVolumeSlider::Resources UIVolumeSlider::imageResources{};  // shared
bool UIVolumeSlider::resourcesReady = false;

UIVolumeSlider::UIVolumeSlider(float xPos, float yPos, float xSize, float ySize, UString name)
    : CBaseUISlider(xPos, yPos, xSize, ySize, std::move(name)) {
    this->type = TYPE::MASTER;
    this->bSelected = false;

    this->bWentMouseInside = false;
    this->fSelectionAnim = 0.0f;

    this->font = engine->getDefaultFont();
    if(!imageResources.disabled[0]) {  // init all at once, once
        for(auto& [res, fileName, resourceName] : std::array{
                std::tuple{&imageResources.disabled[0], "ic_volume_mute_white_48dp.png", "UI_VOLUME_SLIDER_BLOCK_0"},
                std::tuple{&imageResources.enabled[0], "ic_volume_up_white_48dp.png", "UI_VOLUME_SLIDER_BLOCK_1"},
                std::tuple{&imageResources.disabled[1], "ic_music_off_48dp.png", "UI_VOLUME_SLIDER_MUSIC_0"},
                std::tuple{&imageResources.enabled[1], "ic_music_48dp.png", "UI_VOLUME_SLIDER_MUSIC_1"},
                std::tuple{&imageResources.disabled[2], "ic_effects_off_48dp.png", "UI_VOLUME_SLIDER_EFFECTS_0"},
                std::tuple{&imageResources.enabled[2], "ic_effects_48dp.png", "UI_VOLUME_SLIDER_EFFECTS_1"}}) {
            resourceManager->requestNextLoadAsync();
            *res = resourceManager->loadImage(fileName, resourceName);
        }
    }

    this->setFrameColor(0xff7f7f7f);
}

void UIVolumeSlider::drawBlock() {
    if(!resourcesReady) {
        for(const auto& type : std::array{imageResources.disabled, imageResources.enabled}) {
            for(const auto* image : type) {
                if(!image || !image->isReady()) {
                    return;
                }
            }
        }
        resourcesReady = true;
    }

    // draw icon
    Image* img = nullptr;
    if(this->getFloat() < 0.01f)
        img = imageResources.disabled[(size_t)this->type];
    else
        img = imageResources.enabled[(size_t)this->type];

    g->pushTransform();
    {
        const float scaleMultiplier = 0.95f + 0.2f * this->fSelectionAnim;

        g->scale((this->vBlockSize.y / img->getSize().x) * scaleMultiplier,
                 (this->vBlockSize.y / img->getSize().y) * scaleMultiplier);
        g->translate(this->getPos().x + this->vBlockPos.x + this->vBlockSize.x / 2.0f,
                     this->getPos().y + this->vBlockPos.y + this->vBlockSize.y / 2.0f + 1);
        g->setColor(0xffffffff);
        g->drawImage(img);
    }
    g->popTransform();

    // draw percentage
    g->pushTransform();
    {
        g->translate((int)(this->getPos().x + this->getSize().x + this->getSize().x * 0.0335f),
                     (int)(this->getPos().y + this->getSize().y / 2 + this->font->getHeight() / 2));
        g->setColor(0xff000000);
        g->translate(1, 1);
        g->drawString(this->font, fmt::format("{}%", (int)(std::round(this->getFloat() * 100.0f))));
        g->setColor(0xffffffff);
        g->translate(-1, -1);
        g->drawString(this->font, fmt::format("{}%", (int)(std::round(this->getFloat() * 100.0f))));
    }
    g->popTransform();
}

void UIVolumeSlider::onMouseInside() {
    CBaseUISlider::onMouseInside();

    this->bWentMouseInside = true;
}

void UIVolumeSlider::setSelected(bool selected) {
    this->bSelected = selected;

    if(this->bSelected) {
        this->setFrameColor(0xffffffff);
        anim::moveQuadOut(&this->fSelectionAnim, 1.0f, 0.1f, true);
        anim::moveQuadIn(&this->fSelectionAnim, 0.0f, 0.15f, 0.1f, false);
    } else {
        this->setFrameColor(0xff7f7f7f);
        anim::moveLinear(&this->fSelectionAnim, 0.0f, 0.15f * this->fSelectionAnim, true);
    }
}

bool UIVolumeSlider::checkWentMouseInside() {
    const bool temp = this->bWentMouseInside;
    this->bWentMouseInside = false;
    return temp;
}

float UIVolumeSlider::getMinimumExtraTextWidth() {
    return this->getSize().x * 0.0335f * 2.0f + this->font->getStringWidth("100%");
}
