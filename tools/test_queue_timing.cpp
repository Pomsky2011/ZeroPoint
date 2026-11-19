// Exact timing of the PPU's tile-row queue and blit channels, checked cycle by cycle against the rules in docs/ppu.md
// (not against another run of the same code, which is all the fuzzers can offer): a cell is dispatched the cycle after
// a launch finds the queue empty and TQ_DISPATCH_CYCLES + 1 cycles after the one before it, a blit finishes
// `8 + 32 * rows` cycles after its dispatch (opaque), and a queued blit finishes strictly after every older in-flight
// blit it overlaps - including when it would have finished on the very same cycle, where the channel order alone would
// have let the older one draw last.
#include "system.h"
#include <cstdio>
#include <algorithm>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static const uint16_t PAL = 0x5000, MAP = 0x6000;
static const uint16_t F_CLIP = 0x01, F_MODE2 = 0x20;

static uint16_t palEntry(int i) {
    const uint16_t c = static_cast<uint16_t>((((i * 37) & 31) << 11) | (((i * 11) & 31) << 6) | (((i * 5) & 31) << 1));
    return (i % 16 == 0) ? c : (c | 1);
}
static uint8_t tilePx(int id, int p) { return static_cast<uint8_t>(((id * 7 + p * 3 + 1) & 0xFF) | 1); }

static void w16(PPU& ppu, uint16_t addr, uint16_t v) { ppu.writeMemory(addr, v & 0xFF); ppu.writeMemory(addr + 1, v >> 8); }
static uint16_t r16(const PPU& ppu, uint16_t addr) { return ppu.readMemory(addr) | (ppu.readMemory(addr + 1) << 8); }

static void setup(System& sys) {
    sys.getDisplay().setRenderMode(RenderMode::RGBA16);
    PPU& ppu = sys.getPPU();
    for (int i = 0; i < 256; i++) w16(ppu, PAL + 2 * i, palEntry(i));
    static const uint8_t mini[] = {0xFD, 0x64, 0xF1, 0x6A, 0xF7, 0x00};          // SETTILEBANK R25, SETTILE R26 mode 2, JMR (parked at 4)
    for (int id = 0; id < 8; id++) {
        ppu.loadMicrocode(mini, sizeof mini, 0);
        ppu.setRegister(25, 0); ppu.setRegister(26, static_cast<uint16_t>(id));
        ppu.setRegister(62, 4);
        ppu.setExecutionPointer(0);
        if (ppu.getState() != PPUState::Running) ppu.start();
        for (int i = 0; i < 12; i++) ppu.tick();
        for (int p = 0; p < 64; p++) ppu.writeMemory(0x0300 + p, tilePx(id, p));
    }
    static const uint8_t pal[] = {0xF2, 0x08, 0xF6, 0x00, 0xF7, 0x00};          // SETDP R2, PALETTE256, JMR (parked at 4)
    ppu.loadMicrocode(pal, sizeof pal, 0);
    ppu.setRegister(2, PAL); ppu.setRegister(62, 4);
    ppu.setExecutionPointer(0);
    for (int i = 0; i < 400; i++) ppu.tick();
}

static void cell(PPU& ppu, uint16_t addr, int id) {
    w16(ppu, addr, static_cast<uint16_t>(id));
    w16(ppu, addr + 2, 0x1000);
}

static void launch(PPU& ppu, uint16_t src, int count, int x, int y, uint16_t flags, int clip0 = 0, int clip1 = 0) {
    w16(ppu, 0x0350, src); w16(ppu, 0x0352, static_cast<uint16_t>(count)); w16(ppu, 0x0354, static_cast<uint16_t>(x));
    w16(ppu, 0x0356, static_cast<uint16_t>(y)); w16(ppu, 0x0358, static_cast<uint16_t>(clip0)); w16(ppu, 0x035A, static_cast<uint16_t>(clip1));
    w16(ppu, 0x035C, flags);
    w16(ppu, 0x035E, r16(ppu, 0x0366));
    w16(ppu, 0x0360, 1);
}

static void runTo(System& sys, int scanline) {
    for (int g = 0; sys.getDisplay().getCurrentScanline() != scanline && g < 3 * 88740; g++) sys.step();
}

