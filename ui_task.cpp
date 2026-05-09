// ============================================================
// ui_task.cpp — Complete UI implementation
// ============================================================
#include "ui_task.h"
#include "equalizer.h"
#include <functional>

// ─────────────────────────────────────────────────────────────
// Initialisation
// ─────────────────────────────────────────────────────────────
void WalkmanUI::init() {
    // ── Display (full-buffer = F variant → zero flicker) ─────
    _disp = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(
        U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);
    _disp->begin();
    _disp->setFontPosTop();
    _disp->setDrawColor(1);

    // ── Boot splash ───────────────────────────────────────────
    _disp->clearBuffer();
    _disp->setFont(u8g2_font_logisoso16_tf);
    _disp->drawStr(24, 14, "WALKMAN");
    _disp->setFont(u8g2_font_6x10_tf);
    _disp->drawStr(32, 40, "Loading...");
    _disp->drawFrame(10, 52, 108, 8);   // progress outline
    _disp->sendBuffer();

    // ── Encoder ──────────────────────────────────────────────
    _enc = new RotaryEncoder(ENC_CLK_PIN, ENC_DT_PIN, ENC_SW_PIN);
    _enc->begin();

    // ── Scan music library ────────────────────────────────────
    bool ok = MusicLibrary::get().scan("/");

    // Animate progress bar during scan (it's fast but looks polished)
    _disp->clearBuffer();
    _disp->setFont(u8g2_font_logisoso16_tf);
    _disp->drawStr(24, 14, "WALKMAN");
    _disp->setFont(u8g2_font_6x10_tf);
    _disp->drawFrame(10, 52, 108, 8);
    _disp->drawBox(11, 53, 106, 6);    // full bar = done
    _disp->sendBuffer();
    delay(400); // brief hold so user sees the loaded state

    if (ok && MusicLibrary::get().artistCount() > 0) {
        _screen = UIScreen::ARTISTS;
    } else {
        _screen = UIScreen::NO_MUSIC;
    }

    // ── Send initial volume to audio task ─────────────────────
    cmdVolume(_volume);
    cmdEQ(_eqPreset);
}

// ─────────────────────────────────────────────────────────────
// Main update — called every ~10 ms from UI task loop
// ─────────────────────────────────────────────────────────────
void WalkmanUI::update() {
    // ── Encoder polling ───────────────────────────────────────
    _enc->update();
    EncEvent ev;
    while ((ev = _enc->poll()) != EncEvent::NONE)
        onEvent(ev);

    // ── Auto-advance when track ends ──────────────────────────
    if (SharedState::get().trackFinished.exchange(false)) {
        if (_screen == UIScreen::PLAYER) nextSong();
    }

    // ── Frame rate limiter ────────────────────────────────────
    uint32_t now = millis();
    if (now - _lastFrame < (1000u / DISPLAY_FPS)) return;
    _lastFrame = now;

    render();
}

// ─────────────────────────────────────────────────────────────
// Event dispatch
// ─────────────────────────────────────────────────────────────
void WalkmanUI::onEvent(EncEvent ev) {
    switch (ev) {
        case EncEvent::ROT_CW:   onRotate(+1); break;
        case EncEvent::ROT_CCW:  onRotate(-1); break;
        case EncEvent::CLICK_1:  onSingleClick(); break;
        case EncEvent::CLICK_2:  onDoubleClick(); break;
        case EncEvent::CLICK_3:  onTripleClick(); break;
        case EncEvent::LONG_PRESS: onLongPress(); break;
        case EncEvent::BACK_BUTTON: onBackButton(); break; //new button for back
        default: break;
    }
}

