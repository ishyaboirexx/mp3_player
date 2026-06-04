# ESP32-S3 Walkman Player — Build & Architecture Guide
![Vibe Coded](https://img.shields.io/badge/vibe-coded-blueviolet?style=for-the-badge)

<img width="960" height="1280" alt="WhatsApp Image 2026-06-05 at 12 29 25 AM" src="https://github.com/user-attachments/assets/8459ad49-5a31-4fef-939d-4b8d39ccd379" />
(I have yet to design and make the outer body. The photo above shows the functional prototype with exposed modules)


## File Structure
```
walkman_player/
├── walkman_player.ino   ← Main sketch (task creation, hardware init)
├── config.h             ← ALL pin definitions and tunable constants
├── shared_state.h       ← Inter-core synchronisation (mutex + queue)
├── music_library.h/.cpp ← SD card scan → 3-layer Artist/Album/Song tree
├── encoder.h/.cpp       ← KY-040 non-blocking FSM (multi-click + hold)
├── equalizer.h/.cpp     ← Biquad IIR EQ engine with 7 presets
├── audio_task.h/.cpp    ← Core-0 audio engine (MP3/FLAC → I2S)
└── ui_task.h/.cpp       ← Core-1 UI (OLED, menu, navigation)
```

---

## Required Libraries

| Library | Install via | Purpose |
|---------|------------|---------|
| **ESP8266Audio** | Arduino Library Manager | MP3/FLAC decode + I2S output |
| **U8g2** | Arduino Library Manager | SSD1306 OLED, full-buffer, no flicker |

Board: **ESP32S3 Dev Module** — Arduino-ESP32 v2.x/v3.x  
CPU: **240 MHz** (mandatory for real-time audio decode)

---

## Wiring Guide

### Rotary Encoder KY-040
| KY-040 Pin | ESP32-S3 GPIO |
|-----------|--------------|
| CLK       | GPIO 4       |
| DT        | GPIO 5       |
| SW        | GPIO 6       |
| +         | 3.3 V        |
| GND       | GND          |

### UDA1334A I2S DAC
| UDA1334A | ESP32-S3 |
|---------|---------|
| BCLK    | GPIO 16 |
| WSEL    | GPIO 17 |
| DIN     | GPIO 15 |
| VIN     | 3.3 V   |
| GND     | GND     |

### SD Card Module (SPI)
| SD Pin | ESP32-S3 |
|--------|---------|
| CS     | GPIO 10 |
| SCK    | GPIO 12 |
| MISO   | GPIO 13 |
| MOSI   | GPIO 11 |
| VCC    | 3.3 V   |
| GND    | GND     |

### SSD1306 OLED (I2C)
| OLED | ESP32-S3 |
|------|---------|
| SDA  | GPIO 8  |
| SCL  | GPIO 9  |
| VCC  | 3.3 V   |
| GND  | GND     |

### Tactile push button (back button)
| back Pin | ESP32-S3 |
|--------|---------|
|        | GPIO 7  |

---

## SD Card Folder Structure

```
SD Root/
├── Pink Floyd/
│   ├── The Wall/
│   │   ├── 01-In The Flesh.mp3
│   │   └── 02-The Thin Ice.flac
│   └── Wish You Were Here/
│       └── Shine On.mp3
├── Led Zeppelin/
│   └── IV/
│       └── Stairway To Heaven.mp3
└── Any loose mp3 here.mp3   ← Placed in "Unknown Artist/Unknown Album"
```

**Rules:**
- Layer 1: Artist folders
- Layer 2: Album folders inside Artist
- Layer 3: `.mp3` or `.flac` files inside Album
- Loose songs at any level are wrapped in "Singles" / "Unknown" nodes
- Any nesting depth beyond 3 is ignored gracefully

---

## Dual-Core Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         ESP32-S3                                │
│                                                                 │
│  CORE 0 (audioTask, priority 24)                                │
│  ─────────────────────────────────────────────                  │
│  AudioFileSourceSD → AudioGeneratorMP3/FLAC → EQOutput          │
│                                                    │            │
│  EQOutput::ConsumeSample():                        │            │
│    1. Apply quadratic volume curve                 │            │
│    2. Apply biquad EQ (per-sample, inline)         ↓            │
│    3. Forward to AudioOutputI2S::ConsumeSample   I2S DMA        │
│                                                    │            │
│  Command intake: xQueueReceive (non-blocking)      │            │
│  Track-end signal: atomic<bool>.store(true)        ↓            │
│                                                  UDA1334A       │
│  CORE 1 (uiTask, priority 5)                       │            │
│  ─────────────────────────────────────────────     ↓            │
│  RotaryEncoder::update() + poll()               Speaker         │
│  WalkmanUI::update()                                            │
│    • Screen FSM (ARTISTS→ALBUMS→SONGS→PLAYER→EQ)               │
│    • Sends AudioCommand via xQueueSend                          │
│  U8g2 full-buffer render @ 30 FPS                               │
│                                                                 │
│  ──────────── Synchronisation ────────────────                  │
│  PlaybackState: SemaphoreHandle_t mutex                         │
│  Commands:      QueueHandle_t (capacity 8)                      │
│  Track end:     std::atomic<bool> (wait-free)                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## Encoder Input Logic

```
Button press timing state machine:

  IDLE ──(press)──► PRESSED ──(hold > 700ms)──► LONG_PRESS (fires immediately)
                        │
                    (release < 700ms)
                        ↓
                   RELEASED_WAIT ──(350ms window)──► fires click event
                        │
                    (another press within 350ms)
                        ↓
                   PRESSED (click count++)

  Click count:
    1 click  → CLICK_1 (Enter / Play-Pause)
    2 clicks → CLICK_2 (Next track)
    3 clicks → CLICK_3 (Previous track)
    hold     → LONG_PRESS (EQ menu)

  Rotation:
    CLK falling edge sampled with DT HIGH → CW  (scroll down / volume up)
    CLK falling edge sampled with DT LOW  → CCW (scroll up  / volume down)
```

---

## Navigation Map

```
ARTISTS ──(click)──► ALBUMS ──(click)──► SONGS ──(click)──► PLAYER
   ↑                    │                   │                   │
   └─(BACK BUTTON)◄─────┴─(BACK BUTTON)◄────┴─(BACK BUTTON)◄────┘
                                                                │
                                     PLAYER ──(long press)──► EQ MENU
                                       │                        │
                                   (dbl=next)              (click=done)
                                   (tri=prev)
                                  (rot=volume)
```

---

## EQ Presets

| # | Name | Description |
|---|------|-------------|
| 0 | Flat | Unity gain — bypassed entirely (zero CPU) |
| 1 | Rock | +6 dB bass, +3 dB presence & treble |
| 2 | Pop | Mid-forward, +3 dB presence |
| 3 | Jazz | Warm bass, airy top end |
| 4 | Classical | Natural, gentle shelving |
| 5 | Bass Boost | +8 dB bass shelf, hard sub emphasis |
| 6 | Vocal | Scooped lows, +4 dB 2.5 kHz presence |

Adding a preset: add a new `EQPreset` entry to `PRESETS[]` in `equalizer.cpp`. No other changes needed.

---

## Performance Budget (240 MHz)

| Task | Approximate CPU% |
|------|----------------|
| MP3 decode (Helix) | ~15% Core 0 |
| FLAC decode | ~20% Core 0 |
| Biquad EQ (5 bands) | ~3% Core 0 |
| Volume curve | <1% Core 0 |
| U8g2 OLED render | ~5% Core 1 |
| Encoder polling | <1% Core 1 |
| SD read (buffered) | Burst, ~5% Core 0 |

Headroom is generous. PSRAM (8 MB) is available for deeper buffering if needed.

## Music Library Management (PSRAM)

- Vector Pointers: artist and album nodes are stored as pointers within std::vector containers allocated in the external PSRAM
- Capacity: this allows for more than 10,000 files without impacting the heap used by the audio decoder.
- Initialization: The SD card is recursively scanned at boot; pointers are instantiated into PSRAM to ensure rapid UI scrolling. 


---

## Tuning Constants (config.h)

| Constant | Default | Effect |
|---------|---------|--------|
| `I2S_DMA_BUF_COUNT` | 8 | More = more latency tolerance |
| `I2S_DMA_BUF_LEN` | 512 | Larger = fewer interrupts |
| `LONG_PRESS_MS` | 700 | Long press threshold |
| `MULTI_CLICK_GAP_MS` | 350 | Double/triple click window |
| `DISPLAY_FPS` | 30 | Display refresh rate |
| `VOLUME_STEP` | 5 | Per-tick volume increment |
