#pragma once

#include <string>
#include <vector>

#include "core/Fraction.h"

namespace yue2_abcedit {

// C++ port of example_code/abc_tools.py validation logic.

struct CheckerNote {
    Fraction onset;
    int pitch = 0;
    Fraction dur;
};

struct CheckerBar {
    Fraction start;
    Fraction length;
    int meterN = 4;
    int meterD = 4;
};

struct CheckerVoice {
    int meterN = 4;
    int meterD = 4;
    std::string key;
    Fraction time{0, 1};
    std::vector<CheckerNote> notes;
    std::vector<CheckerBar> bars;
    std::vector<std::pair<Fraction, std::string>> chords;
    std::vector<std::pair<Fraction, std::string>> keys;
    bool hasPendingTie = false;
};

struct CheckerResult {
    bool ok = false;
    std::string error;  // first failure, checker-style message
    int bpm = 0;
    int unitDenom = 32;
    Fraction totalTime{0, 1};
    CheckerVoice vocal;
    CheckerVoice ins;
};

class AbcChecker {
public:
    // Returns ok=true when the text satisfies every native-dialect rule.
    // On failure, ok=false and error names group/voice/bar + reason.
    static CheckerResult check(const std::string& text);
};

}  // namespace yue2_abcedit
