// ============================================================
// walkman_player.ino — Main sketch
//
// ESP32-S3 (N16R8) Walkman-style Music Player
// ============================================================
//
// Required libraries (install via Arduino Library Manager):
//   • ESP8266Audio   — Earle F. Philhower III (MP3 + FLAC decode)
//   • U8g2           — oliver (SSD1306 OLED, flicker-free full-buffer)
//
// Board: "ESP32S3 Dev Module" (Arduino-ESP32 v2.x or v3.x)
// CPU Frequency: 240 MHz (essential for zero-stutter decode)
// Flash: 16 MB (N16R8), PSRAM: 8 MB OPI
//
// ─────────────────────────────────────────────────────────────
// DUAL-CORE TASK ARCHITECTURE
// ─────────────────────────────────────────────────────────────
//
//  Core 0 — audioTask (Priority 24, AUDIO_STACK_SIZE)
//    ┌─────────────────────────────────────────────────────┐
//    │  EQOutput (I2S DMA)                                 │
//    │    ↑  ConsumeSample()  [EQ → volume → I2S write]    │
//    │  AudioGeneratorMP3 / AudioGeneratorFLAC             │
//    │    ↑  loop()  [decodes frames, feeds DMA buffers]   │
//    │  AudioFileSourceSD                                  │
//    │    ↑  read()  [SD SPI read, no lock contention]     │
//    │                                                     │
//    │  Commands received via xQueue (non-blocking peek)   │
//    │  Track-end signals via std::atomic<bool>            │
//    └─────────────────────────────────────────────────────┘
//
//  Core 1 — uiTask (Priority 5, UI_STACK_SIZE)
//    ┌─────────────────────────────────────────────────────┐
//    │  RotaryEncoder::update() + poll()                   │
//    │    • Rotation, single/double/triple click, hold     │
//    │  WalkmanUI::update()                                │
//    │    • Navigation FSM (ARTISTS→ALBUMS→SONGS→PLAYER)   │
//    │    • Sends AudioCommand structs to Core 0 queue     │
//    │  U8g2 full-buffer render @ 30 FPS                   │
//    │    • clearBuffer → draw all → sendBuffer (atomic)   │
//    └─────────────────────────────────────────────────────┘
//
//  Shared synchronisation:
//    • PlaybackState: SemaphoreHandle_t mutex (read any, write Audio)
//    • Commands UI→Audio: QueueHandle_t (8 slots, non-blocking send)
//    • Track-end signal: std::atomic<bool> (single bit, wait-free)
//
// ─────────────────────────────────────────────────────────────
// HOW AUDIO NEVER STUTTERS
// ─────────────────────────────────────────────────────────────
//   1. audioTask runs at highest FreeRTOS priority (24) so the
//      OS never preempts it for anything except an interrupt.
//   2. I2S DMA has 8 × 512-sample ring buffers — ~93 ms of headroom.
//      Even if Core 0 stalls briefly (SD seek, malloc), the DAC
//      keeps playing from the DMA ring without a gap.
//   3. SD reads happen inside AudioFileSourceSD::read() which is
//      called from generator->loop() — always on Core 0, no mutex.
//   4. Pause writes 512 frames of silence BEFORE stopping loop(),
//      flushing the DMA ring to silence with no pop or tail.
//   5. Core 1 SD access (library scan) finishes before tasks start.
//      After that, Core 1 never touches SD, so the SPI bus is
//      exclusively owned by Core 0 during playback.
// ─────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include "config.h"
#include "shared_state.h"
#include "audio_task.h"
#include "ui_task.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>

// ─────────────────────────────────────────────────────────────
// Task wrappers
// ─────────────────────────────────────────────────────────────

static void audioTaskWrapper(void* p) {
    audioTask(p);
    // Should never return; if it does, restart cleanly
    Serial.println("[Main] audioTask exited! Restarting...");
    esp_restart();
}

static void uiTaskWrapper(void* p) {
    WalkmanUI::get().init(); // SD scan happens here (Core 1, before audio plays)
    for (;;) {
        WalkmanUI::get().update();
        vTaskDelay(pdMS_TO_TICKS(8)); // ~120 Hz poll — encoder never misses clicks
    }
}

// ─────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────
void setup() {
    // 1. Permanently disable Bluetooth hardware
    btStop();
    esp_bt_controller_disable();
    
    // 2. Permanently disable Wi-Fi hardware
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    // START SERIAL :)
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[Main] === Walkman Player Boot ===");
    Serial.printf("[Main] CPU: %d MHz  Free heap: %d B\n",
                  getCpuFrequencyMhz(), esp_get_free_heap_size());

    // ── Shared state (mutex + queue) ──────────────────────────
    SharedState::get().init();

    // ── SD card (must init before tasks start; Core 1 owns SPI) ─
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, SPI, 20000000UL)) {
        Serial.println("[Main] WARNING: SD card mount failed!");
        // UI will show "No Music Found" — graceful degradation
    } else {
        Serial.printf("[Main] SD mounted. Size: %llu MB\n",
                      SD.cardSize() / (1024 * 1024));
    }

    // ── I2C for OLED ─────────────────────────────────────────
    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
    Wire.setClock(400000); // 400 kHz Fast-Mode for 30 FPS display

    // ── Spawn tasks ───────────────────────────────────────────
    // Audio task first so its I2S DMA is ready before UI plays anything.
    BaseType_t ret;

    ret = xTaskCreatePinnedToCore(
        audioTaskWrapper,
        "AudioTask",
        AUDIO_STACK_SIZE,
        nullptr,
        AUDIO_TASK_PRIORITY,
        nullptr,
        AUDIO_TASK_CORE   // Core 0
    );
    if (ret != pdPASS) {
        Serial.println("[Main] FATAL: audioTask creation failed");
        esp_restart();
    }

    // Small delay so audioTask can init I2S before UI might send PLAY
    vTaskDelay(pdMS_TO_TICKS(200));

    ret = xTaskCreatePinnedToCore(
        uiTaskWrapper,
        "UITask",
        UI_STACK_SIZE,
        nullptr,
        UI_TASK_PRIORITY,
        nullptr,
        UI_TASK_CORE     // Core 1
    );
    if (ret != pdPASS) {
        Serial.println("[Main] FATAL: uiTask creation failed");
        esp_restart();
    }

    Serial.println("[Main] Tasks running. Entering idle loop.");
}

// ─────────────────────────────────────────────────────────────
// loop() — Intentionally empty; all work is in FreeRTOS tasks.
// Arduino's loop() runs on Core 1 at priority 1 — harmless idle.
// ─────────────────────────────────────────────────────────────
void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000)); // suspend for 5 s — effectively dead
}
