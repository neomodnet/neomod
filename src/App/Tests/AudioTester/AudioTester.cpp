// Copyright (c) 2026, WH, All rights reserved.
#include "AudioTester.h"

#if (defined(MCENGINE_FEATURE_SOLOUD) && defined(MCENGINE_FEATURE_BASS))

#include "Engine.h"
#include "Mouse.h"
#include "Keyboard.h"
#include "ConVar.h"
#include "Logging.h"
#include "Graphics.h"
#include "Font.h"
#include "File.h"
#include "UString.h"
#include "Sound.h"
#include "Timing.h"
#include "Environment.h"

#include "SoLoudSoundEngine.h"
#include "BassSoundEngine.h"
#include "SoLoudThread.h"

#include "SoundTouch.h"
#include "soloud_wav.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <vector>

namespace Mc::Tests {
namespace {
struct STLatencyResult {
    float speed;

    // averaged empirical impulse measurement (multiple impulses to cancel WSOLA window jitter)
    double avgOffsetS;     // mean of per-impulse (outputSourceTimeS - inputSourceTimeS)
    double minOffsetS;     // min measured offset
    double maxOffsetS;     // max measured offset
    int impulsesMeasured;  // how many impulses were successfully detected

    // soundtouch internals
    unsigned int initialLatency;
    unsigned int inputSequence;
    unsigned int outputSequence;

    // compensation strategy values (seconds)
    double strat1;   // (initLatency - outSeq/2) / sr
    double strat1n;  // above * speed
    double strat2;   // inputSeq / sr
    double strat2n;  // above * speed
    double strat3;   // initLatency / sr
    double strat3n;  // above * speed

    // simulated mStreamPosition drift
    double driftS;
};

struct BassComparisonResult {
    float speed;
    double avgDiffS;  // avg(soloud_pos - bass_pos), seconds
    int sampleCount;
};

struct CorrelationResult {
    float speed;
    double avgErrorMS;  // current formula: (IL-OS)*spd/sr
    double maxAbsErrorMS;
    double altAvgErrorMS;  // candidate 1: (IS-2*OS)/sr
    double altMaxAbsErrorMS;
    double alt2AvgErrorMS;  // candidate 2: (IS-2*OS-OL/2)/sr
    double alt2MaxAbsErrorMS;
    double driftRateMS;     // error change over duration (last - first)
    double avgCorrelation;  // mean NCC peak (quality indicator, >0.9 = good)
    int windowCount;
};

enum ComparisonState {
    COMP_IDLE,
    COMP_SPEED_START,
    COMP_WARMUP,
    COMP_SAMPLING,
    COMP_DONE,
};

}  // namespace

class AudioTesterImpl {
    NOCOPY_NOMOVE(AudioTesterImpl)
   public:
    AudioTesterImpl();
    ~AudioTesterImpl();

    void draw();
    void update();

    void onKeyDown(KeyboardEvent &e);

   private:
    // impulse test
    void runImpulseTests();
    STLatencyResult runSingleImpulseTest(float speed);

    std::vector<STLatencyResult> m_results;
    bool m_bTestsRun{false};

    // BASS comparison test
    void startBassComparison();
    void updateBassComparison();
    void drawBassComparison(float startY);
    static bool generateTestWav(const std::string &path);

    // cross-correlation test
    void runCorrelationTests();
    CorrelationResult runSingleCorrelationTest(float speed);
    void drawCorrelation(float startY);

    ComparisonState m_compState{COMP_IDLE};
    int m_compSpeedIdx{0};
    double m_compPhaseStartTime{0};
    std::vector<double> m_compDiffs;  // collected (soloud - bass) diffs for current speed
    std::vector<BassComparisonResult> m_compResults;
    bool m_compDone{false};

    std::vector<CorrelationResult> m_corrResults;
    bool m_corrDone{false};

    std::unique_ptr<Sound> m_bassSnd{nullptr};
    std::unique_ptr<Sound> m_soloudSnd{nullptr};
    std::string m_wavPath;

    // very hacky
    SoLoudSoundEngine *m_soloud{nullptr};
    BassSoundEngine *m_bass{nullptr};

