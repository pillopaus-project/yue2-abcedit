// Preview render tests (no Qt): checked scores render valid WAV files,
// invalid content renders nothing.
#include <fstream>
#include <iostream>
#include <string>

#include "audio/PreviewSound.h"
#include "core/AbcChecker.h"

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
std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}
bool validWav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char head[12] = {0};
    f.read(head, 12);
    if (!f) return false;
    if (std::string(head, 4) != "RIFF") return false;
    if (std::string(head + 8, 4) != "WAVE") return false;
    f.seekg(0, std::ios::end);
    return f.tellg() > 44;
}
}  // namespace

int main() {
    // Melody-only score (sample1: Vocal rests, Ins carries the tune).
    {
        auto cr = yue2_abcedit::AbcChecker::check(readFile("example_code/sample1.abc"));
        check(cr.ok, "sample1 passes the checker");
        auto pr = yue2_abcedit::PreviewSound::render(
            cr, "/tmp/opencode/preview-sample1.wav");
        check(pr.ok, "sample1 renders");
        check(pr.seconds > 0 && pr.samples > 0, "sample1 render has length");
        check(validWav("/tmp/opencode/preview-sample1.wav"),
              "sample1 render is a valid WAV");
    }
    // Score with chord symbols (conv_1: Vocal chords over rests + melody).
    {
        auto cr = yue2_abcedit::AbcChecker::check(readFile("conv_1.abc"));
        check(cr.ok, "conv_1 passes the checker");
        check(!cr.vocal.chords.empty(), "conv_1 carries chord symbols");
        auto pr = yue2_abcedit::PreviewSound::render(
            cr, "/tmp/opencode/preview-chords.wav");
        check(pr.ok, "chord score renders");
        check(validWav("/tmp/opencode/preview-chords.wav"),
              "chord render is a valid WAV");
    }
    // Muted chords: melody only, still a valid WAV.
    {
        auto cr = yue2_abcedit::AbcChecker::check(readFile("conv_1.abc"));
        auto pr = yue2_abcedit::PreviewSound::render(
            cr, "/tmp/opencode/preview-muted.wav", false);
        check(pr.ok, "muted chord score renders");
        check(validWav("/tmp/opencode/preview-muted.wav"),
              "muted render is a valid WAV");
    }
    // Invalid content renders nothing (checker message passes through).
    {
        yue2_abcedit::CheckerResult bad;
        bad.ok = false;
        bad.error = "group 1, Vocal, bar 1: something wrong";
        auto pr = yue2_abcedit::PreviewSound::render(
            bad, "/tmp/opencode/preview-bad.wav");
        check(!pr.ok, "invalid score refuses to render");
        check(pr.error == bad.error, "checker message passes through");
    }

    if (failures) {
        std::cout << failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "ALL PREVIEW TESTS PASSED\n";
    return 0;
}
