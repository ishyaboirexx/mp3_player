// ============================================================
// ui_task.h — Core-1 UI engine
//
// Library choice: U8g2 (full-buffer mode)
//   ✔ Complete 128×64 frame composed in RAM, sent in one I2C burst
//   ✔ Zero flicker — display never sees a partially-drawn frame
//   ✔ Rich font/glyph support with proportional fonts
//   ✔ Hardware I2C — no bit-banging, minimal CPU overhead
//
// Menu navigation uses a simple index + scroll-offset scheme.
// The display task runs at ~30 FPS; encoder is polled at
// the same rate which is more than fast enough for human input.
// ============================================================
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "config.h"
#include "encoder.h"
#include "shared_state.h"
#include "music_library.h"

enum class UIScreen : uint8_t {
    BOOT,       // Splash during library scan
    NO_MUSIC,   // SD empty / mount failed
    ARTISTS,    // Layer 1 list
    ALBUMS,     // Layer 2 list
    SONGS,      // Layer 3 list
    PLAYER,     // Now-playing view
    EQ_MENU,    // EQ preset picker
};

class WalkmanUI {
public:
    static WalkmanUI& get() {
        static WalkmanUI inst;
        return inst;
    }

    void init();    // Scan SD, init display + encoder
    void update();  // Call from UI task loop (non-blocking)

private:
    WalkmanUI() = default;

    // ── Hardware ──────────────────────────────────────────────
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C* _disp = nullptr;
    RotaryEncoder*                         _enc  = nullptr;

    // ── Screen FSM ────────────────────────────────────────────
    UIScreen _screen     = UIScreen::BOOT;
    UIScreen _prevScreen = UIScreen::BOOT; // for "back" navigation

    // ── Navigation indices ────────────────────────────────────
    int _ai = 0;   // artist
    int _li = 0;   // album
    int _si = 0;   // song
    int _scrollOff = 0; // top-of-visible-window index

    // ── Player state mirror ───────────────────────────────────
    int _volume  = VOLUME_DEFAULT;
    int _eqPreset = 0;

    // ── Ticker for long song names ────────────────────────────
    int      _tickerPos    = 0;
    uint32_t _tickerLast   = 0;
    int      _tickerDelay  = 0; // countdown frames before scroll starts

    // ── Timing ────────────────────────────────────────────────
    uint32_t _lastFrame  = 0;   // FPS limiter

    // ── Input handlers ────────────────────────────────────────
    void onEvent(EncEvent ev);
    void onRotate(int dir);
    void onSingleClick();
    void onDoubleClick();
    void onTripleClick();
    void onLongPress();
    void onBackButton();

    // ── Navigation helpers ────────────────────────────────────
    void   enterArtist();
    void   enterAlbum();
    void   playSong(int ai, int li, int si);
    void   nextSong();
    void   prevSong();
    void   syncScrollTo(int sel, int total);  // keep sel in view

    // ── Audio command senders ─────────────────────────────────
    void cmdPlay(const char* path, const char* song,
                 const char* artist, const char* album);
    void cmdPauseToggle();
    void cmdVolume(int vol);
    void cmdEQ(int preset);

    // ── Rendering ─────────────────────────────────────────────
    void render();
    void drawBoot();
    void drawNoMusic();
    void drawList(const char* title,
                  std::function<const char*(int)> item,
                  int total, int sel);
    void drawArtists();
    void drawAlbums();
    void drawSongs();
    void drawPlayer();
    void drawEQ();

    // ── Helpers ───────────────────────────────────────────────
    void drawProgressBar(int x, int y, int w, int h,
                         int filled, int total, bool outlined = true);
    void drawTicker(int x, int y, int maxW, const char* text);
    void resetTicker();
};