    bool m_bCreatedSoLoud{false};
};

static constexpr unsigned int SAMPLE_RATE = 44100;
static constexpr unsigned int CHANNELS = 2;
static constexpr unsigned int CHUNK_SIZE = 1024;

// place impulses every 0.5s starting at 0.5s, in a 10-second signal.
// many impulses at different positions averages out WSOLA window alignment jitter.
static constexpr unsigned int IMPULSE_LENGTH = 100;  // ~2.3ms burst, long enough to survive high-speed compression
static constexpr double IMPULSE_SPACING_S = 0.5;
static constexpr double FIRST_IMPULSE_S = 0.5;
static constexpr double SIGNAL_DURATION_S = 10.0;
static constexpr unsigned int TOTAL_INPUT_SAMPLES = static_cast<unsigned int>(SAMPLE_RATE * SIGNAL_DURATION_S);

static constexpr float TEST_SPEEDS[] = {
    0.25f, 0.5f,  0.75f, 1.0f,  1.1f, 1.2f,  1.3f, 1.4f, 1.5f, 1.6f,
    1.7f,  1.75f, 1.8f,  1.85f, 1.9f, 1.95f, 2.0f, 2.5f, 3.0f,
};

// speeds to test in BASS comparison (kept small, ~17.5s total test time)
static constexpr float COMP_SPEEDS[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
static constexpr double COMP_WARMUP_S = 0.5;
static constexpr double COMP_SAMPLE_S = 3.0;
static constexpr double COMP_SEEK_POS_S = 5.0;  // seek position for each test

// cross-correlation test constants
static constexpr int CORR_WINDOW = 2048;    // ~46ms NCC window
static constexpr int CORR_SEARCH = 4096;    // search radius in samples (~93ms)
static constexpr double CORR_STEP_S = 0.1;  // analyze every 100ms of output
static constexpr double CORR_SKIP_S = 1.0;  // skip first/last 1s (SoundTouch priming)

AudioTesterImpl::AudioTesterImpl() {
    debugLog("");

    assert(soundEngine);

    // ultra hacky
    if(soundEngine && soundEngine->getTypeId() == SoundEngine::SOLOUD) {
        m_soloud = static_cast<SoLoudSoundEngine *>(soundEngine.get());

        m_bass = new BassSoundEngine();
        if(!m_bass || !m_bass->succeeded()) {
            SAFE_DELETE(m_bass);
            debugLog("BASS failed to initialize");
            return;
        }
    } else {
        m_bass = static_cast<BassSoundEngine *>(soundEngine.get());
        if(!m_bass || !m_bass->succeeded()) {
            debugLog("BASS failed to initialize");
            return;
        }

        m_bCreatedSoLoud = true;
        m_soloud = new SoLoudSoundEngine();
        if(!m_soloud || !m_soloud->succeeded()) {
            SAFE_DELETE(m_soloud);
            debugLog("SoLoud failed to initialize");
            return;
        }
    }

    // i don't know why these are inconsistent but whatever
    {
        m_soloud->restart();
        m_soloud->setOutputDevice(m_soloud->getDefaultDevice());
    }
    {
        m_bass->updateOutputDevices(true);
        m_bass->setOutputDevice(m_bass->getDefaultDevice());
    }

    // generate WAV for bass comparison (will be used when user presses B)
    const std::string tempDir = fmt::format("{}/.tmp/", env->getCacheDir());  // ~/.cache/neomod, on linux (probably)
    m_wavPath = tempDir + PACKAGE_NAME "_audiotester.wav";

    if(!env->createDirectory(tempDir) || !generateTestWav(m_wavPath)) {
        debugLog("failed to generate test WAV at {}", m_wavPath);
        m_wavPath.clear();
    }
}

AudioTesterImpl::~AudioTesterImpl() {
    debugLog("");

    // stop and clean up comparison sounds
    m_bassSnd.reset();
    m_soloudSnd.reset();

    // clean up WAV file
    if(!m_wavPath.empty()) {
        std::remove(m_wavPath.c_str());
    }

    if(m_bass) {
        if(!m_bCreatedSoLoud) {
            SAFE_DELETE(m_bass);
        }
    }
    if(m_soloud) {
        if(m_bCreatedSoLoud) {
            SAFE_DELETE(m_soloud);
        }
    }
}

// --- WAV generation ---

bool AudioTesterImpl::generateTestWav(const std::string &path) {
    // 30-second stereo 44100Hz 16-bit PCM WAV with a 440Hz sine wave
    constexpr unsigned int wavSampleRate = 44100;
    constexpr unsigned int wavChannels = 2;
    constexpr unsigned int wavDurationS = 30;
    constexpr unsigned int totalSamples = wavSampleRate * wavDurationS;
    constexpr unsigned int dataSize = totalSamples * wavChannels * sizeof(int16_t);

    FILE *f = File::fopen_c(path.c_str(), "wb");
    if(!f) return false;

    // RIFF header
    const uint32_t fileSize = 36 + dataSize;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&fileSize, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f);

    // fmt chunk
    const uint16_t audioFormat = 1;  // PCM
    const uint16_t numChannels = wavChannels;
    const uint32_t sampleRate = wavSampleRate;
    const uint16_t bitsPerSample = 16;
    const uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    const uint16_t blockAlign = numChannels * bitsPerSample / 8;
    const uint32_t fmtChunkSize = 16;

    std::fwrite("fmt ", 1, 4, f);
    std::fwrite(&fmtChunkSize, 4, 1, f);
    std::fwrite(&audioFormat, 2, 1, f);
    std::fwrite(&numChannels, 2, 1, f);
    std::fwrite(&sampleRate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&blockAlign, 2, 1, f);
    std::fwrite(&bitsPerSample, 2, 1, f);

    // data chunk
    std::fwrite("data", 1, 4, f);
    std::fwrite(&dataSize, 4, 1, f);

    // write linear chirp (200Hz → 8000Hz) — unique content at every position for cross-correlation
    constexpr unsigned int batchSize = 4096;
    int16_t batch[batchSize * wavChannels];
    constexpr double f0 = 200.0;
    constexpr double f1 = 8000.0;
    const double chirpRate = (f1 - f0) / wavDurationS;

    for(unsigned int offset = 0; offset < totalSamples; offset += batchSize) {
        unsigned int count = std::min(batchSize, totalSamples - offset);
        for(unsigned int i = 0; i < count; i++) {
            const double t = static_cast<double>(offset + i) / wavSampleRate;
            const double phase = 2.0 * PI * (f0 * t + 0.5 * chirpRate * t * t);
            auto val = static_cast<int16_t>(std::sin(phase) * 16000);
            batch[i * wavChannels + 0] = val;
            batch[i * wavChannels + 1] = val;
        }
        std::fwrite(batch, sizeof(int16_t) * wavChannels, count, f);
    }

    std::fclose(f);
    debugLog("generated test WAV: {} ({} seconds)", path, wavDurationS);
    return true;
}

// --- impulse test (unchanged) ---

STLatencyResult AudioTesterImpl::runSingleImpulseTest(float speed) {
    STLatencyResult r{};
    r.speed = speed;

    soundtouch::SoundTouch st;
    st.setSampleRate(SAMPLE_RATE);
    st.setChannels(CHANNELS);
    st.setSetting(SETTING_USE_AA_FILTER, 1);
    st.setSetting(SETTING_AA_FILTER_LENGTH, 64);
    st.setSetting(SETTING_USE_QUICKSEEK, 0);
    st.setSetting(SETTING_SEQUENCE_MS, 15);
    st.setSetting(SETTING_SEEKWINDOW_MS, 30);
    st.setSetting(SETTING_OVERLAP_MS, 6);

    st.setTempo(speed);

    r.initialLatency = st.getSetting(SETTING_INITIAL_LATENCY);
    r.inputSequence = st.getSetting(SETTING_NOMINAL_INPUT_SEQUENCE);
    r.outputSequence = st.getSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE);

