#pragma once

#include <string>
#include <vector>

namespace yue2_abcedit {

struct FoldOutcome {
    bool kept = true;      // false -> keep melody only, no symbol
    bool flagged = false;  // fold/drop was logged
    std::string symbol;    // folded symbol (empty when !kept)
};

class ChordFold {
public:
    // Infer a chord symbol from simultaneous MIDI pitches (pitch classes).
    // Returns "" when no sonority can be inferred (melody only).
    static std::string infer(const std::vector<int>& pitches);

    // Downgrade any quality to the checker-closed list. Keeps root and bass,
    // never invents a new root. Returns kept=false when the bass function
    // would change (caller keeps melody only + flags).
    static FoldOutcome fold(const std::string& symbol);
};

}  // namespace yue2_abcedit
