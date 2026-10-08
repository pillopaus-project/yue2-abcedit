#include "core/AbcDialect.h"

#include <cctype>
#include <regex>

namespace yue2_abcedit {

const std::string kVoiceVocalDef =
    "V: Vocal clef=treble name=\"Vocal Melody\" snm=\"Vocal\"";
const std::string kVoiceInsDef =
    "V: Ins clef=treble name=\"Ins Melody\" snm=\"Inst.\"";

int barNumber(const Fraction& t, const Fraction& barLen) {
    Fraction q = t / barLen;
    return static_cast<int>(q.n / q.d) + 1;
}

std::string beatPhrase(const Fraction& posInBar) {    long long beat = posInBar.n / posInBar.d;  // whole quarters elapsed
    bool onBeat = (posInBar.n % posInBar.d) == 0;
    if (onBeat) return "on beat " + std::to_string(beat + 1);
    return "inside beat " + std::to_string(beat + 1);
}

bool namedDuration(const Fraction& quarters, std::string& name) {
    static const std::map<Fraction, std::string> names{
        {Fraction(4, 1), "a whole note"},
        {Fraction(2, 1), "a half note"},
        {Fraction(1, 1), "a quarter note"},
        {Fraction(1, 2), "an eighth note"},
        {Fraction(1, 4), "a sixteenth note"},
        {Fraction(1, 8), "a thirty-second note"},
        {Fraction(1, 16), "a sixty-fourth note"},
        {Fraction(1, 32), "a 128th note"},
        {Fraction(1, 64), "a 256th note"},
    };
    auto it = names.find(quarters);
    if (it == names.end()) return false;
    name = it->second;
    return true;
}

bool isStandardKey(const std::string& key) {
    return keyTable().count(key) > 0;
}

std::map<char, int> keyAccidentals(const std::string& key) {
    std::map<char, int> r;
    for (char c : std::string("CDEFGAB")) r[c] = 0;
    auto it = keyTable().find(key);
    if (it == keyTable().end()) return r;
    int count = it->second;
    std::string order = count > 0 ? "FCGDAEB" : "BEADGCF";
    for (int i = 0; i < std::abs(count); ++i)
        r[order[static_cast<size_t>(i)]] = count > 0 ? 1 : -1;
    return r;
}

bool parseMeter(const std::string& text, int& num, int& den) {
    static const std::regex re(R"(([1-9][0-9]*)/([1-9][0-9]*))");
    std::smatch m;
    if (!std::regex_match(text, m, re)) return false;
    num = std::stoi(m[1]);
    den = std::stoi(m[2]);
    if (den > 1024 || (den & (den - 1)) != 0) return false;
    return true;
}

bool isPowerOfTwo(int v) { return v >= 1 && (v & (v - 1)) == 0; }

namespace {
const std::string kPitchName = R"([A-G](?:bb|##|b|#)?)";
std::string chordPattern() {
    std::string q;
    bool first = true;
    for (const auto& s : allowedQualities()) {
        if (!first) q += "|";
        first = false;
        // escape regex chars in qualities like m(maj7)
        for (char c : s) {
            if (c == '(' || c == ')' || c == '+') q += '\\';
            q += c;
        }
    }
    return kPitchName + "(?:" + q + ")(?:/" + kPitchName + ")?";
}
}  // namespace

bool isValidChord(const std::string& chord) {
    static const std::regex re("^" + chordPattern() + "$");
    // Root must use # / b (checker PITCH_NAME uses b/#, not ^/_).
    return std::regex_match(chord, re);
}

namespace {
// Natural semitone offsets, C-based.
int naturalOffset(char letter) {
    switch (letter) {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default: return 0;
    }
}
}  // namespace

std::string spellPitch(int midi, char letter, int alteration,
                       int& writtenBaseOut) {
    int nat = naturalOffset(letter);
    // writtenBase: MIDI value of natural letter in octave 4 (C4=60), before alter.
    // Solve octave: choose octave so that writtenBase + alteration == midi.
    // writtenBase = 60 + rot(letter) + 12*octShift where rot maps C..B.
    // Use pitch-class arithmetic instead: find octave shift in [-4, 4].
    static const std::map<char, int> rot{
        {'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11}};
    int pc = (midi % 12 + 12) % 12;
    (void)pc;
    for (int oct = -4; oct <= 4; ++oct) {
        int base = 60 + rot.at(letter) - naturalOffset('C') + 12 * oct;
        // 60 is C4 natural; rot gives chromatic offset of natural letter.
        // Actually C4=60, D4=62 ... B4=71, C5=72.
        if (base + alteration == midi) {
            writtenBaseOut = base;
            int rel = base - 60;  // semitones above middle C (natural grid)
            // Map to ABC octave: C..B uppercase = octave 4 region, lowercase = 5.
            // Determine diatonic octave number from letter+oct shift.
            int diaOct = 4 + oct;
            // Adjust for letters: base already encodes it; derive token:
            char core = letter;
            std::string tok;
            if (alteration == 1)
                tok += "^";
            else if (alteration == 2)
                tok += "^^";
            else if (alteration == -1)
                tok += "_";
            else if (alteration == -2)
                tok += "__";
            else if (alteration == 0)
                tok += "";  // natural shown only if explicit mark requested
            if (diaOct >= 5) {
                core = static_cast<char>(std::tolower(letter));
                for (int i = 5; i < diaOct; ++i) tok += "'";
                tok = tok + core;
                // rebuild with accidental prefix first
                std::string pre;
                if (alteration == 1)
                    pre = "^";
                else if (alteration == 2)
                    pre = "^^";
                else if (alteration == -1)
                    pre = "_";
                else if (alteration == -2)
                    pre = "__";
                tok = pre + core;
                for (int i = 5; i < diaOct; ++i) tok += "'";
            } else if (diaOct == 4) {
                tok = tok + letter;
            } else {
                tok = tok + letter;
                for (int i = diaOct; i < 4; ++i) tok += ",";
            }
            (void)rel;
            return tok;
        }
    }
    writtenBaseOut = midi;
    return "C";
}

bool chooseSpelling(int midi, const std::map<char, int>& keySig,
                    const std::map<char, int>& barState, char& letterOut,
                    int& alterationOut, bool& needsMarkOut) {
    // Try each letter: compute required alteration, prefer currently-active
    // accidental state (no mark needed), else key default, else fewest marks.
    struct Cand {
        char L;
        int alt;
        bool noMark;
        int cost;
    };
    std::vector<Cand> cands;
    for (char L : std::string("CDEFGAB")) {
        for (int oct = -4; oct <= 4; ++oct) {
            static const std::map<char, int> rot{
                {'C', 0}, {'D', 2}, {'E', 4}, {'F', 5},
                {'G', 7}, {'A', 9}, {'B', 11}};
            int base = 60 + rot.at(L) + 12 * oct;
            int alt = midi - base;
            if (alt < -2 || alt > 2) continue;
            int active = keySig.count(L) ? keySig.at(L) : 0;
            auto it = barState.find(L);
            if (it != barState.end()) active = it->second;
            bool noMark = (alt == active);
            int cost = (noMark ? 0 : 10) + std::abs(alt) +
                       (keySig.count(L) && keySig.at(L) == alt ? 0 : 1);
            cands.push_back({L, alt, noMark, cost});
        }
    }
    if (cands.empty()) return false;
    // Prefer no-mark candidates (accidental propagation), then lowest cost.
    const Cand* best = &cands[0];
    for (const auto& c : cands) {
        if (c.noMark && !best->noMark) {
            best = &c;
        } else if (c.noMark == best->noMark && c.cost < best->cost) {
            best = &c;
        }
    }
    letterOut = best->L;
    alterationOut = best->alt;
    needsMarkOut = !best->noMark;
    return true;
}

std::string midiToAbcNote(int midi, const std::map<char, int>& keySig,
                          std::map<char, int>& barState, bool tie) {
    (void)tie;
    char letter = 'C';
    int alt = 0;
    bool needsMark = true;
    if (!chooseSpelling(midi, keySig, barState, letter, alt, needsMark))
        return "C";
    int writtenBase = 0;
    std::string tok;
    std::string pre;
    if (needsMark) {
        if (alt == 1)
            pre = "^";
        else if (alt == 2)
            pre = "^^";
        else if (alt == -1)
            pre = "_";
        else if (alt == -2)
            pre = "__";
        else
            pre = "=";
        barState[letter] = alt;
    }
    // Octave placement: find octave shift so base+alt == midi.
    static const std::map<char, int> rot{
        {'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11}};
    int octUse = 0;
    for (int oct = -4; oct <= 4; ++oct) {
        if (60 + rot.at(letter) + 12 * oct + alt == midi) {
            octUse = oct;
            break;
        }
    }
    int diaOct = 4 + octUse;
    if (diaOct >= 5) {
        char low = static_cast<char>(std::tolower(letter));
        tok = pre + low;
        for (int i = 5; i < diaOct; ++i) tok += "'";
    } else if (diaOct == 4) {
        tok = pre + letter;
    } else {
        tok = pre + letter;
        for (int i = diaOct; i < 4; ++i) tok += ",";
    }
    int wb = 0;
    spellPitch(midi, letter, alt, wb);
    writtenBase = wb;
    (void)writtenBase;
    return tok;
}

}  // namespace yue2_abcedit
