#include "VolumeOverlay.h"

#include "AnimationHandler.h"
#include "BeatmapInterface.h"
#include "CBaseUIContainer.h"
#include "Changelog.h"
#include "Chat.h"
#include "Environment.h"
#include "OsuConVars.h"
#include "OsuDirectScreen.h"
#include "Engine.h"
#include "KeyBindings.h"
#include "Keyboard.h"
#include "ModSelector.h"
#include "Mouse.h"
#include "OptionsOverlay.h"
#include "Osu.h"
#include "Graphics.h"
#include "MakeDelegateWrapper.h"
#include "PauseOverlay.h"
#include "RankingScreen.h"
#include "Skin.h"
#include "SongBrowser/SongBrowser.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "UI.h"
#include "UIContextMenu.h"
#include "UIVolumeSlider.h"
#include "UserStatsScreen.h"

VolumeOverlay::VolumeOverlay() : UIScreen() {
    cv::volume_master.setCallback(SA::MakeDelegate<&VolumeOverlay::onMasterVolumeChange>(this));
    cv::volume_effects.setCallback(SA::MakeDelegate<&VolumeOverlay::onEffectVolumeChange>(this));
    cv::volume_music.setCallback(SA::MakeDelegate<&VolumeOverlay::onMusicVolumeChange>(this));
    cv::hud_volume_size_multiplier.setCallback(SA::MakeDelegate<&VolumeOverlay::updateLayout>(this));

    this->fVolumeChangeTime = 0.0f;
    this->fVolumeChangeFade = 1.0f;
    this->fLastVolume = cv::volume_master.getFloat();
    this->volumeSliderOverlayContainer = std::make_unique<CBaseUIContainer>();

    this->volumeMaster = new UIVolumeSlider(0, 0, 450, 75, "");
    this->volumeMaster->setType(UIVolumeSlider::TYPE::MASTER);
    this->volumeMaster->setBlockSize(this->volumeMaster->getSize().y + 7, this->volumeMaster->getSize().y);
    this->volumeMaster->setAllowMouseWheel(false);
    this->volumeMaster->setAnimated(false);
    this->volumeMaster->setSelected(true);
    this->volumeSliderOverlayContainer->addBaseUIElement(this->volumeMaster);
    this->volumeEffects =
        new UIVolumeSlider(0, 0, this->volumeMaster->getSize().x, this->volumeMaster->getSize().y / 1.5f, "");
    this->volumeEffects->setType(UIVolumeSlider::TYPE::EFFECTS);
    this->volumeEffects->setBlockSize((this->volumeMaster->getSize().y + 7) / 1.5f,
                                      this->volumeMaster->getSize().y / 1.5f);
    this->volumeEffects->setAllowMouseWheel(false);
    this->volumeEffects->setKeyDelta(cv::volume_change_interval.getFloat());
    this->volumeEffects->setAnimated(false);
    this->volumeSliderOverlayContainer->addBaseUIElement(this->volumeEffects);
    this->volumeMusic =
        new UIVolumeSlider(0, 0, this->volumeMaster->getSize().x, this->volumeMaster->getSize().y / 1.5f, "");
    this->volumeMusic->setType(UIVolumeSlider::TYPE::MUSIC);
    this->volumeMusic->setBlockSize((this->volumeMaster->getSize().y + 7) / 1.5f,
                                    this->volumeMaster->getSize().y / 1.5f);
    this->volumeMusic->setAllowMouseWheel(false);
    this->volumeMusic->setKeyDelta(cv::volume_change_interval.getFloat());
    this->volumeMusic->setAnimated(false);
    this->volumeSliderOverlayContainer->addBaseUIElement(this->volumeMusic);

    soundEngine->setMasterVolume(cv::volume_master.getFloat());
    this->updateLayout();
}

void VolumeOverlay::animate() {
    const bool active = this->fVolumeChangeTime > engine->getTime();
    this->fVolumeChangeTime = engine->getTime() + cv::hud_volume_duration.getFloat() + 0.2f;

    if(!active) {
        this->fVolumeChangeFade = 0.0f;
        anim::moveQuadOut(&this->fVolumeChangeFade, 1.0f, 0.15f, true);
    } else
        anim::moveQuadOut(&this->fVolumeChangeFade, 1.0f, 0.1f * (1.0f - this->fVolumeChangeFade), true);

    anim::moveQuadOut(&this->fVolumeChangeFade, 0.0f, 0.20f, cv::hud_volume_duration.getFloat(), false);
    anim::moveQuadOut(&this->fLastVolume, cv::volume_master.getFloat(), 0.15f, 0.0f, true);
}

