// Copyright (c) 2016, PG, All rights reserved.
#include "UIModSelectorModButton.h"

#include <utility>

#include "Logging.h"
#include "OsuConVars.h"
#include "AnimationHandler.h"
#include "Bancho.h"
#include "BanchoNetworking.h"
#include "Engine.h"
#include "ModSelector.h"
#include "Osu.h"
#include "RichPresence.h"
#include "RoomScreen.h"
#include "Skin.h"
#include "SkinImage.h"
#include "SoundEngine.h"
#include "TooltipOverlay.h"
#include "UI.h"

UIModSelectorModButton::UIModSelectorModButton(ModSelector *osuModSelector, float xPos, float yPos, float xSize,
                                               float ySize, UString name)
    : CBaseUIButton(xPos, yPos, xSize, ySize, std::move(name), "") {
    this->osuModSelector = osuModSelector;
    this->iState = 0;
    this->vScale = this->vBaseScale = vec2(1, 1);
    this->fRot = 0.0f;

    this->fEnabledScaleMultiplier = 1.25f;
    this->fEnabledRotationDeg = 6.0f;
    this->bAvailable = true;
    this->bOn = false;

    this->activeSkinImageMember = nullptr;

    this->bFocusStolenDelay = false;
}

void UIModSelectorModButton::draw() {
    if(!this->bVisible) return;

    if(SkinImage *activeImage =
           this->activeSkinImageMember ? neomod::skin::getSkinImageMember(this->activeSkinImageMember) : nullptr;
       !!activeImage) {
        g->pushTransform();
        {
            g->scale(this->vScale.x, this->vScale.y);

            if(this->fRot != 0.0f) g->rotate(this->fRot);

            g->setColor(0xffffffff);
            // HACK: For "Actual Flashlight" mod, I'm too lazy to add a new skin element
            bool draw_inverted_colors = this->getActiveModName() == US_("afl");
            if(draw_inverted_colors) {
                g->setColorInversion(true);
            }
            activeImage->draw(this->getPos() + this->getSize() / 2.f);
            if(draw_inverted_colors) {
                g->setColorInversion(false);
            }
        }
        g->popTransform();
    }

    if(!this->bAvailable) {
        const int size = this->getSize().x > this->getSize().y ? this->getSize().x : this->getSize().y;

        g->setColor(0xff000000);
        g->drawLine(this->getPos().x + 1, this->getPos().y, this->getPos().x + size + 1, this->getPos().y + size);
        g->setColor(0xffffffff);
        g->drawLine(this->getPos().x, this->getPos().y, this->getPos().x + size, this->getPos().y + size);
        g->setColor(0xff000000);
        g->drawLine(this->getPos().x + size + 1, this->getPos().y, this->getPos().x + 1, this->getPos().y + size);
        g->setColor(0xffffffff);
        g->drawLine(this->getPos().x + size, this->getPos().y, this->getPos().x, this->getPos().y + size);
    }
}

void UIModSelectorModButton::update(CBaseUIEventCtx &c) {
    if(!this->bVisible) return;
    CBaseUIButton::update(c);

    // handle tooltips
    if(this->isMouseInside() && this->bAvailable && this->states.size() > 0 && !this->bFocusStolenDelay) {
        ui->getTooltipOverlay()->begin();
        for(const auto &tooltipTextLine : this->states[this->iState].tooltipTextLines) {
            ui->getTooltipOverlay()->addLine(tooltipTextLine);
        }
        ui->getTooltipOverlay()->end();
    }

    this->bFocusStolenDelay = false;
}

void UIModSelectorModButton::resetState() {
    this->setOn(false, true);
    this->setState(0);
}

void UIModSelectorModButton::onClicked(bool /*left*/, bool /*right*/) {
    if(!this->bAvailable) return;
    using namespace flags::operators;

    // increase state, wrap around, switch on and off
    if(this->bOn) {
        this->iState = (this->iState + 1) % this->states.size();

        // HACK: In multi, skip "Actual Flashlight" mod
        if(BanchoState::is_in_a_multi_room() && this->states[0].modName == US_("fl")) {
            this->iState = this->iState % this->states.size() - 1;
        }

        if(this->iState == 0) {
            this->setOn(false);
        } else {
            this->setOn(true);
        }
    } else {
        this->setOn(true);
    }

    // set new state
    this->setState(this->iState);

    if(BanchoState::is_in_a_multi_room()) {
        auto mod_flags = this->osuModSelector->getModFlags();
        for(auto &slot : BanchoState::room.slots) {
            if(slot.player_id != BanchoState::get_uid()) continue;
            slot.mods = mod_flags;
            if(BanchoState::room.is_host()) {
                BanchoState::room.mods = mod_flags;
                if(BanchoState::room.freemods) {
                    // When freemod is enabled, we only want to force DT, HT, or Target.
                    BanchoState::room.mods &= LegacyFlags::DoubleTime | LegacyFlags::HalfTime | LegacyFlags::Target;
                }
            }
        }

        Packet packet;
        packet.id = OUTP_MATCH_CHANGE_MODS;
        packet.write<LegacyFlags>(mod_flags);
        BANCHO::Net::send_packet(packet);

        // Don't wait on server response to update UI
        ui->getRoom()->on_room_updated(BanchoState::room);
    }

    if(BanchoState::is_online()) {
        RichPresence::updateBanchoMods();
    }
}

void UIModSelectorModButton::onFocusStolen() {
    CBaseUIButton::onFocusStolen();

    this->bMouseInside = false;
    this->bFocusStolenDelay = true;
}

