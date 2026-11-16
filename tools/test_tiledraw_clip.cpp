// TILEDRAW's x is signed 16-bit: a tile can hang off the left edge (clipped per pixel), the right edge already did,
// and one wholly off either side draws nothing. Checked on the async channel path, the all-channels-busy
// synchronous fallback, and the blended path.
#include "ppu.h"
#include "display.h"
#include <cstdio>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

// Nine TILEDRAWs back to back: eight fill every async channel, the ninth takes the synchronous fallback.
static const int kDraws = 9;

// Draws tile 0 (column tx = palette index tx + 1, every row) at (x, y) and returns after the blits complete.
// `blend` turns on blend mode 1 through the VOC translucency port first.
static void drawTile(Display& display, int16_t x, uint16_t y, bool blend) {
    PPU ppu;
    ppu.setDisplay(&display);
    display.setRenderMode(RenderMode::RGBA16);

    for (int i = 0; i < 16; i++) {   // opaque colors, all distinct
        const uint16_t c = static_cast<uint16_t>(((i * 2) << 11) | ((31 - i * 2) << 6) | ((i * 2) << 1) | 1);
        ppu.writeMemory(0x9000 + 2 * i, c & 0xFF);
        ppu.writeMemory(0x9001 + 2 * i, c >> 8);
    }
    for (int row = 0; row < 8; row++)
        for (int k = 0; k < 4; k++)
            ppu.writeMemory(0x0300 + row * 4 + k, static_cast<uint8_t>(((2 * k + 1) << 4) | (2 * k + 2)));

    if (blend) {
        ppu.writeMemory(0x00FC, 0x01);   // blend mode 1
        ppu.writeMemory(0x00FE, 0xFF);   // tiles 0/1 fully opaque
        ppu.writeMemory(0x00FF, 0xFF);
        ppu.writeMemory(0x00FD, 0x08);   // push
    }

    ppu.writeMemory(0x0200, static_cast<uint16_t>(x) & 0xFF); ppu.writeMemory(0x0201, static_cast<uint16_t>(x) >> 8);
    ppu.writeMemory(0x0202, y & 0xFF);                        ppu.writeMemory(0x0203, y >> 8);

    uint8_t prog[2 * kDraws];
    for (int i = 0; i < kDraws; i++) { prog[2 * i] = 0xFC; prog[2 * i + 1] = 0x00; }   // TILEDRAW
    ppu.loadMicrocode(prog, sizeof prog);
    ppu.start();
    for (int i = 0; i < 600; i++) ppu.tick();
}

static Color32 px(Display& d, int x, int y) { return d.getPixel(x, y); }

int main() {
    const uint16_t y = 3;

    for (int blend = 0; blend < 2; blend++) {
        std::printf("== %s ==\n", blend ? "blended" : "opaque");

        // the same tile fully on screen: column tx's color is the reference for it (blending is not the identity
        // when a draw lands on its own earlier pixels, so the blended pass needs its own reference)
        Display ref;
        drawTile(ref, 100, y, blend);
        Color32 col[8];
        for (int tx = 0; tx < 8; tx++) col[tx] = px(ref, 100 + tx, y);
        check(col[0] != col[1] && col[3] != col[4] && col[7] != col[0], "tile columns are distinguishable");
        check(px(ref, 99, y) != col[0] && px(ref, 108, y) != col[7], "an on-screen tile stays inside its 8 columns");

        Display d;
        drawTile(d, -4, y, blend);   // 0xFFFC
        check(px(d, 0, y) == col[4] && px(d, 3, y) == col[7], "x = -4: columns 4-7 land at x = 0-3");
        check(px(d, 4, y) != col[0] && px(d, 4, y) != col[3], "x = -4: nothing wraps around past the tile's right edge");
        check(px(d, 255, y) != col[0] && px(d, 255, y) != col[3], "x = -4: nothing wraps onto the right edge");
        check(px(d, 0, y + 7) == col[4], "x = -4: the last tile row is clipped the same way");

        Display e;
        drawTile(e, 252, y, blend);
        check(px(e, 252, y) == col[0] && px(e, 255, y) == col[3], "x = 252: columns 0-3 land at x = 252-255");
        check(px(e, 0, y) != col[4] && px(e, 3, y) != col[7], "x = 252: nothing wraps onto the left edge");

        Display f;
        drawTile(f, -1, y, blend);
        check(px(f, 0, y) == col[1] && px(f, 6, y) == col[7], "x = -1: columns 1-7 land at x = 0-6");

        Display g;
        drawTile(g, -8, y, blend);
        bool any = false;
        for (int x = 0; x < 16; x++) for (int tx = 0; tx < 8; tx++) if (px(g, x, y) == col[tx]) any = true;
        check(!any, "x = -8: wholly off the left edge draws nothing");

        Display h;
        drawTile(h, -2, y, blend);
        check(px(h, 0, y) == col[2] && px(h, 5, y) == col[7], "x = -2: columns 2-7 land at x = 0-5");
    }

    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