    const double sr = static_cast<double>(SAMPLE_RATE);
    const double initSecs = r.initialLatency / sr;
    const double outputSecs = r.outputSequence / sr;
    const double inputSecs = r.inputSequence / sr;

    r.strat1 = initSecs - (outputSecs / 2.0);
    r.strat1n = r.strat1 * speed;
    r.strat2 = inputSecs;
    r.strat2n = r.strat2 * speed;
    r.strat3 = initSecs;
    r.strat3n = r.strat3 * speed;

    std::vector<unsigned int> impulsePositions;
    for(double t = FIRST_IMPULSE_S; t + (IMPULSE_LENGTH / sr) < SIGNAL_DURATION_S; t += IMPULSE_SPACING_S) {
        impulsePositions.push_back(static_cast<unsigned int>(t * sr));
    }

    std::vector<float> input(static_cast<size_t>(TOTAL_INPUT_SAMPLES) * CHANNELS, 0.0f);
    for(unsigned int pos : impulsePositions) {
        for(unsigned int i = 0; i < IMPULSE_LENGTH; i++) {
            const unsigned int sampleIdx = pos + i;
            if(sampleIdx < TOTAL_INPUT_SAMPLES) {
                input[static_cast<size_t>(sampleIdx) * CHANNELS + 0] = 1.0f;
                input[static_cast<size_t>(sampleIdx) * CHANNELS + 1] = 1.0f;
            }
        }
    }

    std::vector<float> output;
    output.reserve(static_cast<size_t>(TOTAL_INPUT_SAMPLES) * CHANNELS * 2);
    std::vector<float> receiveBuffer(static_cast<size_t>(CHUNK_SIZE) * CHANNELS);
    double simStreamPosition = 0.0;

    unsigned int inputOffset = 0;
    while(inputOffset < TOTAL_INPUT_SAMPLES) {
        unsigned int samplesToFeed = CHUNK_SIZE;
        if(inputOffset + samplesToFeed > TOTAL_INPUT_SAMPLES) samplesToFeed = TOTAL_INPUT_SAMPLES - inputOffset;

        st.putSamples(&input[static_cast<size_t>(inputOffset) * CHANNELS], samplesToFeed);
        inputOffset += samplesToFeed;

        unsigned int received;
        while((received = st.receiveSamples(receiveBuffer.data(), CHUNK_SIZE)) > 0) {
            output.insert(output.end(), receiveBuffer.begin(),
                          receiveBuffer.begin() + static_cast<ptrdiff_t>(received) * CHANNELS);
            simStreamPosition += (static_cast<double>(received) / sr) * speed;
        }
    }

    st.flush();
    {
        unsigned int received;
        while((received = st.receiveSamples(receiveBuffer.data(), CHUNK_SIZE)) > 0) {
            output.insert(output.end(), receiveBuffer.begin(),
                          receiveBuffer.begin() + static_cast<ptrdiff_t>(received) * CHANNELS);
            simStreamPosition += (static_cast<double>(received) / sr) * speed;
        }
    }

    const auto outputSamples = static_cast<unsigned int>(output.size() / CHANNELS);
    const int searchRadius = static_cast<int>(r.outputSequence) * 4;

    double sumOffset = 0.0;
    r.minOffsetS = 1e9;
    r.maxOffsetS = -1e9;
    r.impulsesMeasured = 0;

    for(unsigned int pos : impulsePositions) {
        const double inputTimeS = static_cast<double>(pos) / sr;
        const int expectedOut = static_cast<int>(static_cast<double>(pos) / speed);
        const int searchStart = std::max(0, expectedOut - searchRadius);
        const int searchEnd = std::min(static_cast<int>(outputSamples), expectedOut + searchRadius);

        float peakVal = 0.0f;
        int peakSample = -1;
        for(int i = searchStart; i < searchEnd; i++) {
            float val = std::abs(output[static_cast<size_t>(i) * CHANNELS]);
            if(val > peakVal) {
                peakVal = val;
                peakSample = i;
            }
        }

        if(peakSample < 0 || peakVal < 0.01f) continue;

        const double outWallTimeS = static_cast<double>(peakSample) / sr;
        const double outSourceTimeS = outWallTimeS * speed;
        const double offset = outSourceTimeS - inputTimeS;

        sumOffset += offset;
        r.minOffsetS = std::min(r.minOffsetS, offset);
        r.maxOffsetS = std::max(r.maxOffsetS, offset);
        r.impulsesMeasured++;
    }

    if(r.impulsesMeasured > 0) {
        r.avgOffsetS = sumOffset / r.impulsesMeasured;
    } else {
        r.avgOffsetS = 0.0;
        r.minOffsetS = 0.0;
        r.maxOffsetS = 0.0;
    }

    const double totalInputTimeS = static_cast<double>(TOTAL_INPUT_SAMPLES) / sr;
    r.driftS = simStreamPosition - totalInputTimeS;

    debugLog(
        "speed={:.2f}: avg offset={:>+7.2f}ms [{:>+7.2f} .. {:>+7.2f}] ({:d}/{:d} impulses), "
        "initLat={:d} inSeq={:d} outSeq={:d}, drift={:.3f}ms",
        speed, r.avgOffsetS * 1000.0, r.minOffsetS * 1000.0, r.maxOffsetS * 1000.0, r.impulsesMeasured,
        static_cast<int>(impulsePositions.size()), r.initialLatency, r.inputSequence, r.outputSequence,
        r.driftS * 1000.0);

