// Regression test for the APU clock: normal mode (master/16, 2 cycles per instruction, ~2.1 MIPS), the opt-in fast
// mode (APUCLK $D8001A bit 0: master/8, ~4.25 MIPS) and its price, bus contention on the bank $A0 window (CPU wait
// states, DMA into/out of it at half rate). Also that the batched scheduler (runCycles) stays state-identical to
// step()ing through a mode switch.
#include "system.h"
#include <cstdint>
#include <cstdio>

using namespace ZeroPoint;

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

static const uint32_t APUCLK = 0xD8001A;

// Every word of a fresh APU's RAM is $0000 = NOP with no stall, so an APU that starts at $0100 and is never given a
// program just burns one instruction per instruction slot: a pure rate meter.
static void startNopRunner(System& sys) {
    sys.getAPU().setHalted(false);
    sys.getAPU().setPC(0x0100);
}

static uint64_t apuInstructions(System& sys, uint64_t masterCycles) {
    const uint64_t before = sys.getAPU().getInstructionCount();
    for (uint64_t i = 0; i < masterCycles; i++) sys.step();
    return sys.getAPU().getInstructionCount() - before;
}

// Master cycles until the single queued DMA transfer has finished.
static uint64_t dmaCycles(System& sys, uint32_t src, uint32_t dst, int pages) {
    const uint8_t cfg[9] = {0x00, uint8_t(src), uint8_t(src >> 8), uint8_t(src >> 16),
                            uint8_t(dst), uint8_t(dst >> 8), uint8_t(dst >> 16), uint8_t(pages), 0x01};
    sys.getDMA().queueDMA(cfg);
    uint64_t n = 0;
    do { sys.step(); n++; } while ((sys.getDMA().getActiveChannelCount() || sys.getDMA().getQueuedCount()) && n < 10000000);
    return n;
}

int main() {
    std::printf("normal mode: 2 cycles per instruction, ~2.1 MIPS\n");
    {
        System sys;
        sys.reset();
        startNopRunner(sys);
        check(!sys.getAPU().isFastMode(), "powers up in normal mode");
        const uint64_t n = apuInstructions(sys, 1600000);   // 1.6M master cycles = 100000 APU cycles = 50000 instructions
        check(n >= 49990 && n <= 50010, "50000 instructions per 1.6M master cycles");
    }

    std::printf("fast mode: ~4.25 MIPS through APUCLK\n");
    {
        System sys;
        sys.reset();
        startNopRunner(sys);
        check(sys.getCPU().readByte(APUCLK) == 0, "APUCLK reads 0 in normal mode");
        sys.getCPU().writeByte(APUCLK, 0x01);
        check(sys.getAPU().isFastMode() && sys.getCPU().readByte(APUCLK) == 1, "writing bit 0 selects fast mode, reads back");
        const uint64_t n = apuInstructions(sys, 1600000);
        check(n >= 99980 && n <= 100020, "100000 instructions per 1.6M master cycles (2x normal)");
        sys.getCPU().writeByte(APUCLK, 0x00);
        check(!sys.getAPU().isFastMode(), "writing 0 goes back to normal");
        sys.getCPU().writeByte(APUCLK, 0x01);
        sys.getCPU().writeByte(0xD80010, 0x01);   // APUCTRL reset
        check(!sys.getAPU().isFastMode(), "an APU reset drops back to normal mode");
    }

    std::printf("a NOP stall is still counted in cycles, on top of the 2-cycle instruction\n");
    {
        APU apu;
        apu.writeByte(0x0100, 0x00);   // NOP $0A: 2 + 10 cycles
        apu.writeByte(0x0101, 0x0A);
        apu.setHalted(false);
        apu.setPC(0x0100);
        const uint64_t c0 = apu.getCycleCount();
        apu.step();
        check(apu.getCycleCount() - c0 == 12, "NOP $0A takes 12 cycles");
    }

    std::printf("bus contention: CPU wait states on the bank $A0 window, fast mode only\n");
    {
        System sys;
        sys.reset();
        CPU& cpu = sys.getCPU();
        auto cost = [&](uint32_t addr, bool write) {
            const uint64_t c0 = cpu.getCycleCount();
            if (write) cpu.writeByte(addr, 0x55); else (void)cpu.readByte(addr);
            return cpu.getCycleCount() - c0;
        };
        check(cost(0xA03000, false) == 0 && cost(0xA03000, true) == 0, "normal mode: window access is free");
        cpu.writeByte(APUCLK, 0x01);
        check(cost(0xA03000, false) == 1, "fast mode: a window read costs 1 extra cycle");
        check(cost(0xA03000, true) == 1, "fast mode: a window write costs 1 extra cycle");
        check(cost(0x800100, false) == 0 && cost(0x800100, true) == 0, "fast mode: RAM access is unaffected");
    }

    std::printf("bus contention: DMA into the window runs at half rate in fast mode\n");
    {
        auto run = [&](bool fast, uint32_t dst) {
            System sys;
            sys.reset();
            if (fast) sys.getCPU().writeByte(APUCLK, 0x01);
            return dmaCycles(sys, 0x800000, dst, 2);   // 512 bytes
        };
        const uint64_t ramNormal = run(false, 0x802000), winNormal = run(false, 0xA03000);
        const uint64_t ramFast = run(true, 0x802000), winFast = run(true, 0xA03000);
        std::printf("    cycles: RAM %llu/%llu, window %llu/%llu (normal/fast)\n", (unsigned long long)ramNormal,
                    (unsigned long long)ramFast, (unsigned long long)winNormal, (unsigned long long)winFast);
        check(winNormal == ramNormal, "normal mode: window and RAM cost the same");
        check(ramFast == ramNormal, "fast mode: RAM-to-RAM unaffected");
        check(winFast > winNormal * 3 / 2 && winFast < winNormal * 2 + 64, "fast mode: window transfer is ~2x slower");
    }

    std::printf("the batched scheduler stays state-identical through a mode switch\n");
    {
        System a, b;
        a.reset(); b.reset();
        startNopRunner(a); startNopRunner(b);
        for (int i = 0; i < 400000; i++) a.step();
        b.runCycles(400000);
        check(a.machineHash() == b.machineHash(), "400000 cycles, normal mode");
        a.getCPU().writeByte(APUCLK, 0x01); b.getCPU().writeByte(APUCLK, 0x01);
        for (int i = 0; i < 400000; i++) a.step();
        b.runCycles(400000);
        check(a.machineHash() == b.machineHash(), "400000 more cycles, fast mode");
        a.getCPU().writeByte(APUCLK, 0x00); b.getCPU().writeByte(APUCLK, 0x00);
        for (int i = 0; i < 333333; i++) a.step();
        b.runCycles(333333);
        check(a.machineHash() == b.machineHash(), "333333 more cycles, back in normal mode");
    }

    std::printf(failures ? "%d FAILED\n" : "all APU clock tests passed (%d failures)\n", failures);
    return failures ? 1 : 0;
}