void VolumeOverlay::draw() {
    if(!this->isVisible()) return;

    const float dpiScale = Osu::getUIScale();
    const float sizeMultiplier = cv::hud_volume_size_multiplier.getFloat() * dpiScale;

    if(this->fVolumeChangeFade != 1.0f) {
        g->push3DScene(McRect(this->volumeMaster->getPos().x, this->volumeMaster->getPos().y,
                              this->volumeMaster->getSize().x,
                              (osu->getVirtScreenHeight() - this->volumeMaster->getPos().y) * 2.05f));
        g->rotate3DScene(-(1.0f - this->fVolumeChangeFade) * 90, 0, 0);
        g->translate3DScene(0, (this->fVolumeChangeFade * 60 - 60) * sizeMultiplier / 1.5f,
                            ((this->fVolumeChangeFade) * 500 - 500) * sizeMultiplier / 1.5f);
    }

    this->volumeMaster->setPos(osu->getVirtScreenSize() - this->volumeMaster->getSize() -
                               vec2(this->volumeMaster->getMinimumExtraTextWidth(), this->volumeMaster->getSize().y));
    this->volumeEffects->setPos(this->volumeMaster->getPos() -
                                vec2(0, this->volumeEffects->getSize().y + 20 * sizeMultiplier));
    this->volumeMusic->setPos(this->volumeEffects->getPos() -
                              vec2(0, this->volumeMusic->getSize().y + 20 * sizeMultiplier));

    this->volumeSliderOverlayContainer->draw();

    if(this->fVolumeChangeFade != 1.0f) g->pop3DScene();
}

void VolumeOverlay::update(CBaseUIEventCtx &c) {
    this->volumeMaster->setEnabled(this->fVolumeChangeTime > engine->getTime());
    this->volumeEffects->setEnabled(this->volumeMaster->isEnabled());
    this->volumeMusic->setEnabled(this->volumeMaster->isEnabled());
    this->volumeSliderOverlayContainer->setSize(osu->getVirtScreenSize());
    this->volumeSliderOverlayContainer->update(c);

    if(!this->volumeMaster->isBusy())
        this->volumeMaster->setValue(cv::volume_master.getFloat(), false);
    else {
        cv::volume_master.setValue(this->volumeMaster->getFloat());
        this->fLastVolume = this->volumeMaster->getFloat();
    }

    if(!this->volumeMusic->isBusy())
        this->volumeMusic->setValue(cv::volume_music.getFloat(), false);
    else
        cv::volume_music.setValue(this->volumeMusic->getFloat());

    if(!this->volumeEffects->isBusy())
        this->volumeEffects->setValue(cv::volume_effects.getFloat(), false);
    else
        cv::volume_effects.setValue(this->volumeEffects->getFloat());

    // force focus back to master if no longer active
    if(engine->getTime() > this->fVolumeChangeTime && !this->volumeMaster->isSelected()) {
        this->volumeMusic->setSelected(false);
        this->volumeEffects->setSelected(false);
        this->volumeMaster->setSelected(true);
    }

    // keep overlay alive as long as the user is doing something
    // switch selection if cursor moves inside one of the sliders
    if(this->volumeSliderOverlayContainer->isBusy() ||
       (this->volumeMaster->isEnabled() &&
        (this->volumeMaster->isMouseInside() || this->volumeEffects->isMouseInside() ||
         this->volumeMusic->isMouseInside()))) {
        this->animate();

        const auto &elements = this->volumeSliderOverlayContainer->getElements<UIVolumeSlider>();
        for(int i = 0; i < elements.size(); i++) {
            if(elements[i]->checkWentMouseInside()) {
                for(int c = 0; c < elements.size(); c++) {
                    if(c != i) elements[c]->setSelected(false);
                }
                elements[i]->setSelected(true);
            }
        }
    }

    // volume inactive to active animation
    if(this->bVolumeInactiveToActiveScheduled && this->fVolumeInactiveToActiveAnim > 0.0f) {
        soundEngine->setMasterVolume(std::lerp(cv::volume_master_inactive.getFloat() * cv::volume_master.getFloat(),
                                               cv::volume_master.getFloat(), this->fVolumeInactiveToActiveAnim));

        // check if we're done
        if(this->fVolumeInactiveToActiveAnim == 1.0f) this->bVolumeInactiveToActiveScheduled = false;
    }

    // scroll wheel events (should be separate from update(), but... oh well...)
    const int wheelDelta = mouse->getWheelDeltaVertical() / 120;
    if(wheelDelta != 0 && this->canChangeVolume()) {
        if(wheelDelta > 0) {
            this->volumeUp(wheelDelta);
        } else {
            this->volumeDown(-wheelDelta);
        }
    }
}

