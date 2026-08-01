#pragma once

#include "Sound.h"
#ifdef MCENGINE_FEATURE_BASS

class BassSoundEngine;

class BassSound final : public Sound {
    NOCOPY_NOMOVE(BassSound);
    friend class BassSoundEngine;

   public:
    BassSound(std::string filepath, bool stream, bool overlayable, bool loop)
        : Sound(std::move(filepath), stream, overlayable, loop) {};
    ~BassSound() override { this->destroy(); }

    void setPositionUS(u64 us) override;

    void setSpeed(f32 speed) override;
    void setPitch(f32 pitch) override;
    void setFrequency(float frequency) override;
    void setPan(float pan) override;
    void setLoop(bool loop) override;

    u64 getPositionUS() const override;
    u64 getLengthUS() const override;
    float getFrequency() const override;

    bool isPlaying() const override;
    bool isFinished() const override;

    // inspection
    SOUND_TYPE(BassSound, BASS, Sound)

   protected:
    void init() override;
    void initAsync() override;
    void destroy() override;

    void setHandleVolume(SOUNDHANDLE handle, float volume) override;
    [[nodiscard]] bool isHandleValid(SOUNDHANDLE queryHandle) const override;

   private:
    SOUNDHANDLE getNewHandle();

    u64 paused_position_us{0};
    u32 lengthUS{0};

    SOUNDHANDLE srchandle{0};
};

#endif
