// zp_movie: author, inspect, verify and replay input movies (.zpm). docs/movies.md.
//
//   zp_movie info <movie.zpm>
//   zp_movie dis  <movie.zpm> [out.txt]                       binary -> text
//   zp_movie asm  <script.txt> <out.zpm>                      text -> binary (hashes the files the script names)
//   zp_movie play <movie.zpm> [--roms PATH]... [--wav out.wav] [--ppm out.ppm] [--ignore-core-revision]
//   zp_movie seal <in.zpm> <out.zpm> [--roms PATH]... [--interval CYCLES]   replay it and embed state checkpoints
//
// `play` replays the movie, checks every checkpoint, and exits 1 naming the movie cycle and the chip that first
// differs if it desyncs. --roms are files or directories searched (by content hash) for the ROMs, boot ROM and APU
// BIOS the movie pins; the movie's own directory is always searched.
#include "movie.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage:\n  zp_movie info <movie.zpm>\n  zp_movie dis <movie.zpm> [out.txt]\n  zp_movie asm <script.txt> <out.zpm>\n"
                 "  zp_movie play <movie.zpm> [--roms PATH]... [--wav out.wav] [--ppm out.ppm] [--ignore-core-revision]\n"
                 "  zp_movie seal <in.zpm> <out.zpm> [--roms PATH]... [--interval CYCLES]\n");
    return 2;
}

bool readText(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

struct Args {
    std::vector<std::string> positional, roms;
    std::string wav, ppm;
    uint64_t interval = System::CYCLES_PER_FRAME * 64;
    bool ignoreCore = false;
};

bool parseArgs(int argc, char** argv, int first, Args& a) {
    for (int i = first; i < argc; i++) {
        const std::string s = argv[i];
        if (s == "--roms" && i + 1 < argc) a.roms.push_back(argv[++i]);
        else if (s == "--wav" && i + 1 < argc) a.wav = argv[++i];
        else if (s == "--ppm" && i + 1 < argc) a.ppm = argv[++i];
        else if (s == "--interval" && i + 1 < argc) a.interval = std::strtoull(argv[++i], nullptr, 0);
        else if (s == "--ignore-core-revision") a.ignoreCore = true;
        else if (s.compare(0, 2, "--") == 0) { std::fprintf(stderr, "unknown option %s\n", s.c_str()); return false; }
        else a.positional.push_back(s);
    }
    return true;
}

void buildLibrary(RomLibrary& lib, const Args& a, const std::string& movieDir) {
    lib.add(movieDir.empty() ? "." : movieDir);
    for (const std::string& r : a.roms) lib.add(r);
}

// Little-endian WAV writer for the audio the machine produced (48 kHz stereo 16-bit).
struct Wav {
    std::vector<int16_t> samples;
    bool write(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) f.put((char)((v >> (8 * i)) & 0xFF)); };
        auto u16 = [&](uint16_t v) { f.put((char)(v & 0xFF)); f.put((char)(v >> 8)); };
        const uint32_t bytes = (uint32_t)(samples.size() * 2);
        f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2);
        u32(System::AUDIO_SAMPLE_RATE); u32(System::AUDIO_SAMPLE_RATE * 4); u16(4); u16(16);
        f.write("data", 4); u32(bytes);
        for (int16_t s : samples) u16((uint16_t)s);
        return f.good();
    }
};

bool writePpm(System& sys, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << FB_WIDTH << " " << FULL_HEIGHT << "\n255\n";
    std::vector<uint32_t> row(FB_WIDTH);
    for (int y = 0; y < FULL_HEIGHT; y++) {
        if (!sys.getDisplay().getScanlineSDL(y, row.data())) std::fill(row.begin(), row.end(), 0);
        for (uint32_t px : row) { f.put((char)(px & 0xFF)); f.put((char)((px >> 8) & 0xFF)); f.put((char)((px >> 16) & 0xFF)); }
    }
    return f.good();
}

