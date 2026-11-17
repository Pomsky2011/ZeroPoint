// The blitter keeps the translucency-applied palette in lookup tables (PPU::tileLut / tileLut256) between blits. This is
// the test that they never go stale: ONE PPU lives through a history of palette, translucency and attribute changes,
// and after every step what it drew is compared with a brand-new PPU drawing the same tile once. A fresh PPU has no
// table yet (and an 8bpp palette still "settling" is drawn the direct, per-pixel way), so it is the reference.
// Covers 4bpp and 8bpp, both render modes, and the plain and attribute (transparent) blit paths.
#include "ppu.h"
#include "display.h"
#include <cstdio>
#include <cstdint>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static const uint16_t kRow = 0;

// What the program should have drawn with: the palettes, the translucency nibble and the attribute byte in force.
struct Look {
    uint32_t paletteSeed = 1;
    uint8_t alpha = 0x0F;   // all four translucency slots
    uint8_t attrs = 0;      // $00FC bits 2-4
};

static void pal16(PPU& ppu, uint16_t addr, int i, uint16_t c) { ppu.writeMemory(addr + 2 * i, c & 0xFF); ppu.writeMemory(addr + 2 * i + 1, c >> 8); }

// Writes everything a draw reads into PPU memory (palette, tile, translucency, attributes); the PPU keeps whatever
// else it has. Colors are a function of the seed, with some alpha-0 entries so the transparent path has work to do.
static void setLook(PPU& ppu, bool render32, bool bpp8, const Look& look) {
    uint32_t s = look.paletteSeed * 2654435761u;
    auto next = [&s]() { s = s * 1664525u + 1013904223u; return s >> 8; };
    for (int i = 0; i < 16; i++) {
        const uint32_t r = next() & 31, g = next() & 31, b = next() & 31;
        pal16(ppu, 0x9000, i, static_cast<uint16_t>((i % 5 == 0) ? 0 : ((b << 11) | (g << 6) | (r << 1) | 1)));
    }
    for (int i = 0; i < 256; i++) {
        const uint32_t r = next() & 31, g = next() & 31, b = next() & 31;
        const bool clear = i % 7 == 0;
        if (render32) {
            const uint8_t px[4] = {static_cast<uint8_t>(r * 8), static_cast<uint8_t>(g * 8), static_cast<uint8_t>(b * 8),
                                   static_cast<uint8_t>(clear ? 0x00 : 0xFF)};
            for (int k = 0; k < 4; k++) ppu.writeMemory(0x9100 + 4 * i + k, px[k]);
        } else {
            pal16(ppu, 0x9100, i, static_cast<uint16_t>(clear ? 0 : ((b << 11) | (g << 6) | (r << 1) | 1)));
        }
    }
    for (int ty = 0; ty < 8; ty++) {
        if (bpp8) {
            for (int tx = 0; tx < 8; tx++) ppu.writeMemory(0x0300 + ty * 8 + tx, static_cast<uint8_t>(ty * 8 + tx + 1));
        } else {
            for (int k = 0; k < 4; k++)
                ppu.writeMemory(0x0300 + ty * 4 + k, static_cast<uint8_t>((((2 * k + ty * 3) % 16) << 4) | ((2 * k + 1 + ty * 3) % 16)));
        }
    }
    // $00FC: blend mode 0 + attributes; $00FE/$00FF: the four translucency nibbles; $00FD: bit 3 pushes the
    // translucency, bit 4 the attributes
    ppu.writeMemory(0x00FC, look.attrs);
    ppu.writeMemory(0x00FE, static_cast<uint8_t>(look.alpha | (look.alpha << 4)));
    ppu.writeMemory(0x00FF, static_cast<uint8_t>(look.alpha | (look.alpha << 4)));
    ppu.writeMemory(0x00FD, 0x08 | 0x10);
}

