// ============================================================
// shared_state.h — Mutex-protected state + inter-core command queue
//
// Design rationale:
//   Core 0 (audio) writes: isPlaying, isPaused, currentPositionMs
//   Core 1 (UI)    writes: sends AudioCommand via queue
//   Both read PlaybackState via mutex-guarded copy
// ============================================================
#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "config.h"

// ── Command types sent from UI → Audio task ──────────────────
enum class AudioCmd : uint8_t {
    NONE = 0,
    PLAY,           // Start new file; filePath filled
    PAUSE_TOGGLE,   // Toggle play/pause
    STOP,           // Stop and silence output
    SET_VOLUME,     // param.volume (0–100)
    SET_EQ,         // param.eqPreset index
};

struct AudioCommand {
    AudioCmd cmd   = AudioCmd::NONE;
    struct {
        int volume  = 0;
        int eqPreset = 0;
    } param;
    char filePath[MAX_PATH] = {};
};

// ── Snapshot of playback state (read by UI) ──────────────────
struct PlaybackState {
    bool     isPlaying  = false;
    bool     isPaused   = false;
    int      volume     = VOLUME_DEFAULT;
    int      eqPreset   = 0;
    char     currentFile[MAX_PATH]    = {};
    char     currentSong[MAX_NAME]   = {};
    char     currentArtist[MAX_NAME] = {};
    char     currentAlbum[MAX_NAME]  = {};
};

// ── Singleton shared between both cores ──────────────────────
class SharedState {
public:
    static SharedState& get() {
        static SharedState inst;
        return inst;
    }

    // Called once from setup() before tasks start
    void init() {
        _mutex    = xSemaphoreCreateMutex();
        _cmdQueue = xQueueCreate(8, sizeof(AudioCommand));
        configASSERT(_mutex && _cmdQueue);
        _state.volume = VOLUME_DEFAULT;
    }

    // ── Thread-safe snapshot read ─────────────────────────────
    PlaybackState snapshot() {
        PlaybackState copy;
        xSemaphoreTake(_mutex, portMAX_DELAY);
        copy = _state;
        xSemaphoreGive(_mutex);
        return copy;
    }

    // ── Atomic field writers (used by audio task) ─────────────
    void setPlaying(bool v) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _state.isPlaying = v;
        xSemaphoreGive(_mutex);
    }

    void setPaused(bool v) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _state.isPaused = v;
        xSemaphoreGive(_mutex);
    }

    void setVolume(int v) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _state.volume = v;
        xSemaphoreGive(_mutex);
    }

    void setTrackInfo(const char* file, const char* song,
                      const char* artist, const char* album) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        strlcpy(_state.currentFile,   file,   MAX_PATH);
        strlcpy(_state.currentSong,   song,   MAX_NAME);
        strlcpy(_state.currentArtist, artist, MAX_NAME);
        strlcpy(_state.currentAlbum,  album,  MAX_NAME);
        xSemaphoreGive(_mutex);
    }

    void setEQPreset(int idx) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _state.eqPreset = idx;
        xSemaphoreGive(_mutex);
    }

    // ── Command queue ─────────────────────────────────────────
    //   UI task: enqueue; Audio task: dequeue
    bool sendCommand(const AudioCommand& cmd, TickType_t wait = pdMS_TO_TICKS(5)) {
        return xQueueSend(_cmdQueue, &cmd, wait) == pdTRUE;
    }

    bool recvCommand(AudioCommand& cmd) {
        return xQueueReceive(_cmdQueue, &cmd, 0) == pdTRUE;
    }

    // ── Lock-free track-end signal ────────────────────────────
    // Audio task sets; UI task clears. Single writer/reader → atomic OK.
    std::atomic<bool> trackFinished{false};

private:
    SharedState() = default;
    SemaphoreHandle_t _mutex    = nullptr;
    QueueHandle_t     _cmdQueue = nullptr;
    PlaybackState     _state;
};
