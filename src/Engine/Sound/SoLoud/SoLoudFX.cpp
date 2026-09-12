// Copyright (c) 2025, WH, All rights reserved.
#include "SoLoudFX.h"

#ifdef MCENGINE_FEATURE_SOLOUD

#include "ConVar.h"
#include "Engine.h"
#include "Logging.h"
#include "File.h"
#include "SString.h"
#include "SyncOnce.h"

#include <algorithm>
#include <cmath>

#include "soloud_error.h"
#include "soloud_wavstream.h"
#include "soloud_file.h"

#if SOLOUD_VERSION >= 202601
#include "soloud_ll_mixing.h"
namespace SoLoud {
namespace detail = mixing;
using namespace mixing;
namespace {
forceinline void interlace_samples(void *outputBuffer, const float *const rawBuffer, unsigned int aSamples,
                                   unsigned int stride, unsigned int aChannels, detail::SAMPLE_FORMAT aFormat) {
    return Soloud::mMixer->interlace_samples(outputBuffer, rawBuffer, aSamples, stride, aChannels, aFormat);
}
}  // namespace
}  // namespace SoLoud
#else
#include "soloud_intrin.h"
#endif

#include "SoundTouch.h"

namespace cv {
#ifdef _DEBUG
namespace {
ConVar snd_st_debug("snd_st_debug", false, CLIENT | NOSAVE, "Enable detailed SoundTouch filter logging");
}
#define ST_DEBUG_ENABLED cv::snd_st_debug.getBool()
#else
#define ST_DEBUG_ENABLED 0
#endif
}  // namespace cv

#define ST_DEBUG_LOG(...)      \
    if(ST_DEBUG_ENABLED) {     \
        debugLog(__VA_ARGS__); \
    }

namespace soundtouch {
// need to define this because for some reason soundtouch doesn't give you the header?
// there's no other way to change the algorithm (default cubic)

// also we need to define the FULL class even though we only care about a single static member because of ODR...

class FIFOSampleBuffer;
class TransposerBase {
   public:
    enum ALGORITHM { LINEAR = 0, CUBIC, SHANNON };

   protected:
    virtual int transposeMono(SAMPLETYPE *dest, const SAMPLETYPE *src, int &srcSamples) = 0;
    virtual int transposeStereo(SAMPLETYPE *dest, const SAMPLETYPE *src, int &srcSamples) = 0;
    virtual int transposeMulti(SAMPLETYPE *dest, const SAMPLETYPE *src, int &srcSamples) = 0;
    static ALGORITHM algorithm;

   public:
    double rate;
    int numChannels;
    TransposerBase();
    virtual ~TransposerBase();
    virtual int transpose(FIFOSampleBuffer &dest, FIFOSampleBuffer &src);
    virtual void setRate(double newRate);
    virtual void setChannels(int channels);
    virtual int getLatency() const = 0;
    virtual void resetRegisters() = 0;
    static TransposerBase *newInstance();
    // static function to set interpolation algorithm
    static void setAlgorithm(ALGORITHM a);
};

}  // namespace soundtouch

namespace {  // static
using ALGORITHM = soundtouch::TransposerBase::ALGORITHM;

static ALGORITHM currentTransposerAlgorithm{ALGORITHM::CUBIC};

static ALGORITHM getTransposerValForString(std::string str) {
    ALGORITHM ret = currentTransposerAlgorithm;

    if(!str.empty()) {
        SString::lower_inplace(str);

        if(str.contains("linear"))
            ret = ALGORITHM::LINEAR;
        else if(str.contains("cubic"))
            ret = ALGORITHM::CUBIC;
        else if(str.contains("shannon"))
            ret = ALGORITHM::SHANNON;
    }

    return ret;
}

static Sync::once_flag transposer_callback_set;
}  // namespace

