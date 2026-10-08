#pragma once

#include <string>
#include <vector>

#include "core/SongModel.h"

namespace yue2_abcedit {

// Hand-written focused reader for standard ABC input. Covers only what the
// rebuild needs: header, voices, parts, repeats/endings, tuplets, grace,
// slurs, broken rhythm, decorations, lyric fields. Everything outside the
// native dialect is rebuilt deliberately or refused per bar.

class StandardAbcReader {
public:
    struct Options {
        std::vector<std::string> voiceAssign;  // per probed voice
    };

    static bool probeVoices(const std::string& path,
                            std::vector<SourceEntry>& voices,
                            std::string& error);
    static bool importFile(const std::string& path, const Options& opt,
                           Song& song, std::string& error);
};

}  // namespace yue2_abcedit
