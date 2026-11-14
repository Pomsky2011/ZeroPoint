// test_boot_handoff: boot the real ZPbootROM with two small signed cartridges and check what it does with
// the PPU around the handoff (ZPbootROM/def88186/handoff.def).
//
//   test_boot_handoff <bootrom.bin> <noppu.zpb> <withppu.zpb>
//
// noppu.zpb spins and has no PPU program: the splash must fade to black before the CPU gets there, and the PPU
// must be left idle and the screen black. withppu.zpb loads a PPU program from its ROM with COP #$F7; the
// program must be running and must have written its marker ($1234 at PPU $9000).
// Time is console time (a System frame is ~1.3 ms, not 1/60 s).
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <vector>

using namespace ZeroPoint;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

// Sum of every pixel's channel bytes over the latched output frame, /1000 (a black frame is exactly 0).
uint64_t brightness(System& sys) {
    static std::vector<uint32_t> row(FB_WIDTH);
    uint64_t sum = 0;
    for (int y = 0; y < FULL_HEIGHT; y += 2) {
        if (!sys.getDisplay().getScanlineSDL(y, row.data())) continue;
        for (uint32_t px : row) sum += (px & 0xFF) + ((px >> 8) & 0xFF) + ((px >> 16) & 0xFF);
    }
    return sum;
}

struct Sample { double t; uint64_t b; };

struct Result {
    bool handedOff = false;
    double handoffAt = 0;
    std::vector<Sample> before;     // brightness samples up to the handoff
    uint64_t afterBrightness = ~0ull;
    bool ppuRunning = false;
    int marker0 = -1, marker1 = -1;
};

Result run(const char* bootPath, const char* romPath) {
    System sys;
    Result r;
    if (!sys.loadBootROM(bootPath) || !sys.loadROM(romPath)) { std::fprintf(stderr, "cannot load\n"); std::exit(1); }
    sys.powerOn();
    const double frameSeconds = System::CYCLES_PER_FRAME / 68011355.0;
    const double limit = 40.0;
    double nextSample = 0;
    for (uint64_t f = 0; f * frameSeconds < limit; f++) {
        sys.stepFrame();
        const double t = f * frameSeconds;
        if (!r.handedOff) {
            if (sys.getCPU().getPB() == 0x00) { r.handedOff = true; r.handoffAt = t; }
            else if (t >= nextSample) { r.before.push_back({t, brightness(sys)}); nextSample = t + 0.01; }
        } else if (t >= r.handoffAt + 0.25) {
            break;
        }
    }
    r.afterBrightness = brightness(sys);
    r.ppuRunning = sys.getPPU().getState() == PPUState::Running;
    r.marker0 = sys.getCPU().readByte(0xB09000);
    r.marker1 = sys.getCPU().readByte(0xB09001);
    return r;
}

void checkFade(const Result& r) {
    check(r.handedOff, "the cartridge got control");
    uint64_t maxB = 0;
    for (const auto& s : r.before) maxB = std::max(maxB, s.b);
    check(maxB > 0, "the splash was visible before the handoff");
    // The splash's own lightning strobe sweeps through intermediate brightness all the time, so "went through
    // intermediate levels" proves nothing. A fade is what the strobe is not: monotonically falling. Over the
    // last 0.3 s before the handoff (the fade is ~0.33 s) brightness must never rise, and must end near black.
    std::vector<Sample> tail;
    for (const auto& s : r.before)
        if (s.t >= r.handoffAt - 0.3) tail.push_back(s);
    int rises = 0;
    for (size_t i = 1; i < tail.size(); i++)
        if (tail[i].b * 100 > tail[i - 1].b * 103) rises++;
    const uint64_t first = tail.empty() ? 0 : tail.front().b, last = tail.empty() ? 0 : tail.back().b;
    std::printf("  (peak brightness %llu; last 0.3 s: %zu samples, %llu -> %llu, %d rises; %.2f s to handoff)\n",
                (unsigned long long)maxB, tail.size(), (unsigned long long)first, (unsigned long long)last, rises, r.handoffAt);
    check(tail.size() >= 10 && rises == 0, "brightness only falls over the last 0.3 s before the handoff");
    check(first > 3 * last && last * 20 < maxB, "and it ends near black (a fade, not the strobe)");
    check(r.afterBrightness == 0, "the screen is black once the cartridge runs");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s <bootrom.bin> <noppu.zpb> <withppu.zpb>\n", argv[0]); return 2; }

    std::printf("cartridge with no PPU program\n");
    Result a = run(argv[1], argv[2]);
    checkFade(a);
    check(!a.ppuRunning, "the PPU is idle (the splash is not left running)");

    std::printf("cartridge that loads a PPU program with COP #$F7\n");
    Result b = run(argv[1], argv[3]);
    check(b.handedOff, "the cartridge got control");
    check(b.ppuRunning, "its PPU program is running");
    std::printf("  (PPU $9000 = %02X %02X)\n", b.marker0, b.marker1);
    check(b.marker0 == 0x34 && b.marker1 == 0x12, "it wrote its marker ($1234) into PPU memory");

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
