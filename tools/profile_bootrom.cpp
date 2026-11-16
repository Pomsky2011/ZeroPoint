// Headless profiling workload: boots the WIP ZPbootROM two-bank image
// (CPU RSA-verify/tile-streaming code at $E0 + PPU splash program at $E1)
// with no cartridge attached, and runs it for a fixed number of frames
// through the real System::step() master-clock loop (interpreter, per
// CLAUDE.md's "use interpreter for most tests"). No SDL/Qt/Vulkan - meant
// to be run under `perf record` to profile the whole core (CPU+PPU+APU+
// DMA+Display) under a realistic workload instead of a synthetic one.
//
// Usage: profile_bootrom <path/to/bootrom.bin|none> [frames] [cartridge.zpb] [--frontend] [--vthread]
// --vthread runs the PPU/display catch-up on a worker thread (System::setVideoThread).
// --frontend latches only the last raster of each 13 (what the Qt/SDL frontends do, see Display::setLatchEnabled).
// With a cartridge it also prints the real-time ratio of every console second (>= 1.0 holds 60 FPS).
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <path/to/bootrom.bin> [frames]\n", argv[0]);
        return 1;
    }
    uint64_t frames = (argc > 2) ? strtoull(argv[2], nullptr, 10) : 300;

    System system;
    if (std::string(argv[1]) != "none" && !system.loadBootROM(argv[1])) {   // "none" = the native (HLE) boot
        std::fprintf(stderr, "Failed to load boot ROM: %s\n", argv[1]);
        return 1;
    }
    if (argc > 3 && !system.loadROM(argv[3])) {
        std::fprintf(stderr, "Failed to load cartridge: %s\n", argv[3]);
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

    // 768 System frames is ~1 console second (68.011355 MHz / CYCLES_PER_FRAME)
    const uint64_t perSecond = static_cast<uint64_t>(68011355.0 / System::CYCLES_PER_FRAME);
    auto windowStart = std::chrono::steady_clock::now();
    bool frontend = false;
    for (int i = 4; i < argc; i++) frontend |= std::string(argv[i]) == "--frontend";
    for (int i = 4; i < argc; i++) if (std::string(argv[i]) == "--vthread") system.setVideoThread(true);
    uint64_t f = 0;
    for (; f < frames; f++) {
        if (system.getCPU().isHalted()) break;
        if (frontend) system.getDisplay().setLatchEnabled(f % 13 == 12);
        system.stepFrame();
        if (argc > 3 && (f + 1) % perSecond == 0) {
            const auto now = std::chrono::steady_clock::now();
            const double wall = std::chrono::duration<double>(now - windowStart).count();
            std::printf("console s %3llu: %.2fx real time (%.1f FPS)\n", (unsigned long long)((f + 1) / perSecond),
                        1.0 / wall, 60.0 / wall);
            windowStart = now;
        }
    }

    std::printf("Ran %llu frames before %s.\n", (unsigned long long)f,
                system.getCPU().isHalted() ? "CPU halt" : "frame limit");
    return 0;
}
