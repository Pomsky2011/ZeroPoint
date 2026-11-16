// Tile blits draw in issue order: a cheaper blit issued after a dearer one lands after it, and a TILEDRAW issued
// while every channel is busy waits for one instead of drawing ahead of the blits still in flight.
#include "ppu.h"
#include "display.h"
#include <cstdio>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static void word(PPU& ppu, uint16_t addr, uint16_t v) { ppu.writeMemory(addr, v & 0xFF); ppu.writeMemory(addr + 1, v >> 8); }

enum : uint8_t { TILEDRAW = 0xFC, SETDP = 0xF2, MOVDP = 0xF3, JMR = 0xF7 };

// Runs `prog` (parked on a JMR at its end) with tile 0 = left half palette index 1, right half index 2, at (8, 0),
// R4 = $00FC (VOC attribute byte), R5 = `attrs`: SETDP R4 / MOVDP R5 in the program writes it.
static void run(Display& d, const std::vector<uint8_t>& body, uint8_t attrs, int clip1) {
    PPU ppu;
    ppu.setDisplay(&d);
    for (int row = 0; row < 8; row++) {
        const uint8_t bytes[4] = {0x11, 0x11, 0x22, 0x22};
        for (int k = 0; k < 4; k++) ppu.writeMemory(0x0300 + row * 4 + k, bytes[k]);
    }
    word(ppu, 0x0200, 8); word(ppu, 0x0202, 0);
    word(ppu, 0x0204, 0); word(ppu, 0x0206, static_cast<uint16_t>(clip1));
    std::vector<uint8_t> prog = body;
    const uint16_t park = static_cast<uint16_t>(prog.size());
    prog.push_back(JMR); prog.push_back(0x00);
    ppu.loadMicrocode(prog.data(), prog.size());
    ppu.start();
    ppu.setRegister(4, 0x00FC);
    ppu.setRegister(5, attrs);
    ppu.setRegister(62, park);
    for (int i = 0; i < 4000; i++) ppu.tick();
}

int main() {
    // the two colors: an unflipped tile
    Display ref; ref.setRenderMode(RenderMode::RGBA16);
    run(ref, {TILEDRAW, 0x00}, 0, 0);
    const Color32 c1 = ref.getPixel(8, 4), c2 = ref.getPixel(15, 4);
    check(c1 != c2 && ref.getPixel(11, 4) == c1 && ref.getPixel(12, 4) == c2, "reference: left half one color, right half another");

    std::printf("== a cheaper blit issued later retires later ==\n");
    {
        // A: the full tile (264 cycles). B: hflip, rows 0-1 only (8 + 2*8*4 = 72 cycles), issued right after.
        Display d; d.setRenderMode(RenderMode::RGBA16);
        run(d, {TILEDRAW, 0x00, SETDP, 0x10, MOVDP, 0x14, TILEDRAW, 0x00}, 0x08 | 0x20, 2);
        bool top = true, rest = true;
        for (int tx = 0; tx < 8; tx++) {
            const Color32 flipped = tx < 4 ? c2 : c1, plain = tx < 4 ? c1 : c2;
            for (int y = 0; y < 2; y++) if (d.getPixel(8 + tx, y) != flipped) top = false;
            for (int y = 2; y < 8; y++) if (d.getPixel(8 + tx, y) != plain) rest = false;
        }
        check(top, "rows 0-1 show B (issued second) although B finishes sooner than A");
        check(rest, "rows 2-7 show A");
    }

    std::printf("== a TILEDRAW with every channel busy waits ==\n");
    {
        // eight unflipped draws fill every channel; the ninth is hflipped and is issued while all eight are still in
        // flight, so it has to wait for one and then retire after them (the old synchronous fallback drew it first)
        std::vector<uint8_t> body;
        for (int i = 0; i < 8; i++) { body.push_back(TILEDRAW); body.push_back(0x00); }
        const uint8_t tail[] = {SETDP, 0x10, MOVDP, 0x14, TILEDRAW, 0x00};
        body.insert(body.end(), tail, tail + sizeof tail);
        Display d; d.setRenderMode(RenderMode::RGBA16);
        run(d, body, 0x08, 0);
        bool flipped = true;
        for (int tx = 0; tx < 8; tx++) for (int y = 0; y < 8; y++)
            if (d.getPixel(8 + tx, y) != (tx < 4 ? c2 : c1)) flipped = false;
        check(flipped, "the ninth blit, issued with all eight channels busy, is the one on screen");
    }

    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
