// test_boot_jingle: boot the real ZPbootROM through System and check the boot
// jingle end to end (docs/apu/zpa.txt, "Boot ROM test plan").
//
//   test_boot_jingle <bootrom.bin> <cart.zpb|none> [--bios bios.bin] [--expect 1|2]
//                    [--max-seconds S] [--verbose]
//
// With no --bios the emulator's built-in HLE APU BIOS plays the pack; with
// --bios the assembled one (LLE) does. Checks:
//   1. the Boot ROM's upload makes the APU start the jingle (audible, MMP writes),
//   2. the engine keeps ticking (the charge-up holds) until the verify result,
//   3. the result lands in the CPU mailbox ($A0:1000): 1 = signature passed, 2 = rejected,
//   4. after it every channel is silent and the BIOS is back to waiting for a program,
//   5. a cartridge APU program ($0A header) is then accepted and runs.
//
// Time is console time: one System frame is a 262x340 raster at the 68.011355 MHz
// master clock (~1.3 ms), NOT 1/60 s, so everything below converts through the
// master clock.
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace ZeroPoint;

namespace {

constexpr double MASTER_HZ = 68011355.0;
constexpr double FRAME_SECONDS = System::CYCLES_PER_FRAME / MASTER_HZ;

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

struct Observer {
    uint64_t mmpWrites = 0;
};

void hook(void* ctx, uint16_t a, uint8_t) {
    if (a < 0x80) static_cast<Observer*>(ctx)->mmpWrites++;
}

int tickCounter(APU& apu) { return apu.readByte(0x0207) | (apu.readByte(0x0208) << 8); }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <bootrom.bin> <cart.zpb|none> [--bios bios.bin] [--expect 1|2] "
                             "[--max-seconds S] [--verbose]\n", argv[0]);
        return 2;
    }
    const char* biosPath = nullptr;
    int expect = 1;
    double maxSeconds = 40;
    bool verbose = false;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--bios") && i + 1 < argc) biosPath = argv[++i];
        else if (!std::strcmp(argv[i], "--expect") && i + 1 < argc) expect = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--max-seconds") && i + 1 < argc) maxSeconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--verbose")) verbose = true;
    }

    System system;
    if (biosPath && !system.loadAPUBios(biosPath)) return 1;
    if (!system.loadBootROM(argv[1])) { std::fprintf(stderr, "cannot load boot ROM\n"); return 1; }
    if (std::strcmp(argv[2], "none") && !system.loadROM(argv[2])) { std::fprintf(stderr, "cannot load cartridge\n"); return 1; }
    system.powerOn();

    APU& apu = system.getAPU();
    Observer obs;
    apu.setWriteHook(hook, &obs);

    std::printf("boot jingle test (%s BIOS), expecting result %d\n", biosPath ? "assembly" : "HLE", expect);

    const uint64_t maxFrames = (uint64_t)(maxSeconds / FRAME_SECONDS);
    int peak = 0;
    long resultFrame = -1, firstSoundFrame = -1;
    int resultTick = 0;
    for (uint64_t f = 0; f < maxFrames; f++) {
        system.stepFrame();
        for (int16_t s : system.getAudioBuffer()) peak = std::max(peak, std::abs((int)s));
        system.clearAudioBuffer();
        if (verbose && f % 3000 == 0)
            std::printf("  %5.2fs: tick %d engine=%d mailbox=%d mmp writes=%llu\n", f * FRAME_SECONDS, tickCounter(apu),
                        apu.hleEngineActive() ? 1 : 0, apu.readByte(0x1000), (unsigned long long)obs.mmpWrites);
        if (firstSoundFrame < 0 && obs.mmpWrites > 0) firstSoundFrame = f;
        if (resultFrame < 0 && apu.readByte(0x1000) != 0) {
            resultFrame = f;
            resultTick = tickCounter(apu);
        }
        if (resultFrame >= 0) break;
    }

    // After the result the cartridge's CPU may have halted (the template ROM's does), and
    // System::stepFrame stops everything once the CPU halts - so drive the APU directly.
    // The hold loop only polls the mailbox at its JZ events (every 0.25-0.5 s).
    double silentAfter = -1;
    auto channelsSilent = [&] {
        for (int c = 0; c < 16; c++)
            if (apu.readByte(0x50 + 2 * c) || apu.readByte(0x51 + 2 * c)) return false;
        return true;
    };
    if (resultFrame >= 0) {
        const uint64_t t0 = apu.getCycleCount();
        while (apu.getCycleCount() - t0 < 4250710 && silentAfter < 0) {   // up to one second of APU time
            apu.run(4000);
            if (channelsSilent()) silentAfter = (apu.getCycleCount() - t0) / 4250710.0;
        }
    }

    const double firstSound = firstSoundFrame * FRAME_SECONDS;
    const double resultAt = resultFrame * FRAME_SECONDS;
    std::printf("jingle starts at %.2fs, verify result at %.2fs (engine tick %d), silent %.2fs after it, audio peak %d\n",
                firstSound, resultAt, resultTick, silentAfter, peak);

    check(firstSoundFrame >= 0 && firstSound < 2.0, "the jingle starts within 2 s of power-on");
    check(peak > 1000, "the mixer produced audible sound");
    check(resultFrame >= 0, "the Boot ROM reported a verify result");
    if (resultFrame >= 0) {
        check(apu.readByte(0x1000) == expect, expect == 1 ? "result is 1 (signature passed)" : "result is 2 (signature rejected)");
        // 120 ticks/s from ~0.15 s in: the charge-up hold keeps it ticking right up to the result
        // (a restart or stall would show as far fewer ticks).
        const double wantTicks = (resultAt - firstSound) * 120.0;
        check(resultTick >= 0.97 * wantTicks - 10 && resultTick <= 1.03 * wantTicks + 10,
              "the engine kept ticking at 120 Hz until the result (the charge-up held)");
        check(silentAfter >= 0, "every channel's STL address went to 0 after the result");
        check(silentAfter < 0.7, "the music stopped within 0.7 s of the result");
    }
    check(apu.getPC() >= 0x8000 && apu.getPC() < 0x9000 && !apu.hleEngineActive(),
          "the APU is back in the BIOS idle loop, not the engine");

    // The cartridge's turn: a $0A program header, as a game's CPU code would upload it.
    //   sdp $10 ; scr x,$42 ; sta x,$10 ; hlt        -> writes $42 to $1010
    const uint8_t prog[8] = {0x1C, 0x10, 0x50, 0x42, 0x42, 0x10, 0xE0, 0x00};
    uint8_t sum = 0;
    for (int i = 0; i < 8; i++) { apu.writeByte(0x3020 + i, prog[i]); sum += prog[i]; }
    for (int i = 8; i < 0x3000 - 0x20; i++) apu.writeByte(0x3020 + i, 0);
    apu.writeByte(0x3004, 0x20);
    apu.writeByte(0x3005, 0x30);
    apu.writeByte(0x301F, sum);
    apu.writeByte(0x3000, 0x0A);
    apu.run(1500000);   // the BIOS's checksum scan alone is ~590k APU cycles
    check(apu.readByte(0x1010) == 0x42, "a cartridge APU program ($0A header) is accepted and runs");
    check(apu.readByte(0x3000) == 0, "the BIOS zeroed the header magic");

    std::printf("%s\n", failures ? "FAILED" : "PASS: boot jingle end to end");
    return failures ? 1 : 0;
}