    return r;
}

void AudioTesterImpl::runImpulseTests() {
    debugLog("running SoundTouch impulse latency tests...");
    m_results.clear();

    for(float speed : TEST_SPEEDS) {
        m_results.push_back(runSingleImpulseTest(speed));
    }

    m_bTestsRun = true;
    debugLog("tests complete, {:d} results", m_results.size());
}

// --- BASS comparison test ---

void AudioTesterImpl::startBassComparison() {
    if(m_wavPath.empty() || !m_bass || !m_soloud) {
        debugLog("cannot start BASS comparison: missing WAV or engines");
        return;
    }

    if(m_compState != COMP_IDLE && m_compState != COMP_DONE) {
        debugLog("BASS comparison already running");
        return;
    }

    debugLog("BASS comparison: loading test audio... (SoLoud backend buffer: {} samples = {:.2f}ms)",
             soloud->getBackendBufferSize(),
             static_cast<double>(soloud->getBackendBufferSize()) / soloud->getBackendSamplerate() * 1000.0);

    // create sounds via respective engines (stream=true for SoundTouch processing)
    m_bassSnd.reset(m_bass->createSound(m_wavPath, true, false, false));
    m_bassSnd->loadAsync();
    m_bassSnd->load();
    m_bassSnd->setBaseVolume(0.0f);

    m_soloudSnd.reset(m_soloud->createSound(m_wavPath, true, false, false));
    m_soloudSnd->loadAsync();
    m_soloudSnd->load();
    m_soloudSnd->setBaseVolume(0.0f);

    m_compResults.clear();
    m_compSpeedIdx = 0;
    m_compDone = false;

    m_compState = COMP_SPEED_START;
}

void AudioTesterImpl::updateBassComparison() {
    if(m_compState == COMP_IDLE || m_compState == COMP_DONE) return;

    const double now = Timing::getTimeReal();

    switch(m_compState) {
        case COMP_SPEED_START: {
            if(m_compSpeedIdx >= static_cast<int>(std::size(COMP_SPEEDS))) {
                m_compState = COMP_DONE;
                m_compDone = true;
                debugLog("BASS comparison: all speeds tested");
                break;
            }

            const float speed = COMP_SPEEDS[m_compSpeedIdx];

            // stop, enqueue (creates handles while paused), configure, then play
            m_bass->stop(m_bassSnd.get());
            m_soloud->stop(m_soloudSnd.get());

            m_bass->enqueue(m_bassSnd.get());
            m_soloud->enqueue(m_soloudSnd.get());

            m_bassSnd->setSpeed(speed);
            m_soloudSnd->setSpeed(speed);

            m_bassSnd->setPositionS(COMP_SEEK_POS_S);
            m_soloudSnd->setPositionS(COMP_SEEK_POS_S);

            m_bass->play(m_bassSnd.get());
            m_soloud->play(m_soloudSnd.get());

            m_compDiffs.clear();
            m_compPhaseStartTime = now;
            m_compState = COMP_WARMUP;

            debugLog("BASS comparison: speed={:.2f}x, warming up...", speed);
            break;
        }

        case COMP_WARMUP:
            if(now - m_compPhaseStartTime >= COMP_WARMUP_S) {
                m_compPhaseStartTime = now;
                m_compState = COMP_SAMPLING;
            }
            break;

        case COMP_SAMPLING: {
            // sample both positions
            if(m_bassSnd->isPlaying() && m_soloudSnd->isPlaying()) {
                const double bassS = static_cast<double>(m_bassSnd->getPositionUS()) / 1000000.0;
                const double soloudS = static_cast<double>(m_soloudSnd->getPositionUS()) / 1000000.0;
                m_compDiffs.push_back(soloudS - bassS);
            }

            if(now - m_compPhaseStartTime >= COMP_SAMPLE_S) {
                // compute average difference
                const float speed = COMP_SPEEDS[m_compSpeedIdx];
                BassComparisonResult r{};
                r.speed = speed;
                r.sampleCount = static_cast<int>(m_compDiffs.size());

                if(!m_compDiffs.empty()) {
                    double sum = 0;
                    for(double d : m_compDiffs) sum += d;
                    r.avgDiffS = sum / static_cast<double>(m_compDiffs.size());
                }

                // log SoundTouch's reported latency and the advance-before-render factor
                const auto stDelayMS = m_soloudSnd->getRateBasedStreamDelayMS();
                const double bufferMs =
                    static_cast<double>(soloud->getBackendBufferSize()) / soloud->getBackendSamplerate() * 1000.0;
                const double advanceBeforeRenderMs = bufferMs * speed;

                debugLog(
                    "BASS comparison: speed={:.2f}x, avg diff(soloud-bass)={:>+7.2f}ms ({:d} samples), "
                    "ST latency={:d}ms, advance-before-render={:.1f}ms (buf={:.1f}ms * spd)",
                    speed, r.avgDiffS * 1000.0, r.sampleCount, stDelayMS, advanceBeforeRenderMs, bufferMs);

                m_compResults.push_back(r);
                m_compSpeedIdx++;
                m_compState = COMP_SPEED_START;
            }
            break;
        }

        default:
            break;
    }
}

// --- cross-correlation test ---

