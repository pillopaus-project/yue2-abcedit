#include "core/AbcChecker.h"

#include <cctype>
#include <regex>
#include <sstream>

#include "core/AbcDialect.h"

namespace yue2_abcedit {
namespace {

struct Token {
    bool isChord = false;
    std::string chord;
    bool isKey = false;
    std::string key;
    bool isNote = false;
    std::string acc;  // "", "^", "^^", "_", "__", "="
    char note = 0;    // A-G a-g z
    std::string oct;  // , or '
    int duration = 1;
    bool tie = false;
};

// Manual scanner mirroring TOKEN regex in abc_tools.py:
//   "(chord)" | [K:key] | acc note oct duration tie
bool scanToken(const std::string& body, size_t& cursor, Token& out,
               std::string& err) {
    out = Token();
    if (cursor >= body.size()) return false;
    char c = body[cursor];
    if (c == '"') {
        size_t end = body.find('"', cursor + 1);
        if (end == std::string::npos) {
            err = "unterminated chord symbol";
            return false;
        }
        out.isChord = true;
        out.chord = body.substr(cursor + 1, end - cursor - 1);
        cursor = end + 1;
        return true;
    }
    if (c == '[' && body.compare(cursor, 3, "[K:") == 0) {
        size_t end = body.find(']', cursor + 3);
        if (end == std::string::npos) {
            err = "unterminated inline key";
            return false;
        }
        out.isKey = true;
        out.key = body.substr(cursor + 3, end - cursor - 3);
        cursor = end + 1;
        return true;
    }
    size_t p = cursor;
    std::string acc;
    if (p + 1 < body.size() &&
        ((body[p] == '^' && body[p + 1] == '^') ||
         (body[p] == '_' && body[p + 1] == '_'))) {
        acc = body.substr(p, 2);
        p += 2;
    } else if (body[p] == '^' || body[p] == '_' || body[p] == '=') {
        acc = body.substr(p, 1);
        p += 1;
    }
    if (p >= body.size()) return false;
    char nc = body[p];
    if (!((nc >= 'A' && nc <= 'G') || (nc >= 'a' && nc <= 'g') || nc == 'z'))
        return false;
    out.acc = acc;
    out.note = nc;
    p += 1;
    std::string oct;
    while (p < body.size() && (body[p] == ',' || body[p] == '\'')) oct += body[p++];
    std::string digits;
    while (p < body.size() && std::isdigit(static_cast<unsigned char>(body[p])))
        digits += body[p++];
    out.oct = oct;
    out.duration = digits.empty() ? 1 : std::stoi(digits);
    if (p < body.size() && body[p] == '-') {
        out.tie = true;
        p += 1;
    }
    out.isNote = true;
    cursor = p;
    return true;
}

int naturalSemitone(char letter) {
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

struct VoiceState : CheckerVoice {
    // pending tie: {pitch, writtenBase}
    bool pending = false;
    int pendingPitch = 0;
    int pendingWritten = 0;
};

bool parseBar(const std::string& body, VoiceState& v, const Fraction& unit,
              const std::string& context, std::string& err) {
    Fraction length = barQuarters(v.meterN, v.meterD);
    Fraction start = v.time;
    Fraction offset(0, 1);
    std::map<char, int> local;  // accidental by letter, across octaves
    auto fail = [&](const std::string& m) {
        err = context + ": " + m;
        return false;
    };
    if (body == "Z") {
        if (v.pending) return fail("tie enters a full-measure rest");
        offset = length;
    } else {
        size_t cursor = 0;
        while (cursor < body.size()) {
            if (std::isspace(static_cast<unsigned char>(body[cursor]))) {
                cursor++;
                continue;
            }
            Token t;
            std::string terr;
            size_t before = cursor;
            if (!scanToken(body, cursor, t, terr)) {
                std::string near = body.substr(before, 24);
                if (!terr.empty()) return fail(terr);
                return fail("unsupported token at '" + near + "'");
            }
            if (offset >= length)
                return fail("event after the measure end");
            if (t.isChord) {
                if (!isValidChord(t.chord))
                    return fail("unsupported chord '" + t.chord + "'");
                v.chords.push_back({start + offset, t.chord});
                continue;
            }
            if (t.isKey) {
                if (!isStandardKey(t.key))
                    return fail("Unsupported key '" + t.key + "'");
                v.key = t.key;
                v.keys.push_back({start + offset, t.key});
                local.clear();
                continue;
            }
            // note / rest
            if (allowedDurations().count(t.duration) == 0) {
                return fail("a note " + beatPhrase(offset) +
                            " cannot be written as one token; split it "
                            "with ties");
            }
            Fraction dur = Fraction(t.duration, 1) * unit * Fraction(4, 1);
            if (offset + dur > length)
                return fail("note/rest exceeds meter duration");
            bool hasComma = t.oct.find(',') != std::string::npos;
            bool hasApos = t.oct.find('\'') != std::string::npos;
            if (hasComma && hasApos) return fail("mixed octave marks");
            if (t.note == 'z') {
                if (!t.acc.empty() || !t.oct.empty() || t.tie)
                    return fail(
                        "a rest cannot have accidentals, octave marks or ties");
                if (v.pending)
                    return fail("tie enters a rest");
            } else {
                char letter = static_cast<char>(std::toupper(t.note));
                int written = 60 + naturalSemitone(letter) +
                              (std::islower(static_cast<unsigned char>(t.note)) ? 12 : 0);
                int apos = 0, comma = 0;
                for (char o : t.oct) {
                    if (o == '\'') apos++;
                    if (o == ',') comma++;
                }
                written += 12 * (apos - comma);
                int active = keyAccidentals(v.key)[letter];
                auto it = local.find(letter);
                if (it != local.end()) active = it->second;
                int alteration = active;
                if (!t.acc.empty()) {
                    if (t.acc == "=")
                        alteration = 0;
                    else if (t.acc == "_")
                        alteration = -1;
                    else if (t.acc == "__")
                        alteration = -2;
                    else if (t.acc == "^")
                        alteration = 1;
                    else if (t.acc == "^^")
                        alteration = 2;
                    local[letter] = alteration;
                }
                int pitch = written + alteration;
                if (v.pending) {
                    int oldPitch = v.pendingPitch, oldWritten = v.pendingWritten;
                    if (t.acc.empty() && written == oldWritten) pitch = oldPitch;
                    if (pitch != oldPitch)
                        return fail("tie changes pitch from " +
                                    std::to_string(oldPitch) + " to " +
                                    std::to_string(pitch));
                    v.notes.back().dur = v.notes.back().dur + dur;
                } else {
                    if (pitch < 0 || pitch > 127)
                        return fail("pitch " + std::to_string(pitch) +
                                    " is outside MIDI range");
                    v.notes.push_back(CheckerNote{start + offset, pitch, dur});
                }
                if (t.tie) {
                    v.pending = true;
                    v.pendingPitch = pitch;
                    v.pendingWritten = written;
                } else {
                    v.pending = false;
                }
            }
            offset = offset + dur;
        }
    }
    if (offset != length) {
        if (offset < length) {
            Fraction shortBy = length - offset;
            std::string by;
            if (namedDuration(shortBy, by)) by = " by " + by;
            return fail("the bar falls short of the meter" + by);
        }
        Fraction overBy = offset - length;
        std::string by;
        if (namedDuration(overBy, by)) by = " by " + by;
        return fail("the bar overruns the meter" + by);
    }
    v.bars.push_back(CheckerBar{start, length, v.meterN, v.meterD});
    v.time = v.time + length;
    return true;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    lines.push_back(cur);
    // Drop trailing empty line produced by final newline, like splitlines().
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

}  // namespace

CheckerResult AbcChecker::check(const std::string& text) {
    CheckerResult r;
    auto lines = splitLines(text);
    auto fail = [&](const std::string& m) {
        r.ok = false;
        r.error = m;
        return r;
    };
    if (lines.size() < 12) return fail("Incomplete native two-voice ABC");
    if (lines[0] != "X:1" || lines[1] != "T:")
        return fail("Expected native X:1 and blank T: header");
    if (lines[2].rfind("M:", 0) != 0) return fail("Missing header M:");
    int mn = 4, md = 4;
    if (!parseMeter(lines[2].substr(2), mn, md))
        return fail("Unsupported meter '" + lines[2].substr(2) +
                    "'; write an explicit fraction");
    static const std::regex lure(R"(L:1/([1-9][0-9]*))");
    {
        std::smatch m;
        if (!std::regex_match(lines[3], m, lure))
            return fail("Expected L:1/<power of two>, usually L:1/32");
        int denom = std::stoi(m[1]);
        if (denom > 1024 || !isPowerOfTwo(denom))
            return fail("Unsupported L: denominator");
        r.unitDenom = denom;
    }
    static const std::regex qre(R"(Q:1/4=([1-9][0-9]*))");
    {
        std::smatch m;
        if (!std::regex_match(lines[4], m, qre))
            return fail("Expected integer quarter-note tempo Q:1/4=<BPM>");
        r.bpm = std::stoi(m[1]);
    }
    if (lines[5] != kVoiceVocalDef || lines[6] != kVoiceInsDef)
        return fail("Preserve native Vocal and Ins voice definitions");
    if (lines[7].rfind("K:", 0) != 0) return fail("Missing header K:");
    std::string key = lines[7].substr(2);
    if (!isStandardKey(key))
        return fail("Unsupported key '" + key +
                    "'; use a standard major or minor K: field");

    Fraction unit(1, r.unitDenom);
    VoiceState vocal, ins;
    vocal.meterN = ins.meterN = mn;
    vocal.meterD = ins.meterD = md;
    vocal.key = ins.key = key;
    vocal.keys.push_back({Fraction(0, 1), key});
    ins.keys.push_back({Fraction(0, 1), key});

    size_t cursor = 8;
    int group = 0;
    const char* names[2] = {"Vocal", "Ins"};
    while (cursor < lines.size()) {
        while (cursor < lines.size() &&
               lines[cursor].rfind("% ", 0) == 0)
            cursor++;
        if (cursor == lines.size())
            return fail("Dangling section comment without music");
        group++;
        int counts[2] = {0, 0};
        for (int vi = 0; vi < 2; ++vi) {
            std::string name = names[vi];
            std::string context = "group " + std::to_string(group) + ", " + name;
            if (cursor >= lines.size() || lines[cursor] != "V: " + name)
                return fail(context + ": expected V: " + name);
            cursor++;
            VoiceState& v = (vi == 0 ? vocal : ins);
            std::set<std::string> fields;
            while (cursor < lines.size() &&
                   (lines[cursor].rfind("M:", 0) == 0 ||
                    lines[cursor].rfind("K:", 0) == 0)) {
                size_t p = lines[cursor].find(':');
                std::string fn = lines[cursor].substr(0, p);
                std::string val = lines[cursor].substr(p + 1);
                if (fields.count(fn))
                    return fail(context + ": duplicate " + fn + ": field");
                fields.insert(fn);
                if (fn == "M") {
                    int a = 0, b = 0;
                    if (!parseMeter(val, a, b))
                        return fail(context + ": unsupported meter");
                    v.meterN = a;
                    v.meterD = b;
                } else {
                    if (!isStandardKey(val))
                        return fail(context + ": unsupported key");
                    v.key = val;
                    v.keys.push_back({v.time, val});
                }
                cursor++;
            }
            if (cursor >= lines.size())
                return fail(context + ": missing music line");
            std::string line = lines[cursor];
            if (line.empty() || line.back() != '|')
                return fail(context + ": music line must end with a plain barline");
            cursor++;
            // Split into bars on '|', reject empties (no doubles/repeats).
            std::vector<std::string> bars;
            std::string cur;
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '|') {
                    std::string b = cur;
                    // trim
                    size_t s = b.find_first_not_of(" \t");
                    size_t e = b.find_last_not_of(" \t");
                    b = (s == std::string::npos) ? "" : b.substr(s, e - s + 1);
                    if (b.empty())
                        return fail(context +
                                    ": empty measure or unsupported double/repeat "
                                    "barline");
                    static const std::regex zre(R"(Z([2-4])?)");
                    std::smatch zm;
                    if (std::regex_match(b, zm, zre)) {
                        int rep = zm[1].matched ? std::stoi(zm[1]) : 1;
                        for (int k = 0; k < rep; ++k) bars.push_back("Z");
                    } else {
                        bars.push_back(b);
                    }
                    cur.clear();
                } else {
                    cur += line[i];
                }
            }
            if (bars.size() < 1 || bars.size() > 4)
                return fail(context +
                            ": expected 1-4 measures after expanding Z rests");
            counts[vi] = static_cast<int>(bars.size());
            for (const auto& b : bars) {
                std::string ctx2 = context + ", bar " +
                                   std::to_string(v.bars.size() + 1);
                std::string err;
                if (!parseBar(b, v, unit, ctx2, err)) return fail(err);
            }
        }
        if (counts[0] != counts[1])
            return fail("group " + std::to_string(group) +
                        ": voices have different measure counts");
    }
    if (vocal.pending) return fail("Vocal: unresolved tie at end of score");
    if (ins.pending) return fail("Ins: unresolved tie at end of score");
    if (!ins.chords.empty())
        return fail("Native chord symbols belong in Vocal, not Ins");
    // Voice meter/time grids must match bar-by-bar.
    if (vocal.bars.size() != ins.bars.size())
        return fail("Voice meter/time grids differ");
    for (size_t i = 0; i < vocal.bars.size(); ++i) {
        const auto& a = vocal.bars[i];
        const auto& b = ins.bars[i];
        if (!(a.start == b.start && a.length == b.length &&
              a.meterN == b.meterN && a.meterD == b.meterD))
            return fail("Voice meter/time grids differ");
    }
    if (vocal.keys.size() != ins.keys.size())
        return fail("Voice key-change timelines differ");
    for (size_t i = 0; i < vocal.keys.size(); ++i) {
        if (!(vocal.keys[i].first == ins.keys[i].first &&
              vocal.keys[i].second == ins.keys[i].second))
            return fail("Voice key-change timelines differ");
    }
    r.ok = true;
    r.vocal.meterN = vocal.meterN;
    r.vocal.meterD = vocal.meterD;
    r.vocal.key = vocal.key;
    r.vocal.time = vocal.time;
    r.vocal.notes.clear();
    for (const auto& nn : vocal.notes)
        r.vocal.notes.push_back({nn.onset, nn.pitch, nn.dur});
    r.vocal.bars = vocal.bars;
    r.vocal.chords = vocal.chords;
    r.vocal.keys = vocal.keys;
    r.ins.meterN = ins.meterN;
    r.ins.meterD = ins.meterD;
    r.ins.key = ins.key;
    r.ins.time = ins.time;
    r.ins.notes.clear();
    for (const auto& nn : ins.notes)
        r.ins.notes.push_back({nn.onset, nn.pitch, nn.dur});
    r.ins.bars = ins.bars;
    r.ins.chords = ins.chords;
    r.ins.keys = ins.keys;
    r.totalTime = vocal.time;
    return r;
}

}  // namespace yue2_abcedit
