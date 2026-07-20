// Headless profiling workload: boots the WIP ZPbootROM two-bank image
// (CPU RSA-verify/tile-streaming code at $E0 + PPU splash program at $E1)
// with no cartridge attached, and runs it for a fixed number of frames
// through the real System::step() master-clock loop (interpreter, per
// CLAUDE.md's "use interpreter for most tests"). No SDL/Qt/Vulkan - meant
// to be run under `perf record` to profile the whole core (CPU+PPU+APU+
// DMA+Display) under a realistic workload instead of a synthetic one.
//
// Usage: profile_bootrom <path/to/bootrom.bin> [frames]
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <path/to/bootrom.bin> [frames]\n", argv[0]);
        return 1;
    }
    uint64_t frames = (argc > 2) ? strtoull(argv[2], nullptr, 10) : 300;

    System system;
    if (!system.loadBootROM(argv[1])) {
        std::fprintf(stderr, "Failed to load boot ROM: %s\n", argv[1]);
        return 1;
    }
    // No cartridge loaded - the boot ROM's RSA verify will fail and halt
    // after running the splash sequence, which is fine: the splash (tile
    // banking + concurrent TILEDRAW blitter + CPU hash/verify work) is the
    // expensive part we want profiled.
    system.powerOn();

    std::printf("Running %llu frames (%llu master cycles)...\n",
                (unsigned long long)frames,
                (unsigned long long)(frames * System::CYCLES_PER_FRAME));

    uint64_t f = 0;
    for (; f < frames; f++) {
        if (system.getCPU().isHalted()) break;
        system.stepFrame();
    }

    std::printf("Ran %llu frames before %s.\n", (unsigned long long)f,
                system.getCPU().isHalted() ? "CPU halt" : "frame limit");
    return 0;
}
