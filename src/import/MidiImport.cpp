#include "import/MidiImport.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>

#include "convert/ChordFold.h"
#include "core/AbcDialect.h"
#include "import/MidiFile.h"

namespace yue2_abcedit {
namespace {

struct ActiveNote {
    uint32_t onTick = 0;
    int velocity = 0;
};

struct FlatNote {
    int track = 0;
    int channel = 0;
    int pitch = 0;
    uint32_t onTick = 0;
    uint32_t offTick = 0;
    bool drum = false;
};

struct TrackLayout {
    bool drum = false;
    int notes = 0;
    bool lyrics = false;
};

Fraction ticksToQuarters(uint32_t ticks, int tpq) {
    return Fraction(static_cast<long long>(ticks), tpq);
}

// Dialect-aware defaults: a lyric-bearing track is the sung line (Vocal);
// otherwise the first melodic track is the instrumental lead (Ins) and the
// second melodic track goes to Vocal. Drums are always ignored.
void applyDefaults(const std::vector<TrackLayout>& layout,
                   std::vector<std::string>& assign) {
    assign.assign(layout.size(), "Ignore");
    bool vocalTaken = false, insTaken = false;
    for (size_t i = 0; i < layout.size(); ++i) {
        if (!layout[i].drum && layout[i].notes > 0 && layout[i].lyrics &&
            !vocalTaken) {
            assign[i] = "Vocal";
            vocalTaken = true;
        }
    }
    for (size_t i = 0; i < layout.size(); ++i) {
        if (!layout[i].drum && layout[i].notes > 0 &&
            assign[i] == "Ignore" && !insTaken) {
            assign[i] = "Ins";
            insTaken = true;
            break;
        }
    }
    for (size_t i = 0; i < layout.size(); ++i) {
        if (!layout[i].drum && layout[i].notes > 0 &&
            assign[i] == "Ignore" && !vocalTaken) {
            assign[i] = "Vocal";
            vocalTaken = true;
            break;
        }
    }
}

std::string keyNameFromMeta(int8_t sf, uint8_t minor) {
    static const char* major[] = {"Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F",
                                  "C",  "G",  "D",  "A",  "E",  "B",  "F#",
                                  "C#"};
    static const char* mnr[] = {"Abm", "Ebm", "Bbm", "Fm", "Cm", "Gm", "Dm",
                                "Am",  "Em",  "Bm",  "F#m", "C#m", "G#m",
                                "D#m", "A#m"};
    if (sf < -7 || sf > 7) return "";
    return minor ? mnr[sf + 7] : major[sf + 7];
}

}  // namespace

bool MidiImport::probeTracks(const std::string& path,
                             std::vector<SourceEntry>& tracks,
                             std::string& error) {
    try {
        midifile::MidiFileData f = midifile::parseFile(path);
        tracks.clear();
        std::vector<TrackLayout> layout;
        for (size_t i = 0; i < f.tracks.size(); ++i) {
            const auto& t = f.tracks[i];
            TrackLayout lo;
            int prog = 0;
            bool progSeen = false;
            for (const auto& e : t.events) {
                if (!e.isMeta && (e.status == 0x90) && e.data2 > 0) {
                    lo.notes++;
                    if (e.channel == 9) lo.drum = true;
                }
                if (!e.isMeta && e.status == 0xC0 && !progSeen) {
                    prog = e.data1;
                    progSeen = true;
                }
                if (e.isMeta && e.metaType == 0x05) lo.lyrics = true;
            }
            layout.push_back(lo);
            SourceEntry se;
            se.id = "track" + std::to_string(i);
            se.name = t.name.empty() ? ("Track " + std::to_string(i)) : t.name;
            se.program = prog;
            se.noteCount = lo.notes;
            se.isDrum = lo.drum;
            se.hasLyrics = lo.lyrics;
            tracks.push_back(se);
        }
        std::vector<std::string> assign;
        applyDefaults(layout, assign);
        for (size_t i = 0; i < tracks.size(); ++i) tracks[i].assign = assign[i];
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

bool MidiImport::readDivision(const std::string& path, int& division,
                              std::string& error) {
    try {
        midifile::MidiFileData f = midifile::parseFile(path);
        division = f.division;
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

bool MidiImport::importFile(const std::string& path, const Options& opt,
                            Song& song, std::string& error) {
    try {
        midifile::MidiFileData f = midifile::parseFile(path);
        if (!f.isTpq() && opt.divisionOverride <= 0) {
            error = "SMPTE division not supported";
            return false;
        }
        int tpq = (opt.divisionOverride > 0) ? opt.divisionOverride
                                             : f.division;
        song = Song();
        song.key = "C";
        if (opt.divisionOverride > 0 && opt.divisionOverride != f.division)
            song.log.add(0, "Vocal", 0,
                         "interpreted at " +
                             std::to_string(opt.divisionOverride) +
                             " ticks per quarter note by user override; file "
                             "header declares " +
                             std::to_string(f.division),
                         "warn");

        // Header: first tempo + time signature + key signature win.
        bool tempoSeen = false, meterSeen = false, keySeen = false;
        for (const auto& t : f.tracks) {
            for (const auto& e : t.events) {
                if (e.isMeta && e.metaType == 0x51 && !tempoSeen &&
                    e.metaData.size() == 3) {
                    int mpq = (e.metaData[0] << 16) | (e.metaData[1] << 8) |
                              e.metaData[2];
                    if (mpq > 0) song.bpm = 60000000 / mpq;
                    tempoSeen = true;
                }
                if (e.isMeta && e.metaType == 0x58 && !meterSeen &&
                    e.metaData.size() >= 2) {
                    song.meterN = e.metaData[0];
                    int pw = e.metaData[1];
                    song.meterD = 1 << pw;
                    meterSeen = true;
                }
                if (e.isMeta && e.metaType == 0x59 && !keySeen &&
                    e.metaData.size() >= 2) {
                    std::string k = keyNameFromMeta(
                        static_cast<int8_t>(e.metaData[0]), e.metaData[1]);
                    if (!k.empty() && isStandardKey(k)) {
                        song.key = k;
                        std::string musical =
                            (k.back() == 'm')
                                ? (k.substr(0, k.size() - 1) + " minor")
                                : (k + " major");
                        song.log.add(0, "Vocal", 0,
                                     "key signature: " + musical, "info");
                    }
                    keySeen = true;
                }
            }
        }
        // Warn on mid-song header changes (phase 1: single header value).
        {
            bool warned = false;
            for (const auto& t : f.tracks) {
                for (const auto& e : t.events) {
                    if (e.isMeta &&
                        (e.metaType == 0x51 || e.metaType == 0x58 ||
                         e.metaType == 0x59) &&
                        e.tick != 0 && !warned) {
                        song.log.add(0, "Vocal", 0,
                                     "mid-song tempo/meter/key change found; "
                                     "kept first header value",
                                     "warn");
                        warned = true;
                    }
                }
            }
        }

        // Sustain pedal handling: collect flat notes with extended off times.
        std::vector<FlatNote> flat;
        for (size_t ti = 0; ti < f.tracks.size(); ++ti) {
            const auto& t = f.tracks[ti];
            std::map<std::pair<int, int>, ActiveNote> active;  // (ch,pitch)
            std::map<int, bool> sustain;                       // channel -> down
            std::map<std::pair<int, int>, uint32_t> pedalHold;
            for (const auto& e : t.events) {
                if (e.isMeta) continue;
                if (e.status == 0x90 || e.status == 0x80) {
                    bool on = (e.status == 0x90 && e.data2 > 0);
                    auto key = std::make_pair((int)e.channel, (int)e.data1);
                    if (on) {
                        auto it = active.find(key);
                        if (it != active.end()) {
                            flat.push_back(FlatNote{
                                (int)ti, (int)e.channel, (int)e.data1,
                                it->second.onTick, e.tick, e.channel == 9});
                        }
                        active[key] = ActiveNote{e.tick, e.data2};
                        pedalHold.erase(key);
                    } else {
                        auto it = active.find(key);
                        if (it == active.end()) continue;
                        if (sustain[e.channel]) {
                            pedalHold[key] = e.tick;
                        } else {
                            flat.push_back(FlatNote{
                                (int)ti, (int)e.channel, (int)e.data1,
                                it->second.onTick, e.tick, e.channel == 9});
                            active.erase(it);
                        }
                    }
                } else if (e.status == 0xB0 && e.data1 == 64) {
                    bool down = e.data2 >= 64;
                    sustain[e.channel] = down;
                    if (!down) {
                        std::vector<std::pair<int, int>> done;
                        for (const auto& kv : pedalHold) {
                            if (kv.first.first == (int)e.channel) {
                                auto ait = active.find(kv.first);
                                if (ait != active.end()) {
                                    flat.push_back(
                                        FlatNote{(int)ti, (int)e.channel,
                                                 kv.first.second,
                                                 ait->second.onTick, e.tick,
                                                 e.channel == 9});
                                    active.erase(ait);
                                }
                                done.push_back(kv.first);
                            }
                        }
                        for (const auto& k : done) pedalHold.erase(k);
                    }
                }
            }
            uint32_t lastTick = t.events.empty() ? 0 : t.events.back().tick;
            for (const auto& kv : active) {
                flat.push_back(FlatNote{(int)ti, kv.first.first,
                                         kv.first.second, kv.second.onTick,
                                         std::max(kv.second.onTick, lastTick),
                                         kv.first.first == 9});
            }
        }

        // Assignment: explicit mapper choice, else dialect-aware defaults.
        std::vector<std::string> assign(f.tracks.size(), "Ignore");
        if (!opt.trackAssign.empty()) {
            for (size_t i = 0; i < assign.size() && i < opt.trackAssign.size();
                 ++i)
                assign[i] = opt.trackAssign[i];
        } else {
            std::vector<TrackLayout> layout(f.tracks.size());
            for (const auto& n : flat) {
                if (n.drum) layout[n.track].drum = true;
                if (!n.drum) layout[n.track].notes++;
            }
            for (size_t i = 0; i < f.tracks.size(); ++i)
                for (const auto& e : f.tracks[i].events)
                    if (e.isMeta && e.metaType == 0x05) layout[i].lyrics = true;
            applyDefaults(layout, assign);
        }
        {
            int ignored = 0;
            for (size_t i = 0; i < assign.size(); ++i) {
                int nc = 0;
                for (const auto& n : flat)
                    if (n.track == (int)i && !n.drum) nc++;
                if (assign[i] == "Ignore" && nc > 0) ignored++;
            }
            if (ignored > 0)
                song.log.add(0, "Vocal", 0,
                             "ignored " + std::to_string(ignored) +
                                 " extra track(s); remap in Mapper to keep",
                             "warn");
        }

        Fraction barLen =
            barQuarters(song.meterN, song.meterD);
        auto barOf = [&](const Fraction& t) -> int {
            Fraction q = t / barLen;
            return static_cast<int>(q.n / q.d) + 1;
        };

        // Group flat notes per assigned voice; drop drums and zero lengths.
        std::map<Fraction, std::vector<const FlatNote*>> byOnsetV,
            byOnsetI;
        std::vector<FlatNote> kept;
        for (const auto& n : flat) {
            if (n.drum) continue;
            if (n.offTick <= n.onTick) continue;
            if (assign[n.track] == "Vocal" || assign[n.track] == "Ins")
                kept.push_back(n);
        }
        for (const auto& n : kept) {
            Fraction on = ticksToQuarters(n.onTick, tpq);
            if (assign[n.track] == "Vocal")
                byOnsetV[on].push_back(&n);
            else
                byOnsetI[on].push_back(&n);
        }

        // Melody + chord split per voice. At each onset the lead (highest
        // starting) pitch joins the staff -- unless a higher pitch is already
        // sounding, in which case the starters are harmony, not melody.
        // Chord symbols are inferred from every pitch sounding at the onset
        // (sustained + starting) and always land in Vocal, the dialect's
        // harmony voice. Nothing is dropped silently: exclusions, folds and
        // melody-only sonorities are all logged per bar.
        std::string lastChord;
        std::map<int, int> melodyOnlyPerBar;
        auto buildVoice = [&](std::map<Fraction, std::vector<const FlatNote*>>&
                                  groups,
                              std::vector<SongNote>& out,
                              const std::string& vname) {
            struct Active {
                Fraction end;
                int pitch;
            };
            std::vector<Active> sounding;
            int clips = 0, harmonyOnly = 0;
            for (auto& kv : groups) {
                const Fraction& on = kv.first;
                auto& vec = kv.second;
                sounding.erase(
                    std::remove_if(sounding.begin(), sounding.end(),
                                   [&](const Active& a) { return a.end <= on; }),
                    sounding.end());
                int topSounding = -1;
                for (const auto& a : sounding)
                    topSounding = std::max(topSounding, a.pitch);
                int lead = -1;
                Fraction leadEnd = on;
                for (const auto* o : vec) {
                    Fraction oEnd = ticksToQuarters(o->offTick, tpq);
                    if (o->pitch > lead) {
                        lead = o->pitch;
                        leadEnd = oEnd;
                    } else if (o->pitch == lead && oEnd > leadEnd) {
                        leadEnd = oEnd;
                    }
                }
                // Chord pcs: every pitch sounding at this onset.
                std::set<int> pcs;
                for (const auto& a : sounding) pcs.insert((a.pitch % 12 + 12) % 12);
                for (const auto* o : vec) pcs.insert((o->pitch % 12 + 12) % 12);
                if (topSounding >= 0 && lead < topSounding) {
                    harmonyOnly++;
                    song.log.add(1, vname, barOf(on),
                                 "accompaniment under sustained pitch " +
                                     std::to_string(topSounding) +
                                     "; kept for harmony only",
                                 "info");
                } else {
                    out.push_back(SongNote{on, leadEnd - on, lead});
                }
                sounding.push_back(Active{leadEnd, lead});
                for (const auto* o : vec) {
                    Fraction oEnd = ticksToQuarters(o->offTick, tpq);
                    if (o->pitch != lead) sounding.push_back(Active{oEnd, o->pitch});
                }
                if (pcs.size() >= 2) {
                    std::vector<int> v(pcs.begin(), pcs.end());
                    // Recover octave picture for inference via raw pitches.
                    std::vector<int> raw;
                    for (const auto& a : sounding) raw.push_back(a.pitch);
                    std::string sym = ChordFold::infer(raw);
                    if (sym.empty()) {
                        melodyOnlyPerBar[barOf(on)]++;
                    } else {
                        FoldOutcome fo = ChordFold::fold(sym);
                        if (!fo.kept) {
                            melodyOnlyPerBar[barOf(on)]++;
                            song.log.add(1, vname, barOf(on),
                                         "chord '" + sym +
                                             "' would change bass function; "
                                             "kept melody only",
                                         "fold");
                        } else {
                            if (fo.symbol != lastChord) {
                                song.chords.push_back(ChordEvent{on, fo.symbol});
                                lastChord = fo.symbol;
                            }
                            if (fo.flagged)
                                song.log.add(1, vname, barOf(on),
                                             "chord " + sym + " folded to '" +
                                                 fo.symbol + "'",
                                             "fold");
                        }
                    }
                }
            }
            // Legato overlaps of differing pitches cannot stay in a
            // monophonic staff: clip to the next onset, loudly.
            for (size_t i = 0; i + 1 < out.size(); ++i) {
                Fraction end = out[i].onset + out[i].dur;
                if (end > out[i + 1].onset) {
                    out[i].dur = out[i + 1].onset - out[i].onset;
                    clips++;
                    if (clips == 1)
                        song.log.add(1, vname, barOf(out[i].onset),
                                     "legato overlap clipped to next onset",
                                     "warn");
                }
            }
            if (clips > 1)
                song.log.add(1, vname, 0,
                             "clipped " + std::to_string(clips) +
                                 " legato overlaps in total",
                             "warn");
            if (harmonyOnly > 0)
                song.log.add(1, vname, 0,
                             std::to_string(harmonyOnly) +
                                 " accompaniment onset(s) kept for harmony "
                                 "only",
                             "info");
        };
        buildVoice(byOnsetV, song.vocal, "Vocal");
        buildVoice(byOnsetI, song.ins, "Ins");
        for (const auto& kv : melodyOnlyPerBar)
            song.log.add(1, "Vocal", kv.first,
                         std::to_string(kv.second) +
                             " sonority(ies) kept as melody only (no chord "
                             "inferred)",
                         "warn");
        sortSong(song);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

}  // namespace yue2_abcedit