void WalkmanUI::onRotate(int dir) {
    auto wrap = [](int v, int n) { return ((v % n) + n) % n; };

    switch (_screen) {
        case UIScreen::ARTISTS: {
            int n = MusicLibrary::get().artistCount();
            _ai = wrap(_ai + dir, n);
            syncScrollTo(_ai, n);
            break;
        }
        case UIScreen::ALBUMS: {
            const Artist* a = MusicLibrary::get().artist(_ai);
            if (!a) break;
            int n = (int)a->albums.size();
            _li = wrap(_li + dir, n);
            syncScrollTo(_li, n);
            break;
        }
        case UIScreen::SONGS: {
            const Album* al = MusicLibrary::get().album(_ai, _li);
            if (!al) break;
            int n = (int)al->songs.size();
            _si = wrap(_si + dir, n);
            syncScrollTo(_si, n);
            break;
        }
        case UIScreen::PLAYER:
            // Rotation → volume adjust
            _volume = constrain(_volume + dir * VOLUME_STEP, VOLUME_MIN, VOLUME_MAX);
            cmdVolume(_volume);
            break;
        case UIScreen::EQ_MENU: {
            int n = Equalizer::get().presetCount();
            _eqPreset = wrap(_eqPreset + dir, n);
            syncScrollTo(_eqPreset, n);  // fixes frozen scroll
            cmdEQ(_eqPreset);
            break;
        }
        default: break;
    }
}

void WalkmanUI::onSingleClick() {
    switch (_screen) {
        case UIScreen::ARTISTS: enterArtist(); break;
        case UIScreen::ALBUMS:  enterAlbum();  break;
        case UIScreen::SONGS:   playSong(_ai, _li, _si); break;
        case UIScreen::PLAYER:  cmdPauseToggle(); break;
        case UIScreen::EQ_MENU: _screen = UIScreen::PLAYER; break;
        default: break;
    }
}

void WalkmanUI::onDoubleClick() {
    // Double click = next track (works in Player and Song list)
    if (_screen == UIScreen::PLAYER || _screen == UIScreen::SONGS)
        nextSong();
}

void WalkmanUI::onTripleClick() {
    // Triple click = previous track
    if (_screen == UIScreen::PLAYER || _screen == UIScreen::SONGS)
        prevSong();
}

void WalkmanUI::onBackButton() {
    switch (_screen) {
        case UIScreen::ALBUMS:
            _screen = UIScreen::ARTISTS;
            _li = 0; _scrollOff = 0;
            break;
        case UIScreen::SONGS:
            _screen = UIScreen::ALBUMS;
            _si = 0; _scrollOff = 0;
            break;
        case UIScreen::EQ_MENU:
            _screen = UIScreen::PLAYER;
            break;
        case UIScreen::PLAYER:
            // Optionally: decide if back button exits player to library
            _screen = UIScreen::SONGS; 
            break;
        default:
            // Default behavior to reset to the top artist level
            _screen = UIScreen::ARTISTS;
            _ai = _li = _si = _scrollOff = 0;
            break;
    }
}

void WalkmanUI::onLongPress() {
    switch (_screen) {
        case UIScreen::PLAYER:
            // Only open EQ if we are actually playing/viewing the player
            _eqPreset = Equalizer::get().currentPreset();
            _scrollOff = _eqPreset;
            _screen = UIScreen::EQ_MENU;
            break;

        case UIScreen::EQ_MENU:
            // Close EQ and return to player
            _screen = UIScreen::PLAYER;
            break;

        default:
            // For all other screens (Artists, Albums, etc.), 
            // do nothing or add custom behavior here.
            break;
    }
} 

//void WalkmanUI::onLongPress() {
//    switch (_screen) {
            // Long press in player → open EQ menu
//            _eqPreset = Equalizer::get().currentPreset();
//            _scrollOff = _eqPreset;
//            _screen = UIScreen::EQ_MENU;
//            break;
//        case UIScreen::EQ_MENU:
//            _screen = UIScreen::PLAYER;
//            break;
//        default:
//    }
//}

// ─────────────────────────────────────────────────────────────
// Navigation helpers
// ─────────────────────────────────────────────────────────────
void WalkmanUI::enterArtist() {
    const Artist* a = MusicLibrary::get().artist(_ai);
    if (!a || a->albums.empty()) return;
    _li = 0; _scrollOff = 0;
    _screen = UIScreen::ALBUMS;
}

