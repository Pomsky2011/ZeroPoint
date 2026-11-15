// test_determinism: the whole System must be bit-for-bit deterministic. Boots the same cartridge several ways
// and compares System::stateHash() (plus each chip's own hash, to say who diverged) at every checkpoint.
//
//   test_determinism <bootrom.bin|none> <rom.zpb> <console-seconds> [checkpoint-frames] [--no-verify]
//
// "none" is the HLE Boot ROM. The runs, all against the first:
//   fill A5 / fill 5A   the System is built with placement-new into memory pre-filled with that byte, so any
//                       member the constructors/reset() leave uninitialised keeps the pattern and, if the
//                       machine ever depends on it, diverges
//   again               a second System in the same process, after the first ran: catches state that leaks
//                       through statics and survives a System's destruction
//   per-cycle           System::step() one master cycle at a time instead of runCycles()
//   vthread             runCycles() with the PPU/display catch-up on the worker thread
// Then two probes aimed at hidden state:
//   latches             the $D8 I/O blocks' write-sequence latches (DMA config index, PPU register select, P2 serial
//                       queues) are cleared by reset() and are per System, not process-wide
//   interleaved         two Systems alive at once, stepped alternately, each identical to its solo run
// Exits 1 at the first difference.
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

using Hashes = std::vector<std::pair<const char*, uint64_t>>;   // System::stateHashParts()

// Names of the parts that differ between two checkpoints.
std::string differing(const Hashes& a, const Hashes& b) {
    std::string out;
    for (size_t i = 0; i < a.size() && i < b.size(); i++)
        if (a[i].second != b[i].second) out += std::string(out.empty() ? "" : ", ") + a[i].first;
    return out;
}

struct Config {
    const char* name;
    int fill;            // -1: construct normally; else pre-fill the System's storage with this byte
    bool perCycle;
    bool vthread;
};

std::string g_boot, g_rom;
bool g_verify = true;

std::vector<Hashes> run(const Config& cfg, uint64_t frames, uint64_t checkpointFrames) {
    void* storage = nullptr;
    System* sys;
    if (cfg.fill >= 0) {
        storage = ::operator new(sizeof(System), std::align_val_t(alignof(System)));
        std::memset(storage, cfg.fill, sizeof(System));
        sys = new (storage) System();
    } else {
        sys = new System();
    }
    sys->setVerifySignatures(g_verify);
    if (g_boot != "none" && !sys->loadBootROM(g_boot)) { std::fprintf(stderr, "cannot load boot ROM\n"); std::exit(2); }
    if (!sys->loadROM(g_rom)) { std::fprintf(stderr, "cannot load ROM\n"); std::exit(2); }
    sys->powerOn();
    if (cfg.vthread) sys->setVideoThread(true);

    std::vector<Hashes> out;
    for (uint64_t f = 0; f < frames; f += checkpointFrames) {
        const uint64_t cycles = checkpointFrames * System::CYCLES_PER_FRAME;
        if (cfg.perCycle) {
            for (uint64_t i = 0; i < cycles && !sys->getCPU().isHalted(); i++) sys->step();
        } else {
            sys->runCycles(cycles);
        }
        out.push_back(sys->stateHashParts());
        sys->clearAudioBuffer();
    }

    if (storage) {
        sys->~System();
        ::operator delete(storage, std::align_val_t(alignof(System)));
    } else {
        delete sys;
    }
    return out;
}

// Leaves every $D8 latch dirty: half a DMA config, a PPU register selected, the P2 TX queue full.
void pollute(System& sys) {
    CPU& cpu = sys.getCPU();
    cpu.writeByte(0xD80020, 0);
    for (int i = 0; i < 4; i++) cpu.writeByte(0xD80021, 0x11 * (i + 1));
    cpu.writeByte(0xD80008, 45);
    for (int i = 0; i < 300; i++) cpu.writeByte(0xD8003D, (uint8_t)i);   // SER_DATA
}

// What a pristine machine reads from those latches.
bool latchesClean(System& sys) {
    CPU& cpu = sys.getCPU();
    return cpu.readByte(0xD80020) == 0 &&        // DMA config bytes received
           cpu.readByte(0xD80008) == 0 &&        // PPUREG_ADDR
           cpu.readByte(0xD8003C) == 0x02;       // SER_STATUS: TX empty, nothing to read
}

