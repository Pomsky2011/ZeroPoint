// Boots a boot ROM + cartridge headlessly and dumps latched output frames as raw RGBA bytes (R,G,B,A per pixel,
// 256x256), for looking at the splash without a display (convert with tools/splash_frames.py).
//   dump_splash <bootrom.bin> <cart.zpb> <outprefix> <frame> [<frame>...]
// Writes <outprefix>_<frame>.rgba for each requested frame number (0-based count of stepFrame() calls).
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <bootrom.bin> <cart.zpb> <outprefix> <frame> [<frame>...]\n", argv[0]);
        return 2;
    }
    std::vector<int> frames;
    for (int i = 4; i < argc; i++) frames.push_back(std::atoi(argv[i]));
    const int last = *std::max_element(frames.begin(), frames.end());

    System sys;
    // "none" boots through the native HLE boot ROM (no splash), which verifies and runs the cartridge directly
    const bool hle = std::string(argv[1]) == "none";
    if ((!hle && !sys.loadBootROM(argv[1])) || !sys.loadROM(argv[2])) { std::fprintf(stderr, "cannot load\n"); return 1; }
    sys.powerOn();
    if (std::getenv("ZP_IDLE_STATS")) sys.setIdlePollSkip(true);   // run the idle-poll detector (see the report at the end)

    std::vector<Color32> row(FB_WIDTH);
    for (int f = 0; f <= last; f++) {
        sys.stepFrame();
        if (std::find(frames.begin(), frames.end(), f) == frames.end()) continue;
        char path[512];
        std::snprintf(path, sizeof path, "%s_%d.rgba", argv[3], f);
        FILE* out = std::fopen(path, "wb");
        if (!out) { std::fprintf(stderr, "cannot write %s\n", path); return 1; }
        for (int y = 0; y < 256; y++) {
            if (!sys.getDisplay().getScanline(y, row.data())) std::fill(row.begin(), row.end(), 0u);
            for (Color32 px : row) {
                const uint8_t b[4] = {uint8_t(px >> 24), uint8_t(px >> 16), uint8_t(px >> 8), uint8_t(px)};
                std::fwrite(b, 1, 4, out);
            }
        }
        std::fclose(out);
        const PPU& ppu = sys.getPPU();
        std::printf("wrote %s  (PPU %s EP=$%04X R44=%u R51=%u R63=$%04X, CPU PB=$%02X)\n", path,
                    ppu.getState() == PPUState::Running ? "running" : "NOT running", ppu.getExecutionPointer(),
                    ppu.getRegister(44), ppu.getRegister(51), ppu.getRegister(63), sys.getCPU().getPB());
        if (const char* spec = std::getenv("ZP_DUMP_PPU")) {   // ZP_DUMP_PPU=<hex addr>:<count>: print that PPU memory
            unsigned addr = 0, n = 16;
            std::sscanf(spec, "%x:%u", &addr, &n);
            std::printf("   PPU[%04X..]:", addr);
            for (unsigned i = 0; i < n; i++) std::printf(" %02X", sys.getPPU().readMemory(static_cast<uint16_t>(addr + i)));
            std::printf("\n");
        }
        std::printf("   CPU PC=$%04X  PPU $9300(req)=%u $9301(ack)=%u R28(tiles left)=%u SP=$%04X\n",
                    sys.getCPU().getPC(), ppu.readMemory(0x9300), ppu.readMemory(0x9301), ppu.getRegister(28), ppu.getRegister(61));
    }
    if (std::getenv("ZP_IDLE_STATS")) {
        const IdleStats& st = sys.idleStats();
        std::printf("idle detector: %llu loops confirmed, %llu iterations = %llu CPU cycles (%llu unwatchable, %llu of %llu total CPU cycles)\n",
                    (unsigned long long)st.loopsConfirmed, (unsigned long long)st.iterations, (unsigned long long)st.cpuCycles,
                    (unsigned long long)st.cpuCyclesUnwatchable, (unsigned long long)st.cpuCycles, (unsigned long long)sys.getCPU().getCycleCount());
        std::printf("  skipped: %llu skips, %llu iterations = %llu CPU cycles\n", (unsigned long long)st.skips,
                    (unsigned long long)st.skippedIterations, (unsigned long long)st.skippedCpuCycles);
        static const char* const names[] = {"rom", "workram", "apuram", "ppumem", "timer", "input", "ppuport", "ppufb", "other"};
        for (int i = 0; i < static_cast<int>(WatchClass::COUNT); i++)
            std::printf("  %-8s %llu\n", names[i], (unsigned long long)st.cpuCyclesByClass[i]);
        std::printf("  ended by: %llu changed byte, %llu interrupt, %llu cycle drift, %llu unexplained\n", (unsigned long long)st.woke,
                    (unsigned long long)st.interrupted, (unsigned long long)st.cycleDrift, (unsigned long long)st.unexplained);
    }
    return 0;
}
