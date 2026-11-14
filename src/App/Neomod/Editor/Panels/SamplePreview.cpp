// Copyright (c) 2026, WH, All rights reserved.
#include "SamplePreview.h"

#include "OsuConVars.h"
#include "ResourceManager.h"
#include "Sound.h"
#include "SoundEngine.h"

SamplePreview::~SamplePreview() { this->play({}); }

void SamplePreview::play(const std::string &path) {
    if(this->sound) {
        soundEngine->stop(this->sound);
        resourceManager->destroyResource(this->sound);
        this->sound = nullptr;
    }
    if(path.empty()) return;
    this->sound = resourceManager->loadSoundAbs(path, "", false, false, false);
    if(!this->sound) return;
    this->sound->setBaseVolume(cv::volume_effects.getFloat());
    soundEngine->play(this->sound);
}
