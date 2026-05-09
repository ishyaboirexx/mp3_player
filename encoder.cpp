// ============================================================
// encoder.cpp — KY-040 with ISR-based rotation (from proven sketch)
// ============================================================
#include "encoder.h"
#include "config.h"

// ── ISR shared state ──────────────────────────────────────────
// Declared outside the class so the static ISR can access them.
static volatile int  _isrDelta = 0;
static portMUX_TYPE  _isrMux   = portMUX_INITIALIZER_UNLOCKED;
static int           _isrClk   = 0;
static int           _isrDt    = 0;

// IRAM_ATTR: runs from RAM — no flash cache stalls during ISR
static void IRAM_ATTR encoderISR() {
    // Exact same lookup table as your working sketch.
    // old_AB holds (prev2bits << 2) | curr2bits in its lower 4 bits.
    static uint8_t old_AB = 0b11; // resting state: both pins HIGH
    static const int8_t enc_states[] =
        {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};

    uint8_t A = digitalRead(_isrClk);
    uint8_t B = digitalRead(_isrDt);
    old_AB  <<= 2;
    old_AB   |= (A << 1) | B;

    int8_t movement = enc_states[old_AB & 0x0f];
    if (movement != 0) {
        portENTER_CRITICAL_ISR(&_isrMux);
        _isrDelta += movement;
        portEXIT_CRITICAL_ISR(&_isrMux);
    }
}

// ── RotaryEncoder methods ─────────────────────────────────────

void RotaryEncoder::begin() {
    pinMode(_clk, INPUT_PULLUP);
    pinMode(_dt,  INPUT_PULLUP);
    pinMode(_sw,  INPUT_PULLUP);
    pinMode(BTN_BACK_PIN, INPUT_PULLUP);

    _isrClk = _clk;
    _isrDt  = _dt;

    // Attach to CHANGE on both pins — catches every quadrature edge
    attachInterrupt(digitalPinToInterrupt(_clk), encoderISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_dt),  encoderISR, CHANGE);
}

void RotaryEncoder::push(EncEvent ev) {
    int next = (_qTail + 1) % Q_SIZE;
    if (next == _qHead) return;
    _q[_qTail] = ev;
    _qTail = next;
}

EncEvent RotaryEncoder::poll() {
    if (_qHead == _qTail) return EncEvent::NONE;
    EncEvent ev = _q[_qHead];
    _qHead = (_qHead + 1) % Q_SIZE;
    return ev;
}

void RotaryEncoder::update() {
    uint32_t now = millis();

    // ── 1. Read ISR delta atomically ──────────────────────────
    // Same threshold as working sketch: 4 quarter-steps = 1 detent.
    // Remainder is kept so fast spins aren't truncated.
    int delta = 0;
    portENTER_CRITICAL(&_isrMux);
    if (abs(_isrDelta) >= 4) {
        delta     = _isrDelta / 4;
        _isrDelta = _isrDelta % 4;  // keep remainder
    }
    portEXIT_CRITICAL(&_isrMux);

    if      (delta > 0) push(EncEvent::ROT_CW);
    else if (delta < 0) push(EncEvent::ROT_CCW);

    // ── 2. Button FSM ────────────────────────────────────────
    processButton(now);

    // ── 3. Dedicated Tactile Back Button ─────────────────────
    static bool     lastBackState     = HIGH;
    static uint32_t lastBackPressTime = 0;
    bool backState = digitalRead(BTN_BACK_PIN);
    if (lastBackState == HIGH && backState == LOW) {
        if (now - lastBackPressTime > 50) {
            push(EncEvent::BACK_BUTTON);
            lastBackPressTime = now;
        }
    }
    lastBackState = backState;
}

void RotaryEncoder::processButton(uint32_t now) {
    bool rawNow = (digitalRead(_sw) == LOW);

    if (rawNow != _rawStable) {
        _rawStable  = rawNow;
        _debounceAt = now;
    }
    bool pressed = (now - _debounceAt >= DEBOUNCE_MS) && _rawStable;

    switch (_fsm) {
        case BtnFSM::IDLE:
            if (pressed) {
                _fsm       = BtnFSM::PRESSED;
                _pressAt   = now;
                _longFired = false;
            }
            break;

        case BtnFSM::PRESSED:
            if (!pressed) {
                if (!_longFired) {
                    _clicks++;
                    _releaseAt = now;
                    _fsm = BtnFSM::WAIT;
                } else {
                    _fsm = BtnFSM::IDLE;
                }
            } else if (!_longFired && (now - _pressAt >= LONG_PRESS_MS)) {
                _longFired = true;
                _clicks    = 0;
                push(EncEvent::LONG_PRESS);
            }
            break;

        case BtnFSM::WAIT:
            if (pressed) {
                if (now - _releaseAt < MULTI_CLICK_GAP_MS) {
                    _fsm       = BtnFSM::PRESSED;
                    _pressAt   = now;
                    _longFired = false;
                } else {
                    commitClicks();
                    _fsm       = BtnFSM::PRESSED;
                    _pressAt   = now;
                    _clicks    = 0;
                    _longFired = false;
                }
            } else if (now - _releaseAt >= MULTI_CLICK_GAP_MS) {
                commitClicks();
                _fsm = BtnFSM::IDLE;
            }
            break;
    }
}

void RotaryEncoder::commitClicks() {
    switch (_clicks) {
        case 1: push(EncEvent::CLICK_1); break;
        case 2: push(EncEvent::CLICK_2); break;
        default: if (_clicks >= 3) push(EncEvent::CLICK_3); break;
    }
    _clicks = 0;
}
