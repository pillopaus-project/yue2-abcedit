#include "convert/Quantizer.h"

#include <algorithm>
#include <map>
#include <vector>

#include "core/AbcDialect.h"

namespace yue2_abcedit {

std::vector<int> Quantizer::splitCount(int count) {
    static const std::vector<int> allow{48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1};
    std::vector<int> out;
    int rest = count;
    for (int a : allow) {
        while (rest >= a) {
            // Avoid leaving a remainder of 1 behind a large piece? 1 is
            // allowed, so any greedy split is valid. Keep greedy.
            out.push_back(a);
            rest -= a;
        }
    }
    return out;
}

namespace {
// All onsets+ends that must sit on the grid, per voice + chords.
struct GridPoint {
    Fraction t;
    bool onset;  // false = note ending
};

std::vector<GridPoint> gridPoints(const Song& song) {
    std::vector<GridPoint> pts;
    for (const auto& n : song.vocal) {
        pts.push_back({n.onset, true});
        pts.push_back({n.onset + n.dur, false});
    }
    for (const auto& n : song.ins) {
        pts.push_back({n.onset, true});
        pts.push_back({n.onset + n.dur, false});
    }
    for (const auto& c : song.chords) pts.push_back({c.onset, true});
    return pts;
}

bool fitsOn(int unitDenom, const std::vector<GridPoint>& pts,
            GridPoint& bad) {
    Fraction unit = unitQuarters(unitDenom);
    for (const auto& p : pts) {
        bool exact = false;
        (void)p.t.divExact(unit, exact);
        if (!exact) {
            bad = p;
            return false;
        }
    }
    return true;
}

// Nearest writable position on the given unit (pure closest-slot math;
// no musical judgment -- the caller decides whether the move is legal).
Fraction nearestSlot(const Fraction& p, const Fraction& unit) {
    Fraction q = p / unit + Fraction(1, 2);
    long long k = q.n / q.d;  // q >= 0 for song times
    if (q.n < 0) k = -((-q.n + q.d - 1) / q.d);
    return Fraction(k, 1) * unit;
}

// True when an onset sits on an exact third-based subdivision (tuplet
// structure, not drift). Only exact hits count: the window (1/480 of a
// quarter note) is below single-tick drift at 240 PPQ, so played timing
// never trips it -- only programmed triplets do.
bool phaseThirdLike(const Fraction& p, const Fraction& span) {
    Fraction q = p / span;
    long long k = q.n / q.d;  // floor, song times are >= 0
    Fraction frac = (p - span * Fraction(k, 1)) / span;
    Fraction eps(1, 480);
    auto close = [&](const Fraction& a, const Fraction& b) {
        Fraction d = a - b;
        if (d.n < 0) d.n = -d.n;
        return d <= eps;
    };
    return close(frac, Fraction(1, 3)) || close(frac, Fraction(2, 3));
}

bool onsetThirdLike(const Fraction& p) {
    return phaseThirdLike(p, Fraction(1, 1)) ||
           phaseThirdLike(p, Fraction(2, 1));
}
}  // namespace

Quantizer::Result Quantizer::pickL(const Song& song) {
    Result r;
    auto pts = gridPoints(song);
    if (pts.empty()) {
        r.ok = true;
        r.unitDenom = 32;
        return r;
    }
    // Coarsest first: smallest denominator that fits everything.
    static const std::vector<int> denoms{2,  4,   8,   16,  32,
                                         64, 128, 256, 512, 1024};
    GridPoint bad{Fraction(0, 1), true};
    for (int d : denoms) {
        if (fitsOn(d, pts, bad)) {
            r.ok = true;
            r.unitDenom = d;
            return r;
        }
    }
    r.ok = false;
    // Name the first offending bar and beat (spec: refusals carry bar
    // number). Amounts that fit no grid name no musical value, so the
    // message names the location, never the fraction.
    (void)fitsOn(1024, pts, bad);
    Fraction barLen = barQuarters(song.meterN, song.meterD);
    Fraction qb = bad.t / barLen;
    long long barIdx = qb.n / qb.d;
    r.error = "group 1, bar " + std::to_string(barIdx + 1) + ": a note " +
              std::string(bad.onset ? "attack " : "ending ") +
              beatPhrase(bad.t - Fraction(barIdx, 1) * barLen) +
              " fits no writable grid";
    return r;
}

Quantizer::Result Quantizer::prepare(Song& song) {
    return prepare(song, Fraction(1, 4), Fraction(1, 2));
}

Quantizer::Result Quantizer::prepare(Song& song, const Fraction& onsetTol,
                                     const Fraction& endTol) {
    Result exact = pickL(song);
    if (exact.ok) return exact;  // no rounding when exact works
    static const std::vector<int> denoms{2,  4,   8,   16,  32,
                                         64, 128, 256, 512, 1024};
    Fraction barLen = barQuarters(song.meterN, song.meterD);
    for (int d : denoms) {
        Fraction unit = unitQuarters(d);
        // Trial snap of every grid point (onsets + ends + chord onsets).
        struct Move {
            Fraction from, to;
            bool onset;
        };
        std::vector<Move> moves;
        auto collect = [&](const Fraction& p, bool onset) {
            // nearest grid multiple: floor(p/unit + 1/2)
            Fraction q = p / unit + Fraction(1, 2);
            long long k = q.n / q.d;  // q >= 0 for song times
            if (q.n < 0) k = -((-q.n + q.d - 1) / q.d);
            moves.push_back(Move{p, Fraction(k, 1) * unit, onset});
        };
        for (const auto& n : song.vocal) {
            collect(n.onset, true);
            collect(n.onset + n.dur, false);
        }
        for (const auto& n : song.ins) {
            collect(n.onset, true);
            collect(n.onset + n.dur, false);
        }
        for (const auto& c : song.chords) collect(c.onset, true);
        bool fits = true;
        std::string why;
        // Tolerance as a share of the grid spacing (never a new unit).
        auto tolShareName = [](const Fraction& t) {
            if (t == Fraction(1, 8)) return std::string("an eighth of");
            if (t == Fraction(1, 4)) return std::string("a quarter of");
            if (t == Fraction(1, 3)) return std::string("a third of");
            if (t == Fraction(1, 2)) return std::string("half of");
            if (t == Fraction(1, 4)) return std::string("a quarter of");
            return t.str() + " of";
        };
        for (const auto& m : moves) {
            Fraction delta = m.from - m.to;
            if (delta.n < 0) delta.n = -delta.n;
            Fraction tolHere = (m.onset ? onsetTol : endTol) * unit;
            if (delta > tolHere) {
                fits = false;
                Fraction q = m.from / barLen;
                std::string how;
                if ((m.onset ? onsetTol : endTol).isZero()) {
                    how = "but no snapping is allowed";
                } else {
                    how = "past the " +
                          std::string(m.onset ? "attack" : "release") +
                          " tolerance (" +
                          tolShareName(m.onset ? onsetTol : endTol) +
                          " the grid spacing)";
                }
                why = "group 1, bar " + std::to_string(q.n / q.d + 1) +
                      ": " + std::string(m.onset ? "an attack " : "a note ending ") +
                      beatPhrase(m.from - Fraction(q.n / q.d, 1) * barLen) +
                      " would have to move " + how;
                break;
            }
        }
        if (!fits) {
            if (d == 1024) break;  // fall through to closest-slot fallback
            continue;               // try a finer grid
        }
        // Apply the snap: onsets/ends move, durations recomputed.
        // Zero-length casualties of the snap are dropped, loudly.
        size_t mi = 0;
        int snapped = 0;
        Fraction maxSnap(0, 1);
        std::map<int, int> snapsPerBar;
        auto take = [&]() { return moves[mi++]; };
        auto applyVoice = [&](std::vector<SongNote>& notes,
                              const std::string& vname) {
            std::vector<SongNote> keptNotes;
            for (auto& n : notes) {
                Move a = take(), b = take();
                auto dist = [&](const Move& m) {
                    Fraction dd = m.from - m.to;
                    if (dd.n < 0) dd.n = -dd.n;
                    return dd;
                };
                Fraction da = dist(a), db = dist(b);
                if (!da.isZero() || !db.isZero()) {
                    snapped++;
                    if (da > maxSnap) maxSnap = da;
                    if (db > maxSnap) maxSnap = db;
                    Fraction q = n.onset / barLen;
                    snapsPerBar[static_cast<int>(q.n / q.d) + 1]++;
                }
                Fraction dur = b.to - a.to;
                if (dur.isZero() || dur.n < 0) {
                    int dropBar = barNumber(n.onset, barLen);
                    Fraction dropPos =
                        n.onset - Fraction(dropBar - 1, 1) * barLen;
                    song.log.add(1, vname, dropBar,
                                 "note " + beatPhrase(dropPos) + " of bar " +
                                     std::to_string(dropBar) +
                                     " snapped to zero length; dropped",
                                 "warn");
                    continue;
                }
                keptNotes.push_back(SongNote{a.to, dur, n.pitch});
            }
            notes.swap(keptNotes);
        };
        applyVoice(song.vocal, "Vocal");
        applyVoice(song.ins, "Ins");
        for (auto& c : song.chords) c.onset = take().to;
        for (const auto& kv : snapsPerBar)
            song.log.add(1, "Vocal", kv.first,
                          std::to_string(kv.second) + " timing snap(s) onto the grid " +
                              "(attacks within " +
                              tolShareName(onsetTol) +
                              " the grid spacing, ends within " +
                              tolShareName(endTol) + " the grid spacing)",
                         "info");
        sortSong(song);
        Result r;
        r.ok = true;
        r.unitDenom = d;
        r.snapped = snapped > 0;
        r.snapCount = snapped;
        r.maxSnap = maxSnap;
        return r;
    }
    // Fallback: closest-slot math at the finest L: value. Every onset and
    // sounding end moves to its nearest writable position -- the smallest
    // possible move, keeping the original feel. Triplet-structured onsets
    // are straightened the same way (user-authorized) with a loud per-bar
    // flag, never silently.
    {
        const int d = 1024;
        Fraction unit = unitQuarters(d);
        struct Move {
            Fraction from, to;
        };
        std::vector<Move> moves;
        for (const auto& n : song.vocal) {
            moves.push_back(Move{n.onset, nearestSlot(n.onset, unit)});
            moves.push_back(
                Move{n.onset + n.dur, nearestSlot(n.onset + n.dur, unit)});
        }
        for (const auto& n : song.ins) {
            moves.push_back(Move{n.onset, nearestSlot(n.onset, unit)});
            moves.push_back(
                Move{n.onset + n.dur, nearestSlot(n.onset + n.dur, unit)});
        }
        for (const auto& c : song.chords)
            moves.push_back(Move{c.onset, nearestSlot(c.onset, unit)});
        size_t mi = 0;
        int snapped = 0;
        Fraction maxSnap(0, 1);
        std::map<int, int> snapsPerBar;
        std::map<int, int> tripletPerBar;
        auto take = [&]() { return moves[mi++]; };
        auto applyVoice = [&](std::vector<SongNote>& notes,
                              const std::string& vname) {
            std::vector<SongNote> keptNotes;
            for (auto& n : notes) {
                Move a = take(), b = take();
                if (onsetThirdLike(n.onset))
                    tripletPerBar[barNumber(n.onset, barLen)]++;
                auto dist = [&](const Move& m) {
                    Fraction dd = m.from - m.to;
                    if (dd.n < 0) dd.n = -dd.n;
                    return dd;
                };
                Fraction da = dist(a), db = dist(b);
                if (!da.isZero() || !db.isZero()) {
                    snapped++;
                    if (da > maxSnap) maxSnap = da;
                    if (db > maxSnap) maxSnap = db;
                    Fraction q = n.onset / barLen;
                    snapsPerBar[static_cast<int>(q.n / q.d) + 1]++;
                }
                Fraction dur = b.to - a.to;
                if (dur.isZero() || dur.n < 0) {
                    int dropBar = barNumber(n.onset, barLen);
                    Fraction dropPos =
                        n.onset - Fraction(dropBar - 1, 1) * barLen;
                    song.log.add(1, vname, dropBar,
                                 "note " + beatPhrase(dropPos) + " of bar " +
                                     std::to_string(dropBar) +
                                     " snapped to zero length; dropped",
                                 "warn");
                    continue;
                }
                keptNotes.push_back(SongNote{a.to, dur, n.pitch});
            }
            notes.swap(keptNotes);
        };
        applyVoice(song.vocal, "Vocal");
        applyVoice(song.ins, "Ins");
        for (auto& c : song.chords) c.onset = take().to;
        for (const auto& kv : snapsPerBar)
            song.log.add(1, "Vocal", kv.first,
                          std::to_string(kv.second) + " timing snap(s) onto the grid " +
                              "(closest writable position)",
                         "info");
        for (const auto& kv : tripletPerBar)
            song.log.add(1, "Vocal", kv.first,
                          std::to_string(kv.second) +
                              " triplet-structured onset(s) straightened onto the grid " +
                              "(closest writable position)",
                         "warn");
        sortSong(song);
        Result r;
        r.ok = true;
        r.unitDenom = d;
        r.snapped = snapped > 0;
        r.snapCount = snapped;
        r.maxSnap = maxSnap;
        r.tripletApprox = !tripletPerBar.empty();
        r.tripletBars = static_cast<int>(tripletPerBar.size());
        return r;
    }
}

bool Quantizer::barFits(const Song& song, const Fraction& barStart,
                        const Fraction& barLen, int unitDenom,
                        std::string& reason) {
    Fraction unit = unitQuarters(unitDenom);
    auto check = [&](const std::vector<SongNote>& notes) -> bool {
        for (const auto& n : notes) {
            Fraction end = n.onset + n.dur;
            if (end <= barStart || n.onset >= barStart + barLen) continue;
            // Clip to bar (notes must not cross bar lines in this phase;
            // Quantize preview refuses crossings so Export can tie-split).
            if (n.onset < barStart || end > barStart + barLen) {
                int startBar = barNumber(n.onset, barLen);
                Fraction startPos =
                    n.onset - Fraction(startBar - 1, 1) * barLen;
                reason = "note crosses bar boundary (starts " +
                         beatPhrase(startPos) + " of bar " +
                         std::to_string(startBar) + ")";
                return false;
            }
            bool e1 = false, e2 = false;
            (void)(n.onset - barStart).divExact(unit, e1);
            (void)n.dur.divExact(unit, e2);
            if (!e1 || !e2) {
                reason = "an attack or length " +
                         beatPhrase(n.onset - barStart) +
                         " cannot be written exactly";
                return false;
            }
        }
        return true;
    };
    if (!check(song.vocal)) return false;
    if (!check(song.ins)) return false;
    for (const auto& c : song.chords) {
        if (c.onset < barStart || c.onset >= barStart + barLen) continue;
        bool e = false;
        (void)(c.onset - barStart).divExact(unit, e);
        if (!e) {
            reason = "chord " + beatPhrase(c.onset - barStart) + " not on grid";
            return false;
        }
    }
    return true;
}

}  // namespace yue2_abcedit