int probeLatches() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) failures++;
    };
    {
        System fresh;
        check(latchesClean(fresh), "a fresh System has clean latches");
    }
    {
        System s;
        pollute(s);
        check(!latchesClean(s), "(the polluter really dirties them)");
        s.reset();
        check(latchesClean(s), "reset() clears the latches");
    }
    {
        System s;   // the one above is gone: nothing may have survived it in a static
        check(latchesClean(s), "a System built after a dirty one starts clean");
    }
    {
        System a, b;
        pollute(a);
        check(latchesClean(b), "two live Systems do not share latches");
    }
    return failures;
}

// Two Systems alive at once, stepped alternately, must each match `ref` (the solo run).
bool interleaved(uint64_t frames, uint64_t checkpointFrames, const std::vector<Hashes>& ref) {
    System a, b;
    System* both[2] = {&a, &b};
    for (System* s : both) {
        s->setVerifySignatures(g_verify);
        if (g_boot != "none" && !s->loadBootROM(g_boot)) { std::fprintf(stderr, "cannot load boot ROM\n"); std::exit(2); }
        if (!s->loadROM(g_rom)) { std::fprintf(stderr, "cannot load ROM\n"); std::exit(2); }
        s->powerOn();
    }
    size_t cpIdx = 0;
    for (uint64_t f = 0; f < frames; f += checkpointFrames, cpIdx++) {
        for (System* s : both) {
            s->runCycles(checkpointFrames * System::CYCLES_PER_FRAME);
            Hashes h = s->stateHashParts();
            s->clearAudioBuffer();
            if (cpIdx >= ref.size() || !(h == ref[cpIdx])) {
                std::printf("  FAIL: interleaved           System %c differs from its solo run at checkpoint %zu\n",
                            s == &a ? 'A' : 'B', cpIdx);
                return false;
            }
        }
    }
    std::printf("  ok  : interleaved           two live Systems, %zu checkpoints each, identical to the solo run\n", cpIdx);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <bootrom.bin|none> <rom.zpb> <console-seconds> [checkpoint-frames] [--no-verify]\n", argv[0]);
        return 2;
    }
    g_boot = argv[1];
    g_rom = argv[2];
    const double seconds = std::atof(argv[3]);
    uint64_t cp = 50;
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--no-verify")) g_verify = false;
        else cp = (uint64_t)std::atoll(argv[i]);
    }
    const uint64_t frames = (uint64_t)(seconds * 68011355.0 / System::CYCLES_PER_FRAME);

    const Config base = {"baseline (runCycles)", -1, false, false};
    const Config others[] = {
        {"fill A5", 0xA5, false, false},
        {"fill 5A", 0x5A, false, false},
        {"again, same process", -1, false, false},
        {"per-cycle step()", -1, true, false},
        {"vthread", -1, false, true},
    };

    std::printf("baseline: %llu frames, a checkpoint every %llu\n", (unsigned long long)frames, (unsigned long long)cp);
    std::vector<Hashes> ref = run(base, frames, cp);
    int failures = 0;
    for (const Config& c : others) {
        std::vector<Hashes> got = run(c, frames, cp);
        size_t bad = 0;
        while (bad < ref.size() && bad < got.size() && ref[bad] == got[bad]) bad++;
        if (bad == ref.size() && got.size() == ref.size()) {
            std::printf("  ok  : %-22s %zu checkpoints identical\n", c.name, ref.size());
        } else if (bad < ref.size() && bad < got.size()) {
            std::printf("  FAIL: %-22s first differs at checkpoint %zu (frame %llu): %s\n", c.name, bad,
                        (unsigned long long)((bad + 1) * cp), differing(ref[bad], got[bad]).c_str());
            failures++;
        } else {
            std::printf("  FAIL: %-22s different number of checkpoints (%zu vs %zu)\n", c.name, got.size(), ref.size());
            failures++;
        }
    }
    if (!interleaved(frames, cp, ref)) failures++;
    failures += probeLatches();
    std::printf("%s\n", failures ? "FAILED" : "deterministic");
    return failures ? 1 : 0;
}