namespace SoLoud {
//-------------------------------------------------------------------------
// SLFXStream (WavStream + SoundTouch wrapper) implementation
//-------------------------------------------------------------------------

SLFXStream::SLFXStream(bool preferFFmpeg)
    : AudioSource(),
      mSpeedFactor(1.0f),
      mPitchFactor(1.0f),
      mSource(std::make_unique<WavStream>(preferFFmpeg)),
      mActiveInstance(nullptr) {
    ST_DEBUG_LOG("SoundTouchFilter: Constructor called");

    Sync::call_once(transposer_callback_set, []() -> void {
        // set initial value
        currentTransposerAlgorithm = getTransposerValForString(cv::snd_rate_transpose_algorithm.getString());
        soundtouch::TransposerBase::setAlgorithm(currentTransposerAlgorithm);

        // NOTE: using change callback so we can have a callback for BASS and SoLoud at the same time (hacky)
        cv::snd_rate_transpose_algorithm.setCallback([](std::string_view /*oldv*/, std::string_view newv) {
            currentTransposerAlgorithm = getTransposerValForString(std::string{newv});
            soundtouch::TransposerBase::setAlgorithm(currentTransposerAlgorithm);
        });
    });
}

SLFXStream::~SLFXStream() {
    ST_DEBUG_LOG("SoundTouchFilter: Destructor called");
    // stop all instances before cleanup
    stop();
}

AudioSourceInstance *SLFXStream::createInstance() {
    if(!mSource) return nullptr;

    ST_DEBUG_LOG("SoundTouchFilter: Creating instance with speed={:f}, pitch={:f}", mSpeedFactor, mPitchFactor);

    auto *instance = new SoundTouchFilterInstance(this);
    mActiveInstance.store(instance, std::memory_order_seq_cst);  // track the active instance for position queries
    return instance;
}

void SLFXStream::setSpeedFactor(float aSpeed) {
    ST_DEBUG_LOG("SoundTouchFilter: Speed changed from {:f} to {:f}", mSpeedFactor, aSpeed);
    mSpeedFactor = aSpeed;
    if(mActiveInstance.load(std::memory_order_seq_cst))
        mActiveInstance.load(std::memory_order_seq_cst)->requestSettingUpdate(mSpeedFactor, mPitchFactor);
}

void SLFXStream::setPitchFactor(float aPitch) {
    ST_DEBUG_LOG("SoundTouchFilter: Pitch changed from {:f} to {:f}", mPitchFactor, aPitch);
    mPitchFactor = aPitch;
    if(mActiveInstance.load(std::memory_order_seq_cst))
        mActiveInstance.load(std::memory_order_seq_cst)->requestSettingUpdate(mSpeedFactor, mPitchFactor);
}

float SLFXStream::getSpeedFactor() const { return mSpeedFactor; }

float SLFXStream::getPitchFactor() const { return mPitchFactor; }

time SLFXStream::getInternalLatency() const {
    if(mActiveInstance.load(std::memory_order_seq_cst))
        return mActiveInstance.load(std::memory_order_seq_cst)->getInternalLatency();
    return 0.0;
}

// WavStream-compatibility methods
result SLFXStream::load(const char *aFilename) {
    if(!mSource) return INVALID_PARAMETER;

    mpDiskFile = std::make_unique<DiskFile>(::File::fopen_c(aFilename, "rb"));

    if(!mpDiskFile.get() || !mpDiskFile->getFilePtr()) {
        mpDiskFile.reset();
        return FILE_LOAD_FAILED;
    }

    result result = mSource->loadFile(mpDiskFile.get());
    if(result == SO_NO_ERROR) {
        // copy properties from the loaded wav stream
        mChannels = mSource->mChannels;
        mBaseSamplerate = mSource->mBaseSamplerate;
        mFlags = mSource->mFlags;

        ST_DEBUG_LOG("SLFXStream: Loaded with {:d} channels at {:f} Hz", mChannels, mBaseSamplerate);
    }

    return result;
}

result SLFXStream::loadMem(const unsigned char *aData, unsigned int aDataLen, bool aCopy, bool aTakeOwnership) {
    if(!mSource) return INVALID_PARAMETER;

    result result = mSource->loadMem(aData, aDataLen, aCopy, aTakeOwnership);
    if(result == SO_NO_ERROR) {
        // copy properties from the loaded wav stream
        mChannels = mSource->mChannels;
        mBaseSamplerate = mSource->mBaseSamplerate;
        mFlags = mSource->mFlags;

        ST_DEBUG_LOG("SLFXStream: Loaded from memory with {:d} channels at {:f} Hz", mChannels, mBaseSamplerate);
    }

    return result;
}

result SLFXStream::loadToMem(const char *aFilename) {
    if(!mSource) return INVALID_PARAMETER;

    SoLoud::DiskFile df(::File::fopen_c(aFilename, "rb"));

    if(!df.getFilePtr()) {
        return FILE_LOAD_FAILED;
    }

    result result = mSource->loadFileToMem(&df);
    if(result == SO_NO_ERROR) {
        // copy properties from the loaded wav stream
        mChannels = mSource->mChannels;
        mBaseSamplerate = mSource->mBaseSamplerate;
        mFlags = mSource->mFlags;

        ST_DEBUG_LOG("SLFXStream: Loaded to memory with {:d} channels at {:f} Hz", mChannels, mBaseSamplerate);
    }

    return result;
}

result SLFXStream::loadFile(File *aFile) {
    if(!mSource) return INVALID_PARAMETER;

    result result = mSource->loadFile(aFile);
    if(result == SO_NO_ERROR) {
        // copy properties from the loaded wav stream
        mChannels = mSource->mChannels;
        mBaseSamplerate = mSource->mBaseSamplerate;
        mFlags = mSource->mFlags;

        ST_DEBUG_LOG("SLFXStream: Loaded from file with {:d} channels at {:f} Hz", mChannels, mBaseSamplerate);
    }

    return result;
}

result SLFXStream::loadFileToMem(File *aFile) {
    if(!mSource) return INVALID_PARAMETER;

    result result = mSource->loadFileToMem(aFile);
    if(result == SO_NO_ERROR) {
        // copy properties from the loaded wav stream
        mChannels = mSource->mChannels;
        mBaseSamplerate = mSource->mBaseSamplerate;
        mFlags = mSource->mFlags;

        ST_DEBUG_LOG("SLFXStream: Loaded file to memory with {:d} channels at {:f} Hz", mChannels, mBaseSamplerate);
    }

    return result;
}

double SLFXStream::getLength() { return mSource ? mSource->getLength() : 0.0; }

std::string SLFXStream::getDecoder() {
    if(!mSource) return "<NULL>";

    switch(mSource->mFiletype) {
        case WAVSTREAM_WAV:
            return "dr_wav";
        case WAVSTREAM_OGG:
            return "dr_ogg";
        case WAVSTREAM_FLAC:
            return "dr_flac";
        case WAVSTREAM_MPG123:
            return "libmpg123";
        case WAVSTREAM_DRMP3:
            return "dr_mp3";
        case WAVSTREAM_FFMPEG:
            return "ffmpeg";
        default:
            return "unknown";
    }
}

//-------------------------------------------------------------------------
// SoundTouchFilterInstance implementation
//-------------------------------------------------------------------------

SoundTouchFilterInstance::SoundTouchFilterInstance(SLFXStream *aParent)
    : AudioSourceInstance(),
      mParent(aParent),
      mSourceInstance(nullptr),
      mSoundTouch(nullptr),
      mSTInitialLatency(0),
      mSTOutputSequence(0),
      mSTInputSequence(0),
      mSTLatencySeconds(0.0),
      mSoundTouchSpeed(1.0f),
      mSoundTouchPitch(1.0f),
      mNeedsSettingUpdate(false),
      mBufferSize(0),
      mInterleavedBufferSize(0),
      mProcessingCounter(0) {
    ST_DEBUG_LOG("SoundTouchFilterInstance: Constructor called");

    // create source instance
    if(mParent && mParent->mSource) {
        mSourceInstance.reset(mParent->mSource->createInstance());

        if(mSourceInstance) {
            // initialize the source instance with the parent source
            mSourceInstance->init(*mParent->mSource, 0);

            // initialize with properties from parent filter
            mChannels = mParent->mChannels;
            mBaseSamplerate = mParent->mBaseSamplerate;
            mFlags = mParent->mFlags;
            ST_DEBUG_LOG("SoundTouchFilterInstance: Creating with {:d} channels at {:f} Hz, mFlags={:x}", mChannels,
                         mBaseSamplerate, mFlags);

            // initialize SoundTouch
            mSoundTouch = std::make_unique<soundtouch::SoundTouch>();
            if(mSoundTouch) {
                mSoundTouch->setSampleRate((uint)mBaseSamplerate);
                mSoundTouch->setChannels(mChannels);

                // quality settings pulled out of my ass, there is NO documentation for this library...
                mSoundTouch->setSetting(SETTING_USE_AA_FILTER, 1);
                mSoundTouch->setSetting(SETTING_AA_FILTER_LENGTH, 64);
                mSoundTouch->setSetting(SETTING_USE_QUICKSEEK, 0);
                mSoundTouch->setSetting(SETTING_SEQUENCE_MS, 15);  // wtf should these numbers be?
                mSoundTouch->setSetting(SETTING_SEEKWINDOW_MS, 30);
                mSoundTouch->setSetting(SETTING_OVERLAP_MS, 6);

                // set the actual speed and pitch factors
                mSoundTouch->setTempo(mParent->mSpeedFactor);
                // convert pitch factor to semitones to match BASS behavior
                float pitchSemitones = (mParent->mPitchFactor - 1.0f) * 60.0f;
                mSoundTouch->setPitchSemiTones(pitchSemitones);

                mSoundTouchSpeed = mParent->mSpeedFactor;
                mSoundTouchPitch = mParent->mPitchFactor;

                ST_DEBUG_LOG("SoundTouch: Initialized with speed={:f}, pitch={:f}", mSoundTouchSpeed, mSoundTouchPitch);
                ST_DEBUG_LOG("SoundTouch: Version: {:s}", mSoundTouch->getVersionString());

                // sync cache latency info for offset calc
                updateSTLatency();

                ST_DEBUG_LOG(
                    "SoundTouch: Initial latency: {:} samples ({:.1f}ms), Output sequence: {:} samples, Average "
                    "latency: {:.1f}ms",
                    mSTInitialLatency, (mSTInitialLatency * 1000.0f) / mBaseSamplerate, mSTOutputSequence,
                    mSTLatencySeconds * 1000.0);
            }
        }
    }
}

SoundTouchFilterInstance::~SoundTouchFilterInstance() {
    // clear the active instance reference in parent
    if(mParent) {
        auto thisptr = this;
        mParent->mActiveInstance.compare_exchange_strong(thisptr, nullptr);
    }
}

// public methods below (to be called by SoLoud)
unsigned int SoundTouchFilterInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize) {
    if(aBuffer == nullptr || mParent->mSoloud == nullptr) return 0;

    mProcessingCounter++;

    bool logThisCall = ST_DEBUG_ENABLED && (mProcessingCounter == 1 || mProcessingCounter % 100 == 0);

    if(logThisCall) {
        ST_DEBUG_LOG("=== SoundTouchFilterInstance::getAudio [{:}] ===", mProcessingCounter);
        ST_DEBUG_LOG("Request: {:} samples, bufferSize: {:}, channels: {:}", aSamplesToRead, aBufferSize, mChannels);
        ST_DEBUG_LOG("Current position: {:f} seconds", mStreamPosition);
    }

    if(!mSourceInstance || !mSoundTouch) {
        // return silence if not initialized
        memset(aBuffer, 0, sizeof(float) * aSamplesToRead * mChannels);
        return aSamplesToRead;
    }

    if(logThisCall) ST_DEBUG_LOG("speed: {:f}, pitch: {:f}", mSoundTouchSpeed, mSoundTouchPitch);

    unsigned int samplesInSoundTouch = mSoundTouch->numSamples();

    if(logThisCall) ST_DEBUG_LOG("SoundTouch has {:} samples in buffer", samplesInSoundTouch);

    // keep the SoundTouch buffer well-stocked to ensure consistent output, use a target buffer size larger than necessary
    // EXCEPT: if the source ended, then we just need to get out the last bits of SoundTouch audio,
    //			and don't try to receive more from the source, otherwise we keep flushing empty audio data that SoundTouch generates,
    //			and thus returning the wrong amount of real data we actually "got" (see return samplesReceived)
    if(!mSourceInstance->hasEnded() && samplesInSoundTouch < aSamplesToRead) {
        // feed soundtouch until we have at least aSamplesToRead ready samples
        samplesInSoundTouch = feedSoundTouch(aSamplesToRead, logThisCall);

        // if source ended (after we read more audio from it in feedSoundTouch) and we still need more samples, flush
        if(mSourceInstance->hasEnded() && samplesInSoundTouch < aSamplesToRead) {
            if(logThisCall) ST_DEBUG_LOG("Source has ended, flushing SoundTouch");
            mSoundTouch->flush();
            samplesInSoundTouch = mSoundTouch->numSamples();
        }
    }

    // get processed samples from SoundTouch
    unsigned int samplesAvailable = samplesInSoundTouch;
    unsigned int samplesToReceive = samplesAvailable < aSamplesToRead ? samplesAvailable : aSamplesToRead;
    unsigned int samplesReceived = samplesToReceive;

    if(logThisCall)
        ST_DEBUG_LOG("SoundTouch now has {:} samples available, will receive {:}", samplesAvailable, samplesToReceive);

    // clear output buffer first
    memset(aBuffer, 0, sizeof(float) * aSamplesToRead * mChannels);

    if(samplesToReceive > 0) {
        // get interleaved samples from SoundTouch and convert to non-interleaved
        ensureInterleavedBufferSize(samplesToReceive);
        samplesReceived = mSoundTouch->receiveSamples(mInterleavedBuffer.mData, samplesToReceive);

        if(logThisCall)
            ST_DEBUG_LOG("Received {:} samples from SoundTouch, converting to non-interleaved", samplesReceived);

        // convert from interleaved (LRLRLR...) to non-interleaved (LLLL...RRRR...)
        for(unsigned int i = 0; i < samplesReceived; i++) {
            for(unsigned int ch = 0; ch < mChannels; ch++) {
                aBuffer[ch * aSamplesToRead + i] = mInterleavedBuffer.mData[i * mChannels + ch];
            }
        }

        if(logThisCall && samplesReceived > 0) {
            const float samplesInSeconds = (static_cast<float>(samplesReceived) / mBaseSamplerate);
            ST_DEBUG_LOG("Updated position: received {:} samples ({:.4f} seconds), new position: {:.4f} seconds",
                         samplesReceived, samplesInSeconds, mStreamPosition);
        }
    } else if(logThisCall) {
        ST_DEBUG_LOG("No samples available from SoundTouch, returning silence");
    }

    if(logThisCall && mProcessingCounter % 100 == 0)
        ST_DEBUG_LOG(
            "Position: mStreamPosition={:.3f}s, init_latency={:}, input_seq={}, output_seq={:}, avg_latency={:.1f}ms, "
            "ratio={:.3f}",
            mStreamPosition, mSTInitialLatency, mSTInputSequence, mSTOutputSequence, mSTLatencySeconds * 1000.0,
            mSoundTouch->getInputOutputSampleRatio());

    // update SoundTouch parameters if they've changed, after the last getAudio chunk has played out with the old speed
    applySettingUpdate();

    if(logThisCall) ST_DEBUG_LOG("=== End of getAudio [{:}] ===", mProcessingCounter);

    // reference in soloud_wavstream.cpp shows that only the amount of real data actually "read" (taken from SoundTouch in this instance) is actually returned.
    // it just means that the stream ends properly when the parent source has ended, after the remaining data from the SoundTouch buffer is played out.
    return samplesReceived;
}