void VolumeOverlay::updateLayout() {
    const float dpiScale = Osu::getUIScale();
    const float sizeMultiplier = cv::hud_volume_size_multiplier.getFloat() * dpiScale;

    this->volumeMaster->setSize(300 * sizeMultiplier, 50 * sizeMultiplier);
    this->volumeMaster->setBlockSize(this->volumeMaster->getSize().y + 7 * dpiScale, this->volumeMaster->getSize().y);

    this->volumeEffects->setSize(this->volumeMaster->getSize().x, this->volumeMaster->getSize().y / 1.5f);
    this->volumeEffects->setBlockSize((this->volumeMaster->getSize().y + 7 * dpiScale) / 1.5f,
                                      this->volumeMaster->getSize().y / 1.5f);

    this->volumeMusic->setSize(this->volumeMaster->getSize().x, this->volumeMaster->getSize().y / 1.5f);
    this->volumeMusic->setBlockSize((this->volumeMaster->getSize().y + 7 * dpiScale) / 1.5f,
                                    this->volumeMaster->getSize().y / 1.5f);
}

void VolumeOverlay::onResolutionChange(vec2 /*newResolution*/) { this->updateLayout(); }

void VolumeOverlay::onKeyDown(KeyboardEvent &key) {
    if(key == KEY_MUTE) {
        osu->getMapInterface()->pausePreviewMusic(true);
        key.consume();
    } else {
        // avoid conflicting focus with e.g. songbrowser up/down
        const bool volIncreaseIsUp = cv::INCREASE_VOLUME.getVal<SCANCODE>() == KEY_UP;
        const bool volDecreaseIsDown = cv::DECREASE_VOLUME.getVal<SCANCODE>() == KEY_DOWN;

        if(key == KEY_VOLUMEUP || (key == cv::INCREASE_VOLUME.getVal<SCANCODE>() &&
                                   (!volIncreaseIsUp || this->isVisible() || this->canChangeVolume()))) {
            this->volumeUp();
            key.consume();
        } else if(key == KEY_VOLUMEDOWN || (key == cv::DECREASE_VOLUME.getVal<SCANCODE>() &&
                                            (!volDecreaseIsDown || this->isVisible() || this->canChangeVolume()))) {
            this->volumeDown();
            key.consume();
        }
    }

    if(this->isVisible() && this->canChangeVolume()) {
        if(key == KEY_LEFT) {
            const auto &elements = this->volumeSliderOverlayContainer->getElements<UIVolumeSlider>();
            for(int i = 0; i < elements.size(); i++) {
                if(elements[i]->isSelected()) {
                    const int nextIndex = (i == elements.size() - 1 ? 0 : i + 1);
                    elements[i]->setSelected(false);
                    elements[nextIndex]->setSelected(true);
                    break;
                }
            }
            this->animate();
            key.consume();
        } else if(key == KEY_RIGHT) {
            const auto &elements = this->volumeSliderOverlayContainer->getElements<UIVolumeSlider>();
            for(int i = 0; i < elements.size(); i++) {
                if(elements[i]->isSelected()) {
                    const int prevIndex = (i == 0 ? elements.size() - 1 : i - 1);
                    elements[i]->setSelected(false);
                    elements[prevIndex]->setSelected(true);
                    break;
                }
            }
            this->animate();
            key.consume();
        }
    }
}

bool VolumeOverlay::isBusy() {
    return (this->volumeMaster->isEnabled() && (this->volumeMaster->isBusy() || this->volumeMaster->isMouseInside())) ||
           (this->volumeEffects->isEnabled() &&
            (this->volumeEffects->isBusy() || this->volumeEffects->isMouseInside())) ||
           (this->volumeMusic->isEnabled() && (this->volumeMusic->isBusy() || this->volumeMusic->isMouseInside()));
}