int cmdInfo(const std::string& path) {
    Movie m;
    std::string err;
    if (!Movie::load(path, m, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    size_t inputs[2] = {0, 0}, resets[2] = {0, 0}, yanks = 0, inserts = 0, checks = 0;
    for (const MovieEvent& e : m.events) {
        switch (e.type) {
            case MovieEvent::Input: inputs[e.port & 1]++; break;
            case MovieEvent::SoftReset: resets[0]++; break;
            case MovieEvent::HardReset: resets[1]++; break;
            case MovieEvent::Yank: yanks++; break;
            case MovieEvent::Insert: inserts++; break;
            case MovieEvent::Checkpoint: checks++; break;
        }
    }
    std::printf("core revision : %u\n", m.coreRevision);
    if (m.coreRevision != System::CORE_REVISION) std::printf("                (this core is revision %u: the movie will not replay without --ignore-core-revision)\n", System::CORE_REVISION);
    std::printf("length        : %llu master cycles (%.3f s of console time)\n", (unsigned long long)m.lengthCycles, (double)m.lengthCycles / 68011355.0);
    std::printf("boot ROM      : %s\n", m.hleBoot ? "native (HLE)" : ("sha256 " + toHex(m.bootRom)).c_str());
    std::printf("APU BIOS      : %s\n", m.hleApuBios ? "native (HLE)" : ("sha256 " + toHex(m.apuBios)).c_str());
    std::printf("signatures    : %s\n", m.verifySignatures ? "verified at boot" : "not verified");
    std::printf("starts with   : %s\n", m.initialRom < 0 ? "an empty slot" : m.roms[(size_t)m.initialRom].title.c_str());
    for (size_t i = 0; i < m.roms.size(); i++) std::printf("  rom %zu       : %s  %s\n", i, toHex(m.roms[i].sha).c_str(), m.roms[i].title.c_str());
    std::printf("events        : %zu  (P1 input %zu, P2 input %zu, soft resets %zu, hard resets %zu, yanks %zu, swaps %zu)\n",
                m.events.size() - checks, inputs[0], inputs[1], resets[0], resets[1], yanks, inserts);
    std::printf("checkpoints   : %zu%s\n", checks, checks ? "" : "   (unsealed: nothing to verify; run `zp_movie seal`)");
    if (!m.author.empty()) std::printf("author        : %s\n", m.author.c_str());
    if (!m.description.empty()) std::printf("description   : %s\n", m.description.c_str());
    return 0;
}

int cmdPlay(const Args& a) {
    if (a.positional.size() != 1) return usage();
    Movie m;
    std::string err;
    if (!Movie::load(a.positional[0], m, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    RomLibrary lib;
    buildLibrary(lib, a, std::filesystem::path(a.positional[0]).parent_path().string());
    System sys;
    MovieSession session;
    if (!session.beginPlayback(sys, m, lib, err, a.ignoreCore)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    Wav wav;
    while (!session.finished() && !session.desynced() && session.failure().empty()) {
        session.run(System::CYCLES_PER_FRAME * 16);
        if (!a.wav.empty()) {
            const auto& audio = sys.getAudioBuffer();
            wav.samples.insert(wav.samples.end(), audio.begin(), audio.end());
        }
        sys.clearAudioBuffer();   // presentation: the machine hash doesn't depend on it
    }
    if (!a.wav.empty() && !wav.write(a.wav)) std::fprintf(stderr, "cannot write %s\n", a.wav.c_str());
    if (!a.ppm.empty() && !writePpm(sys, a.ppm)) std::fprintf(stderr, "cannot write %s\n", a.ppm.c_str());
    if (!session.failure().empty()) { std::fprintf(stderr, "FAILED: %s\n", session.failure().c_str()); return 1; }
    if (session.desynced()) { std::printf("%s\n", session.desyncReport().c_str()); return 1; }
    const size_t total = (size_t)std::count_if(m.events.begin(), m.events.end(), [](const MovieEvent& e) { return e.type == MovieEvent::Checkpoint; });
    std::printf("replayed %llu cycles (%.3f s): %zu/%zu checkpoints verified, final machine hash %016llX\n",
                (unsigned long long)session.cycle(), (double)session.cycle() / 68011355.0, session.checkpointsVerified(), total,
                (unsigned long long)sys.machineHash());
    if (total == 0) std::printf("note: this movie has no checkpoints, so nothing was verified (run `zp_movie seal`)\n");
    return 0;
}

int cmdSeal(const Args& a) {
    if (a.positional.size() != 2 || a.interval == 0) return usage();
    Movie m;
    std::string err;
    if (!Movie::load(a.positional[0], m, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    m.events.erase(std::remove_if(m.events.begin(), m.events.end(), [](const MovieEvent& e) { return e.type == MovieEvent::Checkpoint; }),
                   m.events.end());
    m.checkpointInterval = a.interval;
    RomLibrary lib;
    buildLibrary(lib, a, std::filesystem::path(a.positional[0]).parent_path().string());
    System sys;
    MovieSession session;
    if (!session.beginPlayback(sys, m, lib, err, true)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::vector<MovieEvent> checkpoints;
    auto capture = [&]() {
        MovieEvent c;
        c.cycle = session.cycle();
        c.type = MovieEvent::Checkpoint;
        for (const auto& p : sys.machineHashParts()) c.parts.push_back(p.second);
        checkpoints.push_back(std::move(c));
    };
    uint64_t next = a.interval;
    while (!session.finished() && session.failure().empty()) {
        const uint64_t stop = std::min(next, m.lengthCycles);
        session.run(stop - session.cycle());
        if (session.cycle() == next && next < m.lengthCycles) { capture(); next += a.interval; }
    }
    if (!session.failure().empty()) { std::fprintf(stderr, "FAILED: %s\n", session.failure().c_str()); return 1; }
    // Checkpoints before a cycle's other events (they see the state before them); the closing one after everything.
    std::vector<MovieEvent> merged;
    size_t ci = 0;
    for (const MovieEvent& e : m.events) {
        while (ci < checkpoints.size() && checkpoints[ci].cycle <= e.cycle) merged.push_back(checkpoints[ci++]);
        merged.push_back(e);
    }
    while (ci < checkpoints.size()) merged.push_back(checkpoints[ci++]);
    capture();   // at the very end, after the events that fall on it
    merged.push_back(checkpoints.back());
    m.events = merged;
    m.coreRevision = System::CORE_REVISION;
    if (!m.save(a.positional[1], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::printf("sealed %s: %zu checkpoints, one every %llu cycles, final machine hash %016llX\n", a.positional[1].c_str(),
                checkpoints.size(), (unsigned long long)a.interval, (unsigned long long)sys.machineHash());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];
    Args a;
    if (!parseArgs(argc, argv, 2, a)) return 2;
    if (cmd == "info" && a.positional.size() == 1) return cmdInfo(a.positional[0]);
    if (cmd == "dis" && (a.positional.size() == 1 || a.positional.size() == 2)) {
        Movie m;
        std::string err;
        if (!Movie::load(a.positional[0], m, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        const std::string text = m.toText();
        if (a.positional.size() == 2) {
            std::ofstream(a.positional[1]) << text;
            std::printf("wrote %s\n", a.positional[1].c_str());
        } else {
            std::fputs(text.c_str(), stdout);
        }
        return 0;
    }
    if (cmd == "asm" && a.positional.size() == 2) {
        std::string text, err;
        if (!readText(a.positional[0], text)) { std::fprintf(stderr, "cannot read %s\n", a.positional[0].c_str()); return 1; }
        Movie m;
        if (!Movie::fromText(text, m, err)) { std::fprintf(stderr, "%s: %s\n", a.positional[0].c_str(), err.c_str()); return 1; }
        if (!m.save(a.positional[1], err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        std::printf("wrote %s (%zu events, %.3f s)\n", a.positional[1].c_str(), m.events.size(), (double)m.lengthCycles / 68011355.0);
        return 0;
    }
    if (cmd == "play") return cmdPlay(a);
    if (cmd == "seal") return cmdSeal(a);
    return usage();
}
