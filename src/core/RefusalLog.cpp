#include "core/RefusalLog.h"

#include <sstream>

namespace yue2_abcedit {

void RefusalLog::add(int group, const std::string& voice, int bar,
                     const std::string& reason, const std::string& action) {
    entries_.push_back(LogEntry{group, voice, bar, reason, action});
}

bool RefusalLog::hasRefusals() const {
    for (const auto& e : entries_)
        if (e.action == "refuse") return true;
    return false;
}

std::string RefusalLog::renderText() const {
    std::ostringstream os;
    for (const auto& e : entries_) {
        os << "group " << e.group << ", " << e.voice << ", bar " << e.bar
           << " [" << e.action << "]: " << e.reason << "\n";
    }
    return os.str();
}

void RefusalLog::clear() { entries_.clear(); }

}  // namespace yue2_abcedit
