#include "import/StandardAbcReader.h"

#include <cctype>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include "convert/ChordFold.h"
#include "core/AbcDialect.h"

namespace yue2_abcedit {
namespace {

// Event in standard-ABC time (fractions of a quarter).
struct StdEvent {
    Fraction onset{0, 1};
    Fraction dur{0, 1};
    int pitch = -1;          // -1 = rest
    bool chordTone = false;  // extra stack pitch (for chord inference)
    int stackRoot = -1;
    std::string chordSym;    // quoted symbol in source (fold later)
    bool grace = false;
};

struct StdVoiceData {
    std::string id;
    std::string name;
    std::vector<StdEvent> events;
};

struct Header {
    int bpm = 120;
    int meterN = 4, meterD = 4;
    Fraction unit{1, 8};  // L: as whole-note fraction
    std::string key = "C";
};

Fraction abcDurationToQuarters(long long num, long long den,
                               const Fraction& unit) {
    // Standard ABC: duration multiplier * L:. Default L number handling done
    // by caller. unit is whole-note fraction; quarters = mult * unit * 4.
    return Fraction(num, den) * unit * Fraction(4, 1);
}

// Parse an ABC pitch token at p: [acc]letter[oct][dur]. Returns pitch or -2.
int parseStdPitch(const std::string& s, size_t& p, std::string& accOut,
                  std::string& octOut, long long& dNum, long long& dDen,
                  bool& brokenGreater, bool& brokenLess, int& brokenCount) {
    accOut.clear();
    octOut.clear();
    brokenGreater = brokenLess = false;
    brokenCount = 0;
    while (p < s.size() && (s[p] == '^' || s[p] == '_' || s[p] == '=')) {
        accOut += s[p++];
        if (accOut.size() > 2) return -2;
    }
    if (p >= s.size()) return -2;
    char nc = s[p];
    if (!((nc >= 'A' && nc <= 'G') || (nc >= 'a' && nc <= 'g') ||
          nc == 'z' || nc == 'Z' || nc == 'x'))
        return -2;
    p++;
    while (p < s.size() && (s[p] == ',' || s[p] == '\'')) octOut += s[p++];
    // Duration: [num][/[den]].
    dNum = 1;
    dDen = 1;
    size_t ds = p;
    while (p < s.size() && std::isdigit((unsigned char)s[p])) p++;
    std::string num = s.substr(ds, p - ds);
    if (!num.empty()) dNum = std::stoll(num);
    if (p < s.size() && s[p] == '/') {
        p++;
        size_t es = p;
        while (p < s.size() && std::isdigit((unsigned char)s[p])) p++;
        std::string den = s.substr(es, p - es);
        dDen = den.empty() ? 2 : std::stoll(den);
    }
    while (p < s.size() && (s[p] == '>' || s[p] == '<')) {
        if (s[p] == '>') {
            brokenGreater = true;
        } else {
            brokenLess = true;
        }
        brokenCount++;
        p++;
    }
    static const std::map<char, int> nat{{'C', 0}, {'D', 2}, {'E', 4},
                                         {'F', 5}, {'G', 7}, {'A', 9},
                                         {'B', 11}};
    if (nc == 'z' || nc == 'Z' || nc == 'x') return -1;
    char up = (char)std::toupper(nc);
    int semi = nat.at(up);
    int alter = 0;
    if (accOut == "^")
        alter = 1;
    else if (accOut == "^^")
        alter = 2;
    else if (accOut == "_")
        alter = -1;
    else if (accOut == "__")
        alter = -2;
    else if (accOut == "=")
        alter = 0;
    int base = 60 + semi + (std::islower(nc) ? 12 : 0);
    for (char o : octOut) {
        if (o == '\'') base += 12;
        if (o == ',') base -= 12;
    }
    return base + alter;
}

}  // namespace

bool StandardAbcReader::probeVoices(const std::string& path,
                                    std::vector<SourceEntry>& voices,
                                    std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open file";
        return false;
    }
    // File order (first-seen), matching importFile's voice table: the
    // mapper's row index is the assignment index, so sorted output here
    // would silently swap voices on import.
    std::vector<std::string> order;
    std::map<std::string, SourceEntry> seen;
    std::map<std::string, int> noteCount;
    std::map<std::string, bool> lyrics;
    auto ensure = [&](const std::string& vid) {
        if (!seen.count(vid)) {
            SourceEntry se;
            se.id = vid;
            se.name = vid;
            se.program = -1;
            seen[vid] = se;
            order.push_back(vid);
        }
    };
    ensure("default");
    std::string cur = "default";
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("V:", 0) == 0) {
            std::string id = line.substr(2);
            size_t s = id.find_first_not_of(" \t");
            if (s != std::string::npos) id = id.substr(s);
            size_t sp = id.find_first_of(" \t");
            std::string vid = (sp == std::string::npos) ? id : id.substr(0, sp);
            ensure(vid);
            cur = vid;
            continue;
        }
        if (line.rfind("w:", 0) == 0) {
            lyrics[cur] = true;
            continue;
        }
        // Header/comment/directive lines carry no notes. Music lines never
        // have ':' at index 1 (repeat markers "|: ..." win explicitly).
        if (line.empty() || line[0] == '%' ||
            (line.size() > 1 && line[1] == ':' && line[0] != '|'))
            continue;
        size_t p = 0;
        while (p < line.size()) {
            char c = line[p];
            if (c == '(' || c == '[' || c == '{' || c == '!' || c == '+' ||
                c == ')' || c == ']' || c == '}' || c == '|' || c == ':' ||
                c == ' ' || c == '\t' || c == '-' || c == '"' || c == '>' ||
                c == '<') {
                if (c == '"') {
                    size_t q = line.find('"', p + 1);
                    p = (q == std::string::npos) ? line.size() : q + 1;
                } else {
                    p++;
                }
                continue;
            }
            std::string a, o;
            long long dn = 1, dd = 1;
            bool bg = false, bl = false;
            int bc = 0;
            size_t qq = p;
            int pit = parseStdPitch(line, qq, a, o, dn, dd, bg, bl, bc);
            if (pit == -2) {
                p++;
                continue;
            }
            if (pit >= 0) noteCount[cur]++;
            p = qq;
        }
    }
    voices.clear();
    for (const auto& vid : order) {
        // The pre-header "default" voice only exists when music precedes
        // any V: line; drop it when it stayed empty.
        if (vid == "default" && noteCount[vid] == 0 && !lyrics[vid]) continue;
        SourceEntry se = seen[vid];
        se.noteCount = noteCount[vid];
        se.hasLyrics = lyrics[vid];
        voices.push_back(se);
    }
    if (voices.empty()) {
        SourceEntry se;
        se.id = "default";
        se.name = "Default voice";
        voices.push_back(se);
    }
    for (auto& v : voices) v.assign = "Ignore";
    // Defaults: a voice literally named Vocal/Ins (native-dialect files
    // being re-imported) keeps its name. Remaining voices fall back to the
    // positional rule mirroring the MIDI probe: lead -> Ins, second ->
    // Vocal. Name matches win so re-imports never swap voices.
    for (auto& v : voices) {
        if (v.id == "Vocal") v.assign = "Vocal";
        if (v.id == "Ins") v.assign = "Ins";
    }
    int mapped = 0;
    for (auto& v : voices) {
        if (v.assign != "Ignore") continue;
        if (mapped < 2) v.assign = (mapped++ == 0 ? "Ins" : "Vocal");
    }
    return true;
}

