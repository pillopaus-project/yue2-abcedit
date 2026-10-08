#pragma once
// Vendored single-header Standard MIDI File parser (no dependencies).
// Supports SMF type 0/1, TPQ division, note on/off, program change,
// tempo + time-signature meta events, sustain pedal (CC64).

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace yue2_abcedit::midifile {

struct MidiEvent {
    uint32_t tick = 0;
    uint8_t status = 0;    // voice status high nibble, or 0xFF meta / 0xF0 sysex
    uint8_t channel = 0;   // 0-15 for voice messages
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    bool isMeta = false;
    uint8_t metaType = 0;
    std::vector<uint8_t> metaData;
    int trackIndex = 0;
};

struct MidiTrack {
    std::string name;
    std::vector<MidiEvent> events;
};

struct MidiFileData {
    uint16_t format = 1;
    uint16_t division = 480;  // ticks per quarter when top bit clear
    std::vector<MidiTrack> tracks;
    bool isTpq() const { return (division & 0x8000) == 0; }
};

inline uint32_t readVlq(const std::vector<uint8_t>& bytes, size_t& pos) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        if (pos >= bytes.size()) throw std::runtime_error("truncated VLQ");
        uint8_t b = bytes[pos++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return v;
    }
    throw std::runtime_error("VLQ overflow");
}

inline uint32_t readU32(const std::vector<uint8_t>& bytes, size_t& pos) {
    if (pos + 4 > bytes.size()) throw std::runtime_error("truncated u32");
    uint32_t v = (static_cast<uint32_t>(bytes[pos]) << 24) |
                 (static_cast<uint32_t>(bytes[pos + 1]) << 16) |
                 (static_cast<uint32_t>(bytes[pos + 2]) << 8) |
                 bytes[pos + 3];
    pos += 4;
    return v;
}

inline uint16_t readU16(const std::vector<uint8_t>& bytes, size_t& pos) {
    if (pos + 2 > bytes.size()) throw std::runtime_error("truncated u16");
    uint16_t v = static_cast<uint16_t>((bytes[pos] << 8) | bytes[pos + 1]);
    pos += 2;
    return v;
}

inline MidiFileData parseBytes(const std::vector<uint8_t>& bytes) {
    MidiFileData file;
    size_t pos = 0;
    if (bytes.size() < 14) throw std::runtime_error("file too short for MThd");
    if (bytes[0] != 'M' || bytes[1] != 'T' || bytes[2] != 'h' ||
        bytes[3] != 'd')
        throw std::runtime_error("missing MThd magic");
    pos = 4;
    uint32_t hlen = readU32(bytes, pos);
    file.format = readU16(bytes, pos);
    uint16_t ntracks = readU16(bytes, pos);
    file.division = readU16(bytes, pos);
    if (hlen > 6) pos += (hlen - 6);
    if (!file.isTpq())
        throw std::runtime_error("SMPTE division not supported; need TPQ");

    for (int ti = 0; ti < ntracks; ++ti) {
        if (pos + 8 > bytes.size())
            throw std::runtime_error("truncated MTrk header");
        if (bytes[pos] != 'M' || bytes[pos + 1] != 'T' ||
            bytes[pos + 2] != 'r' || bytes[pos + 3] != 'k')
            throw std::runtime_error("missing MTrk magic");
        pos += 4;
        uint32_t tlen = readU32(bytes, pos);
        size_t end = pos + tlen;
        if (end > bytes.size()) throw std::runtime_error("truncated track");
        MidiTrack track;
        uint32_t tick = 0;
        uint8_t running = 0;
        while (pos < end) {
            uint32_t delta = readVlq(bytes, pos);
            tick += delta;
            if (pos >= end) break;
            uint8_t b = bytes[pos];
            uint8_t status;
            if (b & 0x80) {
                status = b;
                pos++;
                if (status != 0xFF && status != 0xF0 && status != 0xF7)
                    running = status;
            } else {
                if (running == 0)
                    throw std::runtime_error("running status without prefix");
                status = running;
            }
            MidiEvent ev;
            ev.tick = tick;
            ev.trackIndex = ti;
            if (status == 0xFF) {
                uint8_t type = bytes[pos++];
                uint32_t mlen = readVlq(bytes, pos);
                if (pos + mlen > end)
                    throw std::runtime_error("truncated meta event");
                ev.isMeta = true;
                ev.status = 0xFF;
                ev.metaType = type;
                ev.metaData.assign(bytes.begin() + pos,
                                   bytes.begin() + pos + mlen);
                pos += mlen;
                if (type == 0x03) {
                    track.name.assign(ev.metaData.begin(), ev.metaData.end());
                }
                track.events.push_back(ev);
            } else if (status == 0xF0 || status == 0xF7) {
                uint32_t slen = readVlq(bytes, pos);
                pos += slen;  // skip sysex
            } else {
                uint8_t hi = status & 0xF0;
                ev.status = hi;
                ev.channel = status & 0x0F;
                int need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
                if (pos + static_cast<size_t>(need) > end)
                    throw std::runtime_error("truncated voice event");
                ev.data1 = bytes[pos++];
                if (need == 2) ev.data2 = bytes[pos++];
                track.events.push_back(ev);
            }
        }
        pos = end;
        file.tracks.push_back(track);
    }
    return file;
}

inline MidiFileData parseFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("cannot open MIDI file: " + path);
    std::vector<uint8_t> bytes;
    int c;
    while ((c = fgetc(f)) != EOF) bytes.push_back(static_cast<uint8_t>(c));
    fclose(f);
    return parseBytes(bytes);
}

}  // namespace yue2_abcedit::midifile
