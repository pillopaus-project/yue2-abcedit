#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/Fraction.h"

namespace yue2_abcedit {

// Closed sets enforced by abc_tools.py.
inline const std::set<int>& allowedDurations() {
    static const std::set<int> s{1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48};
    return s;
}

inline const std::vector<std::string>& allowedQualities() {
    static const std::vector<std::string> q{
        "", "m", "dim", "aug", "7", "maj7", "m7", "dim7",
        "m7b5", "sus4", "sus2", "6", "m6", "7sus4", "m(maj7)"};
    return q;
}

// Sharp-count per key (negative = flats), standard major/minor only.
inline const std::map<std::string, int>& keyTable() {
    static const std::map<std::string, int> t{
        {"Cb", -7}, {"Gb", -6}, {"Db", -5}, {"Ab", -4}, {"Eb", -3},
        {"Bb", -2}, {"F", -1}, {"C", 0}, {"G", 1}, {"D", 2}, {"A", 3},
        {"E", 4}, {"B", 5}, {"F#", 6}, {"C#", 7},
        {"Abm", -7}, {"Ebm", -6}, {"Bbm", -5}, {"Fm", -4}, {"Cm", -3},
        {"Gm", -2}, {"Dm", -1}, {"Am", 0}, {"Em", 1}, {"Bm", 2},
        {"F#m", 3}, {"C#m", 4}, {"G#m", 5}, {"D#m", 6}, {"A#m", 7}};
    return t;
}

bool isStandardKey(const std::string& key);
std::map<char, int> keyAccidentals(const std::string& key);  // letter -> -1/0/+1
bool parseMeter(const std::string& text, int& num, int& den);
bool isPowerOfTwo(int v);
bool isValidChord(const std::string& chord);

// Pitch spelling helpers (checker-compatible: middle C C4 == MIDI 60).
// Returns ABC note core without duration/tie, e.g. "^f", "_B", "c'".
std::string spellPitch(int midi, char letter, int alteration,
                       int& writtenBaseOut);
bool chooseSpelling(int midi, const std::map<char, int>& keySig,
                    const std::map<char, int>& barState, char& letterOut,
                    int& alterationOut, bool& needsMarkOut);

std::string midiToAbcNote(int midi, const std::map<char, int>& keySig,
                          std::map<char, int>& barState, bool tie);

// Musical-interface language (spec section 4): outside the file format,
// timing is spoken as whole/half/quarter/eighth/... notes, bars and
// beats -- never as L: units, unit counts, ticks, or fractional
// quarter-note amounts (a non-standard amount names no musical value,
// so messages name the bar-and-beat location instead).
// 1-based bar number containing song time t in bars of barLen.
int barNumber(const Fraction& t, const Fraction& barLen);
// Position inside a bar in beats (beats are quarter notes): exactly on a
// beat -> "on beat N", otherwise -> "inside beat N". posInBar must be >= 0.
std::string beatPhrase(const Fraction& posInBar);
// Standard note value name ("an eighth note"); false when the amount is
// not exactly one -- callers then name the location, never the fraction.
bool namedDuration(const Fraction& quarters, std::string& name);

extern const std::string kVoiceVocalDef;
extern const std::string kVoiceInsDef;

}  // namespace yue2_abcedit
