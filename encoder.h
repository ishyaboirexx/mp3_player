// ============================================================
// encoder.h — Non-blocking KY-040 handler
//
// State machine overview:
//   IDLE → (press) → PRESSED → (release) → RELEASED_WAIT
//   RELEASED_WAIT → (timeout MULTI_CLICK_GAP_MS) → fires click event
//   PRESSED → (hold > LONG_PRESS_MS) → fires LONG_PRESS immediately
//
// Rotation uses falling-edge detection on CLK with DT sampled
// at that moment for direction — matches KY-040 hardware behavior.
//
// Events are queued (capacity 8) so none are lost between calls.
// ============================================================
#pragma once
#include <Arduino.h>
#include "config.h"

enum class EncEvent : uint8_t {
    NONE = 0,
    ROT_CW,         // Clockwise  → scroll down / volume up
    ROT_CCW,        // Counter-CW → scroll up  / volume down
    CLICK_1,        // Single click  → Enter / Play-Pause
    CLICK_2,        // Double click  → Next track
    CLICK_3,        // Triple click  → Previous track
    LONG_PRESS,     // Hold          → Back / EQ menu
    BACK_BUTTON,    // new added, for back everywhere
};

class RotaryEncoder {
public:
    RotaryEncoder(int clk, int dt, int sw)
        : _clk(clk), _dt(dt), _sw(sw) {}

    void begin();

    // Call as often as possible from the UI task (no blocking).
    void update();

    // Dequeue one event; returns NONE if queue empty.
    EncEvent poll();

private:
    // Pins
    const int _clk, _dt, _sw;

    // ── Rotation ─────────────────────────────────────────────
    // Handled entirely by encoderISR() + _isrDelta — no members needed.

    // ── Button FSM ────────────────────────────────────────────
    enum class BtnFSM : uint8_t { IDLE, PRESSED, WAIT };
    BtnFSM   _fsm         = BtnFSM::IDLE;
    bool     _lastRaw     = false;   // debounced raw state
    bool     _rawStable   = false;   // candidate raw value
    uint32_t _debounceAt  = 0;
    uint32_t _pressAt     = 0;
    uint32_t _releaseAt   = 0;
    int      _clicks      = 0;
    bool     _longFired   = false;

    // ── Event ring buffer ─────────────────────────────────────
    static constexpr int Q_SIZE = 8;
    EncEvent _q[Q_SIZE];
    int _qHead = 0, _qTail = 0;

    void push(EncEvent ev);
    void processButton(uint32_t now);
    void commitClicks();
};
