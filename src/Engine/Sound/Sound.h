#pragma once
// Copyright (c) 2014, PG, All rights reserved.

#include "Resource.h"
#include "PlaybackInterpolator.h"

#include <unordered_map>
#include <cmath>
#include <algorithm>

#define SOUND_TYPE(ClassName, TypeID, ParentClass)                      \
    static constexpr TypeId TYPE_ID = TypeID;                           \
    [[nodiscard]] TypeId getTypeId() const override { return TYPE_ID; } \
    [[nodiscard]] bool isTypeOf(TypeId typeId) const override {         \
        return typeId == TYPE_ID || ParentClass::isTypeOf(typeId);      \
    }

class SoundEngine;

using SOUNDHANDLE = uint32_t;

struct PlaybackParams {
    float pan{0.f};
    float pitch{0.f};
    float volume{1.f};
};

class Sound : public Resource {
    friend class SoundEngine;

   public:
    using TypeId = uint8_t;
    enum SndType : TypeId { BASS, SOLOUD };

   public:
    Sound(std::string filepath, bool stream, bool overlayable, bool loop)
        : Resource(SOUND, std::move(filepath),
                   /*doFilesystemExistenceCheck=*/false),  // we check filesystem status in async load
          bStream(stream),
          bIsLooped(loop),
          bIsOverlayable(overlayable) {
        this->activeHandleCache.reserve(5);
    }

    // rebuild with a new path (or reload with the same path)
    void rebuild(std::string_view newFilePath = "", bool async = false);

    virtual void setPositionUS(u64 us) = 0;
    inline void setPositionMS(u32 ms) { return this->setPositionUS(ms * 1000ULL); };
    inline void setPositionS(f64 secs) {
        return this->setPositionUS(static_cast<u64>(std::round(secs * (1000. * 1000.))));
    };

    virtual void setSpeed(float speed) = 0;
    virtual void setPitch(float pitch) { this->fPitch = pitch; }
    virtual void setFrequency(float frequency) = 0;
    virtual void setPan(float pan) = 0;
    virtual void setLoop(bool loop) = 0;

    // NOTE: this will also update currently playing handle(s) for this sound
    void setBaseVolume(float volume);
    [[nodiscard]] constexpr float getBaseVolume() const { return this->fBaseVolume; }

    [[nodiscard]] f64 getPositionPct() const { return std::clamp<f64>(getPositionS() / getLengthS(), 0.0f, 1.0f); }

    virtual u64 getPositionUS() const = 0;
    inline u32 getPositionMS() const { return (this->getPositionUS() + 500) / 1000; }
    inline f64 getPositionS() const { return static_cast<f64>(this->getPositionUS()) / (1000. * 1000.); }

    virtual u64 getLengthUS() const = 0;
    inline u32 getLengthMS() const { return (this->getLengthUS() + 500) / 1000; }
    inline f64 getLengthS() const { return static_cast<f64>(this->getLengthUS()) / (1000. * 1000.); }

    virtual float getFrequency() const = 0;
    virtual float getPan() const { return this->fPan; }
    virtual float getSpeed() const { return this->fSpeed; }
    virtual float getPitch() const { return this->fPitch; }
    virtual i32 getRateBasedStreamDelayMS() const { return 0; }  // backend dependent

    virtual bool isPlaying() const = 0;
    virtual bool isFinished() const = 0;

    [[nodiscard]] constexpr bool isStream() const { return this->bStream; }
    [[nodiscard]] constexpr bool isLooped() const { return this->bIsLooped; }
    [[nodiscard]] constexpr bool isOverlayable() const { return this->bIsOverlayable; }

    Sound *asSound() final { return this; }
    [[nodiscard]] const Sound *asSound() const final { return this; }

    // type inspection
    [[nodiscard]] virtual TypeId getTypeId() const = 0;
    [[nodiscard]] virtual bool isTypeOf(TypeId /*type_id*/) const { return false; }
    template <typename T>
    [[nodiscard]] bool isType() const {
        return isTypeOf(T::TYPE_ID);
    }
    template <typename T>
    T *as() {
        return isType<T>() ? static_cast<T *>(this) : nullptr;
    }
    template <typename T>
    const T *as() const {
        return isType<T>() ? static_cast<const T *>(this) : nullptr;
    }

   protected:
    void init() override = 0;
    void initAsync() override;
    void destroy() override = 0;

    inline void setLastPlayTime(f64 lastPlayTime) { this->fLastPlayTime = lastPlayTime; }
    [[nodiscard]] constexpr f64 getLastPlayTime() const { return this->fLastPlayTime; }

    // backend-specific query
    virtual bool isHandleValid(SOUNDHANDLE queryHandle) const = 0;
    // backend-specific setter
    virtual void setHandleVolume(SOUNDHANDLE handle, float volume) = 0;

    // currently playing sound instances (updates cache)
    const std::unordered_map<SOUNDHANDLE, PlaybackParams> &getActiveHandles();
    void addActiveInstance(SOUNDHANDLE handle, PlaybackParams instance);

    mutable PlaybackInterpolator interpolator;

    std::unordered_map<SOUNDHANDLE, PlaybackParams> activeHandleCache;

    // so that we don't change the filepath for a possibly currently-async-loading file when rebuilding, and only set it on the next load
    std::string sRebuildFilePath;

    f64 fLastPlayTime{0.0};

    float fPan{0.0f};
    float fSpeed{1.0f};
    float fPitch{1.0f};

    // persistent across all plays for the sound object, only modifiable by setBaseVolume
    float fBaseVolume{1.0f};

    bool bStream;
    bool bIsLooped;
    bool bIsOverlayable;

    bool bIgnored{false};  // early check for audio file validity
    bool bStarted{false};
    bool bPaused{false};

   private:
    static bool isValidAudioFile(std::string_view filePath, std::string_view fileExt);
};