bool SoundTouchFilterInstance::hasEnded() {
    // end when source has ended
    return (!mSourceInstance || mSourceInstance->hasEnded());
}

result SoundTouchFilterInstance::seek(time aSeconds, float *mScratch, unsigned int mScratchSize) {
    if(aSeconds <= 0) return this->rewind();

    if(!mSourceInstance || !mSoundTouch) return INVALID_PARAMETER;

    ST_DEBUG_LOG("Seeking to {:.3f} seconds", aSeconds);

    // seek the source.
    result res = mSourceInstance->seek(aSeconds, mScratch, mScratchSize);

    if(res == SO_NO_ERROR) {
        reSynchronize();
        ST_DEBUG_LOG("Seek complete to {:.3f} seconds", mStreamPosition);
    }

    return res;
}

result SoundTouchFilterInstance::rewind() {
    ST_DEBUG_LOG("Rewinding");

    if(mStreamPosition == 0) {
        reSynchronize();
        ST_DEBUG_LOG("Position was already at the start, cleared ST buffers");
        return SO_NO_ERROR;
    }

    result res = UNKNOWN_ERROR;
    if(mSourceInstance) res = mSourceInstance->rewind();

    if(res == SO_NO_ERROR) {
        reSynchronize();
        ST_DEBUG_LOG("Rewinded, position now {:.3f}", mStreamPosition);
    }

    return res;
}

