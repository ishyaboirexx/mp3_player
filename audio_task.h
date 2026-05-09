// ============================================================
// audio_task.h — Core-0 audio engine (MP3 + FLAC playback)
//
// Library choice: ESP8266Audio (Earle Philhower)
//   ✔ Native ESP32 I2S DMA support
//   ✔ Handles both MP3 (helix decoder) and FLAC
//   ✔ Proven, maintained, minimal RAM footprint
//   ✔ Fits our "single audio loop" architecture perfectly
//
// Custom AudioOutput subclass (EQOutput) intercepts ConsumeSample()
// to apply biquad EQ and quadratic-curve volume scaling before
// writing samples to the UDA1334A via I2S DMA.
//
// Volume curve: gain = (vol/100)²  — perceptually linear loudness.
// ============================================================
#pragma once
#include <Arduino.h>
#include <AudioOutputI2S.h>
#include "config.h"

// ── Custom I2S output: applies EQ + volume inline ─────────────
class EQOutput : public AudioOutputI2S {
public:
    // EXTERNAL_I2S → we set pins with SetPinout(); port 0
    EQOutput() : AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S,
                                 I2S_DMA_BUF_COUNT, APLL_AUTO) {}

    // Override ConsumeSample to inject EQ and volume.
    // Called from audio generator's loop() — always on Core 0.
    bool ConsumeSample(int16_t sample[2]) override;

    // vol: 0–100
    void setVol(int vol) { _vol = constrain(vol, 0, 100); }
    int  getVol() const  { return _vol; }

private:
    int _vol = VOLUME_DEFAULT;
};

// ── Task entry point — pin to Core 0 ─────────────────────────
void audioTask(void* params);
