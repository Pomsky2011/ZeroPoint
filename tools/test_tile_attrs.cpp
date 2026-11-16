// Tile attributes ($00FC bits 2-4: transparent / hflip / vflip, sampled at each TILEDRAW) and the 256-color palette
// in 16-bit render mode. Every draw is compared against the same tile drawn unflipped on screen: the flip tests use an
// 8bpp tile whose 64 pixels all have different palette indices, so a wrong source pixel can't hide.
#include "ppu.h"
#include "display.h"
#include <cstdio>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static const int kDraws = 9;                 // eight async channels + one synchronous fallback
static const Color32 kSentinel = 0x112233FF;
static const uint16_t kRow = 0;              // all draws land on rows 0-7: the 32-bit rolling window is 8 rows

struct Draw {
    bool bpp8 = false;
    uint8_t attrs = 0;                       // value written to $00FC before the draws
    bool blend = false;
    bool render32 = false;
    bool prefill = false;                    // sentinel under the tile, to see what a draw leaves alone
};

static int idx4(int tx, int ty) { return (tx + ty) % 3; }        // 0 is the alpha-0 palette entry
static int idx8(int tx, int ty) { return ty * 8 + tx + 1; }      // 1..64, all different

static void pal16(PPU& ppu, uint16_t addr, int i, uint16_t c) { ppu.writeMemory(addr + 2 * i, c & 0xFF); ppu.writeMemory(addr + 2 * i + 1, c >> 8); }

static void draw(Display& display, int16_t x, const Draw& d) {
    PPU ppu;
    ppu.setDisplay(&display);
    display.setRenderMode(d.render32 ? RenderMode::RGBA32 : RenderMode::RGBA16);
    if (d.prefill) {
        const int xs[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
                          248, 249, 250, 251, 252, 253, 254, 255};
        for (int xv : xs) for (int yv = 0; yv < 8; yv++) display.setPixel32(xv, kRow + yv, kSentinel);
    }

    // PALETTE16 at $9000: entry 0 alpha 0, 1 red, 2 green. PALETTE256 at $9100 in the render mode's entry format.
    pal16(ppu, 0x9000, 0, 0x0000);
    pal16(ppu, 0x9000, 1, (31 << 1) | 1);
    pal16(ppu, 0x9000, 2, (31 << 6) | 1);
    for (int i = 0; i < 256; i++) {
        const int r = i & 31, g = (i >> 5) * 3 + 1, b = i & 15;
        if (d.render32) {
            const uint8_t px[4] = {static_cast<uint8_t>(r * 8), static_cast<uint8_t>(g * 8), static_cast<uint8_t>(b * 8), 0xFF};
            for (int k = 0; k < 4; k++) ppu.writeMemory(0x9100 + 4 * i + k, px[k]);
        } else {
            pal16(ppu, 0x9100, i, static_cast<uint16_t>((b << 11) | (g << 6) | (r << 1) | 1));
        }
    }
    for (int ty = 0; ty < 8; ty++) {
        if (d.bpp8) {
            for (int tx = 0; tx < 8; tx++) ppu.writeMemory(0x0300 + ty * 8 + tx, static_cast<uint8_t>(idx8(tx, ty)));
        } else {
            for (int k = 0; k < 4; k++)
                ppu.writeMemory(0x0300 + ty * 4 + k, static_cast<uint8_t>((idx4(2 * k, ty) << 4) | idx4(2 * k + 1, ty)));
        }
    }
    if (d.blend) {
        ppu.writeMemory(0x00FC, 0x01);   // average
        ppu.writeMemory(0x00FE, 0xFF);
        ppu.writeMemory(0x00FF, 0xFF);
        ppu.writeMemory(0x00FD, 0x08);   // push
    }
    if (d.attrs) ppu.writeMemory(0x00FC, static_cast<uint8_t>(d.attrs | (d.blend ? 0x01 : 0x00)));

    ppu.writeMemory(0x0200, static_cast<uint16_t>(x) & 0xFF); ppu.writeMemory(0x0201, static_cast<uint16_t>(x) >> 8);
    ppu.writeMemory(0x0202, kRow);                            ppu.writeMemory(0x0203, 0);

    // SETDP R1, PALETTE16, SETDP R2, PALETTE256, SETTILE R0 (mode 0 = 4bpp, 2 = 8bpp), TILEDRAW x kDraws
    uint8_t prog[2 * (5 + kDraws)] = {0xF2, 0x04, 0xF5, 0x00, 0xF2, 0x08, 0xF6, 0x00, 0xF1, static_cast<uint8_t>(d.bpp8 ? 2 : 0)};
    for (int i = 0; i < kDraws; i++) { prog[10 + 2 * i] = 0xFC; prog[11 + 2 * i] = 0x00; }
    ppu.loadMicrocode(prog, sizeof prog);
    ppu.start();
    ppu.setRegister(1, 0x9000);
    ppu.setRegister(2, 0x9100);
    for (int i = 0; i < 3000; i++) ppu.tick();
}

static Color32 px(Display& d, int x, int y) { return d.getPixel(x, y); }

// every on-screen pixel of a tile drawn at x with flips (hf, vf) must equal the reference tile's source pixel
static bool matchesRef(Display& d, Display& ref, int x, bool hf, bool vf) {
    for (int ty = 0; ty < 8; ty++)
        for (int tx = 0; tx < 8; tx++) {
            const int dx = x + tx;
            if (dx < 0 || dx > 255) continue;
            const int sx = hf ? 7 - tx : tx, sy = vf ? 7 - ty : ty;
            if (px(d, dx, kRow + ty) != px(ref, 100 + sx, kRow + sy)) return false;
        }
    return true;
}

