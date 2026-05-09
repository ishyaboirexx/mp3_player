// ============================================================
// equalizer.cpp
// ============================================================
#include "equalizer.h"

// ── Preset table ──────────────────────────────────────────────
// Bands ordered: bass shelf → low-mid → mid → high-mid → treble shelf
const EQPreset Equalizer::PRESETS[Equalizer::NUM_PRESETS] = {

    { "Flat", {
        {BQType::LOW_SHELF,  100.f,  0.f, 0.707f},
        {BQType::PEAK_EQ,   1000.f,  0.f, 1.000f},
        {BQType::HIGH_SHELF,8000.f,  0.f, 0.707f},
    }, 3 },

    { "Rock", {
        {BQType::LOW_SHELF,   80.f, +6.f, 0.707f},
        {BQType::PEAK_EQ,    250.f, -2.f, 1.000f},
        {BQType::PEAK_EQ,   2500.f, +2.f, 1.000f},
        {BQType::PEAK_EQ,   5000.f, +3.f, 1.200f},
        {BQType::HIGH_SHELF,10000.f,+3.f, 0.707f},
    }, 5 },

    { "Pop", {
        {BQType::LOW_SHELF,  120.f, +2.f, 0.707f},
        {BQType::PEAK_EQ,    500.f, -1.f, 1.000f},
        {BQType::PEAK_EQ,   1500.f, +3.f, 1.000f},
        {BQType::PEAK_EQ,   5000.f, +3.f, 1.200f},
        {BQType::HIGH_SHELF,10000.f,+2.f, 0.707f},
    }, 5 },

    { "Jazz", {
        {BQType::LOW_SHELF,  100.f, +3.f, 0.707f},
        {BQType::PEAK_EQ,    400.f, +2.f, 1.000f},
        {BQType::PEAK_EQ,   1200.f, -1.f, 1.000f},
        {BQType::PEAK_EQ,   4000.f, +2.f, 1.200f},
        {BQType::HIGH_SHELF, 8000.f,+1.f, 0.707f},
    }, 5 },

    { "Classical", {
        {BQType::LOW_SHELF,   80.f, +2.f, 0.707f},
        {BQType::PEAK_EQ,    250.f, -1.f, 1.000f},
        {BQType::PEAK_EQ,   1000.f,  0.f, 1.000f},
        {BQType::PEAK_EQ,   4000.f, +1.f, 1.200f},
        {BQType::HIGH_SHELF, 9000.f,+2.f, 0.707f},
    }, 5 },

    { "Bass Boost", {
        {BQType::LOW_SHELF,   60.f, +8.f, 0.707f},
        {BQType::PEAK_EQ,    150.f, +5.f, 1.000f},
        {BQType::PEAK_EQ,    500.f, -1.f, 1.000f},
        {BQType::HIGH_SHELF, 8000.f,-1.f, 0.707f},
    }, 4 },

    { "Vocal", {
        {BQType::LOW_SHELF,  200.f, -2.f, 0.707f},
        {BQType::PEAK_EQ,    800.f, +2.f, 1.000f},
        {BQType::PEAK_EQ,   2500.f, +4.f, 1.200f},
        {BQType::PEAK_EQ,   5000.f, +2.f, 1.000f},
        {BQType::HIGH_SHELF, 8000.f,+1.f, 0.707f},
    }, 5 },
};

// ── Init ──────────────────────────────────────────────────────
void Equalizer::init(float sr) {
    _sr = sr;
    setPreset(0);
}

// ── Preset switch ─────────────────────────────────────────────
void Equalizer::setPreset(int idx) {
    if (idx < 0 || idx >= NUM_PRESETS) return;
    _preset = idx;
    const EQPreset& p = PRESETS[idx];
    _nBands = p.nBands;

    for (int i = 0; i < _nBands; ++i) {
        buildCoeffs(p.bands[i], _c[i]);
        // Reset delay elements to prevent transient pop on preset change
        for (int ch = 0; ch < 2; ++ch)
            _s[i][ch] = {0.f, 0.f};
    }
}

const char* Equalizer::presetName(int idx) const {
    if (idx < 0 || idx >= NUM_PRESETS) return "???";
    return PRESETS[idx].name;
}

// ── Bilinear-transform biquad coefficient computation ─────────
// Reference: Audio EQ Cookbook — Robert Bristow-Johnson
void Equalizer::buildCoeffs(const BQBand& b, BQCoeffs& c) {
    const float pi   = (float)M_PI;
    float A   = powf(10.f, b.gainDb / 40.f);  // linear amplitude
    float w0  = 2.f * pi * b.freq / _sr;
    float cw  = cosf(w0);
    float sw  = sinf(w0);
    float alp = sw / (2.f * b.Q);

    float b0, b1, b2, a0, a1, a2;

    switch (b.type) {

        case BQType::PEAK_EQ:
            b0 = 1.f + alp * A;
            b1 = -2.f * cw;
            b2 = 1.f - alp * A;
            a0 = 1.f + alp / A;
            a1 = -2.f * cw;
            a2 = 1.f - alp / A;
            break;

        case BQType::LOW_SHELF: {
            float sqA = sqrtf(A);
            b0 =     A * ((A+1) - (A-1)*cw + 2*sqA*alp);
            b1 = 2 * A * ((A-1) - (A+1)*cw);
            b2 =     A * ((A+1) - (A-1)*cw - 2*sqA*alp);
            a0 =          (A+1) + (A-1)*cw + 2*sqA*alp;
            a1 =    -2 * ((A-1) + (A+1)*cw);
            a2 =          (A+1) + (A-1)*cw - 2*sqA*alp;
        } break;

        case BQType::HIGH_SHELF: {
            float sqA = sqrtf(A);
            b0 =      A * ((A+1) + (A-1)*cw + 2*sqA*alp);
            b1 = -2 * A * ((A-1) + (A+1)*cw);
            b2 =      A * ((A+1) + (A-1)*cw - 2*sqA*alp);
            a0 =           (A+1) - (A-1)*cw + 2*sqA*alp;
            a1 =      2 * ((A-1) - (A+1)*cw);
            a2 =           (A+1) - (A-1)*cw - 2*sqA*alp;
        } break;

        default:
            c = {1.f, 0.f, 0.f, 0.f, 0.f};
            return;
    }

    // Normalise: a0 = 1
    c.b0 = b0 / a0;  c.b1 = b1 / a0;  c.b2 = b2 / a0;
    c.a1 = a1 / a0;  c.a2 = a2 / a0;
}

// ── Direct Form II Transposed — minimal multiply/add overhead ─
float Equalizer::applyBQ(float x, const BQCoeffs& c, BQState& s) {
    float y = c.b0 * x + s.z1;
    s.z1    = c.b1 * x - c.a1 * y + s.z2;
    s.z2    = c.b2 * x - c.a2 * y;
    return y;
}

void Equalizer::processBuffer(int16_t* buf, int frames) {
    if (_preset == 0 || _nBands == 0) return; // Flat bypass
    for (int f = 0; f < frames; ++f) {
        buf[f*2]   = process(buf[f*2],   0);
        buf[f*2+1] = process(buf[f*2+1], 1);
    }
}