// end public methods

// private helpers below (not called by SoLoud)

void SoundTouchFilterInstance::requestSettingUpdate(float speed, float pitch) {
    Sync::scoped_lock lock{mSettingUpdateMutex};
    if(mSoundTouchSpeed != speed || mSoundTouchPitch != pitch) mNeedsSettingUpdate = true;
}

void SoundTouchFilterInstance::applySettingUpdate() {
    if(!mSoundTouch) return;

    {
        Sync::scoped_lock lock{mSettingUpdateMutex};
        if(!mNeedsSettingUpdate) return;
        mNeedsSettingUpdate = false;
    }

    ST_DEBUG_LOG("(Deferred) Updating speed: {:f}->{:f}, pitch: {:f}->{:f}", mSoundTouchSpeed, mParent->mSpeedFactor,
                 mSoundTouchPitch, mParent->mPitchFactor);

    mSoundTouchSpeed = mParent->mSpeedFactor;
    mSoundTouchPitch = mParent->mPitchFactor;

    // actually update the parameters
    mSoundTouch->setTempo(mSoundTouchSpeed);
    // convert to semitones
    float pitchSemitones = (mSoundTouchPitch - 1.0f) * 60.0f;
    mSoundTouch->setPitchSemiTones(pitchSemitones);

    updateSTLatency();
}

