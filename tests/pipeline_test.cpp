// End-to-end pipeline tests (no Qt).
// Case 1: quarter-note MIDI -> L:1/4 -> export -> checker accepts.
// Case 2: dotted rhythm -> coarser grids fail, L:1/8 wins -> checker accepts.
// Case 3: triplet MIDI -> no exact power-of-two grid -> refusal naming bar 1.
#include <iostream>
#include <string>

#include "convert/Exporter.h"
#include "convert/Quantizer.h"
#include "core/AbcChecker.h"
#include "import/MidiImport.h"
#include "import/StandardAbcReader.h"

namespace {
int failures = 0;
void fail(const std::string& m) {
    std::cout << "FAIL: " << m << "\n";
    failures++;
}

bool importDefault(const std::string& path, yue2_abcedit::Song& song) {
    std::string err;
    yue2_abcedit::MidiImport::Options opt;
    if (!yue2_abcedit::MidiImport::importFile(path, opt, song, err)) {
        fail("import " + path + ": " + err);
        return false;
    }
    return true;
}

void checkExportPasses(yue2_abcedit::Song& song, int denom, size_t wantV,
                       size_t wantI) {
    yue2_abcedit::Exporter::Result er =
        yue2_abcedit::Exporter::exportSong(song, denom);
    if (!er.ok) {
        fail("export refused: " + er.error);
        return;
    }
    std::cout << "--- exported ---\n" << er.text << "---\n";
    auto cr = yue2_abcedit::AbcChecker::check(er.text);
    if (!cr.ok) {
        fail("checker rejects pipeline output: " + cr.error);
        return;
    }
    std::cout << "checker: PASS (" << cr.vocal.bars.size() << " bars)\n";
    if (cr.vocal.notes.size() != wantV || cr.ins.notes.size() != wantI)
        fail("round trip changed note counts");
}
}  // namespace

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/data";
    std::string err;

    // Case 1: probe + default map + quarter notes.
    {
        std::vector<yue2_abcedit::SourceEntry> tracks;
        if (!yue2_abcedit::MidiImport::probeTracks(dir + "/scale.mid", tracks,
                                              err)) {
            fail("probe: " + err);
            return 1;
        }
        std::cout << "probe: " << tracks.size() << " tracks\n";
        for (const auto& t : tracks)
            std::cout << "  " << t.id << " name=" << t.name
                      << " prog=" << t.program << " notes=" << t.noteCount
                      << " drum=" << t.isDrum << " assign=" << t.assign << "\n";
        if (tracks.size() != 2) fail("expected 2 tracks");
        // Dialect defaults: no lyrics -> lead track is instrumental (Ins).
        if (tracks[0].assign != "Ins" || tracks[1].assign != "Vocal")
            fail("default mapping should be Ins/Vocal");

        yue2_abcedit::Song song;
        if (!importDefault(dir + "/scale.mid", song)) return 1;
        std::cout << "import: bpm=" << song.bpm << " meter=" << song.meterN
                  << "/" << song.meterD << " vocal=" << song.vocal.size()
                  << " ins=" << song.ins.size() << "\n";
        if (song.bpm != 120) fail("expected bpm 120");
        if (song.vocal.size() != 4 || song.ins.size() != 4)
            fail("expected 4+4 notes");
        if (!song.ins.empty() && song.ins.front().pitch != 60)
            fail("instrumental lead melody should open Ins");
        auto qr = yue2_abcedit::Quantizer::pickL(song);
        if (!qr.ok) {
            fail("pickL refused: " + qr.error);
            return 1;
        }
        std::cout << "pickL: L:1/" << qr.unitDenom << "\n";
        if (qr.unitDenom != 4)
            fail("expected L:1/4 for quarter-note material");
        checkExportPasses(song, qr.unitDenom, 4, 4);
    }

    // Case 2: dotted rhythm forces a finer grid (L:1/8), still exact.
    {
        yue2_abcedit::Song song;
        if (!importDefault(dir + "/dotted.mid", song)) return 1;
        if (song.vocal.size() != 2 || song.ins.size() != 3)
            fail("dotted: expected 2 vocal + 3 ins notes");
        auto qr = yue2_abcedit::Quantizer::pickL(song);
        if (!qr.ok) {
            fail("dotted pickL refused: " + qr.error);
        } else {
            std::cout << "dotted pickL: L:1/" << qr.unitDenom << "\n";
            if (qr.unitDenom != 8)
                fail("expected L:1/8 for dotted-quarter material");
            checkExportPasses(song, qr.unitDenom, 2, 3);
        }
    }

    // Case 3: triplets are unrepresentable -> refusal naming the bar.
    {
        yue2_abcedit::Song song;
        if (!importDefault(dir + "/triplet.mid", song)) return 1;
        auto qr = yue2_abcedit::Quantizer::pickL(song);
        if (qr.ok) {
            fail("triplet pickL should refuse, got L:1/" +
                 std::to_string(qr.unitDenom));
        } else {
            std::cout << "triplet refusal: " << qr.error << "\n";
            if (qr.error.find("bar 1") == std::string::npos)
                fail("refusal must name bar 1");
        }
        // Triplets are not exactly writable, so the closest-slot fallback
        // straightens them with a loud per-bar flag (user-authorized);
        // exact pick still refuses naming the bar (see above).
        {
            yue2_abcedit::Song song2;
            if (!importDefault(dir + "/triplet.mid", song2)) return 1;
            auto pr = yue2_abcedit::Quantizer::prepare(song2);
            if (!pr.ok) {
                fail("triplet prepare refused: " + pr.error);
            } else {
                std::cout << "triplet fallback: L:1/" << pr.unitDenom
                          << " tripletApprox=" << pr.tripletApprox << "\n";
                if (!pr.tripletApprox)
                    fail("triplet fallback must flag straightened feel");
                yue2_abcedit::Exporter::Result er =
                    yue2_abcedit::Exporter::exportSong(song2, pr.unitDenom);
                if (!er.ok) {
                    fail("triplet export refused: " + er.error);
                } else {
                    auto cr = yue2_abcedit::AbcChecker::check(er.text);
                    if (!cr.ok)
                        fail("triplet checker rejects: " + cr.error);
                }
            }
        }
    }

    // Case 4: standard ABC -> rebuild -> export -> checker accepts.
    // Exercises repeat unroll, endings, grace/slur/decoration drops,
    // lyric drop, part label, C-led music lines, simultaneous voices.
    {
        std::vector<yue2_abcedit::SourceEntry> voices;
        if (!yue2_abcedit::StandardAbcReader::probeVoices(dir + "/rebuild.abc",
                                                     voices, err)) {
            fail("abc probe: " + err);
        } else {
            std::cout << "abc probe: " << voices.size() << " voices\n";
            if (voices.size() != 2) fail("expected 2 ABC voices");
        }
        yue2_abcedit::Song song;
        yue2_abcedit::StandardAbcReader::Options opt;
        if (!yue2_abcedit::StandardAbcReader::importFile(dir + "/rebuild.abc", opt,
                                                    song, err)) {
            fail("abc import: " + err);
        } else {
            std::cout << "abc import: vocal=" << song.vocal.size()
                      << " ins=" << song.ins.size()
                      << " sections=" << song.sections.size() << "\n";
            std::cout << "--- rebuild log ---\n"
                      << song.log.renderText() << "---\n";
            if (song.vocal.size() != 24)
                fail("expected 24 vocal notes, got " +
                     std::to_string(song.vocal.size()));
            if (song.ins.size() != 12)
                fail("expected 12 ins notes, got " +
                     std::to_string(song.ins.size()));
            if (song.sections.empty() || song.sections.front() != "A")
                fail("expected section A");
            // Voices must overlap in time (simultaneous), not sequence.
            if (!song.vocal.empty() && !song.ins.empty() &&
                !(song.ins.front().onset < song.vocal.back().onset))
                fail("voices do not overlap; clocks not simultaneous");
            auto qr = yue2_abcedit::Quantizer::pickL(song);
            if (!qr.ok) {
                fail("abc pickL refused: " + qr.error);
            } else {
                std::cout << "abc pickL: L:1/" << qr.unitDenom << "\n";
                if (qr.unitDenom != 8) fail("expected L:1/8");
                checkExportPasses(song, qr.unitDenom, 24, 12);
            }
        }
    }

    // Case 5: lyric-bearing track defaults to the sung voice (Vocal).
    {
        std::vector<yue2_abcedit::SourceEntry> tracks;
        std::string err;
        if (!yue2_abcedit::MidiImport::probeTracks(dir + "/scale-lyrics.mid",
                                              tracks, err)) {
            fail("lyric probe: " + err);
        } else {
            if (tracks.size() != 1) fail("expected 1 lyric track");
            if (!tracks.empty() && !tracks[0].hasLyrics)
                fail("lyric events not detected");
            if (!tracks.empty() && tracks[0].assign != "Vocal")
                fail("lyric track should default to Vocal");
            yue2_abcedit::Song song;
            if (!importDefault(dir + "/scale-lyrics.mid", song)) return 1;
            if (song.vocal.size() != 4 || !song.ins.empty())
                fail("lyric file: expected 4 vocal notes, empty Ins");
        }
    }

    // Case 6: real piano file. D major from key-signature meta, melody in
    // Ins, harmony as Vocal chord symbols, checker accepts the export.
    {
        yue2_abcedit::Song song;
        if (!importDefault("example_code/test_smile.mid", song)) return 1;
        std::cout << "smile: key=" << song.key
                  << " vocal=" << song.vocal.size()
                  << " ins=" << song.ins.size()
                  << " chords=" << song.chords.size() << "\n";
        if (song.key != "D") fail("smile: expected K:D from file meta");
        if (!song.vocal.empty()) fail("smile: Vocal should hold chords only");
        if (song.ins.empty()) fail("smile: Ins should carry the melody");
        if (song.chords.size() < 10)
            fail("smile: expected a working harmony line");
        auto pr = yue2_abcedit::Quantizer::prepare(song);
        if (!pr.ok) {
            fail("smile prepare refused: " + pr.error);
        } else {
            std::cout << "smile grid: L:1/" << pr.unitDenom
                      << (pr.snapped ? " (snapped)" : " (exact)") << "\n";
            yue2_abcedit::Exporter::Result er =
                yue2_abcedit::Exporter::exportSong(song, pr.unitDenom);
            if (!er.ok) {
                fail("smile export refused: " + er.error);
            } else {
                auto cr = yue2_abcedit::AbcChecker::check(er.text);
                if (!cr.ok)
                    fail("smile checker rejects: " + cr.error);
                else
                    std::cout << "smile checker: PASS ("
                              << cr.ins.bars.size() << " bars, "
                              << cr.ins.notes.size() << " ins notes)\n";
            }
        }
    }

    // Case 7: real human-timed file. Exact pick refuses; tolerance
    // snapping converts it with every move logged; checker accepts.
    // Refusals speak bars and beats -- never fractional quarter notes.
    {
        yue2_abcedit::Song song;
        if (!importDefault("Rhodes-008.mid", song)) return 1;
        std::cout << "rhodes: vocal=" << song.vocal.size()
                  << " ins=" << song.ins.size()
                  << " chords=" << song.chords.size() << "\n";
        {
            auto exact = yue2_abcedit::Quantizer::pickL(song);
            if (!exact.ok) {
                std::cout << "rhodes: exact pick refuses as expected\n";
                if (exact.error.find("bar ") == std::string::npos)
                    fail("rhodes: refusal must name the bar");
                if (exact.error.find("beat ") == std::string::npos)
                    fail("rhodes: refusal must name the beat");
                if (exact.error.find("of a quarter note") !=
                    std::string::npos)
                    fail("rhodes: refusal must not speak fractional "
                         "quarters");
            }
        }
        auto pr = yue2_abcedit::Quantizer::prepare(song);
        if (!pr.ok) {
            fail("rhodes prepare refused: " + pr.error);
        } else {
            std::cout << "rhodes grid: L:1/" << pr.unitDenom
                      << " snaps=" << pr.snapCount
                      << " max=" << pr.maxSnap.str() << "\n";
            if (!pr.snapped || pr.snapCount == 0)
                fail("rhodes: expected real snapping work");
            yue2_abcedit::Exporter::Result er =
                yue2_abcedit::Exporter::exportSong(song, pr.unitDenom);
            if (!er.ok) {
                fail("rhodes export refused: " + er.error);
            } else {
                auto cr = yue2_abcedit::AbcChecker::check(er.text);
                if (!cr.ok)
                    fail("rhodes checker rejects: " + cr.error);
                else
                    std::cout << "rhodes checker: PASS ("
                              << cr.ins.bars.size() << " bars)\n";
            }
        }
    }

    // Case 8: resolution override, standard values only. The same
    // scale.mid bytes read against 960 ticks per quarter note become
    // eighth notes, and the grid follows them there. The override is
    // logged. (Default header trust is covered by every other case.)
    {
        yue2_abcedit::Song song;
        yue2_abcedit::MidiImport::Options opt;
        opt.divisionOverride = 960;
        std::string err;
        if (!yue2_abcedit::MidiImport::importFile(dir + "/scale.mid", opt, song,
                                             err)) {
            fail("override import: " + err);
        } else {
            if (song.ins.size() != 4)
                fail("override: expected 4 ins notes");
            auto pr = yue2_abcedit::Quantizer::prepare(song);
            if (!pr.ok) {
                fail("override prepare refused: " + pr.error);
            } else {
                std::cout << "override grid: L:1/" << pr.unitDenom << "\n";
                if (pr.unitDenom != 8)
                    fail("override: expected eighth-note grid");
                checkExportPasses(song, pr.unitDenom, 4, 4);
            }
            if (song.log.renderText().find("user override") ==
                std::string::npos)
                fail("override: expected override log line");
        }
    }

    // Case 9: standard 960 PPQ file with sixteenth-note content picks
    // the sixteenth-note grid by header trust alone, no override.
    {
        yue2_abcedit::Song song;
        if (!importDefault(dir + "/scale-16.mid", song)) return 1;
        if (song.vocal.size() != 2 || song.ins.size() != 4)
            fail("scale-16: expected 2 vocal + 4 ins notes");
        auto pr = yue2_abcedit::Quantizer::prepare(song);
        if (!pr.ok) {
            fail("scale-16 prepare refused: " + pr.error);
        } else {
            std::cout << "scale-16 grid: L:1/" << pr.unitDenom
                      << (pr.snapped ? " (snapped)" : " (exact)") << "\n";
            if (pr.unitDenom != 16 || pr.snapped)
                fail("scale-16: expected exact sixteenth-note grid");
            checkExportPasses(song, pr.unitDenom, 2, 4);
        }
    }

    // Case 10: unquantized single-track file (savedseq.mid, 240 PPQ).
    // Exact pick refuses, but closest-slot fallback converts with every
    // move logged; triplets still refuse (see Case 3); checker accepts.
    {
        yue2_abcedit::Song song;
        if (!importDefault("savedseq.mid", song)) return 1;
        if (yue2_abcedit::Quantizer::pickL(song).ok)
            fail("savedseq: exact pick should refuse unquantized material");
        auto pr = yue2_abcedit::Quantizer::prepare(song);
        if (!pr.ok) {
            fail("savedseq prepare refused: " + pr.error);
        } else {
            std::cout << "savedseq grid: L:1/" << pr.unitDenom
                      << " snaps=" << pr.snapCount << "\n";
            if (!pr.snapped || pr.snapCount == 0)
                fail("savedseq: expected closest-slot snapping work");
            yue2_abcedit::Exporter::Result er =
                yue2_abcedit::Exporter::exportSong(song, pr.unitDenom);
            if (!er.ok) {
                fail("savedseq export refused: " + er.error);
            } else {
                auto cr = yue2_abcedit::AbcChecker::check(er.text);
                if (!cr.ok)
                    fail("savedseq checker rejects: " + cr.error);
                else
                    std::cout << "savedseq checker: PASS ("
                              << cr.ins.bars.size() << " bars, "
                              << cr.ins.notes.size() << " ins notes)\n";
            }
        }
    }

    // Case 11: dense performance file with a drifted first note plus
    // scattered triplet-structured entries (test_not.mid). Nothing may
    // refuse for the first note; triplet bars convert flagged.
    {
        yue2_abcedit::Song song;
        if (!importDefault("test_not.mid", song)) return 1;
        std::cout << "test_not: vocal=" << song.vocal.size()
                  << " ins=" << song.ins.size()
                  << " chords=" << song.chords.size() << "\n";
        auto pr = yue2_abcedit::Quantizer::prepare(song);
        if (!pr.ok) {
            fail("test_not prepare refused: " + pr.error);
        } else {
            std::cout << "test_not grid: L:1/" << pr.unitDenom
                      << " snaps=" << pr.snapCount
                      << " tripletApprox=" << pr.tripletApprox << "\n";
            if (!pr.tripletApprox)
                fail("test_not: expected flagged triplet bars");
            yue2_abcedit::Exporter::Result er =
                yue2_abcedit::Exporter::exportSong(song, pr.unitDenom);
            if (!er.ok) {
                fail("test_not export refused: " + er.error);
            } else {
                auto cr = yue2_abcedit::AbcChecker::check(er.text);
                if (!cr.ok)
                    fail("test_not checker rejects: " + cr.error);
                else
                    std::cout << "test_not checker: PASS ("
                              << cr.ins.bars.size() << " bars, "
                              << cr.ins.notes.size() << " ins notes)\n";
            }
        }
    }

    // Case 12: ABC probe shows real content in file order and native
    // voice names keep their assignment (no silent voice swap, no
    // dropped chord symbols).
    {
        std::vector<yue2_abcedit::SourceEntry> voices;
        std::string err;
        if (!yue2_abcedit::StandardAbcReader::probeVoices("smile.abc", voices,
                                                    err)) {
            fail("smile probe: " + err);
        } else {
            if (voices.size() != 1) fail("smile: expected 1 voice");
            if (!voices.empty() && voices[0].noteCount <= 0)
                fail("smile: probe must count notes");
            if (!voices.empty() && voices[0].assign != "Ins")
                fail("smile: single lead voice should default to Ins");
        }
        if (!yue2_abcedit::StandardAbcReader::probeVoices("conv_1.abc", voices,
                                                     err)) {
            fail("conv_1 probe: " + err);
        } else {
            if (voices.size() != 2) fail("conv_1: expected 2 voices");
            if (voices.size() == 2 &&
                (voices[0].id != "Vocal" || voices[1].id != "Ins"))
                fail("conv_1: probe must keep file order");
            if (voices.size() == 2 &&
                (voices[0].assign != "Vocal" || voices[1].assign != "Ins"))
                fail("conv_1: native names must keep their assignment");
            if (voices.size() == 2 && voices[1].noteCount <= 0)
                fail("conv_1: Ins probe must count notes");
        }
        yue2_abcedit::Song song;
        yue2_abcedit::StandardAbcReader::Options opt;
        for (auto& s : voices) opt.voiceAssign.push_back(s.assign);
        if (!yue2_abcedit::StandardAbcReader::importFile("conv_1.abc", opt, song,
                                                   err)) {
            fail("conv_1 import: " + err);
        } else {
            if (song.ins.empty()) fail("conv_1: Ins melody missing");
            if (song.chords.empty())
                fail("conv_1: Vocal chord symbols dropped");
            if (!song.vocal.empty())
                fail("conv_1: Vocal must hold chords only");
            auto pr = yue2_abcedit::Quantizer::prepare(song);
            if (!pr.ok) fail("conv_1 prepare refused: " + pr.error);
        }
    }

    // Case 13: re-import of converted output (conv_1_nochords.abc) with
    // the empty Vocal voice ignored. Ties must merge (fewer sounding
    // notes than tokens, no extra attacks), refuse-noise must be absent,
    // and the round trip must pass the checker.
    {
        std::vector<yue2_abcedit::SourceEntry> voices;
        std::string err;
        if (!yue2_abcedit::StandardAbcReader::probeVoices("conv_1_nochords.abc",
                                                     voices, err)) {
            fail("nochords probe: " + err);
        } else {
            if (voices.size() != 2) fail("nochords: expected 2 voices");
            if (voices.size() == 2 && voices[1].noteCount <= 0)
                fail("nochords: Ins probe must count notes");
        }
        yue2_abcedit::Song song;
        yue2_abcedit::StandardAbcReader::Options opt;
        opt.voiceAssign.push_back("Ignore");
        opt.voiceAssign.push_back("Ins");
        if (!yue2_abcedit::StandardAbcReader::importFile("conv_1_nochords.abc",
                                                    opt, song, err)) {
            fail("nochords import: " + err);
        } else {
            if (!song.vocal.empty()) fail("nochords: Vocal must be ignored");
            if (song.ins.empty()) fail("nochords: Ins melody missing");
            // 24 tokens hold 3 ties (G6-G1, E1-E8, A1-A3): 21 soundings.
            if (song.ins.size() != 21)
                fail("nochords: ties must merge to 21 notes, got " +
                     std::to_string(song.ins.size()));
            if (song.log.renderText().find("[refuse]") != std::string::npos)
                fail("nochords: log must carry no refusals");
            auto pr = yue2_abcedit::Quantizer::prepare(song);
            if (!pr.ok) {
                fail("nochords prepare refused: " + pr.error);
            } else {
                // NB: exportSong splits notes at bar edges in place, so
                // capture the count before exporting.
                size_t wantIns = song.ins.size();
                yue2_abcedit::Exporter::Result er =
                    yue2_abcedit::Exporter::exportSong(song, pr.unitDenom);
                if (!er.ok) {
                    fail("nochords export refused: " + er.error);
                } else {
                    auto cr = yue2_abcedit::AbcChecker::check(er.text);
                    if (!cr.ok)
                        fail("nochords checker rejects: " + cr.error);
                    else if (cr.ins.notes.size() != wantIns)
                        fail("nochords round trip changed note counts");
                }
            }
        }
    }

    if (failures) {
        std::cout << failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "ALL PIPELINE TESTS PASSED\n";
    return 0;
}
