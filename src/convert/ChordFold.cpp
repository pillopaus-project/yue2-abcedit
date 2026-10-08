#include "convert/ChordFold.h"

#include <algorithm>
#include <map>
#include <set>

#include "core/AbcDialect.h"

namespace yue2_abcedit {
namespace {

std::string pcNameSharp(int pc) {
    static const char* names[12] = {"C",  "C#", "D",  "D#", "E", "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};
    return names[(pc % 12 + 12) % 12];
}

int pcOfRoot(const std::string& root) {
    static const std::map<std::string, int> m{
        {"C", 0}, {"C#", 1}, {"Db", 1}, {"D", 2}, {"D#", 3}, {"Eb", 3},
        {"E", 4}, {"F", 5}, {"F#", 6},  {"Gb", 6}, {"G", 7}, {"G#", 8},
        {"Ab", 8}, {"A", 9}, {"A#", 10}, {"Bb", 10}, {"B", 11}};
    auto it = m.find(root);
    return it == m.end() ? -1 : it->second;
}

// Split "RootQuality/Bass" into root, quality, bass.
bool splitSymbol(const std::string& sym, std::string& root, std::string& qual,
                 std::string& bass) {
    size_t i = 1;
    if (sym.empty() || sym[0] < 'A' || sym[0] > 'G') return false;
    if (sym.size() > 1 && (sym[1] == '#' || sym[1] == 'b')) i = 2;
    root = sym.substr(0, i);
    size_t slash = sym.find('/', i);
    if (slash == std::string::npos) {
        qual = sym.substr(i);
        bass.clear();
    } else {
        qual = sym.substr(i, slash - i);
        bass = sym.substr(slash + 1);
    }
    return true;
}

bool qualityAllowed(const std::string& q) {
    for (const auto& a : allowedQualities())
        if (a == q) return true;
    return false;
}

std::string foldQuality(const std::string& q) {
    if (qualityAllowed(q)) return q;
    // Deterministic downgrade table (spec: 9->7, maj9->maj7, add9->triad...).
    static const std::vector<std::pair<std::string, std::string>> table{
        {"maj9", "maj7"}, {"maj13", "maj7"}, {"maj11", "maj7"},
        {"m9", "m7"},     {"m11", "m7"},     {"m13", "m7"},
        {"9", "7"},       {"11", "7sus4"},   {"13", "7"},
        {"7b9", "7"},     {"7#9", "7"},       {"7#11", "7"},
        {"add9", ""},     {"add11", ""},      {"add4", ""},
        {"6/9", "6"},     {"m6/9", "m6"},     {"69", "6"},
        {"sus", "sus4"},  {"7sus", "7sus4"},  {"9sus4", "7sus4"},
        {"+", "aug"},     {"°", "dim"},       {"o", "dim"},
        {"ø", "m7b5"},    {"5", ""},          {"2", "sus2"},
        {"4", "sus4"},
    };
    for (const auto& kv : table)
        if (kv.first == q) return kv.second;
    // Prefix rules: strip extensions down to a known stem.
    static const std::vector<std::string> stems{
        "m(maj7)", "m7b5", "7sus4", "maj7", "dim7", "sus4", "sus2",
        "dim", "aug", "m7", "m6", "m", "7", "6", ""};
    for (const auto& s : stems) {
        if (q.rfind(s, 0) == 0 && (s.empty() || q.size() == s.size() ||
                                   q[s.size()] == '(' || q[s.size()] == '#')) {
            // Only accept exact stem when remainder looks like an extension.
            if (s == q) return s;
        }
    }
    // "Xm9" style: try stripping trailing extension digits.
    std::string base = q;
    while (!base.empty() &&
           (base.back() == '9' || base.back() == '1')) {
        if (base.size() >= 2 && base.substr(base.size() - 2) == "11") {
            base = base.substr(0, base.size() - 2);
            break;
        }
        if (base.size() >= 2 && base.substr(base.size() - 2) == "13") {
            base = base.substr(0, base.size() - 2);
            break;
        }
        base.pop_back();
    }
    if (qualityAllowed(base)) return base;
    return "";
}

bool isChordTone(int rootPc, const std::string& qual, int bassPc) {
    int rel = (bassPc - rootPc + 12) % 12;
    if (rel == 0) return true;
    if (qual == "" || qual == "6") return rel == 4 || rel == 7;
    if (qual == "m" || qual == "m6") return rel == 3 || rel == 7;
    if (qual == "dim" || qual == "dim7") return rel == 3 || rel == 6;
    if (qual == "aug") return rel == 4 || rel == 8;
    if (qual == "7" || qual == "m7" || qual == "maj7" || qual == "m(maj7)")
        return rel == 4 || rel == 3 || rel == 7 || rel == 10 || rel == 11;
    if (qual == "m7b5") return rel == 3 || rel == 6 || rel == 10;
    if (qual == "sus4" || qual == "7sus4") return rel == 5 || rel == 7;
    if (qual == "sus2") return rel == 2 || rel == 7;
    return rel == 0 || rel == 7;
}

}  // namespace

std::string ChordFold::infer(const std::vector<int>& pitches) {
    if (pitches.size() < 2) return "";
    std::set<int> pcs;
    for (int p : pitches) pcs.insert((p % 12 + 12) % 12);
    if (pcs.size() < 2) return "";
    // Try each pc as root; score triad/seventh matches. Deterministic.
    struct Cand {
        int root;
        std::string qual;
        int score;
    };
    std::vector<Cand> cands;
    for (int r : pcs) {
        bool has3m = pcs.count((r + 3) % 12);
        bool has3M = pcs.count((r + 4) % 12);
        bool has5 = pcs.count((r + 7) % 12);
        bool has5dim = pcs.count((r + 6) % 12);
        bool has7m = pcs.count((r + 10) % 12);
        bool has7M = pcs.count((r + 11) % 12);
        bool has2 = pcs.count((r + 2) % 12);
        bool has4 = pcs.count((r + 5) % 12);
        bool has6 = pcs.count((r + 9) % 12);
        if (has3M && has5 && has7M)
            cands.push_back({r, "maj7", 40});
        else if (has3m && has5 && has7m)
            cands.push_back({r, "m7", 39});
        else if (has3M && has5 && has7m)
            cands.push_back({r, "7", 38});
        else if (has3m && has5dim && has7m)
            cands.push_back({r, "m7b5", 37});
        else if (has3m && has5dim)
            cands.push_back({r, "dim", 30});
        else if (has3M && pcs.count((r + 8) % 12))
            cands.push_back({r, "aug", 30});
        else if (has3M && has5)
            cands.push_back({r, "", 20});
        else if (has3m && has5)
            cands.push_back({r, "m", 20});
        else if (has4 && has5)
            cands.push_back({r, "sus4", 15});
        else if (has2 && has5)
            cands.push_back({r, "sus2", 15});
        else if (has3M && has6)
            cands.push_back({r, "6", 14});
        else if (has3m && has6)
            cands.push_back({r, "m6", 14});
    }
    if (cands.empty()) {
        // Two-note shells: thirds, fifths and sixths name a triad with the
        // third member implied. Seconds, sevenths and tritones stay
        // melody-only (too ambiguous to name).
        if (pcs.size() == 2) {
            auto it = pcs.begin();
            int lo = *it, hi = *std::next(it);
            int iv = (hi - lo + 12) % 12;
            int root = -1;
            std::string qual;
            switch (iv) {
                case 7: root = lo; qual = ""; break;    // fifth -> major
                case 4: root = lo; qual = ""; break;    // major third
                case 3: root = lo; qual = "m"; break;   // minor third
                case 5: root = hi; qual = ""; break;    // fourth = inv. fifth
                case 8: root = hi; qual = ""; break;    // min 6th = inv. maj 3rd
                case 9: root = hi; qual = "m"; break;   // maj 6th = inv. min 3rd
                default: break;
            }
            if (root >= 0) return pcNameSharp(root) + qual;
            return "";
        }
        // Denser sonority with no triad match: the lead is often a
        // non-chord tone over a naming triad, so retry without the lead
        // pitch class (one level only).
        if (pcs.size() >= 3 && !pitches.empty()) {
            int leadPc = (( *std::max_element(pitches.begin(),
                                              pitches.end())) % 12 + 12) % 12;
            if (pcs.count(leadPc)) {
                std::vector<int> rest;
                for (int p : pitches)
                    if (((p % 12 + 12) % 12) != leadPc) rest.push_back(p);
                if (rest.size() >= 2) {
                    // Re-run triad/shell matching on the remainder only.
                    std::set<int> rpcs;
                    for (int p : rest) rpcs.insert((p % 12 + 12) % 12);
                    if (rpcs.size() == 2) return infer(rest);
                    // Manual triad pass on rpcs (no further fallback).
                    std::string sub;
                    {
                        // Manual triad pass on rpcs (no further fallback).
                        struct Cand2 { int root; std::string qual; int score; };
                        std::vector<Cand2> c2;
                        for (int r : rpcs) {
                            bool h3m = rpcs.count((r + 3) % 12);
                            bool h3M = rpcs.count((r + 4) % 12);
                            bool h5 = rpcs.count((r + 7) % 12);
                            bool h5d = rpcs.count((r + 6) % 12);
                            bool h7m = rpcs.count((r + 10) % 12);
                            bool h7M = rpcs.count((r + 11) % 12);
                            if (h3M && h5 && h7M) c2.push_back({r, "maj7", 40});
                            else if (h3m && h5 && h7m) c2.push_back({r, "m7", 39});
                            else if (h3M && h5 && h7m) c2.push_back({r, "7", 38});
                            else if (h3M && h5) c2.push_back({r, "", 20});
                            else if (h3m && h5) c2.push_back({r, "m", 20});
                        }
                        if (!c2.empty()) {
                            std::sort(c2.begin(), c2.end(),
                                      [](const Cand2& a, const Cand2& b) {
                                          if (a.score != b.score)
                                              return a.score > b.score;
                                          return a.root < b.root;
                                      });
                            sub = pcNameSharp(c2[0].root) + c2[0].qual;
                        }
                    }
                    if (!sub.empty()) return sub;
                }
            }
        }
        return "";
    }
    // Prefer highest score; tie-break lowest root (deterministic).
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.root < b.root;
    });
    return pcNameSharp(cands[0].root) + cands[0].qual;
}

FoldOutcome ChordFold::fold(const std::string& symbol) {
    FoldOutcome o;
    std::string root, qual, bass;
    if (!splitSymbol(symbol, root, qual, bass)) {
        o.kept = false;
        o.flagged = true;
        return o;
    }
    if (pcOfRoot(root) < 0) {
        o.kept = false;
        o.flagged = true;
        return o;
    }
    std::string folded = foldQuality(qual);
    bool flagged = (folded != qual);
    if (!bass.empty()) {
        if (pcOfRoot(bass) < 0) {
            o.kept = false;
            o.flagged = true;
            return o;
        }
        // Bass function check: if folded quality no longer contains the bass
        // as a chord tone, keep melody only instead of writing a wrong symbol.
        if (!isChordTone(pcOfRoot(root), folded, pcOfRoot(bass))) {
            o.kept = false;
            o.flagged = true;
            return o;
        }
        o.symbol = root + folded + "/" + bass;
    } else {
        o.symbol = root + folded;
    }
    if (!isValidChord(o.symbol)) {
        o.kept = false;
        o.flagged = true;
        o.symbol.clear();
        return o;
    }
    o.kept = true;
    o.flagged = flagged;
    return o;
}

}  // namespace yue2_abcedit