bool VolumeOverlay::isVisible() { return engine->getTime() < this->fVolumeChangeTime; }

// needless to say, this is not a good way of doing things
bool VolumeOverlay::canChangeVolume() {
    const bool can_scroll =
        this->isBusy() || keyboard->isAltDown() ||                                                                //
        (                                                                                                         //
            !(osu->isInPlayMode() && cv::disable_mousewheel.getBool() && !ui->getPauseOverlay()->isVisible()) &&  //
            (osu->getVirtScreenRect().contains(mouse->getPos())) &&                                               //
            !(ui->getSongBrowser()->isVisible() && db->isFinished()) &&                                           //
            !(ui->getOsuDirectScreen()->isVisible()) &&                                                           //
            !(ui->getOptionsOverlay()->isVisible() && ui->getOptionsOverlay()->isMouseInside()) &&                //
            !(ui->getOptionsOverlay()->getContextMenu()->isVisible()) &&                                          //
            !(ui->getChangelog()->isVisible()) &&                                                                 //
            !(ui->getRankingScreen()->isVisible()) &&                                                             //
            !(ui->getModSelector()->isMouseInScrollView()) &&                                                     //
            !(ui->getChat()->isMouseInChat()) &&                                                                  //
            !(ui->getUserStatsScreen()->isVisible())                                                              //
        );

    return can_scroll;
}

void VolumeOverlay::gainFocus() {
    if(soundEngine->hasExclusiveOutput()) return;

    this->fVolumeInactiveToActiveAnim = 0.0f;
    anim::moveLinear(&this->fVolumeInactiveToActiveAnim, 1.0f, 0.3f, 0.1f, true);
}

void VolumeOverlay::loseFocus() {
    if(soundEngine->hasExclusiveOutput()) return;

    this->bVolumeInactiveToActiveScheduled = true;
    anim::deleteExistingAnimation(&this->fVolumeInactiveToActiveAnim);
    this->fVolumeInactiveToActiveAnim = 0.0f;
    soundEngine->setMasterVolume(cv::volume_master_inactive.getFloat() * cv::volume_master.getFloat());
}

void VolumeOverlay::onVolumeChange(int multiplier) {
    // sanity reset
    this->bVolumeInactiveToActiveScheduled = false;
    anim::deleteExistingAnimation(&this->fVolumeInactiveToActiveAnim);
    this->fVolumeInactiveToActiveAnim = 0.0f;

    // don't actually change volume for the first interaction, just make ourselves visible
    // e.g. scrolling 1 tick should just set visible but not change volume
    // also allow immediate change if we are changing due to a global hotkey
    if(this->isVisible() || !env->winFocused()) {
        // chose which volume to change, depending on the volume overlay, default is master
        ConVar *volumeConVar = &cv::volume_master;
        if(this->volumeMusic->isSelected())
            volumeConVar = &cv::volume_music;
        else if(this->volumeEffects->isSelected())
            volumeConVar = &cv::volume_effects;

        // change the volume
        float newVolume = std::clamp<float>(
            volumeConVar->getFloat() + cv::volume_change_interval.getFloat() * multiplier, 0.0f, 1.0f);
        volumeConVar->setValue(newVolume);
    }

    this->animate();
}

void VolumeOverlay::onMasterVolumeChange(float newValue) {
    if(this->bVolumeInactiveToActiveScheduled) return;  // not very clean, but w/e

    soundEngine->setMasterVolume(newValue);
}

void VolumeOverlay::onEffectVolumeChange() {
    if(!osu || osu->isSkinLoading()) return;
    updateEffectVolume(osu->getSkinMutable());
}

void VolumeOverlay::updateEffectVolume(Skin *skin) {
    if(!skin || !skin->isReady() || skin->sounds.empty()) return;

    float volume = cv::volume_effects.getFloat();
    for(auto &sound : skin->sounds) {
        if(skin != osu->getSkin()) break;
        if(!skin->isReady()) break;
        if(osu->isSkinLoading()) break;  // if you hold CTRL+SHIFT+S down then weird things can happen...
        if(sound && sound->getBaseVolume() != volume) sound->setBaseVolume(volume);
    }
}

void VolumeOverlay::onMusicVolumeChange() {
    auto music = osu->getMapInterface()->getMusic();
    if(music != nullptr) {
        music->setBaseVolume(osu->getMapInterface()->getIdealVolume());
    }
}