// SETDP R1, PALETTE16, SETDP R2, PALETTE256, SETTILE (0 = 4bpp, 2 = 8bpp), TILEDRAW x draws, JMR to itself. Run on a
// PPU that may already have run: the program is rewritten and started over, nothing else is touched.
static void runDraws(PPU& ppu, bool bpp8, int draws, int16_t x) {
    uint8_t prog[2 * (5 + 9 + 1)] = {0xF2, 0x04, 0xF5, 0x00, 0xF2, 0x08, 0xF6, 0x00, 0xF1, static_cast<uint8_t>(bpp8 ? 2 : 0)};
    for (int i = 0; i < draws; i++) { prog[10 + 2 * i] = 0xFC; prog[11 + 2 * i] = 0x00; }
    prog[10 + 2 * draws] = 0xF7; prog[11 + 2 * draws] = 0x00;
    ppu.writeMemory(0x0200, static_cast<uint16_t>(x) & 0xFF); ppu.writeMemory(0x0201, static_cast<uint16_t>(x) >> 8);
    ppu.writeMemory(0x0202, kRow);                            ppu.writeMemory(0x0203, 0);
    ppu.loadMicrocode(prog, 10 + 2 * draws + 2);
    ppu.start();
    ppu.setExecutionPointer(0);
    ppu.setRegister(1, 0x9000);
    ppu.setRegister(2, 0x9100);
    ppu.setRegister(62, static_cast<uint16_t>(10 + 2 * draws));
    for (int i = 0; i < 3000; i++) ppu.tick();
}

static bool sameTile(Display& a, int ax, Display& b, int bx) {
    for (int ty = 0; ty < 8; ty++)
        for (int tx = 0; tx < 8; tx++)
            if (a.getPixel(ax + tx, kRow + ty) != b.getPixel(bx + tx, kRow + ty)) return false;
    return true;
}

static void config(bool render32, bool bpp8) {
    std::printf("== %s, %s ==\n", bpp8 ? "8bpp" : "4bpp", render32 ? "RGBA32" : "RGBA16");
    Display display;
    display.setRenderMode(render32 ? RenderMode::RGBA32 : RenderMode::RGBA16);
    PPU ppu;
    ppu.setDisplay(&display);
    ppu.start();

    int16_t x = 8;
    // one step: set the look on the long-lived PPU, draw `draws` tiles at x, draw the same look once on a fresh PPU
    auto step = [&](const Look& look, int draws, const char* what) {
        setLook(ppu, render32, bpp8, look);
        runDraws(ppu, bpp8, draws, x);
        Display refDisplay;
        refDisplay.setRenderMode(render32 ? RenderMode::RGBA32 : RenderMode::RGBA16);
        PPU ref;
        ref.setDisplay(&refDisplay);
        ref.start();
        setLook(ref, render32, bpp8, look);
        runDraws(ref, bpp8, 1, 100);
        check(sameTile(display, x, refDisplay, 100), what);
        x = static_cast<int16_t>(x + 24);
    };

    Look look;
    step(look, 9, "first palette, enough draws to have built the tables");
    look.paletteSeed = 2;
    step(look, 9, "palette changed under built tables");
    look.alpha = 0x00;
    step(look, 9, "translucency pushed to fully clear");
    look.alpha = 0x07;
    step(look, 9, "translucency pushed to half");
    look.alpha = 0x0F;
    step(look, 9, "translucency back to opaque");
    look.paletteSeed = 3;
    step(look, 2, "palette changed, then only two draws (the 8bpp palette is still settling)");
    look.attrs = 0x04;   // transparent: the attribute blit path
    look.paletteSeed = 4;
    step(look, 9, "transparent attribute, new palette");
    look.paletteSeed = 5;
    step(look, 9, "transparent attribute, palette changed under built tables");
    look.alpha = 0x00;
    step(look, 9, "transparent attribute, translucency to clear");
    look.alpha = 0x0F;
    look.attrs = 0x08;   // hflip
    step(look, 9, "hflip attribute, translucency back");
    look.attrs = 0x04 | 0x10;   // transparent + vflip
    look.paletteSeed = 6;
    step(look, 9, "transparent + vflip, palette changed");
}

int main() {
    for (int render32 = 0; render32 < 2; render32++)
        for (int bpp8 = 0; bpp8 < 2; bpp8++) config(render32 != 0, bpp8 != 0);
    std::printf(failures ? "\n%d FAILED\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
