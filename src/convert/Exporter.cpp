#include "convert/Exporter.h"

#include <algorithm>
#include <map>
#include <sstream>

#include "convert/Quantizer.h"
#include "core/AbcDialect.h"

namespace yue2_abcedit {
namespace {

// A timed token inside one bar: onset offset (in units), unit count, text
// without tie handling, chord prefix, pitch info for tie splitting.
struct BarToken {
    int startUnits = 0;
    int count = 0;
    std::string core;  // ABC note core or "z" (no duration digits yet)
    bool isRest = false;
    std::string chord;  // chord symbol at this onset (Vocal only)
    int midi = 0;
};

// Render one voice's bar body at the given grid. Rests fill gaps. Notes that
// need disallowed counts are tie-split (note) or concatenated (rest).
// Returns false + reason when the bar cannot be written exactly.
bool renderBar(const std::vector<SongNote>& notes,
               const std::vector<ChordEvent>& chords, const Fraction& barStart,
               const Fraction& barLen, int unitDenom,
               const std::map<char, int>& keySig,
               std::map<char, int>& barState, std::string& out,
               std::string& reason, bool vocal) {
    Fraction unit = unitQuarters(unitDenom);
    bool e = false;
    long long barUnits = barLen.divExact(unit, e);
    if (!e) {
        reason = "bar length not on grid";
        return false;
    }
    // Collect clipped notes for this bar.
    struct N {
        int s, c, pitch;
    };
    std::vector<N> ns;
    for (const auto& n : notes) {
        Fraction end = n.onset + n.dur;
        if (end <= barStart || n.onset >= barStart + barLen) continue;
        if (n.onset < barStart || end > barStart + barLen) {
            int startBar = barNumber(n.onset, barLen);
            Fraction startPos = n.onset - Fraction(startBar - 1, 1) * barLen;
            reason = "note crosses bar boundary (starts " +
                     beatPhrase(startPos) + " of bar " +
                     std::to_string(startBar) + ")";
            return false;
        }
        bool e1 = false, e2 = false;
        long long s = (n.onset - barStart).divExact(unit, e1);
        long long c = n.dur.divExact(unit, e2);
        if (!e1 || !e2 || c <= 0) {
            reason = "a note " + beatPhrase(n.onset - barStart) +
                     " cannot be written exactly";
            return false;
        }
        ns.push_back({(int)s, (int)c, n.pitch});
    }
    std::sort(ns.begin(), ns.end(),
              [](const N& a, const N& b) { return a.s < b.s; });
    // Chord map onset-units -> symbol.
    std::map<int, std::string> chordAt;
    if (vocal) {
        for (const auto& ch : chords) {
            if (ch.onset < barStart || ch.onset >= barStart + barLen) continue;
            bool ec = false;
            long long s = (ch.onset - barStart).divExact(unit, ec);
            if (!ec) {
                reason = "chord " + beatPhrase(ch.onset - barStart) +
                         " not on grid";
                return false;
            }
            chordAt[(int)s] = ch.symbol;
        }
    }
    // Full-bar rest shortcut: no notes.
    if (ns.empty()) {
        if (!chordAt.empty()) {
            // Chords over rest: emit chord + rest spans (no Z: Z cannot
            // cover a harmony change).
            int pos = 0;
            std::vector<int> bounds{0};
            for (const auto& kv : chordAt) bounds.push_back(kv.first);
            bounds.push_back((int)barUnits);
            std::ostringstream os;
            for (size_t i = 0; i + 1 < bounds.size(); ++i) {
                int seg = bounds[i + 1] - bounds[i];
                if (seg <= 0) continue;
                auto it = chordAt.find(bounds[i]);
                if (it != chordAt.end()) os << "\"" << it->second << "\"";
                for (int piece : Quantizer::splitCount(seg)) os << "z" << piece;
                pos = bounds[i + 1];
            }
            (void)pos;
            out = os.str();
            return true;
        }
        out = "Z";
        return true;
    }
    // Overlap check (monophonic voices).
    for (size_t i = 1; i < ns.size(); ++i) {
        if (ns[i].s < ns[i - 1].s + ns[i - 1].c) {
            reason = "overlapping notes in monophonic voice";
            return false;
        }
    }
    // Unified segment walk: boundaries at every note edge and every chord
    // onset. A chord change inside a held note splits the notation with a
    // tie ("C"E16-"Am7"E16), exactly as the dialect requires; rest gaps
    // carry their chord symbols at the correct onset. No second pass.
    std::vector<int> bounds{0};
    for (const auto& n : ns) {
        bounds.push_back(n.s);
        bounds.push_back(n.s + n.c);
    }
    for (const auto& kv : chordAt) bounds.push_back(kv.first);
    bounds.push_back((int)barUnits);
    std::sort(bounds.begin(), bounds.end());
    bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
    std::ostringstream os;
    for (size_t b = 0; b + 1 < bounds.size(); ++b) {
        int s0 = bounds[b], s1 = bounds[b + 1];
        int len = s1 - s0;
        if (len <= 0) continue;
        auto cit = chordAt.find(s0);
        if (cit != chordAt.end()) os << "\"" << cit->second << "\"";
        const N* cover = nullptr;
        for (const auto& n : ns)
            if (s0 >= n.s && s1 <= n.s + n.c) cover = &n;
        if (cover == nullptr) {
            for (int piece : Quantizer::splitCount(len)) os << "z" << piece;
            continue;
        }
        bool lastOfNote = (s1 == cover->s + cover->c);
        auto pieces = Quantizer::splitCount(len);
        for (size_t k = 0; k < pieces.size(); ++k) {
            std::string core =
                midiToAbcNote(cover->pitch, keySig, barState, true);
            os << core << pieces[k];
            if (k + 1 < pieces.size() || !lastOfNote) os << "-";
        }
    }
    out = os.str();
    return true;
}

}  // namespace

Exporter::Result Exporter::exportSong(Song& song, int unitDenom) {
    Result r;
    if (song.vocal.empty() && song.ins.empty()) {
        r.error = "group 1, Vocal, bar 1: no notes to export";
        return r;
    }
    Fraction barLen = barQuarters(song.meterN, song.meterD);
    Fraction end = songEnd(song);
    // Bar grid from time 0.
    long long nBars = 0;
    {
        // end / barLen, exact ceiling.
        Fraction q = end / barLen;
        nBars = q.n / q.d + (q.n % q.d != 0 ? 1 : 0);
        if (nBars < 1) nBars = 1;
    }
    std::ostringstream os;
    os << "X:1\nT:\nM:" << song.meterN << "/" << song.meterD << "\nL:1/"
       << unitDenom << "\nQ:1/4=" << song.bpm << "\n"
       << kVoiceVocalDef << "\n"
       << kVoiceInsDef << "\nK:" << song.key << "\n";

    std::map<char, int> keySig = keyAccidentals(song.key);
    // Split notes at bar edges into tied segments first: real performances
    // sustain across barlines, and the dialect writes that as ties.
    auto splitAtBars = [&](std::vector<SongNote>& notes) {
        std::vector<SongNote> split;
        for (const auto& n : notes) {
            Fraction end = n.onset + n.dur;
            Fraction q0 = n.onset / barLen;
            Fraction q1 = end / barLen;
            long long b0 = q0.n / q0.d;
            long long b1 = (q1.n % q1.d == 0) ? (q1.n / q1.d - 1) : (q1.n / q1.d);
            if (b1 < b0) b1 = b0;
            for (long long b = b0; b <= b1; ++b) {
                Fraction segStart =
                    (b == b0) ? n.onset : barLen * Fraction(b, 1);
                Fraction segEnd =
                    (b == b1) ? end : barLen * Fraction(b + 1, 1);
                if (segEnd > segStart)
                    split.push_back(SongNote{segStart, segEnd - segStart,
                                             n.pitch});
            }
        }
        notes.swap(split);
    };
    splitAtBars(song.vocal);
    splitAtBars(song.ins);
    // Bar bodies per voice.
    std::vector<std::string> vocalBars, insBars;
    for (long long b = 0; b < nBars; ++b) {
        Fraction bs = barLen * Fraction(b, 1);
        std::map<char, int> stV, stI;  // accidental state resets per bar
        std::string vb, ib, reason;
        bool okV = renderBar(song.vocal, song.chords, bs, barLen, unitDenom,
                             keySig, stV, vb, reason, true);
        if (!okV) {
            song.log.add(1, "Vocal", (int)b + 1, reason, "refuse");
            r.error = "group 1, Vocal, bar " + std::to_string(b + 1) + ": " +
                      reason;
            return r;
        }
        // Ties across bar lines: if a note ends exactly at bar end and the
        // next bar starts with the same pitch, join with '-'. renderBar
        // emits per-bar tokens; detect adjacency here by suffix/prefix patch:
        // (handled by comparing raw notes, not text).
        bool okI = renderBar(song.ins, {}, bs, barLen, unitDenom, keySig, stI,
                             ib, reason, false);
        if (!okI) {
            song.log.add(1, "Ins", (int)b + 1, reason, "refuse");
            r.error = "group 1, Ins, bar " + std::to_string(b + 1) + ": " +
                      reason;
            return r;
        }
        vocalBars.push_back(vb);
        insBars.push_back(ib);
    }
    // Cross-bar ties: patch bar text where a pitch sustains across the line.
    // Find notes ending at bar edge with same pitch starting next bar, same
    // voice: append '-' to bar b body and drop nothing else (checker merges).
    // Only valid when both sides are pitched (not Z / not rest-ending).
    auto patchTies = [&](std::vector<SongNote>& notes,
                         std::vector<std::string>& bars) {
        for (long long b = 0; b + 1 < nBars; ++b) {
            Fraction edge = barLen * Fraction(b + 1, 1);
            const SongNote* ending = nullptr;
            const SongNote* starting = nullptr;
            for (const auto& n : notes) {
                if (n.onset + n.dur == edge) ending = &n;
                if (n.onset == edge) starting = &n;
            }
            if (ending && starting && ending->pitch == starting->pitch &&
                bars[b] != "Z" && bars[b + 1] != "Z") {
                bars[b] += "-";
            }
        }
    };
    patchTies(song.vocal, vocalBars);
    patchTies(song.ins, insBars);

    // Groups: 1-4 bars per music line; single group here (no mid-song
    // meter/key changes in this phase). Section comment from song.sections.
    std::string section =
        song.sections.empty() ? "verse" : song.sections.front();
    auto emitVoice = [&](const std::string& vname,
                         const std::vector<std::string>& bars) {
        for (size_t i = 0; i < bars.size(); i += 4) {
            size_t n = std::min<size_t>(4, bars.size() - i);
            // Z-run compression: consecutive full-bar "Z" stay as-is per bar;
            // checker expands Z2-Z4 only when written that way. Keep single
            // Z per bar for simplicity (valid: 1-4 bars each "Z").
            for (size_t k = 0; k < n; ++k) {
                if (k) os << "|";
                os << bars[i + k];
            }
            os << "|\n";
        }
    };
    (void)emitVoice;
    for (size_t i = 0; i < vocalBars.size(); i += 4) {
        size_t n = std::min<size_t>(4, vocalBars.size() - i);
        os << "% " << section << "\nV: Vocal\n";
        for (size_t k = 0; k < n; ++k) {
            if (k) os << "|";
            os << vocalBars[i + k];
        }
        os << "|\nV: Ins\n";
        for (size_t k = 0; k < n; ++k) {
            if (k) os << "|";
            os << insBars[i + k];
        }
        os << "|\n";
    }
    r.ok = true;
    r.text = os.str();
    return r;
}

}  // namespace yue2_abcedit
