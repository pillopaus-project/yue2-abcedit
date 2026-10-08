// yue2-abcconvert: headless MIDI / standard-ABC to native-ABC conversion.
// Same pipeline as the wizard (probe with defaults, import, quantize,
// export). No editor, no audio.
//
// Usage:
//   yue2-abcconvert [--snap-on] [--force] <input> <output.abc> <log.txt>
//
//   --snap-on  allow closest-slot timing snaps (logged per bar, triplet
//              feels flagged); default is exact-only (off-grid refuses).
//   --force    overwrite existing output/log files; otherwise existing
//              files refuse (export must never overwrite silently).
//
// Exit codes: 0 converted, 1 conversion refused (reason on stderr),
// 2 usage or I/O error.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "convert/Exporter.h"
#include "convert/Quantizer.h"
#include "core/AbcChecker.h"
#include "core/AbcDialect.h"
#include "import/MidiImport.h"
#include "import/StandardAbcReader.h"

namespace {

bool exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

bool isMidiPath(const std::string& path) {
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) c = static_cast<char>(std::tolower(c));
    return ext == "mid" || ext == "midi";
}

int usage() {
    std::cerr << "usage: yue2-abcconvert [--snap-on] [--force] "
                 "<input> <output.abc> <log.txt>\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    bool snapOn = false;
    bool force = false;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--snap-on")
            snapOn = true;
        else if (a == "--force")
            force = true;
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "unknown flag: " << a << "\n";
            return usage();
        } else {
            positional.push_back(a);
        }
    }
    if (positional.size() != 3) return usage();
    const std::string& inPath = positional[0];
    const std::string& outPath = positional[1];
    const std::string& logPath = positional[2];

    if (!force && exists(outPath)) {
        std::cerr << "refused: output exists (use --force to overwrite): "
                  << outPath << "\n";
        return 1;
    }
    if (!force && exists(logPath)) {
        std::cerr << "refused: log exists (use --force to overwrite): "
                  << logPath << "\n";
        return 1;
    }

    bool midi = isMidiPath(inPath);
    yue2_abcedit::Song song;
    std::string err;
    if (midi) {
        std::vector<yue2_abcedit::SourceEntry> tracks;
        if (!yue2_abcedit::MidiImport::probeTracks(inPath, tracks, err)) {
            std::cerr << "probe failed: " << err << "\n";
            return 2;
        }
        yue2_abcedit::MidiImport::Options opt;  // dialect defaults, header trust
        if (!yue2_abcedit::MidiImport::importFile(inPath, opt, song, err)) {
            std::cerr << "import failed: " << err << "\n";
            return 2;
        }
    } else {
        std::vector<yue2_abcedit::SourceEntry> voices;
        if (!yue2_abcedit::StandardAbcReader::probeVoices(inPath, voices, err)) {
            std::cerr << "probe failed: " << err << "\n";
            return 2;
        }
        yue2_abcedit::StandardAbcReader::Options opt;  // probe defaults
        if (!yue2_abcedit::StandardAbcReader::importFile(inPath, opt, song, err)) {
            std::cerr << "import failed: " << err << "\n";
            return 2;
        }
    }

    yue2_abcedit::Quantizer::Result qr =
        snapOn ? yue2_abcedit::Quantizer::prepare(song)
               : yue2_abcedit::Quantizer::pickL(song);

    std::ostringstream log;
    log << "input=" << inPath << (midi ? " (midi)" : " (abc)") << "\n";
    log << "snap=" << (snapOn ? "on" : "off") << "\n";
    log << "bpm=" << song.bpm << " meter=" << song.meterN << "/"
        << song.meterD << " key=" << song.key << " vocal=" << song.vocal.size()
        << " notes ins=" << song.ins.size() << " notes\n";
    if (!qr.ok) {
        log << "quantize=REFUSED " << qr.error << "\n";
        log << song.log.renderText();
        std::ofstream lf(logPath, std::ios::trunc);
        if (!lf) {
            std::cerr << "cannot write log: " << logPath << "\n";
            return 2;
        }
        lf << log.str();
        std::cerr << "conversion refused: " << qr.error << "\n";
        return 1;
    }
    log << "grid=" << (qr.snapped ? "snapped" : "exact")
        << " snaps=" << qr.snapCount
        << " tripletApprox=" << (qr.tripletApprox ? "yes" : "no") << "\n";

    yue2_abcedit::Exporter::Result er =
        yue2_abcedit::Exporter::exportSong(song, qr.unitDenom);
    log << song.log.renderText();
    if (!er.ok) {
        log << "export=REFUSED " << er.error << "\n";
        std::ofstream lf(logPath, std::ios::trunc);
        if (!lf) {
            std::cerr << "cannot write log: " << logPath << "\n";
            return 2;
        }
        lf << log.str();
        std::cerr << "conversion refused: " << er.error << "\n";
        return 1;
    }
    {
        std::ofstream out(outPath, std::ios::trunc);
        if (!out) {
            std::cerr << "cannot write output: " << outPath << "\n";
            return 2;
        }
        out << er.text;
    }
    // Final gate: the written file must pass the checker.
    auto cr = yue2_abcedit::AbcChecker::check(er.text);
    log << "checker=" << (cr.ok ? "PASS" : "FAIL");
    if (!cr.ok) log << " " << cr.error;
    log << "\n";
    {
        std::ofstream lf(logPath, std::ios::trunc);
        if (!lf) {
            std::cerr << "cannot write log: " << logPath << "\n";
            return 2;
        }
        lf << log.str();
    }
    if (!cr.ok) {
        std::cerr << "conversion refused: output fails the checker: "
                  << cr.error << "\n";
        return 1;
    }
    std::cout << "wrote " << outPath << " (" << cr.vocal.bars.size()
              << " bars) + " << logPath << "\n";
    return 0;
}