bool StandardAbcReader::importFile(const std::string& path, const Options& opt,
                                   Song& song, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open file";
        return false;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    song = Song();
    Header hdr;
    std::map<std::string, StdVoiceData> voices;
    std::string curVoice = "default";
    voices[curVoice] = StdVoiceData{curVoice, curVoice, {}};
    std::string curPart;
    int group = 0;

    // First pass: header fields + voice table.
    std::vector<std::string> voiceOrder;
    for (const auto& ln : lines) {
        if (ln.rfind("V:", 0) == 0) {
            std::string rest = ln.substr(2);
            size_t s = rest.find_first_not_of(" \t");
            if (s != std::string::npos) rest = rest.substr(s);
            size_t sp = rest.find_first_of(" \t");
            std::string vid = (sp == std::string::npos) ? rest : rest.substr(0, sp);
            if (!voices.count(vid)) {
                voices[vid] = StdVoiceData{vid, vid, {}};
                voiceOrder.push_back(vid);
            }
        }
    }
    if (voiceOrder.empty()) voiceOrder.push_back("default");

    bool sawLyrics = false;  // w: fields imply a sung lead voice
    auto assignOf = [&](const std::string& vid) -> std::string {
        for (size_t i = 0; i < voiceOrder.size() && i < opt.voiceAssign.size();
             ++i)
            if (voiceOrder[i] == vid) return opt.voiceAssign[i];
        // Dialect defaults (no explicit mapper choice): a voice literally
        // named Vocal/Ins keeps its name (native files being re-imported);
        // otherwise a lyric-bearing source sings the lead (Vocal), else the
        // lead voice is instrumental -> Ins.
        if (vid == "Vocal") return "Vocal";
        if (vid == "Ins") return "Ins";
        bool leadIsVocal = sawLyrics;
        for (size_t i = 0; i < voiceOrder.size(); ++i)
            if (voiceOrder[i] == vid) {
                if (i == 0) return leadIsVocal ? "Vocal" : "Ins";
                if (i == 1) return leadIsVocal ? "Ins" : "Vocal";
                return "Ignore";
            }
        return "Ignore";
    };

    // Second pass: music lines.
    // Per-voice clocks: standard-ABC voices sound simultaneously, so each
    // voice restarts at its own accumulated time, not the global maximum.
    std::map<std::string, Fraction> voiceTime;
    Fraction songTime(0, 1);
    int repeatDepth = 0;
    // Open ties per voice: '-' joins the next same-pitch note into the
    // indexed event instead of a new attack (rebuild rule: ties are kept
    // only for duration splits).
    std::map<std::string, std::pair<int, size_t>> tieOpen;
    // True bar number for log lines: derived from voice time, never from
    // token counts.
    auto barAt = [&](const Fraction& tt) {
        Fraction bl = barQuarters(hdr.meterN, hdr.meterD);
        Fraction q = tt / bl;
        return static_cast<int>(q.n / q.d) + 1;
    };
    for (const auto& ln : lines) {
        if (ln.empty()) continue;
        if (ln.rfind("M:", 0) == 0) {
            int a = 4, b = 4;
            if (!parseMeter(ln.substr(2), a, b)) {
                song.log.add(0, "Vocal", 0, "unsupported meter " + ln.substr(2),
                             "refuse");
            } else {
                hdr.meterN = a;
                hdr.meterD = b;
            }
            continue;
        }
        if (ln.rfind("L:", 0) == 0) {
            std::string v = ln.substr(2);
            size_t sl = v.find('/');
            if (sl != std::string::npos) {
                long long a = std::stoll(v.substr(0, sl));
                long long b = std::stoll(v.substr(sl + 1));
                hdr.unit = Fraction(a, b);
            }
            continue;
        }
        if (ln.rfind("Q:", 0) == 0) {
            std::string v = ln.substr(2);
            size_t eq = v.find('=');
            if (eq != std::string::npos) hdr.bpm = std::stoi(v.substr(eq + 1));
            continue;
        }
        if (ln.rfind("K:", 0) == 0) {
            std::string k = ln.substr(2);
            size_t s = k.find_first_not_of(" \t");
            if (s != std::string::npos) k = k.substr(s);
            // Key lines may carry trailing directives (clef=, transpose=,
            // middle=...): the key token itself never contains whitespace.
            size_t sp = k.find_first_of(" \t");
            if (sp != std::string::npos) k = k.substr(0, sp);
            if (!isStandardKey(k)) {
                song.log.add(0, "Vocal", 0,
                             "unsupported mode '" + k + "'; refused, no fold",
                             "refuse");
            } else {
                hdr.key = k;
            }
            continue;
        }
        if (ln.rfind("V:", 0) == 0) {
            std::string rest = ln.substr(2);
            size_t s = rest.find_first_not_of(" \t");
            if (s != std::string::npos) rest = rest.substr(s);
            size_t sp = rest.find_first_of(" \t");
            curVoice = (sp == std::string::npos) ? rest : rest.substr(0, sp);
            continue;
        }
        if (ln.rfind("P:", 0) == 0) {
            curPart = ln.substr(2);
            continue;
        }
        if (ln.rfind("w:", 0) == 0) {
            sawLyrics = true;
            song.log.add(group, curVoice, barAt(voiceTime[curVoice]),
                         "lyric w: field dropped", "drop");
            continue;
        }
        // Remaining header fields (C:, O:, R:, ...): identified by ':' at
        // index 1. Music lines never have that shape -- except repeat
        // markers "|: ...", which win explicitly. Note-led lines like
        // "C, D, E, ..." are no longer eaten here.
        if (ln.empty() || ln[0] == '%' ||
            (ln.size() > 1 && ln[1] == ':' && ln[0] != '|')) {
            continue;
        }
        // Music line: tokenize with support for repeats/endings/tuplets/
        // grace/slur/decoration/broken/chords/stacks.
        group++;
        if (!curPart.empty()) {
            song.sections.push_back(curPart);
            curPart.clear();
        }
        // Repeat unroll: handle |: :| by duplicating segment (max 8x guard).
        std::string expanded = ln;
        {
            bool hasOpen = expanded.find("|:") != std::string::npos;
            bool hasClose = expanded.find(":|") != std::string::npos;
            if (hasOpen || hasClose) {
                // Flatten: repeat played twice -> two copies in sequence.
                // Strip markers, duplicate inner body once.
                std::string stripped;
                for (size_t i = 0; i < expanded.size(); ++i) {
                    if (expanded.compare(i, 2, "|:") == 0 ||
                        expanded.compare(i, 2, ":|") == 0) {
                        i++;
                        continue;
                    }
                    stripped += expanded[i];
                }
                if (++repeatDepth > 8) {
                    song.log.add(group, curVoice, barAt(voiceTime[curVoice]),
                                 "repeat explosion guard: refused above 8",
                                 "refuse");
                    error = "repeat explosion";
                    return false;
                }
                expanded = stripped + "|" + stripped + "|";
                song.log.add(group, curVoice, barAt(voiceTime[curVoice]),
                             "repeat unrolled x2", "info");
                // Strip alternate endings [1 [2 markers.
                std::string noAlt;
                for (size_t i = 0; i < expanded.size(); ++i) {
                    if (expanded[i] == '[' &&
                        i + 1 < expanded.size() &&
                        std::isdigit((unsigned char)expanded[i + 1])) {
                        i++;
                        continue;
                    }
                    noAlt += expanded[i];
                }
                expanded = noAlt;
            }
        }
        Fraction t = voiceTime[curVoice];
        size_t p = 0;
        int tupletLeft = 0;
        Fraction tupletScale{2, 3};  // (3 -> 2/3 each
        Fraction brokenNext{1, 1};   // scale for the note after a broken mark
        std::vector<int> stackPitches;
        auto flushStack = [&](const Fraction& on, const Fraction& dur) {
            if (stackPitches.empty()) return;
            int top = *std::max_element(stackPitches.begin(), stackPitches.end());
            StdEvent ev;
            ev.onset = on;
            ev.dur = dur;
            ev.pitch = top;
            voices[curVoice].events.push_back(ev);
            if (stackPitches.size() > 1) {
                std::string sym = ChordFold::infer(stackPitches);
                if (!sym.empty()) {
                    FoldOutcome fo = ChordFold::fold(sym);
                    StdEvent cev;
                    cev.onset = on;
                    cev.dur = Fraction(0, 1);
                    cev.pitch = -2;  // chord-only marker
                    cev.chordSym = fo.kept ? fo.symbol : "";
                    voices[curVoice].events.push_back(cev);
                    song.log.add(group, curVoice, barAt(on),
                                 "polyphonic stack: top pitch kept, chord '" +
                                     sym + "' inferred",
                                 fo.kept ? "fold" : "warn");
                } else {
                    song.log.add(group, curVoice, barAt(on),
                                 "polyphonic stack kept melody only", "warn");
                }
            }
            stackPitches.clear();
        };
        while (p < expanded.size()) {
            char c = expanded[p];
            if (c == ' ' || c == '\t' || c == '|' || c == ':' || c == '[' ||
                c == ']') {
                if (c == '[') {
                    // Alternate ending outside a repeat block: endings play
                    // as sequential measures (rebuild rule), so skip the
                    // "[N" marker and keep the notes.
                    if (p + 1 < expanded.size() &&
                        std::isdigit(
                            static_cast<unsigned char>(expanded[p + 1]))) {
                        song.log.add(group, curVoice, barAt(t),
                                     "alternate ending: sequential measure",
                                     "info");
                        p += 2;
                        continue;
                    }
                    // Inline key change: phase 1 keeps the single header key.
                    if (expanded.compare(p, 3, "[K:") == 0) {
                        size_t q = expanded.find(']', p);
                        song.log.add(group, curVoice, barAt(t),
                                     "inline key change ignored; kept header "
                                     "key",
                                     "warn");
                        p = (q == std::string::npos) ? expanded.size() : q + 1;
                        continue;
                    }
                    // stack [CEG] (a '-' inside marks a tied member:
                    // skipped, members keep full length).
                    size_t q = p + 1;
                    std::vector<int> tmp;
                    bool ok = true;
                    while (q < expanded.size() && expanded[q] != ']') {
                        if (expanded[q] == '-') {
                            q++;  // intra-stack tie mark, not a pitch
                            continue;
                        }
                        std::string a, o;
                        long long dn = 1, dd = 1;
                        bool bg = false, bl = false;
                        int bc = 0;
                        size_t qq = q;
                        int pit = parseStdPitch(expanded, qq, a, o, dn, dd, bg,
                                               bl, bc);
                        if (pit < -1) {
                            ok = false;
                            break;
                        }
                        if (pit >= 0) tmp.push_back(pit);
                        q = qq;
                    }
                    if (ok && q < expanded.size() && expanded[q] == ']') {
                        // stack duration from first memberLength? use unit.
                        stackPitches = tmp;
                        p = q + 1;
                        // duration follows? parse trailing length on ']'.
                        size_t r = p;
                        long long dn = 1, dd = 1;
                        size_t rs = r;
                        while (r < expanded.size() &&
                               std::isdigit((unsigned char)expanded[r]))
                            r++;
                        std::string num = expanded.substr(rs, r - rs);
                        if (!num.empty()) dn = std::stoll(num);
                        if (r < expanded.size() && expanded[r] == '/') {
                            r++;
                            size_t es = r;
                            while (r < expanded.size() &&
                                   std::isdigit((unsigned char)expanded[r]))
                                r++;
                            std::string den = expanded.substr(es, r - es);
                            dd = den.empty() ? 2 : std::stoll(den);
                        }
                        p = r;
                        Fraction ddur =
                            abcDurationToQuarters(dn, dd, hdr.unit);
                        if (tupletLeft > 0) {
                            ddur = ddur * tupletScale;
                            tupletLeft--;
                        }
                        flushStack(t, ddur);
                        t = t + ddur;
                        continue;
                    }
                }
                p++;
                continue;
            }
            if (c == '(') {
                // Tuplet (3 or slur (legato): digit follows -> tuplet.
                if (p + 1 < expanded.size() &&
                    std::isdigit((unsigned char)expanded[p + 1])) {
                int n = expanded[p + 1] - '0';
                    tupletLeft = n;
                    if (n == 3)
                        tupletScale = Fraction(2, 3);
                    else if (n == 2)
                        tupletScale = Fraction(3, 2);
                    else
                        tupletScale = Fraction(1, 1);
                    song.log.add(group, curVoice, barAt(t),
                                 "tuplet (" + std::to_string(n) +
                                     ": exact write or refuse",
                                 "info");
                    p += 2;
                    continue;
                }
                // slur start: delete.
                song.log.add(group, curVoice, barAt(t), "slur deleted", "drop");
                p++;
                continue;
            }
            if (c == ')') {
                p++;
                continue;
            }
            if (c == '{') {
                // grace: drop grace pitch, keep full duration on main note.
                size_t q = expanded.find('}', p);
                song.log.add(group, curVoice, barAt(t),
                             "grace note dropped, main note kept", "drop");
                p = (q == std::string::npos) ? expanded.size() : q + 1;
                continue;
            }
            if (c == '!' || c == '+') {
                size_t q = expanded.find(c, p + 1);
                song.log.add(group, curVoice, barAt(t), "decoration deleted",
                             "drop");
                p = (q == std::string::npos) ? expanded.size() : q + 1;
                continue;
            }
            if (c == '"') {
                size_t q = expanded.find('"', p + 1);
                if (q == std::string::npos) break;
                std::string sym = expanded.substr(p + 1, q - p - 1);
                FoldOutcome fo = ChordFold::fold(sym);
                StdEvent ev;
                ev.onset = t;
                ev.dur = Fraction(0, 1);
                ev.pitch = -2;
                ev.chordSym = fo.kept ? fo.symbol : "";
                voices[curVoice].events.push_back(ev);
                if (fo.flagged)
                    song.log.add(group, curVoice, barAt(t),
                                 "chord '" + sym + "' folded", "fold");
                p = q + 1;
                continue;
            }
            if (c == '-') {
                // Tie: the next same-pitch note joins the last pitch event
                // instead of a new attack. Chord markers carry no pitch,
                // so look past them; a tie from a rest is invalid source.
                auto& evs = voices[curVoice].events;
                int idx = -1;
                for (int i = static_cast<int>(evs.size()) - 1; i >= 0; --i) {
                    if (evs[i].pitch == -2) continue;
                    if (evs[i].pitch >= 0) idx = i;
                    break;
                }
                if (idx < 0)
                    song.log.add(group, curVoice, barAt(t),
                                 "stray tie mark ignored", "warn");
                else
                    tieOpen[curVoice] = {evs[idx].pitch,
                                         static_cast<size_t>(idx)};
                p++;
                continue;
            }
            // Regular pitch token.
            {
                std::string a, o;
                long long dn = 1, dd = 1;
                bool bg = false, bl = false;
                int bc = 0;
                size_t qq = p;
                int pit = parseStdPitch(expanded, qq, a, o, dn, dd, bg, bl, bc);
                if (pit == -2) {
                    song.log.add(group, curVoice, barAt(t),
                                 std::string("unsupported token '") + c +
                                     "' refused",
                                 "refuse");
                    tieOpen.erase(curVoice);
                    p++;
                    continue;
                }
                Fraction ddur = abcDurationToQuarters(dn, dd, hdr.unit);
                // Broken rhythm: expand to explicit durations.
                // A>B (mark on first note): this note x3/2, next x1/2.
                // A<B (mark on first note): this note x1/2, next x3/2.
                if (bg && !bl) {
                    ddur = ddur * Fraction(3, 2);
                    brokenNext = Fraction(1, 2);
                    song.log.add(group, curVoice, barAt(t),
                                 "broken rhythm '>' expanded", "info");
                } else if (bl && !bg) {
                    ddur = ddur * Fraction(1, 2);
                    brokenNext = Fraction(3, 2);
                    song.log.add(group, curVoice, barAt(t),
                                 "broken rhythm '<' expanded", "info");
                } else if (bg || bl) {
                    song.log.add(group, curVoice, barAt(t),
                                 "double broken rhythm refused", "refuse");
                } else if (!(brokenNext == Fraction(1, 1))) {
                    ddur = ddur * brokenNext;
                    brokenNext = Fraction(1, 1);
                }
                if (tupletLeft > 0) {
                    ddur = ddur * tupletScale;
                    tupletLeft--;
                }
                auto tied = tieOpen.find(curVoice);
                if (tied != tieOpen.end()) {
                    if (pit >= 0 && pit == tied->second.first) {
                        voices[curVoice].events[tied->second.second].dur =
                            voices[curVoice].events[tied->second.second].dur +
                            ddur;
                        t = t + ddur;
                        tieOpen.erase(tied);
                        p = qq;
                        continue;
                    }
                    song.log.add(group, curVoice, barAt(t),
                                 std::string("tie into ") +
                                     (pit < 0 ? "rest" : "different pitch") +
                                     " ignored; kept attacks",
                                 "warn");
                    tieOpen.erase(tied);
                }
                StdEvent ev;
                ev.onset = t;
                ev.dur = ddur;
                ev.pitch = pit;
                voices[curVoice].events.push_back(ev);
                t = t + ddur;
                p = qq;
            }
        }
        voiceTime[curVoice] = t;
        if (t > songTime) songTime = t;
    }

    song.bpm = hdr.bpm;
    song.meterN = hdr.meterN;
    song.meterD = hdr.meterD;
    song.key = hdr.key;
    // Map probed voices to Vocal/Ins (default first two mapped).
    int vi = 0;
    for (const auto& vid : voiceOrder) {
        std::string a = assignOf(vid);
        const auto& evs = voices[vid].events;
        std::vector<SongNote>* dst = nullptr;
        if (a == "Vocal")
            dst = &song.vocal;
        else if (a == "Ins")
            dst = &song.ins;
        else {
            int nc = 0;
            for (const auto& e : evs)
                if (e.pitch >= 0) nc++;
            if (nc > 0)
                song.log.add(0, "Vocal", 0,
                             "extra voice '" + vid + "' ignored (" +
                                 std::to_string(nc) + " notes)",
                             "warn");
            continue;
        }
        for (const auto& e : evs) {
            if (e.pitch == -2) {
                if (a == "Vocal" && !e.chordSym.empty())
                    song.chords.push_back(ChordEvent{e.onset, e.chordSym});
                continue;
            }
            if (e.pitch < 0) continue;  // rests shape bars at export
            dst->push_back(SongNote{e.onset, e.dur, e.pitch});
        }
        vi++;
    }
    sortSong(song);
    return true;
}

}  // namespace yue2_abcedit
