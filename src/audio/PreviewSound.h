#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/AbcChecker.h"

namespace yue2_abcedit {

// Renders an already-checked score to a 16-bit mono WAV preview.
// Input is a CheckerResult (the live verdict), never raw text: invalid
// content has no sound. Melody notes sound exactly; chord symbols sound
// their pitch classes in a fixed declared voicing (block, struck at each
// symbol, held till the next symbol or score end); rests are silence.
class PreviewSound {
public:
    static constexpr int kRate = 44100;

    struct Result {
        bool ok = false;
        std::string error;  // checker message or render refusal
        double seconds = 0;
        int samples = 0;
    };

    // Writes a fresh WAV to wavPath (overwrites). No audio libraries.
    // soundChords=false renders melody only; chord symbols stay silent
    // (they remain in the file untouched).
    static Result render(const CheckerResult& checked,
                         const std::string& wavPath,
                         bool soundChords = true);
};

}  // namespace yue2_abcedit