int main() {
    const uint8_t T = 0x04, H = 0x08, V = 0x10;   // $00FC bits 2-4: transparent, hflip, vflip

    std::printf("== 256-color palette in 16-bit mode (8bpp tile) ==\n");
    Draw d8; d8.bpp8 = true;
    Display ref8;
    draw(ref8, 100, d8);
    bool allDistinct = true;
    for (int a = 0; a < 64; a++) for (int b = a + 1; b < 64; b++)
        if (px(ref8, 100 + a % 8, kRow + a / 8) == px(ref8, 100 + b % 8, kRow + b / 8)) allDistinct = false;
    check(allDistinct, "all 64 pixels use different palette256 entries (indices >= 16 are real colors, not gray)");

    std::printf("== flips ==\n");
    struct { uint8_t attrs; bool hf, vf; const char* name; } flips[] = {
        {H, true, false, "hflip"}, {V, false, true, "vflip"}, {static_cast<uint8_t>(H | V), true, true, "hflip + vflip"}};
    for (const auto& f : flips) {
        Display a, b;
        Draw dd = d8; dd.attrs = f.attrs;
        draw(a, 100, dd);
        check(matchesRef(a, ref8, 100, f.hf, f.vf), f.name);
        dd.blend = true;   // the per-pixel path blends, flips the same way
        Draw plain = d8; plain.blend = true;
        draw(b, 100, dd);
        Display refb;
        draw(refb, 100, plain);
        bool ok = true;
        for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
            const int sx = f.hf ? 7 - tx : tx, sy = f.vf ? 7 - ty : ty;
            if (px(b, 100 + tx, kRow + ty) != px(refb, 100 + sx, kRow + sy)) ok = false;
        }
        check(ok, f.hf && f.vf ? "hflip + vflip, blended" : f.hf ? "hflip, blended" : "vflip, blended");
    }

    std::printf("== flips clip like unflipped tiles ==\n");
    {
        Display a;
        Draw dd = d8; dd.attrs = H;
        draw(a, -4, dd);
        check(matchesRef(a, ref8, -4, true, false), "hflip at x = -4");
        Display b;
        dd.attrs = static_cast<uint8_t>(H | V);
        draw(b, 252, dd);
        check(matchesRef(b, ref8, 252, true, true), "hflip + vflip at x = 252");
    }

    std::printf("== transparency (4bpp, palette entry 0 has alpha 0) ==\n");
    {
        Draw d4; d4.prefill = true;
        Display ref4;
        draw(ref4, 100, d4);
        // opaque draw: the alpha-0 entry overwrites the sentinel
        bool overwrote = true;
        for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++)
            if (idx4(tx, ty) == 0 && px(ref4, 100 + tx, kRow + ty) == px(ref4, 98, kRow + ty)) overwrote = false;
        check(overwrote, "without the T bit an alpha-0 pixel overwrites what was there");

        Display a;
        Draw dt = d4; dt.attrs = T;
        draw(a, 100, dt);
        bool ok = true, any0 = false;
        for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
            const Color32 got = px(a, 100 + tx, kRow + ty);
            if (idx4(tx, ty) == 0) { any0 = true; if (got != px(a, 98, kRow + ty)) ok = false; }   // untouched = the prefilled pixel next to the tile
            else if (got != px(ref4, 100 + tx, kRow + ty)) ok = false;
        }
        check(any0 && ok, "with the T bit alpha-0 pixels keep the background and the rest draw normally");

        Display b;
        dt.attrs = static_cast<uint8_t>(T | H | V);
        draw(b, -4, dt);
        bool ok2 = true;
        for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
            const int dx = -4 + tx;
            if (dx < 0) continue;
            const int sx = 7 - tx, sy = 7 - ty;
            const Color32 got = px(b, dx, kRow + ty);
            if (idx4(sx, sy) == 0) { if (got != px(b, 8, kRow + ty)) ok2 = false; }
            else if (got != px(ref4, 100 + sx, kRow + sy)) ok2 = false;
        }
        check(ok2, "T + hflip + vflip at x = -4");

        Display c;
        dt.attrs = T; dt.blend = true;
        draw(c, 100, dt);
        bool ok3 = true;
        for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++)
            if (idx4(tx, ty) == 0 && px(c, 100 + tx, kRow + ty) != px(c, 98, kRow + ty)) ok3 = false;
        check(ok3, "T with blending: alpha-0 pixels are skipped, not blended");
    }

    std::printf("== 32-bit render mode ==\n");
    {
        Draw r; r.bpp8 = true; r.render32 = true;
        Display ref32;
        draw(ref32, 100, r);
        Display a;
        r.attrs = static_cast<uint8_t>(H | V);
        draw(a, 100, r);
        check(matchesRef(a, ref32, 100, true, true), "attribute bits take effect in 32-bit mode too");
        check(px(ref32, 100, kRow) != px(ref32, 101, kRow), "PALETTE256 reads 4-byte entries in 32-bit mode");

        Display d32; PPU p;
        p.setDisplay(&d32);
        d32.setRenderMode(RenderMode::RGBA32);
        p.writeMemory(0x00FC, 0xFF);
        check(p.readMemory(0x00FC) == 0x1C, "in 32-bit mode only the attribute bits of $00FC are writable");
        d32.setRenderMode(RenderMode::RGBA16);
        p.writeMemory(0x00FC, 0xFF);
        check(p.readMemory(0x00FC) == 0xFF, "in 16-bit mode all of $00FC is");
    }

    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