CorrelationResult AudioTesterImpl::runSingleCorrelationTest(float speed) {
    CorrelationResult r{};
    r.speed = speed;

    // load source PCM via SoLoud::Wav
    SoLoud::Wav sourceWav;
    if(sourceWav.load(m_wavPath.c_str()) != SoLoud::SO_NO_ERROR) {
        debugLog("correlation: failed to load WAV");
        return r;
    }

    const unsigned int srcChannels = sourceWav.mChannels;
    const unsigned int srcSamplesPerCh = sourceWav.mSampleCount;
    const double sr = static_cast<double>(sourceWav.mBaseSamplerate);

    // extract channel 0 (SoLoud stores non-interleaved: [ch0_0..ch0_N, ch1_0..ch1_N])
    std::vector<float> sourceMono(srcSamplesPerCh);
    std::memcpy(sourceMono.data(), sourceWav.mData, srcSamplesPerCh * sizeof(float));

    // process through SoundTouch (same settings as SoLoudFX.cpp and impulse test)
    soundtouch::SoundTouch st;
    st.setSampleRate(static_cast<unsigned int>(sr));
    st.setChannels(srcChannels);
    st.setSetting(SETTING_USE_AA_FILTER, 1);
    st.setSetting(SETTING_AA_FILTER_LENGTH, 64);
    st.setSetting(SETTING_USE_QUICKSEEK, 0);
    st.setSetting(SETTING_SEQUENCE_MS, 15);
    st.setSetting(SETTING_SEEKWINDOW_MS, 30);
    st.setSetting(SETTING_OVERLAP_MS, 6);
    st.setTempo(speed);

    const unsigned int stInitialLatency = st.getSetting(SETTING_INITIAL_LATENCY);
    const unsigned int stInputSequence = st.getSetting(SETTING_NOMINAL_INPUT_SEQUENCE);
    const unsigned int stOutputSequence = st.getSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE);

    // convert non-interleaved → interleaved for SoundTouch
    std::vector<float> interleavedInput(static_cast<size_t>(srcSamplesPerCh) * srcChannels);
    for(unsigned int i = 0; i < srcSamplesPerCh; i++) {
        for(unsigned int ch = 0; ch < srcChannels; ch++) {
            interleavedInput[static_cast<size_t>(i) * srcChannels + ch] =
                sourceWav.mData[static_cast<size_t>(ch) * srcSamplesPerCh + i];
        }
    }

    // feed through SoundTouch and collect output
    std::vector<float> interleavedOutput;
    interleavedOutput.reserve(interleavedInput.size() * 2);
    std::vector<float> receiveBuffer(static_cast<size_t>(CHUNK_SIZE) * srcChannels);

    unsigned int inputOffset = 0;
    while(inputOffset < srcSamplesPerCh) {
        unsigned int toFeed = std::min(CHUNK_SIZE, srcSamplesPerCh - inputOffset);
        st.putSamples(&interleavedInput[static_cast<size_t>(inputOffset) * srcChannels], toFeed);
        inputOffset += toFeed;

        unsigned int received;
        while((received = st.receiveSamples(receiveBuffer.data(), CHUNK_SIZE)) > 0) {
            interleavedOutput.insert(interleavedOutput.end(), receiveBuffer.begin(),
                                     receiveBuffer.begin() + static_cast<ptrdiff_t>(received) * srcChannels);
        }
    }

    st.flush();
    {
        unsigned int received;
        while((received = st.receiveSamples(receiveBuffer.data(), CHUNK_SIZE)) > 0) {
            interleavedOutput.insert(interleavedOutput.end(), receiveBuffer.begin(),
                                     receiveBuffer.begin() + static_cast<ptrdiff_t>(received) * srcChannels);
        }
    }

    // extract channel 0 from output
    const auto outputSamplesPerCh = static_cast<unsigned int>(interleavedOutput.size() / srcChannels);
    std::vector<float> outputMono(outputSamplesPerCh);
    for(unsigned int i = 0; i < outputSamplesPerCh; i++) {
        outputMono[i] = interleavedOutput[static_cast<size_t>(i) * srcChannels];
    }

    // current formula: (INITIAL_LATENCY - OUTPUT_SEQUENCE) * speed / sampleRate
    const double compensationS =
        (static_cast<double>(stInitialLatency) - static_cast<double>(stOutputSequence)) * speed / sr;
    // candidate 1: (INPUT_SEQUENCE - 2 * OUTPUT_SEQUENCE) / sampleRate
    const double altCompensationS =
        (static_cast<double>(stInputSequence) - 2.0 * static_cast<double>(stOutputSequence)) / sr;
    // candidate 2: (INPUT_SEQUENCE - 2 * OUTPUT_SEQUENCE - overlap/2) / sampleRate
    const double overlapSamples = st.getSetting(SETTING_OVERLAP_MS) * sr / 1000.0;
    const double alt2CompensationS =
        (static_cast<double>(stInputSequence) - 2.0 * static_cast<double>(stOutputSequence) - overlapSamples / 2.0) /
        sr;

    // sliding NCC
    const double outputDurationS = static_cast<double>(outputSamplesPerCh) / sr;
    const int halfWindow = CORR_WINDOW / 2;

    struct WindowResult {
        double outputTimeS;
        double errorMS;
        double altErrorMS;
        double alt2ErrorMS;
        float correlationPeak;
    };
    std::vector<WindowResult> windows;

    for(double outTimeS = CORR_SKIP_S; outTimeS <= outputDurationS - CORR_SKIP_S; outTimeS += CORR_STEP_S) {
        const int outCenter = static_cast<int>(outTimeS * sr);
        if(outCenter - halfWindow < 0 || outCenter + halfWindow > static_cast<int>(outputSamplesPerCh)) continue;

        // extract output window
        const float *outWin = &outputMono[outCenter - halfWindow];

        // compute output window energy for NCC normalization
        double outEnergy = 0.0;
        for(int i = 0; i < CORR_WINDOW; i++) {
            outEnergy += static_cast<double>(outWin[i]) * outWin[i];
        }
        if(outEnergy < 1e-12) continue;

        // expected source position (in samples)
        const double expectedSrcSample = outTimeS * speed * sr;
        const int searchCenter = static_cast<int>(expectedSrcSample);

        float bestCorr = -1.0f;
        int bestOffset = 0;

        // sweep NCC over search range
        const int searchStart = std::max(halfWindow, searchCenter - CORR_SEARCH);
        const int searchEnd = std::min(static_cast<int>(srcSamplesPerCh) - halfWindow, searchCenter + CORR_SEARCH);

        for(int srcCenter = searchStart; srcCenter < searchEnd; srcCenter++) {
            const float *srcWin = &sourceMono[srcCenter - halfWindow];

            double cross = 0.0;
            double srcEnergy = 0.0;
            for(int i = 0; i < CORR_WINDOW; i++) {
                cross += static_cast<double>(outWin[i]) * srcWin[i];
                srcEnergy += static_cast<double>(srcWin[i]) * srcWin[i];
            }

            const double denom = std::sqrt(outEnergy * srcEnergy);
            if(denom < 1e-12) continue;

            auto ncc = static_cast<float>(cross / denom);
            if(ncc > bestCorr) {
                bestCorr = ncc;
                bestOffset = srcCenter;
            }
        }

        if(bestCorr < 0.0f) continue;

        // parabolic sub-sample interpolation
        double actualSrcSample = bestOffset;
        if(bestOffset > searchStart && bestOffset < searchEnd - 1) {
            const float *srcPrev = &sourceMono[bestOffset - 1 - halfWindow];
            const float *srcNext = &sourceMono[bestOffset + 1 - halfWindow];

            double crossPrev = 0.0, crossNext = 0.0;
            double energyPrev = 0.0, energyNext = 0.0;
            for(int i = 0; i < CORR_WINDOW; i++) {
                crossPrev += static_cast<double>(outWin[i]) * srcPrev[i];
                energyPrev += static_cast<double>(srcPrev[i]) * srcPrev[i];
                crossNext += static_cast<double>(outWin[i]) * srcNext[i];
                energyNext += static_cast<double>(srcNext[i]) * srcNext[i];
            }

            const double denomPrev = std::sqrt(outEnergy * energyPrev);
            const double denomNext = std::sqrt(outEnergy * energyNext);
            const double nccPrev = (denomPrev > 1e-12) ? crossPrev / denomPrev : 0.0;
            const double nccNext = (denomNext > 1e-12) ? crossNext / denomNext : 0.0;

            const double denom2 = 2.0 * (nccPrev - 2.0 * bestCorr + nccNext);
            if(std::abs(denom2) > 1e-12) {
                actualSrcSample += (nccPrev - nccNext) / denom2;
            }
        }

        // compute error: what each formula predicts vs what NCC found
        const double actualSourceS = actualSrcSample / sr;
        const double naiveSourceS = outTimeS * speed;
        const double errorMS = (naiveSourceS - compensationS - actualSourceS) * 1000.0;
        const double altErrorMS = (naiveSourceS - altCompensationS - actualSourceS) * 1000.0;
        const double alt2ErrorMS = (naiveSourceS - alt2CompensationS - actualSourceS) * 1000.0;

        windows.push_back({outTimeS, errorMS, altErrorMS, alt2ErrorMS, bestCorr});
    }

    r.windowCount = static_cast<int>(windows.size());
    if(windows.empty()) {
        debugLog("correlation: speed={:.2f}, no valid windows", speed);
        return r;
    }

    // aggregate results
    double sumError = 0.0, sumAltError = 0.0, sumAlt2Error = 0.0, sumCorr = 0.0;
    double maxAbs = 0.0, altMaxAbs = 0.0, alt2MaxAbs = 0.0;
    for(const auto &w : windows) {
        sumError += w.errorMS;
        sumAltError += w.altErrorMS;
        sumAlt2Error += w.alt2ErrorMS;
        const double ae = std::abs(w.errorMS);
        const double altAe = std::abs(w.altErrorMS);
        const double alt2Ae = std::abs(w.alt2ErrorMS);
        if(ae > maxAbs) maxAbs = ae;
        if(altAe > altMaxAbs) altMaxAbs = altAe;
        if(alt2Ae > alt2MaxAbs) alt2MaxAbs = alt2Ae;
        sumCorr += w.correlationPeak;
    }

    const auto n = static_cast<double>(windows.size());
    r.avgErrorMS = sumError / n;
    r.maxAbsErrorMS = maxAbs;
    r.altAvgErrorMS = sumAltError / n;
    r.altMaxAbsErrorMS = altMaxAbs;
    r.alt2AvgErrorMS = sumAlt2Error / n;
    r.alt2MaxAbsErrorMS = alt2MaxAbs;
    r.avgCorrelation = sumCorr / n;
    r.driftRateMS = windows.back().errorMS - windows.front().errorMS;

    debugLog(
        "correlation: speed={:.2f}x, cur={:>+7.2f}ms, alt1={:>+7.2f}ms (max {:.1f}), "
        "alt2={:>+7.2f}ms (max {:.1f}), NCC={:.4f}, win={:d}",
        speed, r.avgErrorMS, r.altAvgErrorMS, r.altMaxAbsErrorMS, r.alt2AvgErrorMS, r.alt2MaxAbsErrorMS,
        r.avgCorrelation, r.windowCount);

    return r;
}