void WalkmanUI::enterAlbum() {
    const Album* al = MusicLibrary::get().album(_ai, _li);
    if (!al || al->songs.empty()) return;
    _si = 0; _scrollOff = 0;
    _screen = UIScreen::SONGS;
}

void WalkmanUI::playSong(int ai, int li, int si) {
    const Artist* a  = MusicLibrary::get().artist(ai);
    const Album*  al = MusicLibrary::get().album(ai, li);
    const Song*   s  = MusicLibrary::get().song(ai, li, si);
    if (!a || !al || !s) return;

    _ai = ai; _li = li; _si = si;
    resetTicker();
    cmdPlay(s->path, s->name, a->name, al->name);
    _screen = UIScreen::PLAYER;
}

void WalkmanUI::nextSong() {
    const Album* al = MusicLibrary::get().album(_ai, _li);
    if (!al || al->songs.empty()) return;
    _si = (_si + 1) % (int)al->songs.size();
    playSong(_ai, _li, _si);
}

void WalkmanUI::prevSong() {
    const Album* al = MusicLibrary::get().album(_ai, _li);
    if (!al || al->songs.empty()) return;
    _si = (_si - 1 + (int)al->songs.size()) % (int)al->songs.size();
    playSong(_ai, _li, _si);
}

void WalkmanUI::syncScrollTo(int sel, int total) {
    (void)total;
    // Scroll window: keep selected item inside [scrollOff, scrollOff+MENU_VISIBLE)
    if (sel < _scrollOff)
        _scrollOff = sel;
    if (sel >= _scrollOff + MENU_VISIBLE)
        _scrollOff = sel - MENU_VISIBLE + 1;
}

// ─────────────────────────────────────────────────────────────
// Command senders (UI → Audio task via queue)
// ─────────────────────────────────────────────────────────────
void WalkmanUI::cmdPlay(const char* path, const char* song,
                        const char* artist, const char* album) {
    // Update shared metadata first (UI task writes, Audio task reads display)
    SharedState::get().setTrackInfo(path, song, artist, album);

    AudioCommand cmd;
    cmd.cmd = AudioCmd::PLAY;
    strlcpy(cmd.filePath, path, MAX_PATH);
    SharedState::get().sendCommand(cmd);
}

void WalkmanUI::cmdPauseToggle() {
    AudioCommand cmd;
    cmd.cmd = AudioCmd::PAUSE_TOGGLE;
    SharedState::get().sendCommand(cmd);
}

void WalkmanUI::cmdVolume(int vol) {
    AudioCommand cmd;
    cmd.cmd = AudioCmd::SET_VOLUME;
    cmd.param.volume = vol;
    SharedState::get().sendCommand(cmd);
}

void WalkmanUI::cmdEQ(int preset) {
    AudioCommand cmd;
    cmd.cmd = AudioCmd::SET_EQ;
    cmd.param.eqPreset = preset;
    SharedState::get().sendCommand(cmd);
}

// ─────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────
void WalkmanUI::render() {
    _disp->clearBuffer();

    switch (_screen) {
        case UIScreen::BOOT:     drawBoot();     break;
        case UIScreen::NO_MUSIC: drawNoMusic();  break;
        case UIScreen::ARTISTS:  drawArtists();  break;
        case UIScreen::ALBUMS:   drawAlbums();   break;
        case UIScreen::SONGS:    drawSongs();    break;
        case UIScreen::PLAYER:   drawPlayer();   break;
        case UIScreen::EQ_MENU:  drawEQ();       break;
    }

    _disp->sendBuffer(); // Atomic I2C burst — no partial frames visible
}

// ── Boot / error screens ──────────────────────────────────────
void WalkmanUI::drawBoot() {
    _disp->setFont(u8g2_font_logisoso16_tf);
    _disp->drawStr(24, 14, "WALKMAN");
    _disp->setFont(u8g2_font_6x10_tf);
    _disp->drawStr(32, 38, "Loading...");
}

