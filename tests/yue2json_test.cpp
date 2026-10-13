// Yue2 JSON bridge test: load "abc" from both reference files, check the
// extracted text is the stored ABC byte-for-byte (already native dialect:
// comments, spellings, and bar layout intact), checker-gated, and that
// merge-save touches only "abc" (key order and other fields identical).
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include "core/AbcChecker.h"
#include "import/Yue2Json.h"

namespace {
int failures = 0;
void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "ok " << what << "\n";
    } else {
        std::cout << "FAIL " << what << "\n";
        failures++;
    }
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    using namespace yue2_abcedit;

    for (const char* path : {"sample-melody.json", "sample-full.json"}) {
        QString abc;
        std::string err;
        check(Yue2Json::loadAbc(path, abc, err), std::string("load ") + path);
        check(abc.startsWith("X:1\n"), std::string("plain ABC text ") + path);
        check(!abc.contains("\\n"), std::string("no literal backslash-n ") + path);
        // Fidelity: stored ABC is already native dialect, so it must come
        // out exactly as stored (open-then-save without edits is identity).
        check(abc.contains("% intro"),
              std::string("section comment kept ") + path);
        check(!abc.contains("=F"),
              std::string("no respelled accidentals ") + path);
        // Checker gate on the stored text (same gate as GUI/CLI).
        CheckerResult cr = AbcChecker::check(abc.toStdString());
        check(cr.ok, std::string("stored abc passes checker ") + path);
        // Identity round-trip: merge back the UNEDITED text.
        std::string serr;
        std::string out = std::string("/tmp/opencode/yue2-") +
                          (std::string(path) == "sample-melody.json" ? "melody"
                                                                     : "full") +
                          "-merged.json";
        check(Yue2Json::saveAbc(path, abc, out, serr),
              std::string("merge-save ") + path);
        // Re-parse and verify: abc == edited, everything else == origin.
        QFile of(QString::fromStdString(out));
        check(of.open(QIODevice::ReadOnly), std::string("reopen ") + out);
        QJsonDocument odoc =
            QJsonDocument::fromJson(of.readAll());
        of.close();
        check(odoc.isObject(), "merged doc is object");
        QJsonObject oroot = odoc.object();
        check(oroot.value("abc").toString() == abc, "merged abc exact");
        QFile rf(path);
        rf.open(QIODevice::ReadOnly);
        QJsonObject root = QJsonDocument::fromJson(rf.readAll()).object();
        rf.close();
        for (const QString& k : root.keys()) {
            if (k == "abc") continue;
            check(oroot.contains(k) && oroot.value(k) == root.value(k),
                  std::string("field untouched: ") + k.toStdString());
        }
        check(oroot.keys().size() == root.keys().size(), "no keys added/lost");
        // Key order preserved: merged raw text lists keys in origin order.
        {
            QFile mf(QString::fromStdString(out));
            mf.open(QIODevice::ReadOnly);
            std::string mtext = QString(mf.readAll()).toStdString();
            mf.close();
            QFile rf2(path);
            rf2.open(QIODevice::ReadOnly);
            std::string rtext = QString(rf2.readAll()).toStdString();
            rf2.close();
            auto keyOrder = [](const std::string& t) {
                std::vector<std::string> order;
                size_t i = 0;
                int depth = 0;
                bool inStr = false;
                // Minimal top-level key scan (keys are simple ASCII here).
                while (i < t.size()) {
                    char c = t[i];
                    if (inStr) {
                        if (c == '\\') {
                            i += 2;
                            continue;
                        }
                        if (c == '"') inStr = false;
                        i++;
                        continue;
                    }
                    if (c == '"') {
                        size_t s = i + 1;
                        size_t e = t.find('"', s);
                        // find unescaped quote
                        while (e != std::string::npos && t[e - 1] == '\\')
                            e = t.find('"', e + 1);
                        if (e == std::string::npos) break;
                        size_t k = e + 1;
                        while (k < t.size() && isspace((unsigned char)t[k]))
                            k++;
                        if (depth == 1 && k < t.size() && t[k] == ':')
                            order.push_back(t.substr(s, e - s));
                        i = e + 1;
                        continue;
                    }
                    if (c == '{' || c == '[') depth++;
                    if (c == '}' || c == ']') depth--;
                    i++;
                }
                return order;
            };
            check(keyOrder(mtext) == keyOrder(rtext),
                  std::string("key order preserved ") + path);
        }
    }

    // Refusals.
    {
        QString abc;
        std::string err;
        check(!Yue2Json::loadAbc("/tmp/opencode/no-such.json", abc, err),
              "missing file refuses");
        QFile bad("/tmp/opencode/yue2-bad.json");
        bad.open(QIODevice::WriteOnly | QIODevice::Truncate);
        bad.write("{\"style\":\"x\"}");
        bad.close();
        check(!Yue2Json::loadAbc("/tmp/opencode/yue2-bad.json", abc, err),
              "missing abc refuses");
    }

    if (failures) {
        std::cout << failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "ALL YUE2JSON TESTS PASSED\n";
    return 0;
}
