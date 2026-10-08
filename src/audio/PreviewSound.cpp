#include "audio/PreviewSound.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <map>
#include <vector>

namespace yue2_abcedit {
namespace {

// Fixed render constants (declared, not hidden): CD-rate mono preview,
// sine reference tones, short fades so attacks never click.
constexpr int kRate = 44100;
constexpr double kAttackSec = 0.005;
constexpr double kReleaseSec = 0.030;
// Blended reference tone: present sine body with a quieter square edge.
// Wrong notes still stick out; long listens stay bearable.
constexpr double kMelodyGain = 0.50;
constexpr double kChordGain = 0.20;
constexpr double kSineMix = 0.70;
constexpr double kSquareMix = 0.30;

// Pitch classes spelled from the checker's closed chord vocabulary.
// Root/quality give exact classes; octave and rhythm below are the
// declared voicing (block around middle C, bass an octave below root).
int pitchClass(char letter, int alter) {
    static const std::map<char, int> base{
        {'C', 0}, {'D', 2}, {'E', 4}, {'F', 5},
        {'G', 7}, {'A', 9}, {'B', 11}};
    return (base.at(letter) + alter + 120) % 12;
}

bool spellPitchToken(const std::string& tok, size_t& pos, int& pc) {
    if (pos >= tok.size()) return false;
    char letter = tok[pos];
    if (letter < 'A' || letter > 'G') return false;
    pos++;
    int alter = 0;
    if (pos < tok.size() && tok[pos] == '#') {
        // Double forms (##) come before singles in the checker pattern.
        if (pos + 1 < tok.size() && tok[pos + 1] == '#') {
            alter = 2;
            pos += 2;
        } else {
            alter = 1;
            pos += 1;
        }
    } else if (pos < tok.size() && tok[pos] == 'b') {
        if (pos + 1 < tok.size() && tok[pos + 1] == 'b') {
            alter = -2;
            pos += 2;
        } else {
            alter = -1;
            pos += 1;
        }
    }
    pc = pitchClass(letter, alter);
    return true;
}

// Splits "RootQuality/Bass" into class lists. Returns false when the
// symbol is outside the closed list (caller keeps melody only there).
bool spellChord(const std::string& symbol, std::vector<int>& tones,
                int& bassPc) {
    static const std::vector<std::pair<std::string, std::vector<int>>> quals{
        {"m(maj7)", {0, 3, 7, 11}}, {"7sus4", {0, 5, 7, 10}},
        {"maj7", {0, 4, 7, 11}}, {"dim7", {0, 3, 6, 9}},
        {"m7b5", {0, 3, 6, 10}}, {"sus4", {0, 5, 7}},
        {"sus2", {0, 2, 7}}, {"dim", {0, 3, 6}},
        {"aug", {0, 4, 8}}, {"m7", {0, 3, 7, 10}},
        {"m6", {0, 3, 7, 9}}, {"7", {0, 4, 7, 10}},
        {"6", {0, 4, 7, 9}}, {"m", {0, 3, 7}},
        {"", {0, 4, 7}},
    };
    size_t pos = 0;
    int root = 0;
    if (!spellPitchToken(symbol, pos, root)) return false;
    std::string rest = symbol.substr(pos);
    std::string bassTok;
    auto slash = rest.find('/');
    if (slash != std::string::npos) {
        bassTok = rest.substr(slash + 1);
        rest = rest.substr(0, slash);
    }
    const std::vector<int>* steps = nullptr;
    for (const auto& q : quals) {
        if (rest == q.first) {
            steps = &q.second;
            break;
        }
    }
    if (!steps) return false;
    // Fixed voicing: classes stacked above middle C from the symbol root.
    int rootMidi = 60 + (root - 0 + 12) % 12;
    tones.clear();
    for (int s : *steps) tones.push_back(rootMidi + s);
    bassPc = root;
    if (!bassTok.empty()) {
        size_t bp = 0;
        int b = root;
        if (!spellPitchToken(bassTok, bp, b) || bp != bassTok.size())
            return false;
        bassPc = b;
    }
    return true;
}

struct Tone {
    double start = 0;  // seconds
    double len = 0;    // seconds
    double freq = 0;   // Hz
    double gain = 0;
};

void writeU16(std::ofstream& out, uint16_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
}

void writeU32(std::ofstream& out, uint32_t v) {
    out.put(static_cast<char>(v & 0xFF));
    out.put(static_cast<char>((v >> 8) & 0xFF));
    out.put(static_cast<char>((v >> 16) & 0xFF));
    out.put(static_cast<char>((v >> 24) & 0xFF));
}

}  // namespace

PreviewSound::Result PreviewSound::render(const CheckerResult& checked,
                                          const std::string& wavPath,
                                          bool soundChords) {
    Result r;
    if (!checked.ok) {
        r.error = checked.error;
        return r;
    }
    if (checked.bpm <= 0) {
        r.error = "no tempo to render";
        return r;
    }
    double secPerQuarter = 60.0 / checked.bpm;
    auto toSec = [&](const Fraction& q) {
        return (static_cast<double>(q.n) / q.d) * secPerQuarter;
    };
    double totalSec =
        toSec(checked.totalTime <= Fraction(0, 1) ? checked.vocal.time
                                                  : checked.totalTime);

    std::vector<Tone> tones;
    auto addMelody = [&](const std::vector<CheckerNote>& notes) {
        for (const auto& n : notes) {
            double dur = toSec(n.dur);
            if (dur <= 0) continue;
            double freq = 440.0 * std::pow(2.0, (n.pitch - 69) / 12.0);
            tones.push_back(Tone{toSec(n.onset), dur, freq, kMelodyGain});
        }
    };
    addMelody(checked.vocal.notes);
    addMelody(checked.ins.notes);

    // Chord symbols: pitch classes from the closed list, block voicing.
    // Muted on request (melody-only preview); the file keeps them all.
    for (size_t i = 0; soundChords && i < checked.vocal.chords.size(); ++i) {
        const auto& [onset, symbol] = checked.vocal.chords[i];
        std::vector<int> classes;
        int bass = 0;
        if (!spellChord(symbol, classes, bass)) continue;  // melody only
        double start = toSec(onset);
        double end = (i + 1 < checked.vocal.chords.size())
                         ? toSec(checked.vocal.chords[i + 1].first)
                         : totalSec;
        if (end <= start) continue;
        for (int midi : classes) {
            double freq = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
            tones.push_back(Tone{start, end - start, freq, kChordGain});
        }
        int rootBase = classes.front() - (classes.front() % 12);
        int bassMidi = rootBase - 12 + (bass - 0 + 12) % 12;
        double bassFreq = 440.0 * std::pow(2.0, (bassMidi - 69) / 12.0);
        tones.push_back(Tone{start, end - start, bassFreq, kChordGain});
    }

    if (tones.empty()) {
        r.error = "nothing to render";
        return r;
    }

    int frames = static_cast<int>(std::ceil(totalSec * kRate));
    if (frames < 1) frames = 1;
    std::vector<double> mix(frames, 0.0);
    const double twoPi = 2.0 * 3.141592653589793;
    for (const auto& t : tones) {
        int s0 = static_cast<int>(t.start * kRate);
        int s1 = static_cast<int>((t.start + t.len) * kRate);
        if (s0 < 0) s0 = 0;
        if (s1 > frames) s1 = frames;
        for (int s = s0; s < s1; ++s) {
            double age = (s - s0) / static_cast<double>(kRate);
            double tail = (s1 - s) / static_cast<double>(kRate);
            double env = 1.0;
            if (age < kAttackSec) env = age / kAttackSec;
            double rel = t.len / 2 < kReleaseSec ? t.len / 2 : kReleaseSec;
            if (tail < rel && rel > 0) env = tail / rel < env ? tail / rel : env;
            double phase = std::sin(twoPi * t.freq * (s - s0) /
                                      static_cast<double>(kRate));
            double wave =
                kSineMix * phase + kSquareMix * (phase >= 0 ? 1.0 : -1.0);
            mix[s] += t.gain * env * wave;
        }
    }
    double peak = 0;
    for (double v : mix) {
        double a = v < 0 ? -v : v;
        if (a > peak) peak = a;
    }
    double norm = peak > 0.89 ? 0.89 / peak : 1.0;

    std::ofstream out(wavPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        r.error = "cannot write " + wavPath;
        return r;
    }
    uint32_t dataBytes = static_cast<uint32_t>(frames * 2);
    out.write("RIFF", 4);
    writeU32(out, 36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeU32(out, 16);
    writeU16(out, 1);          // PCM
    writeU16(out, 1);          // mono
    writeU32(out, kRate);      // rate
    writeU32(out, kRate * 2);  // byte rate
    writeU16(out, 2);          // block align
    writeU16(out, 16);         // bits
    out.write("data", 4);
    writeU32(out, dataBytes);
    for (double v : mix) {
        double s = v * norm;
        if (s > 1) s = 1;
        if (s < -1) s = -1;
        writeU16(out, static_cast<uint16_t>(static_cast<int16_t>(s * 32767)));
    }
    out.close();
    if (!out) {
        r.error = "cannot write " + wavPath;
        return r;
    }
    r.ok = true;
    r.seconds = totalSec;
    r.samples = frames;
    return r;
}

}  // namespace yue2_abcedit
