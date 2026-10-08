#pragma once

#include <string>

#include "core/SongModel.h"

namespace yue2_abcedit {

// Writes the bounded native dialect (header + grouped two-voice body).
// Refuses bars that cannot be written exactly instead of guessing.
class Exporter {
public:
    struct Result {
        bool ok = false;
        std::string text;
        std::string error;  // first refusal (group/voice/bar + reason)
    };

    static Result exportSong(Song& song, int unitDenom);
};

}  // namespace yue2_abcedit
