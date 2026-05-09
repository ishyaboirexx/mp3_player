#include "music_library.h"

bool MusicLibrary::isAudioFile(const char* name) {
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    char ext[8];
    strlcpy(ext, dot + 1, sizeof(ext));
    for (char* p = ext; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return strcmp(ext, "mp3") == 0 || strcmp(ext, "flac") == 0;
}

void MusicLibrary::stripExt(const char* name, char* out, int outLen) {
    strlcpy(out, name, outLen);
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

bool MusicLibrary::scan(const char* rootPath) {
    _artists.clear();
    _scanned = false;

    File root = SD.open(rootPath);
    if (!root || !root.isDirectory()) {
        Serial.println("[Lib] Cannot open root");
        return false;
    }

    scanLevel1(root, rootPath);
    root.close();

    _scanned = !_artists.empty();
    return _scanned;
}

void MusicLibrary::scanLevel1(fs::File& dir, const char* path) {
    File entry = dir.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            Artist ar;
            strlcpy(ar.name, entry.name(), MAX_NAME);
            strlcpy(ar.path, entry.path(), MAX_PATH);
            scanLevel2(ar, entry);
            if (!ar.albums.empty()) _artists.push_back(ar);
        }
        entry = dir.openNextFile();
    }
}

void MusicLibrary::scanLevel2(Artist& ar, fs::File& dir) {
    File entry = dir.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            Album al;
            strlcpy(al.name, entry.name(), MAX_NAME);
            strlcpy(al.path, entry.path(), MAX_PATH);
            scanLevel3(al, entry);
            if (!al.songs.empty()) ar.albums.push_back(al);
        }
        entry = dir.openNextFile();
    }
}

void MusicLibrary::scanLevel3(Album& al, fs::File& dir) {
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory() && isAudioFile(entry.name())) {
            Song s;
            stripExt(entry.name(), s.name, MAX_NAME);
            strlcpy(s.path, entry.path(), MAX_PATH);
            s.isFlac = strcasestr(entry.name(), ".flac") != nullptr;
            al.songs.push_back(s);
        }
        entry = dir.openNextFile();
    }
}

const Artist* MusicLibrary::artist(int ai) const {
    if (ai < 0 || ai >= (int)_artists.size()) return nullptr;
    return &_artists[ai];
}

const Album* MusicLibrary::album(int ai, int li) const {
    const Artist* ar = artist(ai);
    if (!ar || li < 0 || li >= (int)ar->albums.size()) return nullptr;
    return &ar->albums[li];
}

const Song* MusicLibrary::song(int ai, int li, int si) const {
    const Album* al = album(ai, li);
    if (!al || si < 0 || si >= (int)al->songs.size()) return nullptr;
    return &al->songs[si];
}

bool MusicLibrary::nextSong(const char* currentPath, char* out, int outLen) const {
    int ai, li, si;
    if (!findIndices(currentPath, ai, li, si)) return false;
    
    const Album* al = album(ai, li);
    int next = (si + 1) % (int)al->songs.size();
    strlcpy(out, al->songs[next].path, outLen);
    return true;
}

bool MusicLibrary::prevSong(const char* currentPath, char* out, int outLen) const {
    int ai, li, si;
    if (!findIndices(currentPath, ai, li, si)) return false;

    const Album* al = album(ai, li);
    int prev = (si - 1 + (int)al->songs.size()) % (int)al->songs.size();
    strlcpy(out, al->songs[prev].path, outLen);
    return true;
}

bool MusicLibrary::findIndices(const char* path, int& ai, int& li, int& si) const {
    for (int a = 0; a < (int)_artists.size(); a++) {
        for (int l = 0; l < (int)_artists[a].albums.size(); l++) {
            for (int s = 0; s < (int)_artists[a].albums[l].songs.size(); s++) {
                if (strcmp(_artists[a].albums[l].songs[s].path, path) == 0) {
                    ai = a; li = l; si = s;
                    return true;
                }
            }
        }
    }
    return false;
}