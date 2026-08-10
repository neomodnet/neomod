#include "VolumeOverlay.h"

#include "Osu.h"
#include "OsuConVars.h"
#include "MakeDelegateWrapper.h"
#include "AnimationHandler.h"
#include "UIVolumeSlider.h"
#include "Engine.h"
#include "Sound.h"
#include "SoundEngine.h"
#include "Graphics.h"
#include "Mouse.h"
#include "Keyboard.h"
#include "OsuKeyBinds.h"
#include "KeyBindings.h"
#include "OptionsOverlay.h"
#include "UIContextMenu.h"
#include "CBaseUIDispatch.h"
#include "Environment.h"
#include "BeatmapInterface.h"
#include "Skin.h"

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

    // the dispatch fall-through wheel sink: offered whatever no hit candidate consumed
    // (unregistered automatically via the element dtor's onElementDestroyed report)
    CBaseUIDispatch::setWheelSink(this);
}

VolumeOverlay::~VolumeOverlay() {
    cv::volume_master.reset();
    cv::volume_effects.reset();
    cv::volume_music.reset();
    cv::hud_volume_size_multiplier.reset();
}

void VolumeOverlay::animate() {
    const bool active = this->fVolumeChangeTime > engine->getTime();
    this->fVolumeChangeTime = engine->getTime() + cv::hud_volume_duration.getFloat() + 0.2f;

    if(!active) {
        this->fVolumeChangeFade = 0.0f;
        this->fVolumeChangeFade.set(1.0f, 0.15f, anim::QuadOut);
    } else
        this->fVolumeChangeFade.set(1.0f, 0.1f * (1.0f - this->fVolumeChangeFade), anim::QuadOut);

    this->fVolumeChangeFade.append(0.0f, 0.20f, anim::QuadOut, cv::hud_volume_duration.getFloat());
    this->fLastVolume.set(cv::volume_master.getFloat(), 0.15f, anim::QuadOut);
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

void VolumeOverlay::tick() {
    UIScreen::tick();

    this->volumeMaster->setEnabled(this->fVolumeChangeTime > engine->getTime());
    this->volumeEffects->setEnabled(this->volumeMaster->isEnabled());
    this->volumeMusic->setEnabled(this->volumeMaster->isEnabled());
    this->volumeSliderOverlayContainer->setSize(osu->getVirtScreenSize());
    this->volumeSliderOverlayContainer->tick();

    if(!this->volumeMaster->isBusy()) {
        if(this->volumeMaster->getFloat() != cv::volume_master.getFloat()) {
            this->volumeMaster->setValue(cv::volume_master.getFloat(), false);
        }
    } else {
        if(cv::volume_master.getFloat() != this->volumeMaster->getFloat()) {
            cv::volume_master.setValue(this->volumeMaster->getFloat());
        }
        this->fLastVolume = this->volumeMaster->getFloat();
    }

    if(!this->volumeMusic->isBusy()) {
        if(this->volumeMusic->getFloat() != cv::volume_music.getFloat()) {
            this->volumeMusic->setValue(cv::volume_music.getFloat(), false);
        }
    } else {
        if(cv::volume_music.getFloat() != this->volumeMusic->getFloat()) {
            cv::volume_music.setValue(this->volumeMusic->getFloat());
        }
    }

    if(!this->volumeEffects->isBusy()) {
        if(this->volumeEffects->getFloat() != cv::volume_effects.getFloat()) {
            this->volumeEffects->setValue(cv::volume_effects.getFloat(), false);
        }
    } else {
        if(cv::volume_effects.getFloat() != this->volumeEffects->getFloat()) {
            cv::volume_effects.setValue(this->volumeEffects->getFloat());
        }
    }

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

        const auto &elements = this->volumeSliderOverlayContainer->getElementsAs<UIVolumeSlider>();
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
}

void VolumeOverlay::updateInput(CBaseUIEventCtx &c) {
    // while the user is on the visible sliders (hover/drag), the overlay claims the wheel
    // outright: continuous adjustment must beat the scroll surfaces beneath (the sliders
    // themselves decline it, setAllowMouseWheel(false))
    if(this->isBusy()) c.addWheelClaim(this);

    this->volumeSliderOverlayContainer->updateInput(c);
}

bool VolumeOverlay::onWheel(int deltaVertical, int /*deltaHorizontal*/) {
    const int notches = deltaVertical / 120;
    if(notches == 0) return false;

    // the global gates that survived canChangeVolume's screen enumeration (exclusivity is
    // structural now: this only runs when no candidate consumed the wheel, or as the
    // hovered-slider claim): on the sliders or with alt held, always allow; otherwise
    // respect the play-mode mousewheel disable and ignore out-of-screen wheel. the disable
    // only applies to ACTIVE gameplay, not while paused (you're not aiming, so let the wheel
    // adjust volume - the pause menu has no wheel use of its own)
    if(!this->isBusy() && !keyboard->isAltDown()) {
        if(osu->isInPlayModeAndNotPaused() && cv::disable_mousewheel.getBool()) return false;
        if(!osu->getVirtScreenRect().contains(mouse->getPos())) return false;
    }

    if(notches > 0) {
        this->volumeUp(notches);
    } else {
        this->volumeDown(-notches);
    }
    return true;
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

// regular screen walk-ordered keydown handler
void VolumeOverlay::onKeyDown(KeyboardEvent &key) {
    if(key == KEY_MUTE) {
        osu->getMapInterface()->pausePreviewMusic(true);
        key.consume();
        return;
    }

    const bool volIncreaseIsUp = binds::INCREASE_VOLUME == KEY_UP;
    const bool volDecreaseIsDown = binds::DECREASE_VOLUME == KEY_DOWN;
    if(key == KEY_VOLUMEUP || (key == binds::INCREASE_VOLUME && (!volIncreaseIsUp || this->isVisible()))) {
        this->volumeUp();
        key.consume();
    } else if(key == KEY_VOLUMEDOWN || (key == binds::DECREASE_VOLUME && (!volDecreaseIsDown || this->isVisible()))) {
        this->volumeDown();
        key.consume();
    }
}

// the global arrow-volume key sink
void VolumeOverlay::onArrowVolumeFallback(KeyboardEvent &key) {
    if(!this->canChangeVolume()) return;

    if(key == binds::INCREASE_VOLUME) {
        this->volumeUp();
        key.consume();
    } else if(key == binds::DECREASE_VOLUME) {
        this->volumeDown();
        key.consume();
    } else if(this->isVisible() && key == KEY_LEFT) {
        const auto &elements = this->volumeSliderOverlayContainer->getElementsAs<UIVolumeSlider>();
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
    } else if(this->isVisible() && key == KEY_RIGHT) {
        const auto &elements = this->volumeSliderOverlayContainer->getElementsAs<UIVolumeSlider>();
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

bool VolumeOverlay::isBusy() {
    return (this->volumeMaster->isEnabled() && (this->volumeMaster->isBusy() || this->volumeMaster->isMouseInside())) ||
           (this->volumeEffects->isEnabled() &&
            (this->volumeEffects->isBusy() || this->volumeEffects->isMouseInside())) ||
           (this->volumeMusic->isEnabled() && (this->volumeMusic->isBusy() || this->volumeMusic->isMouseInside()));
}

bool VolumeOverlay::isVisible() { return engine->getTime() < this->fVolumeChangeTime; }

// the play-mode / on-screen gate shared by both volume-input paths (onWheel and the arrow
// fallback): may the volume gesture act right now? exclusivity vs other consumers is structural
// (the wheel sink and onArrowVolumeFallback only run when nothing else consumed), so there is no
// screen enumeration here anymore.
bool VolumeOverlay::canChangeVolume() {
    if(this->isBusy() || keyboard->isAltDown()) return true;

    // no volume gesture during active play, only while paused (you're not aiming then), mirroring onWheel
    if(osu->isInPlayModeAndNotPaused() && cv::disable_mousewheel.getBool()) return false;
    if(!osu->getVirtScreenRect().contains(mouse->getPos())) return false;

    return true;
}

void VolumeOverlay::gainFocus() {
    if(soundEngine->hasExclusiveOutput()) return;

    this->fVolumeInactiveToActiveAnim = 0.0f;
    this->fVolumeInactiveToActiveAnim.set(1.0f, 0.3f, anim::Linear, 0.1f);
}

void VolumeOverlay::loseFocus() {
    if(soundEngine->hasExclusiveOutput()) return;

    this->bVolumeInactiveToActiveScheduled = true;
    this->fVolumeInactiveToActiveAnim.stop();
    this->fVolumeInactiveToActiveAnim = 0.0f;
    soundEngine->setMasterVolume(cv::volume_master_inactive.getFloat() * cv::volume_master.getFloat());
}

void VolumeOverlay::onVolumeChange(int multiplier) {
    // sanity reset
    this->bVolumeInactiveToActiveScheduled = false;
    this->fVolumeInactiveToActiveAnim.stop();
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