void UIModSelectorModButton::setBaseScale(float xScale, float yScale) {
    this->vBaseScale.x = xScale;
    this->vBaseScale.y = yScale;
    this->vScale = this->vBaseScale;

    if(this->bOn) {
        this->vScale = this->vBaseScale * this->fEnabledScaleMultiplier;
        this->fRot = this->fEnabledRotationDeg;
    }
}

void UIModSelectorModButton::setOn(bool on, bool silent) {
    if(!this->bAvailable) return;

    bool prevState = this->bOn;
    this->bOn = on;

    // Disable all states except current
    for(int i = 0; i < this->states.size(); i++) {
        if(i == this->iState) {
            if(this->states[i].cvar->getBool() != on) {
                this->states[i].cvar->setValue(on);
            }
        } else {
            if(this->states[i].cvar->getBool()) {
                this->states[i].cvar->setValue(false);
            }
        }
    }

    if(silent) {
        // set values directly, without animation
        anim::deleteExistingAnimation(&this->fRot);
        anim::deleteExistingAnimation(&this->vScale.x);
        anim::deleteExistingAnimation(&this->vScale.y);

        if(this->bOn) {
            this->fRot = this->fEnabledRotationDeg;
            this->vScale.x = this->vBaseScale.x * this->fEnabledScaleMultiplier;
            this->vScale.y = this->vBaseScale.y * this->fEnabledScaleMultiplier;
        } else {
            this->fRot = 0.0f;
            this->vScale = this->vBaseScale;
        }
        // return early
        return;
    }

    // FIXME: called in (???):
    // BeatmapInterface::start
    // ModSelector::enableModsFromFlags
    // ModSelector::onOverrideSliderChange
    // ModSelector::onOverrideSliderLockChange
    // ModSelector::onCheckboxChange
    // Replay::Mods::use
    // OptionsOverlayImpl::onModChangingToggle
    osu->updateMods();

    constexpr float animationDuration = 0.05f;

    if(this->bOn) {
        if(prevState) {
            // swap effect
            constexpr float swapDurationMultiplier = 0.65f;
            anim::moveLinear(&this->fRot, 0.0f, animationDuration * swapDurationMultiplier, true);
            anim::moveLinear(&this->vScale.x, this->vBaseScale.x, animationDuration * swapDurationMultiplier, true);
            anim::moveLinear(&this->vScale.y, this->vBaseScale.y, animationDuration * swapDurationMultiplier, true);

            anim::moveLinear(&this->fRot, this->fEnabledRotationDeg, animationDuration * swapDurationMultiplier,
                             animationDuration * swapDurationMultiplier, false);
            anim::moveLinear(&this->vScale.x, this->vBaseScale.x * this->fEnabledScaleMultiplier,
                             animationDuration * swapDurationMultiplier, animationDuration * swapDurationMultiplier,
                             false);
            anim::moveLinear(&this->vScale.y, this->vBaseScale.y * this->fEnabledScaleMultiplier,
                             animationDuration * swapDurationMultiplier, animationDuration * swapDurationMultiplier,
                             false);
        } else {
            anim::moveLinear(&this->fRot, this->fEnabledRotationDeg, animationDuration, true);
            anim::moveLinear(&this->vScale.x, this->vBaseScale.x * this->fEnabledScaleMultiplier, animationDuration,
                             true);
            anim::moveLinear(&this->vScale.y, this->vBaseScale.y * this->fEnabledScaleMultiplier, animationDuration,
                             true);
        }

        soundEngine->play(osu->getSkin()->s_check_on);
    } else {
        anim::moveLinear(&this->fRot, 0.0f, animationDuration, true);
        anim::moveLinear(&this->vScale.x, this->vBaseScale.x, animationDuration, true);
        anim::moveLinear(&this->vScale.y, this->vBaseScale.y, animationDuration, true);

        if(prevState) {
            soundEngine->play(osu->getSkin()->s_check_off);
        }
    }
}

void UIModSelectorModButton::setState(int state) {
    this->iState = state;

    // update image
    if(this->iState < this->states.size() && this->states[this->iState].skinImageMember != nullptr) {
        this->activeSkinImageMember = this->states[this->iState].skinImageMember;
    }

    // FIXME: needing to call a bunch of functions from all over the place just to keep
    // mod selection consistent across all UI is really psychotic
    this->osuModSelector->updateScoreMultiplierLabelText();
    this->osuModSelector->updateOverrideSliderLabels();
}

void UIModSelectorModButton::setState(unsigned int state, bool initialState, ConVar *cvar, UString modName,
                                      const UString &tooltipText, SkinImageSkinMember skinMember) {
    // dynamically add new state
    while(this->states.size() < state + 1) {
        STATE t{};
        t.skinImageMember = nullptr;
        this->states.push_back(t);
    }
    this->states[state].cvar = cvar;
    this->states[state].modName = std::move(modName);
    this->states[state].tooltipTextLines = tooltipText.split(US_("\n"));
    this->states[state].skinImageMember = skinMember;

    // set initial state image
    if(this->states.size() == 1)
        this->activeSkinImageMember = this->states[0].skinImageMember;
    else if(this->iState > -1 && this->iState < this->states.size())  // update current state image
        this->activeSkinImageMember = this->states[this->iState].skinImageMember;

    // set initial state on (but without firing callbacks)
    if(initialState) {
        this->setState(state);
        this->setOn(true, true);
    }
}

const UString &UIModSelectorModButton::getActiveModName() const {
    if(this->states.size() > 0 && this->iState < this->states.size())
        return this->states[this->iState].modName;
    else
        return CBaseUIElement::emptyUString;
}
