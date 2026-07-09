#include <cstdlib>
// Microbenchmark for PPU::tick().
// Fills PPU memory with one instruction class per scenario and lets the 16-bit
// execution pointer wrap, producing a steady stream of that instruction with no
// loop-branch noise. vblank/hblank stay false, so we measure the normal hot path.
#include "ppu.h"
#include "display.h"
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace ZeroPoint;
using clk = std::chrono::steady_clock;

// Build a 16-bit big-endian instruction word.
static inline void fill(PPU& ppu, uint16_t word) {
    uint8_t hi = word >> 8, lo = word & 0xFF;
    for (uint32_t a = 0; a < 65536; a += 2) {
        ppu.writeMemory((uint16_t)a, hi);
        ppu.writeMemory((uint16_t)(a + 1), lo);
    }
}

static double run(const char* name, uint16_t word, uint64_t iters,
                  Display* disp, bool setRegs = false, bool tiledraw = false) {
    PPU ppu;
    if (disp) ppu.setDisplay(disp);
    fill(ppu, word);
    ppu.start();
    if (setRegs) {
        // Give ALU operands nonzero values (avoids div-by-zero skip, real work).
        for (uint8_t r = 0; r < 58; r++) ppu.setRegister(r, (uint16_t)(r + 1) * 7);
    }
    if (tiledraw) {
        // TILEDRAW reads the draw position fresh each tick from $0200-$0203.
        // The memory fill overwrote it, so set a valid in-window position
        // (x=100, y=0) after filling. Tile 0 / mode 0 (16-bit 4bpp) exercise
        // the full 64-pixel palette + setPixel32 loop regardless of contents.
        ppu.writeMemory(0x0200, 100); ppu.writeMemory(0x0201, 0);  // x = 100
        ppu.writeMemory(0x0202, 0);   ppu.writeMemory(0x0203, 0);  // y = 0
    }
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++) ppu.tick();
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    double per = ns / (double)iters;
    // Consume a register so the loop can't be elided.
    volatile uint16_t sink = ppu.getRegister(2);
    (void)sink;
    printf("  %-22s %7.3f ns/tick   %8.1f Mticks/s\n", name, per, 1000.0 / per);
    return per;
}

int main(int argc, char** argv) {
    uint64_t N = 300'000'000ULL;
    if (argc > 1) N = strtoull(argv[1], nullptr, 10);
    Display disp;  // real display so TILEDRAW/MOVDP paths are valid

    printf("PPU::tick() microbenchmark  (%llu ticks/scenario, -O3)\n\n",
           (unsigned long long)N);
    printf("Instruction dispatch cost by class:\n");

    // opcode<<12 | operand.  regX in bits 11-6, regY in bits 5-0.
    const uint16_t rXrY = (2u << 6) | 3u;      // r2, r3
    run("NOP (0x1)",        0x1800,          N, nullptr);
    run("INC r2 (0x8)",     0x8000 | 2,      N, nullptr, true);
    run("ADD r2,r3 (0xA)",  0xA000 | rXrY,   N, nullptr, true);
    run("SUB r2,r3 (0xB)",  0xB000 | rXrY,   N, nullptr, true);
    run("MUL r2,r3 (0xC)",  0xC000 | rXrY,   N, nullptr, true);
    run("INTDIV r2,r3 (0xD)",0xD000 | rXrY,  N, nullptr, true);
    run("CMP r2,r3 (0x4)",  0x4000 | rXrY,   N, nullptr, true);
    run("SWAPREG r2,r3 (0x2)",0x2000 | rXrY, N, nullptr, true);
    // Preset E: CPREG (sub 0x3): copy target reg. Preset F: SETDP (sub 0x2).
    run("PRESET_E CPREG (0xE)", 0xE000 | (0x3 << 10), N, nullptr, true);
    run("PRESET_F SETDP (0xF)", 0xF000 | (0x2 << 8),  N, nullptr, true);

    // TILEDRAW is the real per-frame heavy path: one instruction draws a full
    // 8x8 tile (64 pixels). Far fewer iters since each tick does ~64x the work.
    printf("\nHeavy path:\n");
    run("PRESET_F TILEDRAW (0xF)", 0xF000 | (0xC << 8), N / 32, &disp, false, true);

    return 0;
}
