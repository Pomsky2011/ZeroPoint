// test_batched: System's batched master clock (System::runCycles) must be state-identical to stepping
// System::step() one master cycle at a time. Runs the same boot ROM + cartridge both ways and hashes the
// whole machine (CPU registers, WRAM, PPU, Display, APU, every audio sample produced) at checkpoints.
//
//   test_batched <bootrom.bin|none> <rom.zpb> <console-seconds> [checkpoint-frames] [--dump]
//
// --dump prints just the per-cycle hashes (to diff against an older build's: a refactor of the PPU's timing
// must not change them).
//
// Prints each mode's CPU time (so the speedup is visible, and holds up under other load) and fails at the first checkpoint that differs.
#include "system.h"
#include <chrono>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ZeroPoint;

namespace {

uint64_t mix(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
template <typename T> uint64_t mixv(uint64_t h, const T& v) { return mix(h, &v, sizeof v); }

uint64_t machineHash(System& s) {
    uint64_t h = 1469598103934665603ull;
    CPU& cpu = s.getCPU();
    h = mixv(h, cpu.getA()); h = mixv(h, cpu.getX()); h = mixv(h, cpu.getY()); h = mixv(h, cpu.getSP());
    h = mixv(h, cpu.getD()); h = mixv(h, cpu.getPC()); h = mixv(h, cpu.getPB()); h = mixv(h, cpu.getDB());
    h = mixv(h, cpu.getFlags().toByte()); h = mixv(h, cpu.getCycleCount()); h = mixv(h, cpu.getInstructionCount());
    h = mixv(h, cpu.isHalted());
    for (uint32_t a = 0x800000; a < 0x810000; a++) { uint8_t b = cpu.readByte(a); h = mixv(h, b); }
    h = mixv(h, s.getPPU().stateHash());
    h = mixv(h, s.getDisplay().stateHash());
    APU& apu = s.getAPU();
    h = mixv(h, apu.getPC()); h = mixv(h, apu.getSP()); h = mixv(h, apu.getCycleCount());
    h = mixv(h, apu.getInstructionCount()); h = mixv(h, apu.isHalted());
    const auto& audio = s.getAudioBuffer();
    h = mix(h, audio.data(), audio.size() * sizeof(int16_t));
    return h;
}

struct Result {
    std::vector<uint64_t> hashes;
    double seconds = 0;
    uint64_t cycles = 0;
};

Result run(const char* boot, const char* rom, double seconds, int checkpointFrames, bool batched) {
    System sys;
    if (std::strcmp(boot, "none") && !sys.loadBootROM(boot)) { std::fprintf(stderr, "cannot load boot ROM\n"); std::exit(1); }
    if (!sys.loadROM(rom)) { std::fprintf(stderr, "cannot load ROM\n"); std::exit(1); }
    sys.powerOn();
    const uint64_t frames = (uint64_t)(seconds * 68011355.0 / System::CYCLES_PER_FRAME);
    Result r;
    const std::clock_t c0 = std::clock();   // process CPU time: unaffected by other load on the host
    for (uint64_t f = 0; f < frames; f += checkpointFrames) {
        const uint64_t cycles = (uint64_t)checkpointFrames * System::CYCLES_PER_FRAME;
        if (batched) {
            sys.runCycles(cycles);
        } else {
            for (uint64_t i = 0; i < cycles && !sys.getCPU().isHalted(); i++) sys.step();
        }
        r.hashes.push_back(machineHash(sys));
        sys.clearAudioBuffer();
    }
    r.seconds = static_cast<double>(std::clock() - c0) / CLOCKS_PER_SEC;
    r.cycles = frames * System::CYCLES_PER_FRAME;
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <bootrom.bin|none> <rom.zpb> <console-seconds> [checkpoint-frames]\n", argv[0]);
        return 2;
    }
    const double seconds = std::atof(argv[3]);
    const int cp = argc > 4 ? std::atoi(argv[4]) : 200;
    Result a = run(argv[1], argv[2], seconds, cp, false);
    if (argc > 5 && !std::strcmp(argv[5], "--dump")) {   // per-cycle hashes only, to diff against another build
        for (uint64_t h : a.hashes) std::printf("%016llx\n", (unsigned long long)h);
        return 0;
    }
    Result b = run(argv[1], argv[2], seconds, cp, true);
    std::printf("per-cycle: %.2f s CPU for %.2f s of console time (%.2fx real time)\n", a.seconds, seconds, seconds / a.seconds);
    std::printf("batched  : %.2f s CPU for %.2f s of console time (%.2fx real time), %.2fx faster\n", b.seconds, seconds,
                seconds / b.seconds, a.seconds / b.seconds);
    size_t n = std::min(a.hashes.size(), b.hashes.size());
    for (size_t i = 0; i < n; i++) {
        if (a.hashes[i] != b.hashes[i]) {
            std::printf("FAIL: state differs at checkpoint %zu (frame %zu, %.3f s)\n", i, (i + 1) * (size_t)cp,
                        (i + 1) * (double)cp * System::CYCLES_PER_FRAME / 68011355.0);
            return 1;
        }
    }
    if (a.hashes.size() != b.hashes.size()) { std::printf("FAIL: different number of checkpoints\n"); return 1; }
    std::printf("ok  : %zu checkpoints identical\n", n);
    return 0;
}