time SoundTouchFilterInstance::getInternalLatency() const { return mSTLatencySeconds.load(std::memory_order_acquire); }

void SoundTouchFilterInstance::ensureBufferSize(unsigned int samples) {
    if(samples > mBufferSize) {
        ST_DEBUG_LOG("SoundTouchFilterInstance: Resizing non-interleaved buffer from {:} to {:} samples", mBufferSize,
                     samples);

        mBufferSize = samples;
        mBuffer.init(static_cast<u32>(mBufferSize * mChannels));
        memset(mBuffer.mData, 0, sizeof(float) * mBufferSize * mChannels);
    }
}

void SoundTouchFilterInstance::ensureInterleavedBufferSize(unsigned int samples) {
    unsigned int requiredSize = samples * mChannels;
    if(requiredSize > mInterleavedBufferSize) {
        ST_DEBUG_LOG("SoundTouchFilterInstance: Resizing interleaved buffer from {:} to {:} samples",
                     mInterleavedBufferSize / mChannels, samples);

        mInterleavedBufferSize = requiredSize;
        mInterleavedBuffer.init(mInterleavedBufferSize);
        memset(mInterleavedBuffer.mData, 0, sizeof(float) * mInterleavedBufferSize);
    }
}

unsigned int SoundTouchFilterInstance::feedSoundTouch(unsigned int targetBufferLevel, bool logThis) {
    unsigned int currentSamples = mSoundTouch->numSamples();

    if(logThis)
        ST_DEBUG_LOG("Starting feedSoundTouch: current={:}, target={:}, speed={:.3f}", currentSamples,
                     targetBufferLevel, mSoundTouchSpeed);

    // read in power-of-two chunks from the source until we have enough samples ready in soundtouch
    // NOMINAL_INPUT_SEQUENCE will be smaller for lower rates (less source data needed for the same amount of output samples), and
    // vice versa for higher rates
    unsigned int chunkSize = mSTInputSequence;
    if(chunkSize < 32U)
        chunkSize = 32U;
    else if(chunkSize & (chunkSize - 1)) {
        unsigned int i = 1;
        while(i < chunkSize) i *= 2;
        chunkSize = i / 2;  // one power-of-two smaller, so we don't overshoot targetBufferLevel by too much
    }

    // then set it to a SIMD-aligned multiple (assume AVX, 32 bytes, as a catch-all)
    chunkSize = (chunkSize + 31) & ~31;

    ensureBufferSize(chunkSize);
    ensureInterleavedBufferSize(chunkSize);

    // bound the work per call by the input that could possibly be needed:
    // - priming SoundTouch (it outputs nothing before mSTInitialLatency input samples, e.g. right after a seek cleared it),
    // - the input for the requested output,
    // - and a chunk of slack.
    const unsigned int maxInputSamples =
        mSTInitialLatency +
        static_cast<unsigned int>(std::ceil(targetBufferLevel / mSoundTouch->getInputOutputSampleRatio())) + chunkSize;
    unsigned int samplesFed = 0;
    unsigned int chunksProcessed = 0;

    while(currentSamples < targetBufferLevel && !mSourceInstance->hasEnded() && samplesFed < maxInputSamples) {
        unsigned int samplesRead = mSourceInstance->getAudio(mBuffer.mData, chunkSize, mBufferSize);

        if(samplesRead == 0)  // no more data available
            break;

        if(logThis)
            ST_DEBUG_LOG("Chunk {:}: read {:} samples from source (chunkSize {})", chunksProcessed + 1, samplesRead,
                         chunkSize);

        // convert from non-interleaved to interleaved format
        // see SoLoud::Soloud::mix
        unsigned int stride = (samplesRead + 31) & ~31;
        interlace_samples(mInterleavedBuffer.mData, mBuffer.mData, samplesRead, stride, mChannels,
                          detail::SAMPLE_FLOAT32);

        // feed the chunk to SoundTouch
        mSoundTouch->putSamples(mInterleavedBuffer.mData, samplesRead);

        currentSamples = mSoundTouch->numSamples();
        samplesFed += samplesRead;
        chunksProcessed++;

        if(logThis) ST_DEBUG_LOG("After chunk {:}: SoundTouch has {:} samples", chunksProcessed, currentSamples);
    }

    if(logThis)
        ST_DEBUG_LOG("feedSoundTouch complete: processed {:} chunks, final buffer level={:}", chunksProcessed,
                     currentSamples);

    return currentSamples;
}

