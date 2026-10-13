#include "import/Yue2Json.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <functional>
#include <vector>

namespace yue2_abcedit {

bool Yue2Json::loadAbc(const std::string& path, QString& abcOut,
                       std::string& error) {
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = "cannot open file";
        return false;
    }
    QByteArray raw = f.readAll();
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError) {
        error = std::string("invalid JSON: ") + perr.errorString().toStdString();
        return false;
    }
    if (!doc.isObject()) {
        error = "invalid Yue2 JSON: top level is not an object";
        return false;
    }
    QJsonObject root = doc.object();
    if (!root.contains("abc") || !root.value("abc").isString()) {
        error = "invalid Yue2 JSON: missing string field \"abc\"";
        return false;
    }
    QString abc = root.value("abc").toString();
    // Normalize CRLF so the editor pipeline always sees plain \n text.
    abc.replace("\r\n", "\n");
    abc.replace('\r', '\n');
    if (abc.trimmed().isEmpty()) {
        error = "invalid Yue2 JSON: empty \"abc\" field";
        return false;
    }
    abcOut = abc;
    return true;
}

bool Yue2Json::saveAbc(const std::string& originPath, const QString& newAbc,
                       const std::string& outPath, std::string& error) {
    QFile f(QString::fromStdString(originPath));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = "cannot open origin JSON";
        return false;
    }
    QByteArray raw = f.readAll();
    f.close();
    QJsonParseError perr;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &perr);
    if (perr.error != QJsonParseError::NoError) {
        error = std::string("invalid JSON: ") + perr.errorString().toStdString();
        return false;
    }
    if (!doc.isObject()) {
        error = "invalid Yue2 JSON: top level is not an object";
        return false;
    }
    if (!doc.object().contains("abc") ||
        !doc.object().value("abc").isString()) {
        error = "invalid Yue2 JSON: missing string field \"abc\"";
        return false;
    }
    // Order-preserving splice: QJsonObject re-serializes keys alphabetically,
    // which reorders the file. Instead, locate every top-level "abc" string
    // value in the raw text and replace it with the escaped new text, leaving
    // every other byte (key order, whitespace, other fields) untouched.
    std::string text = raw.toStdString();
    struct Span {
        size_t begin = 0, end = 0;
    };
    std::vector<Span> hits;
    {
        size_t n = text.size();
        auto skipWs = [&](size_t i) {
            while (i < n && (text[i] == ' ' || text[i] == '\t' ||
                             text[i] == '\n' || text[i] == '\r'))
                i++;
            return i;
        };
        // Parse one JSON string starting at the opening quote; returns the
        // index just past the closing quote, or npos on unterminated input.
        // Handles \" and \\ escapes (any \x is skipped pairwise, which is
        // all a span scan needs).
        auto stringEnd = [&](size_t open) {
            size_t i = open + 1;
            while (i < n) {
                if (text[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (text[i] == '"') return i + 1;
                i++;
            }
            return std::string::npos;
        };
        // Decode a raw (still-escaped) JSON string body to compare keys.
        auto decode = [&](size_t open, size_t close) {
            // close = index just past the closing quote.
            std::string out;
            for (size_t i = open + 1; i + 1 < close;) {
                if (text[i] == '\\' && i + 1 < close - 1) {
                    char e = text[i + 1];
                    if (e == 'n')
                        out += '\n';
                    else if (e == 't')
                        out += '\t';
                    else if (e == 'r')
                        out += '\r';
                    else
                        out += e;  // \" \\ \/ and \uXXXX: byte-compare suffices
                    i += 2;
                } else {
                    out += text[i++];
                }
            }
            return out;
        };
        size_t i = skipWs(0);
        if (i >= n || text[i] != '{') {
            error = "invalid Yue2 JSON: top level is not an object";
            return false;
        }
        int depth = 0;
        bool expectKey = false;  // at depth 1, next string may be a member key
        i++;  // past '{'
        depth = 1;
        expectKey = true;
        // Forward declaration via std::function for the value skipper.
        std::function<size_t(size_t)> skipValue = [&](size_t k) -> size_t {
            k = skipWs(k);
            if (k >= n) return std::string::npos;
            char c = text[k];
            if (c == '"') return stringEnd(k);
            if (c == '{' || c == '[') {
                char open = c, shut = (c == '{' ? '}' : ']');
                size_t j = k + 1;
                int d = 1;
                while (j < n && d > 0) {
                    if (text[j] == '"') {
                        size_t e = stringEnd(j);
                        if (e == std::string::npos) return std::string::npos;
                        j = e;
                    } else {
                        if (text[j] == open) d++;
                        if (text[j] == shut) d--;
                        j++;
                    }
                }
                return (d == 0) ? j : std::string::npos;
            }
            // number / true / false / null: run to the next delimiter.
            size_t j = k;
            while (j < n && text[j] != ',' && text[j] != '}' &&
                   text[j] != ']' && text[j] != ' ' && text[j] != '\t' &&
                   text[j] != '\n' && text[j] != '\r')
                j++;
            return j;
        };
        while (i < n) {
            i = skipWs(i);
            if (i >= n) break;
            char c = text[i];
            if (c == '}' && depth == 1) break;
            if (c == ',') {
                i++;
                expectKey = (depth == 1);
                continue;
            }
            if (c == '"') {
                size_t e = stringEnd(i);
                if (e == std::string::npos) break;
                if (depth == 1 && expectKey) {
                    size_t k = skipWs(e);
                    if (k < n && text[k] == ':') {
                        if (decode(i, e) == "abc") {
                            size_t v = skipValue(k + 1);
                            if (v == std::string::npos ||
                                skipWs(k + 1) >= n ||
                                text[skipWs(k + 1)] != '"') {
                                error = "invalid Yue2 JSON: \"abc\" is not a "
                                        "string";
                                return false;
                            }
                            size_t vs = skipWs(k + 1);
                            hits.push_back({vs, v});
                        }
                        i = skipValue(k + 1);
                        expectKey = false;
                        continue;
                    }
                }
                i = e;
                expectKey = false;
                continue;
            }
            // Any other token at depth 1 outside a value slot: skip it.
            if (c == '{' || c == '[') {
                size_t e = skipValue(i);
                if (e == std::string::npos) break;
                i = e;
                expectKey = false;
                continue;
            }
            i++;
        }
    }
    if (hits.empty()) {
        error = "invalid Yue2 JSON: missing string field \"abc\"";
        return false;
    }
    // Escape the new text as a JSON string (same rules as QJsonDocument:
    // quotes, backslashes, control chars; UTF-8 passes through).
    std::string esc = "\"";
    for (QChar qc : newAbc) {
        ushort u = qc.unicode();
        if (u == '"')
            esc += "\\\"";
        else if (u == '\\')
            esc += "\\\\";
        else if (u == '\n')
            esc += "\\n";
        else if (u == '\r')
            esc += "\\r";
        else if (u == '\t')
            esc += "\\t";
        else if (u == '\b')
            esc += "\\b";
        else if (u == '\f')
            esc += "\\f";
        else if (u < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", u);
            esc += buf;
        } else {
            // Append the UTF-8 bytes of exactly this QChar.
            esc += QString(qc).toUtf8().toStdString();
        }
    }
    esc += "\"";
    // Replace from the back so earlier spans stay valid. Every top-level
    // "abc" is replaced, so first-wins and last-wins parsers agree.
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        text.replace(it->begin, it->end - it->begin, esc);
    QFile w(QString::fromStdString(outPath));
    if (!w.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        error = "cannot write output file";
        return false;
    }
    w.write(QByteArray::fromStdString(text));
    w.close();
    return true;
}

}  // namespace yue2_abcedit
