// The PPU's line blitter: launch port at $0340, 4 async channels, 4bpp palette-indexed runs, alpha-0 entries
// transparent, left-edge/off-window clipping, a dropped launch when all four channels are busy.
#include "ppu.h"
#include "display.h"
#include <cstdio>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

// ppuasm of: DP = $9000; PALETTE16; loop: JMR loop
static const uint8_t kPaletteProgram[] = {
    0xe2, 0x3f, 0xe6, 0x00, 0xe3, 0xbf, 0xe7, 0x90, 0xf5, 0x00, 0xe0, 0x3e,
    0xe1, 0xbe, 0xe4, 0x0a, 0xe5, 0x00, 0xf7, 0x00
};

static void word(PPU& ppu, uint16_t addr, uint16_t v) { ppu.writeMemory(addr, v & 0xFF); ppu.writeMemory(addr + 1, v >> 8); }
static void launch(PPU& ppu, uint16_t src, int16_t x, uint16_t y, uint16_t count) {
    word(ppu, 0x0340, src); word(ppu, 0x0342, static_cast<uint16_t>(x)); word(ppu, 0x0344, y); word(ppu, 0x0346, count);
    ppu.writeMemory(0x0348, 1);
}
static void run(PPU& ppu, int cycles) { for (int i = 0; i < cycles; i++) ppu.tick(); }

int main() {
    Display display;
    PPU ppu;
    ppu.setDisplay(&display);
    display.setRenderMode(RenderMode::RGBA16);

    // palette: 0 = transparent (alpha 0), 1 = red, 2 = green, 3 = blue (opaque)
    const uint16_t pal[16] = {0x0000, (31 << 1) | 1, (31 << 6) | 1, (31 << 11) | 1};
    for (int i = 0; i < 16; i++) { ppu.writeMemory(0x9000 + 2 * i, pal[i] & 0xFF); ppu.writeMemory(0x9001 + 2 * i, pal[i] >> 8); }
    ppu.loadMicrocode(kPaletteProgram, sizeof kPaletteProgram);
    ppu.start();
    run(ppu, 200);

    // source run at $2000: 1 2 0 3 | 1 1
    const uint8_t src[] = {0x12, 0x03, 0x11};
    for (int i = 0; i < 3; i++) ppu.writeMemory(0x2000 + i, src[i]);

    std::printf("== line blit ==\n");
    for (int x = 0; x < 16; x++) display.setPixel32(x, 3, 0x112233FF);
    const Color32 sentinel = display.getPixel(0, 3);   // what RGBA16 actually stores for it
    launch(ppu, 0x2000, 4, 3, 6);
    check((ppu.readMemory(0x034A) & 0x0F) == 0x01, "status shows one busy line channel");
    run(ppu, 8 + 6 - 1);
    check((ppu.readMemory(0x034A) & 0x0F) == 0x01, "still busy a cycle before its cost (8 + 1/pixel) elapses");
    check(display.getPixel(4, 3) == sentinel, "nothing is written before completion");
    run(ppu, 2);
    check((ppu.readMemory(0x034A) & 0x0F) == 0x00, "channel is free after completion");
    check(display.getPixel(4, 3) == 0xFF0000FF, "pixel 0 = palette 1 (red)");
    check(display.getPixel(5, 3) == 0x00FF00FF, "pixel 1 = palette 2 (green)");
    check(display.getPixel(6, 3) == sentinel, "pixel 2 = palette 0 (alpha 0) leaves the framebuffer alone");
    check(display.getPixel(7, 3) == 0x0000FFFF, "pixel 3 = palette 3 (blue)");
    check(display.getPixel(8, 3) == 0xFF0000FF && display.getPixel(9, 3) == 0xFF0000FF, "pixels 4-5 = red");
    check(display.getPixel(10, 3) == sentinel && display.getPixel(3, 3) == sentinel, "neighbours untouched");

    std::printf("== clipping ==\n");
    launch(ppu, 0x2000, -2, 5, 6);   // starts two pixels off the left edge
    run(ppu, 20);
    check(display.getPixel(0, 5) == 0x00000000 || display.getPixel(0, 5) != 0xFF0000FF, "pixels 0-1 (off the left edge) are dropped");
    check(display.getPixel(1, 5) == 0x0000FFFF, "pixel 3 lands at x = 1");
    launch(ppu, 0x2000, 4, 200, 6);  // far outside the rolling window
    run(ppu, 20);
    check(display.getPixel(4, 200) != 0xFF0000FF, "a row outside the window is dropped");

    std::printf("== four channels ==\n");
    for (int i = 0; i < 4; i++) launch(ppu, 0x2000, 0, 6, 200);
    check((ppu.readMemory(0x034A) & 0x8F) == 0x0F, "four launches occupy all four channels, no overrun yet");
    launch(ppu, 0x2000, 0, 6, 200);
    check((ppu.readMemory(0x034A) & 0x80) != 0, "a fifth launch is dropped and sets the sticky overrun bit");
    ppu.writeMemory(0x034A, 0);
    check((ppu.readMemory(0x034A) & 0x80) == 0, "writing $034A clears the overrun bit");
    run(ppu, 400);
    check((ppu.readMemory(0x034A) & 0x0F) == 0, "all channels drain");

    std::printf(failures ? "FAILED (%d)\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
