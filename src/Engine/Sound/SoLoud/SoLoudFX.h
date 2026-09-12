#pragma once
// Copyright (c) 2025, WH, All rights reserved.
#ifndef SOLOUD_FX_H
#define SOLOUD_FX_H

#include "config.h"

#ifdef MCENGINE_FEATURE_SOLOUD

#include "noinclude.h"
#include "SyncMutex.h"

#include "soloud.h"
#include "soloud_audiosource.h"

#include <atomic>
#include <memory>

namespace soundtouch
{
class SoundTouch;
typedef float SAMPLETYPE;
} // namespace soundtouch

namespace SoLoud
{
class WavStream;
class File;
class DiskFile;
class SLFXStream;
class SoundTouchFilterInstance;

/**
 * SoundTouchFilterInstance - instance of the SoundTouch filter that processes audio
 *
 * handles the audio processing through SoundTouch, incl. converting between
 * SoLoud's non-interleaved format and SoundTouch's interleaved format.
 * includes special handling for OGG files to maintain frame integrity.
 */
class SoundTouchFilterInstance : public AudioSourceInstance
{
	friend class SLFXStream;
	NOCOPY_NOMOVE(SoundTouchFilterInstance)

public:
	SoundTouchFilterInstance(SLFXStream *aParent);
	~SoundTouchFilterInstance() override;

	// core audio processing method
	unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize) override;

	// playback state management
	bool hasEnded() override;
	result seek(time aSeconds, float *mScratch, unsigned int mScratchSize) override;
	result rewind() override;

	// accurate position tracking
	[[nodiscard]] time getInternalLatency() const;

private:
	// buffer management
	void ensureBufferSize(unsigned int samples);
	void ensureInterleavedBufferSize(unsigned int samples);

	// feed SoundTouch using fixed-size chunks, return how many samples are in soundtouch after
	unsigned int feedSoundTouch(unsigned int targetBufferLevel, bool logThis);

	// buffer synchronization and positioning
	void reSynchronize();
	void updateSTLatency();

	void requestSettingUpdate(float speed, float pitch);
	// apply a requested speed/pitch change to SoundTouch (only with the audio mutex held, i.e. from getAudio or a seek)
	void applySettingUpdate();

	// member variables
	SLFXStream *mParent;                                  // parent filter
	std::unique_ptr<AudioSourceInstance> mSourceInstance; // source instance to process
	std::unique_ptr<soundtouch::SoundTouch> mSoundTouch;  // soundtouch processor

	// soundtouch setting cache for calculting offset trailing behind source stream
	unsigned int mSTInitialLatency;
	unsigned int mSTOutputSequence;
	unsigned int mSTInputSequence;

	// this is derived from the above, it doesn't change very often so it makes sense to keep it cached as well
	std::atomic<double> mSTLatencySeconds;

	float mSoundTouchSpeed;
	float mSoundTouchPitch;

	// to be used to signal the instance that the soundtouch parameters need to be adjusted at the next best time
	bool mNeedsSettingUpdate;
	Sync::mutex mSettingUpdateMutex;

	// buffers for format conversion
	SoLoud::AlignedFloatBuffer mBuffer; // temporary read buffer from source (non-interleaved)
	unsigned int mBufferSize;           // size in samples

	SoLoud::AlignedFloatBuffer mInterleavedBuffer; // interleaved audio buffer for SoundTouch
	unsigned int mInterleavedBufferSize;           // size in samples

	// debugging and tracking
	unsigned int mProcessingCounter; // counter for logspam avoidance
};

/**
 * SLFXStream - streaming audio source with built-in SoundTouch processing
 *
 * combines WavStream functionality with SoundTouch time-stretching and pitch-shifting.
 * provides the same loading interface as WavStream but with additional methods for
 * independent control of playback speed and pitch.
 */
class SLFXStream : public AudioSource
{
	NOCOPY_NOMOVE(SLFXStream)
public:
	SLFXStream(bool preferFFmpeg = false);
	~SLFXStream() override;

	// SoundTouch control interface
	void setSpeedFactor(float aSpeed); // 1.0 = normal, 2.0 = double speed, etc.
	void setPitchFactor(float aPitch); // 1.0 = normal, 2.0 = one octave up, 0.5 = one octave down

	[[nodiscard]] float getSpeedFactor() const;
	[[nodiscard]] float getPitchFactor() const;

	// AudioSource interface
	AudioSourceInstance *createInstance() override;

	// WavStream-compatible loading interface
	result load(const char *aFilename);
	result loadMem(const unsigned char *aData, unsigned int aDataLen, bool aCopy = false, bool aTakeOwnership = true);
	result loadToMem(const char *aFilename);
	result loadFile(File *aFile);
	result loadFileToMem(File *aFile);

	// utility methods
	double getLength();
	std::string getDecoder();

	// accurate position access for active instance
	time getInternalLatency() const;

protected:
	friend class SoundTouchFilterInstance;

	float mSpeedFactor; // current speed factor (tempo)
	float mPitchFactor; // current pitch factor

	// internal audio stream
	std::unique_ptr<WavStream> mSource;

	// track the active instance for position queries
	mutable std::atomic<SoundTouchFilterInstance *> mActiveInstance;

private:
	// to use as a wrapper for loading with unicode paths on Windows,
	// since SoLoud's API doesn't support that natively
	std::unique_ptr<DiskFile> mpDiskFile{nullptr};
};

} // namespace SoLoud

#endif // MCENGINE_FEATURE_SOLOUD
#endif // SOLOUD_FX_H
