// Copyright (c) 2016, PG, All rights reserved.
#include "UISlider.h"

#include <utility>

#include "Osu.h"
#include "Skin.h"
#include "Graphics.h"
#include "Image.h"

UISlider::UISlider(float xPos, float yPos, float xSize, float ySize, UString name)
    : CBaseUISlider(xPos, yPos, xSize, ySize, std::move(name)) {
    this->setBlockSize(20, 20);
}

void UISlider::draw() {
    if(!this->bVisible) return;

    Image *img = osu->getSkin()->i_circle_empty;
    if(img == nullptr) {
        CBaseUISlider::draw();
        return;
    }

    int lineAdd = 1;

    float line1Start = this->getPos().x + (this->vBlockSize.x - 1) / 2 + 1;
    float line1End = this->getPos().x + this->vBlockPos.x + lineAdd;
    float line2Start = this->getPos().x + this->vBlockPos.x + this->vBlockSize.x - lineAdd;
    float line2End = this->getPos().x + this->getSize().x - (this->vBlockSize.x - 1) / 2;

    // draw sliding line
    g->setColor(this->frameColor);
    if(line1End > line1Start)
        g->drawLine((int)(line1Start), (int)(this->getPos().y + this->getSize().y / 2.0f + 1), (int)(line1End),
                    (int)(this->getPos().y + this->getSize().y / 2.0f + 1));
    if(line2End > line2Start)
        g->drawLine((int)(line2Start), (int)(this->getPos().y + this->getSize().y / 2.0f + 1), (int)(line2End),
                    (int)(this->getPos().y + this->getSize().y / 2.0f + 1));

    // draw sliding block
    vec2 blockCenter = this->getPos() + this->vBlockPos + this->vBlockSize / 2.f;
    vec2 scale = vec2(this->vBlockSize.x / img->getWidth(), this->vBlockSize.y / img->getHeight());

    g->setColor(this->frameColor);
    g->pushTransform();
    {
        g->scale(scale.x, scale.y);
        g->translate(blockCenter.x, blockCenter.y + 1);
        g->drawImage(osu->getSkin()->i_circle_empty);
    }
    g->popTransform();
}
