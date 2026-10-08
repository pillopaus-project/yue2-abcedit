// C++ checker parity test: sample1.abc must pass; negative cases must
// fail with the same reasons as abc_tools.py.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "core/AbcChecker.h"

namespace {
std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

int failures = 0;
void expectPass(const std::string& name, const std::string& text) {
    auto r = yue2_abcedit::AbcChecker::check(text);
    if (!r.ok) {
        std::cout << "FAIL " << name << ": unexpectedly rejected: " << r.error
                  << "\n";
        failures++;
    } else {
        std::cout << "ok " << name << " (" << r.vocal.bars.size() << " bars)\n";
    }
}
void expectFail(const std::string& name, const std::string& text,
                const std::string& wantSubstr) {
    auto r = yue2_abcedit::AbcChecker::check(text);
    if (r.ok) {
        std::cout << "FAIL " << name << ": unexpectedly accepted\n";
        failures++;
    } else if (r.error.find(wantSubstr) == std::string::npos) {
        std::cout << "FAIL " << name << ": wrong reason: " << r.error
                  << " (wanted '" << wantSubstr << "')\n";
        failures++;
    } else if (r.error.find("of a quarter note") != std::string::npos) {
        std::cout << "FAIL " << name
                  << ": banned fractional-quarter wording: " << r.error
                  << "\n";
        failures++;
    } else {
        std::cout << "ok " << name << " -> " << r.error << "\n";
    }
}
}  // namespace

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "example_code";
    std::string sample = readFile(dir + "/sample1.abc");
    if (sample.empty()) {
        std::cout << "FAIL: cannot read sample1.abc\n";
        return 1;
    }
    expectPass("sample1.abc", sample);

    const std::string kHead =
        "X:1\nT:\nM:4/4\nL:1/32\nQ:1/4=88\n"
        "V: Vocal clef=treble name=\"Vocal Melody\" snm=\"Vocal\"\n"
        "V: Ins clef=treble name=\"Ins Melody\" snm=\"Inst.\"\nK:G\n";
    const std::string kGoodV = "\"Gmaj7\"B8d8c8A8|F16G16|\n";
    const std::string kGoodI = "Z2|\n";
    auto doc = [&](const std::string& v, const std::string& i) {
        return kHead + "% verse\nV: Vocal\n" + v + "V: Ins\n" + i;
    };
    expectPass("spec example", doc(kGoodV, kGoodI));
    expectFail("bad tie pitch", doc("\"G\"B8-^B8c8A8|F16G16|\n", kGoodI),
               "tie changes pitch");
    expectFail("tie into rest", doc("\"G\"B8-z8c8A8|F16G16|\n", kGoodI),
               "tie enters a rest");
    expectFail("chord in Ins", doc(kGoodV, "\"G\"B8d8c8A8|F16G16|\n"),
               "belong in Vocal");
    expectFail("measure mismatch", doc(kGoodV, "Z|\n"), "different measure");
    expectFail("bad duration", doc("C10C22|F16G16|\n", kGoodI),
               "as one token");
    expectFail("bad duration names beat", doc("C10C22|F16G16|\n", kGoodI),
               "on beat 1");
    expectFail("short bar", doc("B8d8c8|F16G16|\n", kGoodI),
               "falls short of the meter by a quarter note");
    expectFail("long bar", doc("B8d8c8A8B8|F16G16|\n", kGoodI),
               "event after the measure end");
    expectFail("Z over harmony", doc("\"G\"Z|\n", "Z|\n"),
               "unsupported token");  // Z cannot cover a chord change

    if (failures) {
        std::cout << failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "ALL CHECKER TESTS PASSED\n";
    return 0;
}