void AudioTesterImpl::runCorrelationTests() {
    if(m_wavPath.empty()) {
        debugLog("correlation: no WAV file available");
        return;
    }

    debugLog("running cross-correlation tests...");
    m_corrResults.clear();

    for(float speed : COMP_SPEEDS) {
        m_corrResults.push_back(runSingleCorrelationTest(speed));
    }

    m_corrDone = true;
    debugLog("correlation tests complete, {:d} speeds tested", m_corrResults.size());
}

// --- draw ---

void AudioTesterImpl::drawCorrelation(float startY) {
    McFont *font = engine->getDefaultFont();
    if(!font) return;

    const float lineH = font->getHeight() * 1.5f;

    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(20.0f, startY + font->getHeight());

        g->drawString(font, US_("=== SoundTouch Cross-Correlation Test ==="));
        g->translate(0, lineH * 1.2f);

        if(!m_corrDone) {
            g->setColor(0xffaaaaaa);
            g->drawString(font, US_("Press C to run (offline NCC, measures formula accuracy vs chirp signal)"));
        } else {
            g->drawString(font, US_("Speed | (IL-OS)*spd/sr  | (IS-2*OS)/sr   | -OL/2 variant  | NCC"));
            g->translate(0, lineH);
            g->drawString(font, US_("------+-----------------+----------------+----------------+------"));
            g->translate(0, lineH);

            for(const auto &r : m_corrResults) {
                const bool altGood = std::abs(r.altAvgErrorMS) < 5.0;
                const bool alt2Good = std::abs(r.alt2AvgErrorMS) < 5.0;

                auto line = fmt::format(
                    "{:.2f}x | {:>+7.2f} ({:>5.1f}) | {:>+6.2f} ({:>4.1f}) | {:>+6.2f} ({:>4.1f}) | {:.3f}"_cf, r.speed,
                    r.avgErrorMS, r.maxAbsErrorMS, r.altAvgErrorMS, r.altMaxAbsErrorMS, r.alt2AvgErrorMS,
                    r.alt2MaxAbsErrorMS, r.avgCorrelation);

                g->setColor(alt2Good ? 0xff88ff88 : altGood ? 0xffffffff : 0xffff8888);
                g->drawString(font, line);
                g->translate(0, lineH);
            }

            g->translate(0, lineH * 0.5f);
            g->setColor(0xffaaaaaa);
            g->drawString(font, US_("Press C to re-run | Format: avg (maxAbs) in ms | OL = overlap samples"));
        }
    }
    g->popTransform();
}