static uint16_t px(System& sys, int x, int y) { return Display::color32To16(sys.getDisplay().getPixel(x, y)); }

int main() {
    const int Y = 104, FILL_X = 200, X = 16, PX = X + 3;   // the tiles' rows are inside the window at scanline 100

    std::printf("== a cell is dispatched the cycle after a launch finds the queue empty, then every 9 cycles\n");
    {
        System sys;
        setup(sys);
        PPU& ppu = sys.getPPU();
        for (int i = 0; i < 3; i++) cell(ppu, MAP + 4 * i, 2 + i);
        runTo(sys, 100);
        launch(ppu, MAP, 3, 8, Y, F_MODE2 | F_CLIP, Y, Y + 1);
        int at[3] = {-1, -1, -1};
        for (int n = 1; n <= 40; n++) {
            sys.step();
            const int pending = ppu.readMemory(0x0360);
            for (int k = 0; k < 3; k++) if (pending == 2 - k && at[k] < 0) at[k] = n;
        }
        check(at[0] == 1, "first cell: the cycle after the launch");
        check(at[1] == 10, "second cell: 9 cycles after the first");
        check(at[2] == 19, "third cell: 9 cycles after the second");
    }

    // F takes channel 0 and finishes early; A (eight rows, 264 cycles) takes channel 1; B, one clipped row (40 cycles) over the
    // same pixels, is launched so that it is dispatched `lag` cycles after A + 224: it would finish on cycle A.done + lag.
    // It must finish after A whatever `lag`, and it takes channel 0, so on lag = 0 only the overlap rule (not the channel
    // order, which would make it draw first) puts it on top.
    for (const int lag : {-2, -1, 0, 1, 2}) {
        std::printf("== a blit that would finish %d cycle(s) %s the older one it overlaps finishes strictly after it\n",
                    lag < 0 ? -lag : lag, lag < 0 ? "before" : lag == 0 ? "with" : "after");
        System sys;
        setup(sys);
        PPU& ppu = sys.getPPU();
        cell(ppu, MAP + 0, 1); cell(ppu, MAP + 4, 2); cell(ppu, MAP + 8, 3);   // F, A, B
        runTo(sys, 100);
        launch(ppu, MAP + 0, 1, FILL_X, Y, F_MODE2 | F_CLIP, Y, Y + 1);
        int n = 0, dF = -1, dA = -1, dB = -1;
        auto stepOne = [&]() { sys.step(); n++; };
        while (dF < 0 && n < 100) { stepOne(); if (ppu.readMemory(0x0360) == 0) dF = n; }
        launch(ppu, MAP + 4, 1, X, Y, F_MODE2);
        while (dA < 0 && n < 200) { stepOne(); if (ppu.readMemory(0x0360) == 0) dA = n; }
        check(dA == dF + 1, "A is dispatched the cycle after its launch");
        const int doneA = dA + 264, wantB = dA + 224 + lag;
        while (n < wantB - 1) stepOne();
        launch(ppu, MAP + 8, 1, X, Y, F_MODE2 | F_CLIP, Y, Y + 1);
        while (dB < 0 && n < wantB + 20) { stepOne(); if (ppu.readMemory(0x0360) == 0) dB = n; }
        check(dB == wantB, "B is dispatched on the cycle the test aimed at");
        const uint16_t colA = palEntry(tilePx(2, 3));   // tile row 0, column 3
        const uint16_t colB = palEntry(tilePx(3, 3));
        check(colA != colB, "the two tiles differ at the pixel watched");
        int sawA = -1, sawB = -1;
        while (n < doneA + 10) {
            stepOne();
            const uint16_t p = px(sys, PX, Y);
            if (p == colA && sawA < 0) sawA = n;
            if (p == colB && sawB < 0) sawB = n;
        }
        const int wantDoneB = std::max(dB + 40, doneA + 1);
        check(sawA == doneA, "A is drawn on cycle dispatch + 264");
        check(sawB == wantDoneB, "B is drawn on cycle max(dispatch + 40, A.done + 1), over A");
        check(px(sys, PX, Y) == colB && px(sys, PX, Y + 3) == palEntry(tilePx(2, 3 * 8 + 3)), "B's row is on top of A, A's other rows are untouched");
    }

    std::printf(failures ? "FAILED: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
