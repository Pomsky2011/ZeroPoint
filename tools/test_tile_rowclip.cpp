// TILEDRAW's row clip ($00FC bit 5 = write only screen rows [$0204, $0206)): the clip itself, how it combines with
// flips/blending, the cost it saves, and what it exists for - a tile straddling a band boundary issued once per band
// composes exactly like one unclipped draw, where re-issuing it without the clip overwrites what drew after it.
#include "ppu.h"
#include "display.h"
#include <cstdio>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static const Color32 kSentinel = 0x112233FF;
static const uint8_t H = 0x08, V = 0x10, C = 0x20;   // $00FC bits: hflip, vflip, row clip

static void pal16(PPU& ppu, int i, uint16_t c) { ppu.writeMemory(0x9100 + 2 * i, c & 0xFF); ppu.writeMemory(0x9101 + 2 * i, c >> 8); }
static void word(PPU& ppu, uint16_t addr, uint16_t v) { ppu.writeMemory(addr, v & 0xFF); ppu.writeMemory(addr + 1, v >> 8); }

// One TILEDRAW of an 8bpp tile (indices base+1..base+64, all different colors) at (x, y) on `display`, in its own PPU.
static void blit(Display& display, int base, int x, int y, uint8_t attrs, int clip0, int clip1, bool blend = false) {
    PPU ppu;
    ppu.setDisplay(&display);
    for (int i = 0; i < 256; i++) pal16(ppu, i, static_cast<uint16_t>(((i & 15) << 11) | (((i >> 5) * 3 + 1) << 6) | ((i & 31) << 1) | 1));
    for (int i = 0; i < 64; i++) ppu.writeMemory(0x0300 + i, static_cast<uint8_t>(base + i + 1));
    ppu.writeMemory(0x00FC, static_cast<uint8_t>(attrs | (blend ? 0x01 : 0x00)));   // attributes + blend mode (average)
    if (blend) { ppu.writeMemory(0x00FE, 0xFF); ppu.writeMemory(0x00FF, 0xFF); }
    ppu.writeMemory(0x00FD, static_cast<uint8_t>(0x10 | (blend ? 0x08 : 0x00)));   // push the attributes (and the blend mode)
    word(ppu, 0x0200, static_cast<uint16_t>(x)); word(ppu, 0x0202, static_cast<uint16_t>(y));
    word(ppu, 0x0204, static_cast<uint16_t>(clip0)); word(ppu, 0x0206, static_cast<uint16_t>(clip1));
    // SETDP R2, PALETTE256, SETTILE R0 mode 2 (8bpp), TILEDRAW, then JMR to itself: parked, so the PPU doesn't run on
    // through zero memory and then execute the tile buffer as code
    const uint8_t prog[] = {0xF2, 0x08, 0xF6, 0x00, 0xF1, 0x02, 0xFC, 0x00, 0xF7, 0x00};
    ppu.loadMicrocode(prog, sizeof prog);
    ppu.start();
    ppu.setRegister(2, 0x9100);
    ppu.setRegister(62, 8);
    for (int i = 0; i < 1200; i++) ppu.tick();
}

static Color32 px(Display& d, int x, int y) { return d.getPixel(x, y); }

static void prefill(Display& d, int x0, int x1) {
    for (int x = x0; x < x1; x++) for (int y = 0; y < 16; y++) d.setPixel32(x, y, kSentinel);
}