void AudioTesterImpl::drawBassComparison(float startY) {
    McFont *font = engine->getDefaultFont();
    if(!font) return;

    const float lineH = font->getHeight() * 1.5f;

    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(20.0f, startY + font->getHeight());

        g->drawString(font, US_("=== BASS vs SoLoud Position Comparison ==="));
        g->translate(0, lineH * 1.2f);

        if(m_compState == COMP_IDLE) {
            g->setColor(0xffaaaaaa);
            g->drawString(font, US_("Press B to start (plays muted audio through both engines, ~20s)"));
        } else if(!m_compDone) {
            g->setColor(0xffffff66);
            if(m_compSpeedIdx < static_cast<int>(std::size(COMP_SPEEDS))) {
                auto status = fmt::format("Testing speed {:.2f}x ({:d}/{:d})... {:s}"_cf, COMP_SPEEDS[m_compSpeedIdx],
                                          m_compSpeedIdx + 1, static_cast<int>(std::size(COMP_SPEEDS)),
                                          m_compState == COMP_WARMUP ? "warming up" : "sampling");
                g->drawString(font, status);
            } else {
                g->drawString(font, US_("Finishing..."));
            }
        } else {
            // find 1.0x baseline to subtract engine-constant offset
            double baseline = 0.0;
            for(const auto &r : m_compResults) {
                if(std::abs(r.speed - 1.0f) < 0.01f) {
                    baseline = r.avgDiffS;
                    break;
                }
            }

            g->drawString(font, US_("Speed | SoLoud-BASS | Baselined  | Impulse   | Match?"));
            g->translate(0, lineH);
            g->drawString(font, US_("------+-------------+------------+-----------+-------"));
            g->translate(0, lineH);

            for(const auto &cr : m_compResults) {
                // find matching impulse result
                double impulseOffset = 0.0;
                bool hasImpulse = false;
                for(const auto &ir : m_results) {
                    if(std::abs(ir.speed - cr.speed) < 0.01f) {
                        impulseOffset = ir.avgOffsetS;
                        hasImpulse = true;
                        break;
                    }
                }

                // baselined = raw diff minus 1.0x baseline (removes engine-constant offset)
                const double baselined = cr.avgDiffS - baseline;

                // compare baselined against impulse (relative to impulse at 1.0x)
                double impulseBaselined = 0.0;
                if(hasImpulse) {
                    double impulse1x = 0.0;
                    for(const auto &ir : m_results) {
                        if(std::abs(ir.speed - 1.0f) < 0.01f) {
                            impulse1x = ir.avgOffsetS;
                            break;
                        }
                    }
                    impulseBaselined = impulseOffset - impulse1x;
                }

                const double errorMs = hasImpulse ? std::abs(baselined - impulseBaselined) * 1000.0 : -1.0;

                auto line =
                    fmt::format("{:.2f}x | {:>+8.2f}ms  | {:>+7.2f}ms  | {:>+7.2f}ms | {:s}"_cf, cr.speed,
                                cr.avgDiffS * 1000.0, baselined * 1000.0, hasImpulse ? impulseBaselined * 1000.0 : 0.0,
                                hasImpulse ? (errorMs < 3.0 ? "OK" : fmt::format("{:.1f}ms off"_cf, errorMs)) : "N/A");

                g->setColor((!hasImpulse || errorMs < 3.0) ? 0xffffffff : 0xffff8888);
                g->drawString(font, line);
                g->translate(0, lineH);
            }

            g->translate(0, lineH * 0.5f);
            g->setColor(0xffaaaaaa);
            g->drawString(
                font, fmt::format("Baseline (1.0x raw diff): {:>+.2f}ms | Press B to re-run"_cf, baseline * 1000.0));
            g->translate(0, lineH);
            g->drawString(font,
                          US_("Baselined = raw diff minus 1.0x constant | Impulse = impulse test relative to 1.0x"));
        }
    }
    g->popTransform();
}

