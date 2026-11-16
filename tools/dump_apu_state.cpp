// Boots a cartridge (with the real boot ROM, or "none" for the HLE boot) and dumps the APU's whole 64 KiB address
// space plus a few registers some console time after the cartridge takes over, to diff what the boot leaves behind.
//   dump_apu_state <bootrom.bin|none> <cart.zpb> <seconds-after-handoff> <out.bin>
// Handoff = the CPU's program bank first reads $00 (the cartridge); with "none" the boot is native, so the clock
// starts at power-on instead.
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <bootrom.bin|none> <cart.zpb> <seconds-after-handoff> <out.bin>\n", argv[0]);
        return 2;
    }
    const bool hle = !std::strcmp(argv[1], "none");
    System sys;
    if ((!hle && !sys.loadBootROM(argv[1])) || !sys.loadROM(argv[2])) { std::fprintf(stderr, "cannot load\n"); return 1; }
    sys.powerOn();
    const double frameSeconds = System::CYCLES_PER_FRAME / 68011355.0;
    const double after = std::atof(argv[3]);
    double handoffAt = hle ? 0.0 : -1.0;
    for (uint64_t f = 0; f * frameSeconds < 120.0; f++) {
        sys.stepFrame();
        const double t = f * frameSeconds;
        if (handoffAt < 0 && sys.getCPU().getPB() == 0x00) { handoffAt = t; std::printf("handoff at %.3f s\n", t); }
        if (handoffAt >= 0 && t >= handoffAt + after) break;
    }
    APU& apu = sys.getAPU();
    std::vector<uint8_t> mem(65536);
    for (int a = 0; a < 65536; a++) mem[a] = apu.readByte(static_cast<uint16_t>(a));
    FILE* out = std::fopen(argv[4], "wb");
    if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[4]); return 1; }
    std::fwrite(mem.data(), 1, mem.size(), out);
    std::fclose(out);
    std::printf("APU pc=$%04X sp=$%04X fast=%d hleEngine=%d; wrote %s\n", apu.getPC(), apu.getSP(), apu.isFastMode(),
                apu.hleEngineActive(), argv[4]);
    return 0;
}
