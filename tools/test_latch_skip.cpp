// test_latch_skip: a frontend that latches only the last raster of each batch it steps (Display::setLatchEnabled)
// must show exactly the frame one that latches every raster shows, and must not disturb the machine.
//
//   test_latch_skip <bootrom.bin> <rom.zpb> [batches] [rasters-per-batch]
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <bootrom.bin> <rom.zpb> [batches] [rasters-per-batch]\n", argv[0]);
        return 2;
    }
    const int batches = argc > 3 ? std::atoi(argv[3]) : 400;
    const int perBatch = argc > 4 ? std::atoi(argv[4]) : 13;

    // One System at a time (test_determinism covers several at once).
    struct Shown { uint64_t frame; uint64_t ppu; uint64_t pc; uint64_t cycles; };
    auto run = [&](bool skip) {
        System sys;
        std::vector<Shown> out;
        if (!sys.loadBootROM(argv[1]) || !sys.loadROM(argv[2])) std::exit(2);
        sys.powerOn();
        std::vector<Color32> row(FB_WIDTH);
        for (int n = 0; n < batches; n++) {
            for (int i = 0; i < perBatch; i++) {
                sys.getDisplay().setLatchEnabled(!skip || i + 1 == perBatch);
                sys.stepFrame();
            }
            uint64_t h = 1469598103934665603ull;
            for (int y = 0; y < FULL_HEIGHT; y++) {
                sys.getDisplay().getScanline(y, row.data());
                const uint8_t* p = reinterpret_cast<const uint8_t*>(row.data());
                for (size_t k = 0; k < FB_WIDTH * sizeof(Color32); k++) h = (h ^ p[k]) * 1099511628211ull;
            }
            out.push_back({h, sys.getPPU().stateHash(), sys.getCPU().getPC(), sys.getCPU().getCycleCount()});
        }
        return out;
    };
    const std::vector<Shown> full = run(false);
    const std::vector<Shown> skipped = run(true);
    for (int n = 0; n < batches; n++) {
        if (full[n].frame != skipped[n].frame) { std::printf("FAIL: batch %d shows a different frame\n", n); return 1; }
        if (full[n].ppu != skipped[n].ppu || full[n].pc != skipped[n].pc || full[n].cycles != skipped[n].cycles) {
            std::printf("FAIL: batch %d: machine state differs\n", n);
            return 1;
        }
    }
    std::printf("ok  : %d batches of %d rasters show identical frames\n", batches, perBatch);
    return 0;
}
