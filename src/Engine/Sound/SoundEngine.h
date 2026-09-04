#pragma once
// Copyright (c) 2014, PG, All rights reserved.
#include "types.h"
#include "noinclude.h"

#include "Delegate.h"

#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <array>

#define SOUND_ENGINE_TYPE(ClassName, TypeID, ParentClass)               \
    static constexpr TypeId TYPE_ID = TypeID;                           \
    [[nodiscard]] TypeId getTypeId() const override { return TYPE_ID; } \
    [[nodiscard]] bool isTypeOf(TypeId typeId) const override {         \
        return typeId == TYPE_ID || ParentClass::isTypeOf(typeId);      \
    }

class Sound;
using SOUNDHANDLE = uint32_t;

struct ASIOBufferLimits {
    long minSize{-1};
    long maxSize{-1};
    long preferredSize{-1};
    long granularity{-1};
};

class SoundEngine {
    NOCOPY_NOMOVE(SoundEngine)

    friend class Sound;

   public:
    enum class OutputDriver : uint8_t {
        NONE,
        BASS,         // directsound/wasapi non-exclusive mode/alsa
        BASS_WASAPI,  // exclusive mode
        BASS_ASIO,    // exclusive move
        SOLOUD_MA,    // miniaudio (which has an assortment of output backends internally)
        SOLOUD_SDL    // SDL3 (ditto, multiple output backends internally)
    };

   protected:
    struct OUTPUT_DEVICE {
        int id{-1};
        bool isInit{false};
        bool enabled{true};
        bool isDefault{false};
        std::string name{"Default"};
        OutputDriver driver{OutputDriver::NONE};
    };

   public:
    using TypeId = uint8_t;
    enum SndEngineType : TypeId { BASS, SOLOUD, MAX };

    SoundEngine() = default;
    virtual ~SoundEngine() { this->restartCBs = {}; }

    // Factory method to create the appropriate sound engine
    static SoundEngine *initialize();
    // checked on startup by engine
    [[nodiscard]] inline bool succeeded() const { return this->bInitSuccess; }

    // Sound* object factory (based on active backend)
    virtual Sound *createSound(std::string filepath, bool stream, bool overlayable, bool loop) = 0;

    virtual void restart() = 0;
    virtual void shutdown() { ; }
    virtual void update() { ; }
    virtual void onFocusGained() = 0;
    virtual void onFocusLost() = 0;

    // Here, 'volume' means the volume for this play() call, NOT for the sound itself
    // e.g. when calling setVolume(), you're applying a modifier to all currently playing samples of that sound
    virtual bool play(Sound *snd, f32 pan = 0.f, f32 pitch = 0.f, f32 playVolume = 1.f, bool startPaused = false) = 0;

    // Get a sound ready for playback, but don't start it yet.
    inline bool enqueue(Sound *snd, f32 pan = 0.f, f32 pitch = 0.f, f32 playVolume = 1.f) {
        return this->play(snd, pan, pitch, playVolume, true);
    }

    virtual void pause(Sound *snd) = 0;
    virtual void stop(Sound *snd) = 0;

    virtual bool isReady() = 0;
    virtual bool hasExclusiveOutput() { return false; }

    virtual bool isASIO() { return false; }
    virtual ASIOBufferLimits getASIOBufferLimits() = 0;
    virtual void openControlPanel() = 0;

    using ASIOBufferChangeCallback = std::function<void(const ASIOBufferLimits &info)>;
    inline void setOnASIOBufferChangeCB(ASIOBufferChangeCallback cb) { this->asio_buffer_change_cb = std::move(cb); }

    virtual void setOutputDevice(const OUTPUT_DEVICE &device) = 0;
    virtual void setMasterVolume(float volume) = 0;

    OUTPUT_DEVICE getDefaultDevice();
    OUTPUT_DEVICE getWantedDevice();
    std::vector<OUTPUT_DEVICE> getOutputDevices();

    virtual void updateOutputDevices(bool printInfo) = 0;
    virtual bool initializeOutputDevice(const OUTPUT_DEVICE &device) = 0;

    virtual void onFreqChanged(float /* oldValue */, float /* newValue */) { ; }
    virtual void onParamChanged(float /* oldValue */, float /* newValue */) { ; }

    using AudioOutputChangedCallback = SA::delegate<void()>;
    inline void setDeviceChangeBeforeCallback(const AudioOutputChangedCallback &callback) {
        this->restartCBs[0] = callback;
    }
    inline void setDeviceChangeAfterCallback(const AudioOutputChangedCallback &callback) {
        this->restartCBs[1] = callback;
    }

    // call this once app init is done, i.e. configs are read, so convar callbacks aren't spuriously fired during init
    virtual void allowInternalCallbacks() { ; }

    [[nodiscard]] inline std::string_view getOutputDeviceName() const { return this->currentOutputDevice.name; }
    [[nodiscard]] constexpr auto getOutputDriverType() const { return this->currentOutputDevice.driver; }
    [[nodiscard]] constexpr float getVolume() const { return this->fMasterVolume; }

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

    // clamp a wanted buffer size according to device limits
    float ASIO_Clamp(long minSize, long maxSize, long defaultSize, long granularity, long wantedSize);

   protected:
    std::vector<OUTPUT_DEVICE> outputDevices;
    OUTPUT_DEVICE currentOutputDevice;

    float fMasterVolume{1.0f};

    std::array<AudioOutputChangedCallback, 2> restartCBs;  // first to exec before restart, second to exec after restart
    bool bInitSuccess{false};

    ASIOBufferChangeCallback asio_buffer_change_cb{nullptr};
};

// define/managed in Engine.cpp, declared here for convenience
extern SoundEngine *soundEngine;