void WalkmanUI::drawNoMusic() {
    _disp->setFont(u8g2_font_9x15B_tf);
    _disp->drawStr(8, 10, "No Music Found");
    _disp->setFont(u8g2_font_6x10_tf);
    _disp->drawStr(4, 32, "Insert SD card with");
    _disp->drawStr(4, 44, "Artist/Album/Song");
    _disp->drawStr(4, 56, "folder structure");
}

// ── Generic scrollable list ───────────────────────────────────
// title: header text; item(i) returns string for row i; total: row count; sel: selected
void WalkmanUI::drawList(const char* title,
                          std::function<const char*(int)> item,
                          int total, int sel) {
    // ── Header bar ───────────────────────────────────────────
    _disp->setFont(u8g2_font_6x10_tf);
    _disp->drawBox(0, 0, 128, 12);
    _disp->setDrawColor(0);
    _disp->drawStr(2, 2, title);

    // Item counter in header (right-aligned)
    char counter[12];
    snprintf(counter, sizeof(counter), "%d/%d", sel + 1, total);
    int cw = _disp->getStrWidth(counter);
    _disp->drawStr(126 - cw, 2, counter);
    _disp->setDrawColor(1);

    // ── Scrollbar ────────────────────────────────────────────
    if (total > MENU_VISIBLE) {
        int trackH  = 52;   // height of scroll area
        int thumbH  = max(4, trackH * MENU_VISIBLE / total);
        int thumbY  = 12 + (_scrollOff * (trackH - thumbH) / max(1, total - MENU_VISIBLE));
        _disp->drawBox(126, thumbY, 2, thumbH);
    }

    // ── Items ─────────────────────────────────────────────────
    for (int row = 0; row < MENU_VISIBLE; ++row) {
        int idx = _scrollOff + row;
        if (idx >= total) break;

        int y = 14 + row * 13;

        if (idx == sel) {
            // Highlighted row
            _disp->drawBox(0, y - 1, 124, 12);
            _disp->setDrawColor(0);
            _disp->drawStr(4, y, item(idx));
            _disp->setDrawColor(1);
        } else {
            _disp->drawStr(4, y, item(idx));
        }
    }
}

void WalkmanUI::drawArtists() {
    auto& lib = MusicLibrary::get();
    drawList(
        "ARTISTS",
        [&](int i) -> const char* {
            const Artist* a = lib.artist(i);
            return a ? a->name : "---";
        },
        lib.artistCount(), _ai
    );
}

void WalkmanUI::drawAlbums() {
    const Artist* a = MusicLibrary::get().artist(_ai);
    if (!a) return;
    drawList(
        a->name,
        [&](int i) -> const char* {
            const Album* al = MusicLibrary::get().album(_ai, i);
            return al ? al->name : "---";
        },
        (int)a->albums.size(), _li
    );
}

void WalkmanUI::drawSongs() {
    const Album* al = MusicLibrary::get().album(_ai, _li);
    if (!al) return;
    drawList(
        al->name,
        [&](int i) -> const char* {
            const Song* s = MusicLibrary::get().song(_ai, _li, i);
            return s ? s->name : "---";
        },
        (int)al->songs.size(), _si
    );
}

