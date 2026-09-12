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
#include "Sound.h"
#include "Timing.h"
#include "Environment.h"

#include "SoLoudSoundEngine.h"
#include "BassSoundEngine.h"

#include "soloud.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Mc::Tests {
namespace {
struct BassComparisonResult {
    float speed;
    double avgDiffS;  // avg(soloud_pos - bass_pos), seconds
    int sampleCount;
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
    // BASS comparison test
    void startBassComparison();
    void updateBassComparison();
    void drawBassComparison(float startY);
    static bool generateTestWav(const std::string &path);

    ComparisonState m_compState{COMP_IDLE};
    int m_compSpeedIdx{0};
    double m_compPhaseStartTime{0};
    std::vector<double> m_compDiffs;  // collected (soloud - bass) diffs for current speed
    std::vector<BassComparisonResult> m_compResults;
    bool m_compDone{false};

    std::unique_ptr<Sound> m_bassSnd{nullptr};
    std::unique_ptr<Sound> m_soloudSnd{nullptr};
    std::string m_wavPath;

    // very hacky
    SoLoudSoundEngine *m_soloud{nullptr};
    BassSoundEngine *m_bass{nullptr};

    bool m_bCreatedSoLoud{false};
};

// speeds to test in BASS comparison (kept small, ~17.5s total test time)
static constexpr float COMP_SPEEDS[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
static constexpr double COMP_WARMUP_S = 0.5;
static constexpr double COMP_SAMPLE_S = 3.0;
static constexpr double COMP_SEEK_POS_S = 5.0;  // seek position for each test

AudioTesterImpl::AudioTesterImpl() {
    debugLog("");

    assert(soundEngine);

    // ultra hacky
    if(soundEngine && soundEngine->getTypeId() == SoundEngine::SOLOUD) {
        m_soloud = static_cast<SoLoudSoundEngine *>(soundEngine);

        m_bass = new BassSoundEngine();
        if(!m_bass || !m_bass->succeeded()) {
            SAFE_DELETE(m_bass);
            debugLog("BASS failed to initialize");
            return;
        }
    } else {
        m_bass = static_cast<BassSoundEngine *>(soundEngine);
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
    // 30-second stereo 44100Hz 16-bit PCM WAV with a linear chirp
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

    // write linear chirp (200Hz to 8000Hz), unique content at every position
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

    // create sounds via respective engines (stream=true for tempo processing)
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

                // log the advance-before-render factor
                const double bufferMs =
                    static_cast<double>(soloud->getBackendBufferSize()) / soloud->getBackendSamplerate() * 1000.0;
                const double advanceBeforeRenderMs = bufferMs * speed;

                debugLog(
                    "BASS comparison: speed={:.2f}x, avg diff(soloud-bass)={:>+7.2f}ms ({:d} samples), "
                    "advance-before-render={:.1f}ms (buf={:.1f}ms * spd)",
                    speed, r.avgDiffS * 1000.0, r.sampleCount, advanceBeforeRenderMs, bufferMs);

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

// --- draw ---

void AudioTesterImpl::drawBassComparison(float startY) {
    McFont *font = engine->getDefaultFont();
    if(!font) return;

    const float lineH = font->getHeight() * 1.5f;

    g->setColor(0xffffffff);
    g->pushTransform();
    {
        g->translate(20.0f, startY + font->getHeight());

        g->drawString(font, "=== BASS vs SoLoud Position Comparison ===");
        g->translate(0, lineH * 1.2f);

        if(m_compState == COMP_IDLE) {
            g->setColor(0xffaaaaaa);
            g->drawString(font, "Press B to start (plays muted audio through both engines, ~20s)");
        } else if(!m_compDone) {
            g->setColor(0xffffff66);
            if(m_compSpeedIdx < static_cast<int>(std::size(COMP_SPEEDS))) {
                auto status = fmt::format("Testing speed {:.2f}x ({:d}/{:d})... {:s}", COMP_SPEEDS[m_compSpeedIdx],
                                          m_compSpeedIdx + 1, static_cast<int>(std::size(COMP_SPEEDS)),
                                          m_compState == COMP_WARMUP ? "warming up" : "sampling");
                g->drawString(font, status);
            } else {
                g->drawString(font, "Finishing...");
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

            g->drawString(font, "Speed | SoLoud-BASS | Baselined");
            g->translate(0, lineH);
            g->drawString(font, "------+-------------+-----------");
            g->translate(0, lineH);

            for(const auto &cr : m_compResults) {
                // baselined = raw diff minus 1.0x baseline (removes engine-constant offset)
                const double baselined = cr.avgDiffS - baseline;

                auto line = fmt::format("{:.2f}x | {:>+8.2f}ms  | {:>+7.2f}ms", cr.speed, cr.avgDiffS * 1000.0,
                                        baselined * 1000.0);

                g->setColor(std::abs(baselined) < 0.003 ? 0xffffffff : 0xffff8888);
                g->drawString(font, line);
                g->translate(0, lineH);
            }

            g->translate(0, lineH * 0.5f);
            g->setColor(0xffaaaaaa);
            g->drawString(font,
                          fmt::format("Baseline (1.0x raw diff): {:>+.2f}ms | Press B to re-run", baseline * 1000.0));
            g->translate(0, lineH);
            g->drawString(font, "Baselined = raw diff minus 1.0x constant (should stay near 0 at every speed)");
        }
    }
    g->popTransform();
}

void AudioTesterImpl::draw() { drawBassComparison(30.0f); }

void AudioTesterImpl::update() { updateBassComparison(); }

void AudioTesterImpl::onKeyDown(KeyboardEvent &e) {
    if(e == KEY_B) {
        startBassComparison();
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
bool AudioTester::onShutdown() {
    debugLog("");
    return true;
}
Sound *AudioTester::getSound(ActionSound action) const {
    debugLog("{}", static_cast<size_t>(action));
    return nullptr;
}
void AudioTester::showNotification(const NotificationInfo &notif) {
    debugLog("text: {} color: {} duration: {} class: {} preset: {} cb: {:p}", notif.text, notif.custom_color,
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
void AudioTester::onButtonChange(ButtonEvent &event) { (void)event; }
void AudioTester::onWheelVertical(int delta) { (void)delta; }
void AudioTester::onWheelHorizontal(int delta) { (void)delta; }

}  // namespace Mc::Tests

#endif  //  (defined(MCENGINE_FEATURE_SOLOUD) && defined(MCENGINE_FEATURE_BASS))
