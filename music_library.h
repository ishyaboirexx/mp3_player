#pragma once
#include <Arduino.h>
#include <vector>
#include <SD.h>
#include "config.h"

// ── PSRAM Allocator ──────────────────────────────────────────
template <class T>
struct PSRAMAllocator {
    typedef T value_type;
    PSRAMAllocator() = default;
    template <class U> PSRAMAllocator(const PSRAMAllocator<U>&) {}
    T* allocate(std::size_t n) {
        T* p = (T*)ps_malloc(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return p;
    }
    void deallocate(T* p, std::size_t) { free(p); }
};

// ── Data nodes ───────────────────────────────────────────────
struct Song {
    char name[MAX_NAME];   
    char path[MAX_PATH];   
    bool isFlac;           
};

struct Album {
    char name[MAX_NAME];
    char path[MAX_PATH];
    std::vector<Song, PSRAMAllocator<Song>> songs; 
};

struct Artist {
    char name[MAX_NAME];
    char path[MAX_PATH];
    std::vector<Album, PSRAMAllocator<Album>> albums; 
};

// ── Library singleton ─────────────────────────────────────────
class MusicLibrary {
public:
    static MusicLibrary& get() {
        static MusicLibrary inst;
        return inst;
    }

    bool scan(const char* rootPath = "/");
    bool scanned() const { return _scanned; }
    int  artistCount() const { return (int)_artists.size(); }

    const Artist* artist(int ai)             const;
    const Album* album (int ai, int li)     const;
    const Song* song  (int ai, int li, int si) const;

    bool nextSong(const char* currentPath, char* out, int outLen) const;
    bool prevSong(const char* currentPath, char* out, int outLen) const;
    bool findIndices(const char* path, int& ai, int& li, int& si) const;

    static bool isAudioFile(const char* name);
    static void stripExt(const char* name, char* out, int outLen);

private:
    MusicLibrary() = default;
    // Signatures updated to use fs::File& to match implementation
    void scanLevel1(fs::File& dir, const char* path);
    void scanLevel2(Artist& ar, fs::File& dir);
    void scanLevel3(Album& al, fs::File& dir);

    std::vector<Artist, PSRAMAllocator<Artist>> _artists;
    bool _scanned = false;
};