// ── Now-playing screen ────────────────────────────────────────
//
//  ┌──────────────────────────────────┐
//  │ ♪ Song Name (ticker if long)     │  row 0
//  │   Artist Name                    │  row 1
//  │   Album Name                     │  row 2
//  │  ──────────────────────────────  │
//  │  [████████████░░░░░░░░░] 2:34   │  progress
//  │  VOL [███████░░░░] 70   ► / ‖   │  volume + status
//  └──────────────────────────────────┘
void WalkmanUI::drawPlayer() {
    PlaybackState st = SharedState::get().snapshot();

    // ── Song name (with scrolling ticker) ─────────────────────
    _disp->setFont(u8g2_font_7x13B_tf);
    drawTicker(0, 0, 128, st.currentSong[0] ? st.currentSong : "---");

    // ── Artist + Album ────────────────────────────────────────
    _disp->setFont(u8g2_font_6x10_tf);
    // Truncate to fit display width
    char tmp[22];
    snprintf(tmp, sizeof(tmp), "%s", st.currentArtist);
    _disp->drawStr(0, 14, tmp);
    snprintf(tmp, sizeof(tmp), "%s", st.currentAlbum);
    _disp->drawStr(0, 24, tmp);

    // ── Divider ───────────────────────────────────────────────
    _disp->drawHLine(0, 34, 128);

    // ── Volume bar ────────────────────────────────────────────
    _disp->setFont(u8g2_font_5x7_tf);
    _disp->drawStr(0, 38, "VOL");
    drawProgressBar(18, 38, 80, 7, st.volume, 100, true);

    char volStr[5];
    snprintf(volStr, sizeof(volStr), "%3d", st.volume);
    _disp->drawStr(100, 38, volStr);

    // ── Play/Pause icon ───────────────────────────────────────
    _disp->setFont(u8g2_font_6x10_tf);
    if (st.isPlaying && !st.isPaused) {
        _disp->drawStr(118, 38, "|>");
    } else if (st.isPaused) {
        _disp->drawStr(118, 38, "||");
    } else {
        _disp->drawStr(118, 38, "[]");
    }

    // ── EQ preset label ───────────────────────────────────────
    _disp->setFont(u8g2_font_5x7_tf);
    char eqStr[16];
    snprintf(eqStr, sizeof(eqStr), "EQ:%s",
             Equalizer::get().presetName(st.eqPreset));
    _disp->drawStr(0, 50, eqStr);

    // ── Hint ──────────────────────────────────────────────────
    _disp->drawStr(100, 50, "HOLD");
}

// ── EQ picker ────────────────────────────────────────────────
void WalkmanUI::drawEQ() {
    auto& eq = Equalizer::get();
    drawList(
        "EQUALIZER",
        [&](int i) -> const char* { return eq.presetName(i); },
        eq.presetCount(), _eqPreset
    );
    // Hint at bottom
    _disp->setFont(u8g2_font_5x7_tf);
    _disp->drawStr(2, 58, "Rotate=select  Click=done");
}

// ─────────────────────────────────────────────────────────────
// Drawing primitives
// ─────────────────────────────────────────────────────────────

// Generic horizontal progress bar
void WalkmanUI::drawProgressBar(int x, int y, int w, int h,
                                 int filled, int total, bool outlined) {
    if (outlined) _disp->drawFrame(x, y, w, h);
    if (total <= 0) return;
    int inner = constrain(filled * (w - 2) / total, 0, w - 2);
    if (inner > 0)
        _disp->drawBox(x + 1, y + 1, inner, h - 2);
}

// Scrolling ticker for text wider than maxW pixels
void WalkmanUI::drawTicker(int x, int y, int maxW, const char* text) {
    int tw = _disp->getStrWidth(text);

    if (tw <= maxW) {
        // Fits — draw centered
        _disp->drawStr(x + (maxW - tw) / 2, y, text);
        resetTicker();
        return;
    }

    // Needs scrolling
    uint32_t now = millis();
    if (now - _tickerLast > 80) {   // scroll one pixel every 80 ms
        _tickerLast = now;
        if (_tickerDelay > 0) {
            --_tickerDelay; // hold at start for a moment
        } else {
            ++_tickerPos;
            if (_tickerPos > tw + 20)   // wrap with a gap
                _tickerPos = -maxW / 2; // restart from right
        }
    }

    // Clip drawing to [x, x+maxW]
    _disp->setClipWindow(x, y, x + maxW - 1, y + 14);
    _disp->drawStr(x - _tickerPos, y, text);
    _disp->setMaxClipWindow();
}

void WalkmanUI::resetTicker() {
    _tickerPos   = 0;
    _tickerDelay = 20; // hold 20 frames (~667 ms at 30 FPS) before scrolling
}
