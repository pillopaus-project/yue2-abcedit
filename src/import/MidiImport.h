#pragma once

#include <string>
#include <vector>

#include "core/SongModel.h"

namespace yue2_abcedit {

// Converts a Standard MIDI File into the internal Song model.
// Exact rational timing: ticks -> quarters via TPQ division, no floats.
class MidiImport {
public:
    struct Options {
        // Assignment per track index: "Vocal" | "Ins" | "Ignore".
        // Empty = dialect-aware defaults (lyrics -> Vocal, else lead -> Ins).
        std::vector<std::string> trackAssign;
        // Ticks-per-quarter override. 0 = trust the file header. Set this
        // when the file carries a wrong division (rewritten by tools that
        // rescale ticks without fixing the header, or authored against a
        // player-side resolution): every timestamp is then read against the
        // override instead. Always logged when used.
        int divisionOverride = 0;
    };

    // Reads the declared MThd division without importing.
    static bool readDivision(const std::string& path, int& division,
                             std::string& error);

    // Fills `tracks` with one row per MIDI track for the mapper UI.
    static bool probeTracks(const std::string& path,
                            std::vector<SourceEntry>& tracks,
                            std::string& error);

    // Full import honoring options.trackAssign.
    static bool importFile(const std::string& path, const Options& opt,
                           Song& song, std::string& error);
};

}  // namespace yue2_abcedit
