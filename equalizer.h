// ============================================================
// equalizer.h — Biquad IIR EQ with named presets
//
// Implementation: Direct Form II Transposed biquad stages.
// Each preset defines up to 5 bands; Flat preset short-circuits
// to a pass-through (no compute cost on default setting).
//
// Why biquad IIR?
//   - O(N) per sample — zero additional latency vs FIR
//   - Exact analog-matched frequency response via bilinear transform
//   - Runs comfortably on ESP32-S3 @ 240 MHz alongside MP3 decode
// ============================================================
#pragma once
#include <Arduino.h>
#include <cmath>
#include "config.h"

enum class BQType : uint8_t {
    LOW_SHELF,
    HIGH_SHELF,
    PEAK_EQ,
};

struct BQBand {
    BQType type;
    float  freq;     // Hz
    float  gainDb;   // ±dB
    float  Q;        // quality factor
};

struct BQCoeffs {
    float b0, b1, b2;   // numerator
    float a1, a2;       // denominator (a0 normalised to 1)
};

struct BQState {
    float z1 = 0.f, z2 = 0.f; // per-channel transposed delay elements
};

struct EQPreset {
    const char* name;
    BQBand      bands[5];
    int         nBands;
};

class Equalizer {
public:
    static Equalizer& get() {
        static Equalizer inst;
        return inst;
    }

    void init(float sampleRate = AUDIO_SAMPLE_RATE);

    void       setPreset(int idx);
    int        currentPreset()  const { return _preset; }
    int        presetCount()    const { return NUM_PRESETS; }
    const char* presetName(int idx) const;

    // Process one int16_t sample for given channel (0=L, 1=R)
    // MUST be called from audio task only (no mutex needed — single writer).
    inline int16_t process(int16_t in, int ch);

    // Convenience: process interleaved stereo buffer in-place
    void processBuffer(int16_t* buf, int frames);

    static constexpr int NUM_PRESETS = 7;
    static constexpr int MAX_BANDS   = 5;

private:
    Equalizer() = default;

    void  buildCoeffs(const BQBand& b, BQCoeffs& c);
    float applyBQ(float x, const BQCoeffs& c, BQState& s);

    float    _sr      = AUDIO_SAMPLE_RATE;
    int      _preset  = 0;
    int      _nBands  = 0;

    BQCoeffs _c[MAX_BANDS];
    BQState  _s[MAX_BANDS][2]; // [band][channel]

    static const EQPreset PRESETS[NUM_PRESETS];
};

// ── Inline hot path ───────────────────────────────────────────
inline int16_t Equalizer::process(int16_t in, int ch) {
    if (_preset == 0 || _nBands == 0) return in; // Flat bypass

    float x = (float)in;
    for (int i = 0; i < _nBands; ++i)
        x = applyBQ(x, _c[i], _s[i][ch]);

    // Hard clip to int16 range — avoids undefined behaviour on overflow
    if (x >  32767.f) x =  32767.f;
    if (x < -32768.f) x = -32768.f;
    return (int16_t)x;
}
