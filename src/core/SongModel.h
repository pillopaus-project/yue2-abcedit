#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/Fraction.h"
#include "core/RefusalLog.h"

namespace yue2_abcedit {

// Internal song model: exact quarter-note timing, two monophonic voices.

struct SongNote {
    Fraction onset{0, 1};
    Fraction dur{0, 1};
    int pitch = 60;  // MIDI 0-127
};

struct ChordEvent {
    Fraction onset{0, 1};
    std::string symbol;  // e.g. "Am7", "G", "D/F#"
};

struct KeyEvent {
    Fraction onset{0, 1};
    std::string key;  // standard major/minor, e.g. "G", "G#m"
};

// One source track/voice row for the mapper UI.
struct SourceEntry {
    std::string id;       // track/voice identifier
    std::string name;     // display name
    int program = 0;      // MIDI program (or -1 for ABC voices)
    int noteCount = 0;
    bool isDrum = false;
    bool hasLyrics = false;  // MIDI lyric events present (sung line hint)
    std::string assign = "Ignore";  // "Vocal" | "Ins" | "Ignore"
};

struct Song {
    int bpm = 120;
    int meterN = 4;
    int meterD = 4;
    std::string key = "C";
    // Meter/key timeline changes (must become legal new groups on export).
    std::vector<std::pair<Fraction, std::pair<int, int>>> meterChanges;
    std::vector<KeyEvent> keyChanges;
    std::vector<SongNote> vocal;
    std::vector<SongNote> ins;
    std::vector<ChordEvent> chords;  // Vocal only
    std::vector<std::string> sections;
    RefusalLog log;
};

Fraction songEnd(const Song& song);
void sortSong(Song& song);

}  // namespace yue2_abcedit
