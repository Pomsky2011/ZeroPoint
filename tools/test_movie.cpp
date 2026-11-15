// test_movie: input movies (include/movie.h, docs/movies.md) end to end, on ROMs the test builds itself.
//
//   test_movie <workdir>
//
//   * a movie records inputs on both ports, soft/hard resets, a yank and a Stop 'n' Swop with awkward slice sizes, then
//     replays with different slicing, the video thread, frame skip toggled and the audio buffer drained at random: no
//     desync, same final machine hash
//   * tampering is caught and named: a shifted input, a wrong ROM, a flipped byte, the wrong core revision
//   * the binary and text forms round-trip, and the parser survives thousands of corrupted/truncated movies
#include "movie.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 7) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
    uint32_t below(uint32_t n) { return (uint32_t)(next() % n); }
};

// A cartridge that mirrors both controller ports into work RAM in a loop, so what the player does shows up in the machine.
// `slot` picks the RAM bytes it writes, so two different cartridges leave different traces.
std::vector<uint8_t> echoRom(uint8_t slot) {
    const uint8_t code[] = {
        0x3D, 0x30, 0x00, 0xD8, 0x55, (uint8_t)(slot + 0), 0x00, 0x80,   // lda $D80030 ; sta $80:slot+0   P1 direction
        0x3D, 0x31, 0x00, 0xD8, 0x55, (uint8_t)(slot + 1), 0x00, 0x80,   // P1 control
        0x3D, 0x32, 0x00, 0xD8, 0x55, (uint8_t)(slot + 2), 0x00, 0x80,   // P1 buttons
        0x3D, 0x38, 0x00, 0xD8, 0x55, (uint8_t)(slot + 3), 0x00, 0x80,   // P2 direction
        0x3D, 0x39, 0x00, 0xD8, 0x55, (uint8_t)(slot + 4), 0x00, 0x80,   // P2 control
        0x10, 0x00, 0x00, 0x00,                                // jmp $00:0000
    };
#pragma pack(push, 1)
    struct { char magic[4]; uint8_t version, reserved; uint16_t headerSize; uint32_t romSize, entry; char title[32]; char dev[16]; } h;
#pragma pack(pop)
    std::memset(&h, 0, sizeof h);
    std::memcpy(h.magic, "ZPB", 4);
    h.version = 1;
    h.headerSize = 64;
    h.romSize = sizeof code;
    h.entry = 0;
    std::snprintf(h.title, sizeof h.title, "echo%u", slot);
    std::vector<uint8_t> out(sizeof h + sizeof code);
    std::memcpy(out.data(), &h, sizeof h);
    std::memcpy(out.data() + sizeof h, code, sizeof code);
    return out;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& d) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d.data()), (std::streamsize)d.size());
}

