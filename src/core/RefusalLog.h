#pragma once

#include <string>
#include <vector>

namespace yue2_abcedit {

struct LogEntry {
    int group = 0;
    std::string voice;
    int bar = 0;
    std::string reason;
    std::string action;  // "refuse", "fold", "drop", "warn", "info"
};

class RefusalLog {
public:
    void add(int group, const std::string& voice, int bar,
             const std::string& reason, const std::string& action);
    bool hasRefusals() const;
    const std::vector<LogEntry>& entries() const { return entries_; }
    std::string renderText() const;
    void clear();

private:
    std::vector<LogEntry> entries_;
};

}  // namespace yue2_abcedit