void AudioTesterImpl::draw() {
    McFont *font = engine->getDefaultFont();
    if(!font) return;

    const float lineH = font->getHeight() * 1.5f;
    const float startX = 20.0f;
    const float startY = 30.0f;

    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(startX, startY + font->getHeight());

        g->drawString(font, US_("=== SoundTouch Impulse Latency Test ==="));
        g->translate(0, lineH * 1.2f);

        if(!m_bTestsRun) {
            g->drawString(font, US_("Press R to run tests"));
        } else {
            g->drawString(font, US_("Speed | AvgOffset | Jitter    | Hits | ST Algo   | ST*spd    | InSeq     | "
                                    "InSeq*spd | InitLat | InSeq | OutSeq"));
            g->translate(0, lineH);

            g->drawString(font, US_("------+-----------+-----------+------+-----------+-----------+-----------+--------"
                                    "---+---------+-------+-------"));
            g->translate(0, lineH);

            for(const auto &r : m_results) {
                if(r.impulsesMeasured == 0) {
                    auto line = fmt::format("{:.2f}x | (no impulses detected)"_cf, r.speed);
                    g->setColor(0xffff6666);
                    g->drawString(font, line);
                    g->setColor(0xffffffff);
                } else {
                    const double jitterMs = (r.maxOffsetS - r.minOffsetS) * 1000.0;
                    auto line = fmt::format(
                        "{:.2f}x | {:>+7.2f}ms | {:>6.2f}ms  | {:>2d}/{:d} | {:>+7.1f}ms | {:>+7.1f}ms | {:>+7.1f}ms | {:>+7.1f}ms | {:>5d}   | {:>5d} | {:>5d}"_cf,
                        r.speed, r.avgOffsetS * 1000.0, jitterMs, r.impulsesMeasured,
                        static_cast<int>((SIGNAL_DURATION_S - FIRST_IMPULSE_S) / IMPULSE_SPACING_S), r.strat1 * 1000.0,
                        r.strat1n * 1000.0, r.strat2 * 1000.0, r.strat2n * 1000.0, r.initialLatency, r.inputSequence,
                        r.outputSequence);
                    g->drawString(font, line);
                }
                g->translate(0, lineH);
            }

            g->translate(0, lineH * 0.5f);

            g->setColor(0xffaaaaaa);
            g->drawString(font, US_("Press R to re-run impulse | Negative = mStreamPosition BEHIND actual audio"));
        }
    }
    g->popTransform();

    // draw BASS comparison below impulse results
    float compY = startY + (font->getHeight() * 1.5f) * (3.0f + static_cast<float>(m_results.size()) + 2.5f) + 20.0f;
    drawBassComparison(compY);

    // draw correlation results below BASS comparison
    float corrY = compY + lineH * (3.0f + static_cast<float>(m_compResults.size()) + 3.5f) + 20.0f;
    drawCorrelation(corrY);
}

void AudioTesterImpl::update() { updateBassComparison(); }

void AudioTesterImpl::onKeyDown(KeyboardEvent &e) {
    if(e == KEY_R) {
        runImpulseTests();
        e.consume();
    } else if(e == KEY_B) {
        startBassComparison();
        e.consume();
    } else if(e == KEY_C) {
        runCorrelationTests();
        e.consume();
    }
}

// passthroughs to impl
AudioTester::AudioTester() : App(), MouseListener(), m_impl(std::make_unique<AudioTesterImpl>()) {
    // we dont actually use mouse events here right now but just doing this for consistency
    // (TODO: shouldn't need to manually register Apps as mouse listeners?)
    mouse->addListener(this);
}

AudioTester::~AudioTester() { mouse->removeListener(this); }

void AudioTester::draw() { m_impl->draw(); }
void AudioTester::update() { m_impl->update(); }
void AudioTester::onKeyDown(KeyboardEvent &e) { m_impl->onKeyDown(e); }

// misc app stubs (unnecessary)
void AudioTester::onResolutionChanged(vec2 newResolution) { debugLog("{}", newResolution); }
void AudioTester::onDPIChanged() { debugLog(""); }
bool AudioTester::isInGameplay() const { return false; }
bool AudioTester::isInUnpausedGameplay() const { return false; }
void AudioTester::stealFocus() { debugLog(""); }
bool AudioTester::onShutdown() {
    debugLog("");
    return true;
}
Sound *AudioTester::getSound(ActionSound action) const {
    debugLog("{}", static_cast<size_t>(action));
    return nullptr;
}
void AudioTester::showNotification(const NotificationInfo &notif) {
    debugLog("text: {} color: {} duration: {} class: {} preset: {} cb: {:p}", notif.text, notif.custom_color.v,
             notif.duration, static_cast<size_t>(notif.nclass), static_cast<size_t>(notif.preset),
             fmt::ptr(&notif.callback));
    if(notif.callback) {
        notif.callback();
    }
}

void AudioTester::onFocusGained() { debugLog(""); }
void AudioTester::onFocusLost() { debugLog(""); }
void AudioTester::onMinimized() { debugLog(""); }
void AudioTester::onRestored() { debugLog(""); }
void AudioTester::onKeyUp(KeyboardEvent &e) { (void)e; }
void AudioTester::onChar(KeyboardEvent &e) { (void)e; }
void AudioTester::onButtonChange(ButtonEvent event) { (void)event; }
void AudioTester::onWheelVertical(int delta) { (void)delta; }
void AudioTester::onWheelHorizontal(int delta) { (void)delta; }

}  // namespace Mc::Tests

#endif  //  (defined(MCENGINE_FEATURE_SOLOUD) && defined(MCENGINE_FEATURE_BASS))