// What the "player" does, as (movie cycle, action). Applied while recording with awkward slice sizes.
struct Act { uint64_t cycle; int kind; int port = 0; uint8_t d = 0, c = 0, b = 0; };
enum { IN, SOFT, HARD, YANK, SWAP };

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <workdir>\n", argv[0]); return 2; }
    const std::string work = argv[1];
    const std::vector<uint8_t> romA = echoRom(0x00), romB = echoRom(0x20);
    writeFile(work + "/a.zpb", romA);
    writeFile(work + "/b.zpb", romB);

    const uint64_t F = System::CYCLES_PER_FRAME;
    const uint8_t on = PlayerInput::CTRL_CONNECTION;
    const std::vector<Act> script = {
        {0, IN, 0, PlayerInput::DIR_UP, on, 0},
        {F / 3, IN, 1, PlayerInput::DIR_LEFT, on | PlayerInput::CTRL_PAUSE, 0},      // plug in player 2
        {F, IN, 0, 0, on | PlayerInput::CTRL_BIGLEFT, PlayerInput::BTN_1},
        {F + 17, IN, 0, PlayerInput::DIR_DOWN, on, PlayerInput::BTN_1 | PlayerInput::BTN_2},
        {3 * F, IN, 1, 0, on, PlayerInput::BTN_3},
        {4 * F + 1, SOFT},
        {4 * F + 90, IN, 0, PlayerInput::DIR_RIGHT, on, 0},
        {6 * F, YANK},
        {6 * F + 40, IN, 0, PlayerInput::DIR_LEFT, on, PlayerInput::BTN_4},
        {7 * F + 5, SWAP},
        {7 * F + 500, IN, 1, PlayerInput::DIR_UP, on, 0},
        {9 * F, HARD},
        {9 * F + 3, IN, 0, PlayerInput::DIR_UPLEFT, on, PlayerInput::BTN_2},
        {10 * F, SOFT},
        {11 * F, IN, 1, 0, 0, 0},                                                      // unplug player 2
    };
    const uint64_t end = 12 * F;

    // ---- record -------------------------------------------------------------------------------------------------------
    std::printf("record\n");
    Movie recorded;
    uint64_t recordedHash = 0;
    std::string err;
    {
        System sys;
        MovieSession rec;
        MovieSession::RecordSetup setup;
        setup.cartridgePath = work + "/a.zpb";
        setup.verifySignatures = false;
        setup.checkpointInterval = F * 3 + 11;   // deliberately not aligned to anything
        setup.author = "test_movie";
        check(rec.beginRecording(sys, setup, err), err.empty() ? "recording starts on a fresh System" : err.c_str());
        Rng slice(1);
        auto goTo = [&](uint64_t cycle) {
            while (rec.cycle() < cycle) rec.run(std::min<uint64_t>(cycle - rec.cycle(), 1 + slice.below(slice.below(4) ? 4000 : 90000)));
        };
        for (const Act& a : script) {
            goTo(a.cycle);
            switch (a.kind) {
                case IN: rec.input(a.port, a.d, a.c, a.b); break;
                case SOFT: rec.softReset(); break;
                case HARD: rec.hardReset(); break;
                case YANK: rec.yank(); break;
                case SWAP: rec.insert(romB, "b.zpb"); break;
            }
        }
        goTo(end);
        recordedHash = sys.machineHash();
        recorded = rec.finishRecording();
        check(recorded.lengthCycles == end, "the movie is as long as the session");
        size_t checkpoints = 0;
        for (const MovieEvent& e : recorded.events) checkpoints += e.type == MovieEvent::Checkpoint;
        check(checkpoints >= 4, "checkpoints were embedded (every ~3 frames, plus a closing one)");
        check(recorded.roms.size() == 2 && recorded.initialRom == 0, "the ROM table pins both cartridges by hash");
        check(recorded.save(work + "/session.zpm", err), "saved");
    }

    // ---- replay -------------------------------------------------------------------------------------------------------
    std::printf("replay\n");
    RomLibrary lib;
    lib.add(work);
    check(lib.size() >= 2, "the ROM library finds the cartridges by content");
    Movie loaded;
    check(Movie::load(work + "/session.zpm", loaded, err), "loads back");
    check(loaded.serialize() == recorded.serialize(), "load(save(m)) == m, byte for byte");

    // 0 = frame slices, 1 = random slices + vthread + frame skip + audio drain, 2 = on a System that was already running
    // something else (a frontend reuses one across ROM loads)
    struct Way { const char* name; int mode; };
    for (const Way& w : {Way{"in frame-sized slices", 0}, Way{"random slices, video thread, frame skip toggled, audio drained", 1},
                         Way{"on a reused, dirty System", 2}}) {
        System sys;
        MovieSession play;
        if (w.mode == 1) sys.setVideoThread(true);
        if (w.mode == 2) {
            sys.setVerifySignatures(true);
            sys.loadROMImage(romB);
            sys.powerOn();
            sys.getCPU().setPlayerInput(PlayerInput::DIR_UP, 0, PlayerInput::BTN_4);
            sys.getCPU().setPlayer2Input(PlayerInput::DIR_DOWN, PlayerInput::CTRL_CONNECTION, 0);
            sys.runCycles(5 * F);
            for (uint32_t i = 0; i < 2000; i++) sys.getCPU().writeByte(0x800000 + i * 37, (uint8_t)i), sys.getCPU().writeByte(0xB00400 + i, (uint8_t)(i * 5));
            sys.runCycles(2 * F);
        }
        if (!play.beginPlayback(sys, loaded, lib, err)) { check(false, err.c_str()); continue; }
        Rng slice(99);
        while (!play.finished() && !play.desynced() && play.failure().empty()) {
            if (w.mode == 0 || w.mode == 2) {
                play.run(F);
            } else {
                if (slice.below(3) == 0) sys.getDisplay().setLatchEnabled(slice.below(2) != 0);
                if (slice.below(4) == 0) sys.clearAudioBuffer();
                play.run(1 + slice.below(slice.below(5) ? 3000 : 120000));
            }
        }
        const std::string label = std::string("replays ") + w.name;
        check(!play.desynced() && play.failure().empty() && play.finished(), play.desynced() ? play.desyncReport().c_str() : label.c_str());
        size_t total = 0;
        for (const MovieEvent& e : loaded.events) total += e.type == MovieEvent::Checkpoint;
        check(play.checkpointsVerified() == total && total > 0, "every checkpoint verified");
        check(sys.machineHash() == recordedHash, "the final machine hash equals the recording's");
    }

    // ---- tampering ----------------------------------------------------------------------------------------------------
    std::printf("tampering is caught\n");
    auto replay = [&](const Movie& m, std::string& report, bool ignoreCore = false) {
        System sys;
        MovieSession play;
        if (!play.beginPlayback(sys, m, lib, report, ignoreCore)) return false;
        while (!play.finished() && !play.desynced() && play.failure().empty()) play.run(F);
        if (play.desynced()) { report = play.desyncReport(); return false; }
        if (!play.failure().empty()) { report = play.failure(); return false; }
        return true;
    };
    // Checkpoints verify the machine's state at their own instants: a change that leaves no trace by the next checkpoint
    // (an input that is overwritten a few cycles later, a one-cycle shift the program never reads) is not a different run.
    // So the tampers below are ones that persist across a checkpoint.
    {
        Movie m = loaded;
        for (MovieEvent& e : m.events)
            if (e.type == MovieEvent::Input && e.port == 0 && e.dir == PlayerInput::DIR_RIGHT) { e.dir = PlayerInput::DIR_LEFT; break; }
        std::string report;
        const bool ok = replay(m, report);
        std::printf("      (%s)\n", report.c_str());
        check(!ok && report.find("DESYNC") != std::string::npos && report.find("cpu") != std::string::npos,
              "a different direction desyncs, and the report names the CPU/RAM");
    }
    {
        Movie m = loaded;
        for (MovieEvent& e : m.events)
            if (e.type == MovieEvent::Input && e.port == 0 && e.dir == PlayerInput::DIR_RIGHT) { e.cycle = 6 * F + 30; break; }   // after a checkpoint, not before it
        std::string report;
        check(!replay(m, report) && report.find("DESYNC") != std::string::npos, "an input moved past a checkpoint desyncs");
    }
    {
        Movie m = loaded;
        for (size_t i = 0; i < m.events.size(); i++)
            if (m.events[i].type == MovieEvent::Yank) { m.events.erase(m.events.begin() + (long)i); break; }
        std::string report;
        check(!replay(m, report) && report.find("DESYNC") != std::string::npos, "a missing yank desyncs");
    }
    {
        Movie m = loaded;
        m.roms[1].sha[0] ^= 1;
        std::string report;
        check(!replay(m, report) && report.find("not in the ROM library") != std::string::npos, "a swap to an unknown ROM is refused by name");
    }
    {
        Movie m = loaded;
        m.coreRevision += 1;
        std::string report;
        check(!replay(m, report) && report.find("core revision") != std::string::npos, "the wrong core revision is refused");
        check(replay(m, report, true), "...unless told to ignore it (the checkpoints still pass: nothing changed)");
    }
    {
        std::vector<uint8_t> bytes = loaded.serialize();
        bytes[bytes.size() / 2] ^= 0x04;
        Movie m;
        std::string e2;
        check(!Movie::parse(bytes.data(), bytes.size(), m, e2), "a flipped byte fails the checksum");
    }

    // ---- formats ------------------------------------------------------------------------------------------------------
    std::printf("formats\n");
    {
        Movie back;
        const std::string text = loaded.toText();
        check(Movie::fromText(text, back, err) && back.serialize() == loaded.serialize(), "text -> binary round-trips exactly");
        Movie bad;
        check(!Movie::fromText("zpm-text 1\n@10 frobnicate\n", bad, err), "the text parser rejects unknown events");
        check(!Movie::fromText("zpm-text 1\n@10 softreset\n@5 softreset\n", bad, err), "...and events out of order");
    }
    {
        // The parser must survive anything: truncations, flips, and random bytes behind a valid-looking prefix.
        const std::vector<uint8_t> good = loaded.serialize();
        Rng r(5);
        int accepted = 0, rejected = 0;
        for (int i = 0; i < 4000; i++) {
            std::vector<uint8_t> b = good;
            switch (i % 4) {
                case 0: b.resize(r.below((uint32_t)b.size())); break;
                case 1: for (int k = 0; k < 1 + (int)r.below(4); k++) b[r.below((uint32_t)b.size())] ^= (uint8_t)(1 + r.below(255)); break;
                case 2: { const uint32_t at = 14 + r.below((uint32_t)b.size() - 22); b[at] = (uint8_t)r.next(); b[at + 1] = (uint8_t)r.next(); break; }
                default: for (auto& x : b) if (r.below(40) == 0) x = (uint8_t)r.next(); break;
            }
            // Fix the checksum so the mutation reaches the structural parser instead of dying at the integrity check.
            if (b.size() >= 8 && i % 2 == 0) {
                uint64_t h = 1469598103934665603ull;
                for (size_t k = 0; k + 8 < b.size(); k++) h = (h ^ b[k]) * 1099511628211ull;
                for (int k = 0; k < 8; k++) b[b.size() - 8 + k] = (uint8_t)(h >> (8 * k));
            }
            Movie m;
            std::string e3;
            (Movie::parse(b.data(), b.size(), m, e3) ? accepted : rejected)++;
        }
        std::printf("      (%d parsed, %d rejected, none crashed)\n", accepted, rejected);
        check(rejected > 0, "4000 corrupted movies: the parser survived them all");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