int main() {
    Display d;
    d.setRenderMode(RenderMode::RGBA16);
    prefill(d, 96, 160);
    const Color32 blank = px(d, 96, 0);   // what the sentinel reads back as in RGBA16

    // reference: the unclipped tile at x = 140
    blit(d, 0, 140, 0, 0, 0, 0);
    Color32 ref[8][8];
    for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) ref[ty][tx] = px(d, 140 + tx, ty);

    std::printf("== the clip window ==\n");
    blit(d, 0, 100, 0, C, 2, 5);
    bool inside = true, outside = true;
    for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
        const Color32 got = px(d, 100 + tx, ty);
        if (ty >= 2 && ty < 5) { if (got != ref[ty][tx]) inside = false; }
        else if (got != blank) outside = false;
    }
    check(inside, "rows 2-4 of a window [2, 5) are drawn exactly like the unclipped tile");
    check(outside, "rows 0-1 and 5-7 are left alone");

    blit(d, 0, 112, 0, C | V, 2, 5);
    bool flipped = true, flipOut = true;
    for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
        const Color32 got = px(d, 112 + tx, ty);
        if (ty >= 2 && ty < 5) { if (got != ref[7 - ty][tx]) flipped = false; }
        else if (got != blank) flipOut = false;
    }
    check(flipped && flipOut, "with vflip the window still selects destination rows; they show the flipped source rows");

    blit(d, 0, 124, 0, C | H, 6, 8);
    bool hf = true;
    for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
        const Color32 got = px(d, 124 + tx, ty);
        if (ty >= 6) { if (got != ref[ty][7 - tx]) hf = false; }
        else if (got != blank) hf = false;
    }
    check(hf, "with hflip and a window at the bottom of the tile");

    blit(d, 0, 148 - 4 - 8, 8, C, 9, 10);   // x = 136: a one-row window, tile at rows 8-15
    check(px(d, 136, 9) == ref[1][0] && px(d, 136, 8) == blank && px(d, 136, 10) == blank, "a one-row window draws one row");

    Display e; e.setRenderMode(RenderMode::RGBA16); prefill(e, 96, 160);
    blit(e, 0, 100, 0, C, 5, 5);
    blit(e, 0, 110, 0, C, 6, 2);
    blit(e, 0, 120, 0, C, 20, 40);
    bool none = true;
    for (int x = 100; x < 128; x++) for (int y = 0; y < 8; y++) if (px(e, x, y) != blank) none = false;
    check(none, "an empty, an inverted and a disjoint window draw nothing");

    Display f; f.setRenderMode(RenderMode::RGBA16); prefill(f, 96, 160);
    blit(f, 0, 100, 0, C, 0, 8, true);
    Display g; g.setRenderMode(RenderMode::RGBA16); prefill(g, 96, 160);
    blit(g, 0, 100, 0, C, 3, 5, true);
    bool blendOk = true;
    for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
        const Color32 got = px(g, 100 + tx, ty);
        if (ty >= 3 && ty < 5) { if (got != px(f, 100 + tx, ty)) blendOk = false; }
        else if (got != blank) blendOk = false;
    }
    check(blendOk, "blended: rows in the window blend as usual, the rest are not touched");

    std::printf("== cost ==\n");
    {
        // default palette, 4bpp tile of index 5: one TILEDRAW, then watch when the pixels land
        auto landed = [&](uint8_t attrs, int clip0, int clip1, int ticks, int row) {
            Display dd; dd.setRenderMode(RenderMode::RGBA16);
            dd.setPixel32(10, row, kSentinel);
            const Color32 sent = dd.getPixel(10, row);
            PPU ppu; ppu.setDisplay(&dd);
            for (int i = 0; i < 32; i++) ppu.writeMemory(0x0300 + i, 0x55);
            ppu.writeMemory(0x00FC, attrs);
            ppu.writeMemory(0x00FD, 0x10);   // push the attributes
            word(ppu, 0x0200, 8); word(ppu, 0x0202, 0); word(ppu, 0x0204, static_cast<uint16_t>(clip0)); word(ppu, 0x0206, static_cast<uint16_t>(clip1));
            const uint8_t prog[] = {0xFC, 0x00, 0xF7, 0x00};   // TILEDRAW, then park on the JMR
            ppu.loadMicrocode(prog, sizeof prog);
            ppu.start();
            ppu.setRegister(62, 2);
            for (int i = 0; i < ticks; i++) ppu.tick();
            return dd.getPixel(10, row) != sent;
        };
        // 3 rows: 8 + 3*8*4 = 104 cycles; 8 rows: 8 + 64*4 = 264
        check(!landed(C, 2, 5, 90, 3) && landed(C, 2, 5, 125, 3), "a 3-row window completes after ~104 cycles");
        check(!landed(0, 0, 0, 125, 3) && landed(0, 0, 0, 285, 3), "the unclipped tile still takes ~264");
    }

    std::printf("== a tile straddling a band boundary, issued once per band ==\n");
    {
        // A (low z) straddles the row-8 band boundary at y = 4; B (high z) sits in band 0 only, at y = 0, overlapping A's
        // top half. Band 1 has to redraw A without touching band 0's rows, where B is on top of it. Rows 0-15 are in the window.
        Display one; one.setRenderMode(RenderMode::RGBA16); prefill(one, 96, 160);
        blit(one, 0, 100, 4, 0, 0, 0);
        blit(one, 64, 104, 0, 0, 0, 0);

        Display banded; banded.setRenderMode(RenderMode::RGBA16); prefill(banded, 96, 160);
        blit(banded, 0, 100, 4, C, 0, 8);      // band 0: every tile that touches it, in z order, clipped to it
        blit(banded, 64, 104, 0, C, 0, 8);
        blit(banded, 0, 100, 4, C, 8, 16);     // band 1: only A touches it

        Display naive; naive.setRenderMode(RenderMode::RGBA16); prefill(naive, 96, 160);
        blit(naive, 0, 100, 4, 0, 0, 0);
        blit(naive, 64, 104, 0, 0, 0, 0);
        blit(naive, 0, 100, 4, 0, 0, 0);       // band 1 again, unclipped: rewrites A's band-0 rows over B

        bool same = true, naiveSame = true;
        for (int y = 0; y < 16; y++) for (int x = 96; x < 128; x++) {
            if (px(banded, x, y) != px(one, x, y)) same = false;
            if (px(naive, x, y) != px(one, x, y)) naiveSame = false;
        }
        check(same, "A then B in band 0, A in band 1, each clipped to its band == A then B drawn once");
        check(!naiveSame, "control: the same re-issue without the clip redraws A over B (this is what the clip prevents)");
    }

    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
