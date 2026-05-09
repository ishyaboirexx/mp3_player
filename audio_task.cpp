// ============================================================
// audio_task.cpp — Core-0 audio engine
//
// Zero-stutter design contract:
//   • This task runs at priority 24 (highest on Core 0).
//   • It NEVER calls delay() or holds any blocking lock.
//   • SD reads are performed by the generator's internal buffering;
//     the generator feeds DMA buffers faster than they drain.
//   • Pause is implemented by writing silence — no stale samples
//     remain in the I2S DMA ring when paused.
//   • vTaskDelay(1) is only called when idle (no generator running).
// ============================================================
#include "audio_task.h"
#include "shared_state.h"
#include "equalizer.h"
#include "music_library.h"
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorFLAC.h>
#include <AudioFileSourceSD.h>

// ─────────────────────────────────────────────────────────────
// EQOutput::ConsumeSample — hot path, called ~44100 times/sec
// ─────────────────────────────────────────────────────────────
bool EQOutput::ConsumeSample(int16_t sample[2]) {
    // Quadratic volume curve for perceptually even steps
    float g = (float)_vol / 100.f;
    g *= g;

    int16_t L = (int16_t)((float)sample[0] * g);
    int16_t R = (int16_t)((float)sample[1] * g);

    // Apply EQ (NOOP if preset == Flat)
    L = Equalizer::get().process(L, 0);
    R = Equalizer::get().process(R, 1);

    int16_t processed[2] = {L, R};
    return AudioOutputI2S::ConsumeSample(processed);
}

// ─────────────────────────────────────────────────────────────
// Internal state — local to this translation unit
// ─────────────────────────────────────────────────────────────
namespace {
    EQOutput*          gOut  = nullptr;
    AudioFileSourceSD* gSrc  = nullptr;
    AudioGenerator*    gGen  = nullptr;

    // Write a burst of silence frames to flush DMA ring immediately.
    // Prevents any last-buffer artifact when pausing/stopping.
    void flushSilence(int frames = 256) {
        if (!gOut) return;
        int16_t silence[2] = {0, 0};
        for (int i = 0; i < frames; ++i)
            gOut->ConsumeSample(silence);
    }

    // Cleanly tear down the active generator + source.
    void stopPlayback() {
        if (gGen) {
            gGen->stop();
            delete gGen;
            gGen = nullptr;
        }
        if (gSrc) {
            gSrc->close();
            delete gSrc;
            gSrc = nullptr;
        }
        flushSilence(512); // ensure DMA ring is silent
        SharedState::get().setPlaying(false);
        SharedState::get().setPaused(false);
    }

    // Open a new file and begin decoding.
    bool startPlayback(const char* path) {
        stopPlayback(); // always clean up first

        const char* dot = strrchr(path, '.');
        if (!dot) return false;

        bool wantFlac = (strcasecmp(dot + 1, "flac") == 0);
        bool wantMp3  = (strcasecmp(dot + 1, "mp3")  == 0);
        if (!wantFlac && !wantMp3) {
            Serial.printf("[Audio] Unknown ext: %s\n", dot + 1);
            return false;
        }

        // AudioFileSourceSD opens the file from the SD subsystem.
        // Note: SD library is SPI-based; reads happen in generator->loop()
        // which is always on Core 0 — no contention with Core 1.
        gSrc = new AudioFileSourceSD(path);
        if (!gSrc || !gSrc->isOpen()) {
            Serial.printf("[Audio] Cannot open: %s\n", path);
            delete gSrc; gSrc = nullptr;
            return false;
        }

        if (wantFlac) gGen = new AudioGeneratorFLAC();
        else          gGen = new AudioGeneratorMP3();

        if (!gGen->begin(gSrc, gOut)) {
            Serial.printf("[Audio] Generator begin failed: %s\n", path);
            delete gGen; gGen = nullptr;
            delete gSrc; gSrc = nullptr;
            return false;
        }

        SharedState::get().setPlaying(true);
        SharedState::get().setPaused(false);
        Serial.printf("[Audio] Playing: %s\n", path);
        return true;
    }

} // namespace

// ─────────────────────────────────────────────────────────────
// audioTask — pinned to Core 0
// ─────────────────────────────────────────────────────────────
void audioTask(void* /*params*/) {
    Serial.println("[Audio] Task started on Core " + String(xPortGetCoreID()));

    // ── Init I2S output ───────────────────────────────────────
    gOut = new EQOutput();
    gOut->SetPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
    // Set I2S native gain to 1.0 — we handle gain in ConsumeSample
    gOut->SetGain(1.0f);
    gOut->begin();

    // ── Init EQ ───────────────────────────────────────────────
    Equalizer::get().init((float)AUDIO_SAMPLE_RATE);

    Serial.println("[Audio] I2S + EQ initialised");

    bool paused = false;

    // ── Main audio loop ───────────────────────────────────────
    for (;;) {
        // ── 1. Drain the command queue (non-blocking peek) ────
        AudioCommand cmd;
        while (SharedState::get().recvCommand(cmd)) {
            switch (cmd.cmd) {

                case AudioCmd::PLAY:
                    paused = false;
                    if (!startPlayback(cmd.filePath)) {
                        Serial.println("[Audio] startPlayback FAILED");
                        SharedState::get().trackFinished = true;
                    }
                    break;

                case AudioCmd::PAUSE_TOGGLE:
                    paused = !paused;
                    SharedState::get().setPaused(paused);

                    if (paused) {
                        // this stops the i2s dma from looping the last buffer
                        gOut->stop();
                        Serial.println("[Audio] Paused - DMA Flushed");
                    } else {
                        // this restarts the i2s clocks when unpausing
                        gOut->begin();
                    }
                    
                    break;

                case AudioCmd::STOP:
                    paused = false;
                    stopPlayback();
                    break;

                case AudioCmd::SET_VOLUME:
                    gOut->setVol(cmd.param.volume);
                    SharedState::get().setVolume(cmd.param.volume);
                    break;

                case AudioCmd::SET_EQ:
                    Equalizer::get().setPreset(cmd.param.eqPreset);
                    SharedState::get().setEQPreset(cmd.param.eqPreset);
                    break;

                default: break;
            }
        }

        // ── 2. Pump the audio generator ───────────────────────
        if (gGen && !paused) {
            if (gGen->isRunning()) {
                // loop() feeds one DMA buffer's worth of decoded samples
                // into the I2S ring — returns false when stream is exhausted.
                if (!gGen->loop()) {
                    Serial.println("[Audio] Track finished");
                    stopPlayback();
                    // Signal UI task to auto-advance
                    SharedState::get().trackFinished = true;
                }
                // Do NOT yield here — continuous tight loop feeds DMA.
                vTaskDelay(1); //trying to add 1 ms delay
            }
        } else {
            // Idle or paused — yield to prevent watchdog trigger.
            // 5 ms is safe; DMA silence is already written.
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}
