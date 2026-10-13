#pragma once

#include <QString>
#include <string>

namespace yue2_abcedit {

// Minimal Yue2 JSON bridge: a Yue2 JSON file is any JSON object with a
// guaranteed string field "abc" (native-dialect ABC text). Every other
// top-level field is variable per file and must remain untouched.
// Qt's QJsonDocument handles the text formatting both ways: "\n" unescapes
// to real newlines on load, real newlines re-escape on save.
class Yue2Json {
public:
    // Extract the "abc" text from a Yue2 JSON file. Returns false + error
    // when the file cannot be read/parsed or "abc" is missing/not a string.
    static bool loadAbc(const std::string& path, QString& abcOut,
                        std::string& error);
    // Merge edited ABC back: re-parse the origin file, replace only "abc",
    // write to outPath. All other fields untouched by construction.
    static bool saveAbc(const std::string& originPath, const QString& newAbc,
                        const std::string& outPath, std::string& error);
};

}  // namespace yue2_abcedit
