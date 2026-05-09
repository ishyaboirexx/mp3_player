// ============================================================
// config.h — Hardware pin map, timing constants, task config
// ============================================================
#pragma once
#include <Arduino.h>

// ── Rotary Encoder (KY-040) ─────────────────────────────────
#define ENC_CLK_PIN      4    // CLK (A)
#define ENC_DT_PIN       5    // DT  (B)
#define ENC_SW_PIN       6    // SW  (button, active-LOW)

// ── Tactile Push button ─────────────────────────────────
#define BTN_BACK_PIN     7

// ── I2S DAC (UDA1334A) ──────────────────────────────────────
#define I2S_BCLK_PIN    16    // Bit clock
#define I2S_LRC_PIN     17    // Left/Right clock (WS)
#define I2S_DOUT_PIN    15    // Serial data out

// ── SD Card (SPI) ────────────────────────────────────────────
#define SD_CS_PIN       10
#define SD_SCK_PIN      12
#define SD_MISO_PIN     13
#define SD_MOSI_PIN     11

// ── SSD1306 OLED (Hardware I2C) ──────────────────────────────
#define OLED_SDA_PIN     8
#define OLED_SCL_PIN     9
#define OLED_ADDR       0x3C

// ── Audio Parameters ─────────────────────────────────────────
#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_BITS          16
#define AUDIO_CHANNELS      2
#define I2S_DMA_BUF_COUNT   8       // DMA ring buffers (latency vs stability)
#define I2S_DMA_BUF_LEN     512     // samples per DMA buffer

// ── Volume ───────────────────────────────────────────────────
#define VOLUME_DEFAULT   70     // 0–100
#define VOLUME_MIN        0
#define VOLUME_MAX      100
#define VOLUME_STEP       5

// ── Encoder Timing (ms) ──────────────────────────────────────
#define DEBOUNCE_MS         30
#define MULTI_CLICK_GAP_MS 350   // window to detect 2nd/3rd click
#define LONG_PRESS_MS      700

// ── Display ──────────────────────────────────────────────────
#define DISPLAY_W       128
#define DISPLAY_H        64
#define DISPLAY_FPS      30      // ~33 ms frame budget
#define MENU_VISIBLE      4      // rows shown at once in list menus

// ── FreeRTOS Task Config ─────────────────────────────────────
#define AUDIO_TASK_CORE      0   // Dedicated audio core
#define UI_TASK_CORE         1   // UI / FS / input core
#define AUDIO_TASK_PRIORITY 24   // Highest — must never miss I2S DMA deadline
#define UI_TASK_PRIORITY     5
#define AUDIO_STACK_SIZE  20000
#define UI_STACK_SIZE      8192

// ── Music Library Limits ─────────────────────────────────────
#define MAX_ARTISTS  80
#define MAX_ALBUMS   32
#define MAX_SONGS   128
#define MAX_PATH    256
#define MAX_NAME     64

// ── Ticker scroll speed (display frames between shifts) ──────
#define TICKER_DELAY_FRAMES  8
