#include "core/SongModel.h"

#include <algorithm>

namespace yue2_abcedit {

Fraction songEnd(const Song& song) {
    Fraction end(0, 1);
    for (const auto& n : song.vocal) end = std::max(end, n.onset + n.dur);
    for (const auto& n : song.ins) end = std::max(end, n.onset + n.dur);
    return end;
}

void sortSong(Song& song) {
    auto byOnset = [](const SongNote& a, const SongNote& b) {
        if (a.onset != b.onset) return a.onset < b.onset;
        return a.pitch < b.pitch;
    };
    std::sort(song.vocal.begin(), song.vocal.end(), byOnset);
    std::sort(song.ins.begin(), song.ins.end(), byOnset);
    std::sort(song.chords.begin(), song.chords.end(),
              [](const ChordEvent& a, const ChordEvent& b) {
                  return a.onset < b.onset;
              });
}

}  // namespace yue2_abcedit