void SoundTouchFilterInstance::updateSTLatency() {
    if(!mSoundTouch) {
        mSTLatencySeconds.store(0.0, std::memory_order_release);
        return;
    }

    mSTInitialLatency = mSoundTouch->getSetting(SETTING_INITIAL_LATENCY);
    mSTOutputSequence = mSoundTouch->getSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE);
    mSTInputSequence = mSoundTouch->getSetting(SETTING_NOMINAL_INPUT_SEQUENCE);

    const double sr = static_cast<double>(mParent->mBaseSamplerate);

    // WSOLA pipeline offset: (INPUT_SEQUENCE - 2*OUTPUT_SEQUENCE - overlap/2) / sampleRate.
    // empirically validated via cross-correlation of chirp signal against SoundTouch output
    // across 0.5x-2.0x (avg error <2.3ms). the speed dependence comes from INPUT_SEQUENCE
    // varying with tempo; no explicit speed multiplier is needed. the overlap/2 term accounts
    // for the WSOLA overlap-add read position within the processing window.
    // this only covers the SoundTouch component; engine-level delays (position caching,
    // interpolator smoothing, audio queue depth) add a few more ms, dependent on system buffer size.
    double resultSeconds = 0.0;
    if(int strat = cv::snd_soloud_offset_compensation_strategy.getInt(); strat != 0) {
        double pipelineSamples = 0.0;
        if(strat == 1) {
            const double overlapMs = mSoundTouch->getSetting(SETTING_OVERLAP_MS);
            const double overlapSamples = overlapMs * sr / 1000.0;
            pipelineSamples = static_cast<double>(mSTInputSequence) - 2.0 * static_cast<double>(mSTOutputSequence) -
                              overlapSamples / 2.0;
        } else {
            pipelineSamples = static_cast<double>(mSTInputSequence) - 2.0 * static_cast<double>(mSTOutputSequence);
        }
        resultSeconds = pipelineSamples / sr;
    }

    ST_DEBUG_LOG("ST latency: speed={:.2f} compensation={:.2f}ms (initLat={} inSeq={} outSeq={})", mSoundTouchSpeed,
                 resultSeconds * 1000.0, mSTInitialLatency, mSTInputSequence, mSTOutputSequence);

    mSTLatencySeconds.store(resultSeconds, std::memory_order_release);
}

void SoundTouchFilterInstance::reSynchronize() {
    // clear SoundTouch buffers to reset its internal state, and apply a pending speed/pitch change right away
    if(mSoundTouch) {
        mSoundTouch->clear();
        applySettingUpdate();
    }

    if(mSourceInstance) {
        // the engine points mStreamPosition at the source's read cursor before a seek and expects it to be left at
        // the new position afterwards (see Soloud::seek), which for us is wherever the source stream actually landed
        mStreamPosition = mSourceInstance->mStreamPosition;
    }
}

}  // namespace SoLoud

#endif  // MCENGINE_FEATURE_SOLOUD
