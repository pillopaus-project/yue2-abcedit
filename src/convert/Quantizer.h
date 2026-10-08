#pragma once

#include <string>
#include <vector>

#include "core/Fraction.h"
#include "core/SongModel.h"

namespace yue2_abcedit {

// Grid + L: picker (spec section 5). Picks the coarsest L: denominator
// (power of two, <= 1024) that writes every onset and sounding duration as
// an allowed multiplier, splitting disallowed counts with ties.
// Refuses bars that cannot be written exactly — never rounds.
class Quantizer {
public:
    struct Result {
        bool ok = false;
        int unitDenom = 32;  // chosen L: denominator
        bool snapped = false;  // true when tolerance snapping was applied
        int snapCount = 0;     // grid points moved by snapping
        Fraction maxSnap{0, 1};  // largest single snap, in quarter notes
        // Closest-slot fallback straightened triplet-structured onsets
        // (user-authorized, flagged per bar, never silent).
        bool tripletApprox = false;
        int tripletBars = 0;  // bars carrying a triplet-straighten flag
        std::string error;
    };

    // Exact grid pick (no rounding). Fails naming the first off-grid bar.
    static Result pickL(const Song& song);

    // Full preparation: exact pick first; if that refuses, snap to the
    // coarsest grid where every attack moves at most onsetTol of a step
    // and every note end at most endTol. Attacks define rhythm, so the
    // defaults (1/4, 1/2) refuse systematic third-based subdivisions
    // (triplets sit exactly 1/3 step off on every power-of-two grid)
    // while human microtiming snaps. Releases are articulation and snap
    // freely. Tolerances are a per-file musical judgment call -- the UI
    // exposes them with a warning at/above the 1/3 triplet boundary.
    // Every snap is logged per bar: explicit, never silent rounding.
    static Result prepare(Song& song, const Fraction& onsetTol,
                          const Fraction& endTol);
    static Result prepare(Song& song);  // default tolerances

    // Split a raw unit count into allowed pieces (ties). E.g. 10 -> {8,2}.
    // Always succeeds (1 is allowed); prefers fewest/largest pieces.
    static std::vector<int> splitCount(int count);

    // True when every onset/duration in [barStart, barStart+barLen) is
    // expressible on `unitDenom`.
    static bool barFits(const Song& song, const Fraction& barStart,
                        const Fraction& barLen, int unitDenom,
                        std::string& reason);
};

}  // namespace yue2_abcedit
