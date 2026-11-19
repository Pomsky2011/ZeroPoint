// The PPU's tile-row queue (write-ahead, docs/ppu.md "Tile-row queue"): rows of tilemap cells queued ahead of the beam
// through the $0350 port, dispatched onto the blit channels once their rows are writable, with a sticky late bit for
// a row that was queued too late and an overflow bit for a launch that does not fit. Also the write-ahead open-bus
// mode, and that the batched executor stays state-identical with a queue running.
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace ZeroPoint;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

static const uint16_t PAL = 0x5000, MAP = 0x6000, ROW = 0x6100;   // ROW is 256-byte aligned: a 64-cell row
static const uint16_t H = 0x08, V = 0x10, T = 0x04;
static const uint16_t F_CLIP = 0x01, F_FLIPX = 0x02, F_FLIPY = 0x04, F_WRAP = 0x08, F_MODE2 = 0x20, F_TRANSPARENT = 0x40;

static uint16_t palEntry(int i) {
    const uint16_t c = static_cast<uint16_t>((((i * 37) & 31) << 11) | (((i * 11) & 31) << 6) | (((i * 5) & 31) << 1));
    return (i % 16 == 0) ? c : (c | 1);   // 0, 16, 32.. are alpha 0
}
static uint8_t tilePx(int bank, int id, int p) {
    const int v = (bank * 64 + id) * 7 + p * 3 + 1;
    return static_cast<uint8_t>(bank == 2 && (p % 5 == 0) ? (v & 0xF0) : (v & 0xFF));   // bank 2 has see-through pixels (index % 16 == 0)
}

static void w16(PPU& ppu, uint16_t addr, uint16_t v) { ppu.writeMemory(addr, v & 0xFF); ppu.writeMemory(addr + 1, v >> 8); }
static uint16_t r16(const PPU& ppu, uint16_t addr) { return ppu.readMemory(addr) | (ppu.readMemory(addr + 1) << 8); }

static void setup(System& sys) {
    sys.getDisplay().setRenderMode(RenderMode::RGBA16);
    PPU& ppu = sys.getPPU();
    for (int i = 0; i < 256; i++) w16(ppu, PAL + 2 * i, palEntry(i));
    // one parked tile-select program per tile (as test_layers does), then the palette load last
    static const uint8_t mini[] = {0xFD, 0x64, 0xF1, 0x6A, 0xF7, 0x00};          // SETTILEBANK R25, SETTILE R26 mode 2, JMR (parked at 4)
    for (int bank = 0; bank < 4; bank++) for (int id = 0; id < 80; id++) {
        ppu.loadMicrocode(mini, sizeof mini, 0);
        ppu.setRegister(25, static_cast<uint16_t>(bank)); ppu.setRegister(26, static_cast<uint16_t>(id));
        ppu.setRegister(62, 4);
        ppu.setExecutionPointer(0);
        if (ppu.getState() != PPUState::Running) ppu.start();
        for (int i = 0; i < 12; i++) ppu.tick();
        for (int p = 0; p < 64; p++) ppu.writeMemory(0x0300 + p, tilePx(bank, id, p));
    }
    static const uint8_t pal[] = {0xF2, 0x08, 0xF6, 0x00, 0xF7, 0x00};          // SETDP R2, PALETTE256, JMR (parked at 4)
    ppu.loadMicrocode(pal, sizeof pal, 0);
    ppu.setRegister(2, PAL); ppu.setRegister(62, 4);
    ppu.setExecutionPointer(0);
    for (int i = 0; i < 400; i++) ppu.tick();
}

static void cell(PPU& ppu, uint16_t addr, int bank, int id, uint16_t attrs) {
    w16(ppu, addr, static_cast<uint16_t>(0xAB00 | id));            // the high byte is ignored on purpose
    w16(ppu, addr + 2, static_cast<uint16_t>(bank | attrs | 0x1000));
}

static void launch(PPU& ppu, uint16_t src, int count, int x, int y, uint16_t flags, int clip0 = 0, int clip1 = 0, int frame = -1) {
    w16(ppu, 0x0350, src); w16(ppu, 0x0352, static_cast<uint16_t>(count)); w16(ppu, 0x0354, static_cast<uint16_t>(x));
    w16(ppu, 0x0356, static_cast<uint16_t>(y)); w16(ppu, 0x0358, static_cast<uint16_t>(clip0)); w16(ppu, 0x035A, static_cast<uint16_t>(clip1));
    w16(ppu, 0x035C, flags);
    w16(ppu, 0x035E, frame < 0 ? r16(ppu, 0x0366) : static_cast<uint16_t>(frame));
    w16(ppu, 0x0360, 1);
}

static void runTo(System& sys, int scanline) {
    for (int g = 0; sys.getDisplay().getCurrentScanline() != scanline && g < 3 * 88740; g++) sys.step();
}

// the colour a tile pixel (id, bank) lands as, for a destination pixel (tx, ty) of the tile, with the attributes of the cell; -1 = transparent
static int expect(int bank, int id, uint16_t attrs, int tx, int ty) {
    const int sx = (attrs & H) ? 7 - tx : tx, sy = (attrs & V) ? 7 - ty : ty;
    const uint16_t c = palEntry(tilePx(bank, id, sy * 8 + sx));
    if ((attrs & T) && !(c & 1)) return -1;
    return c;
}
static uint16_t got(System& sys, int x, int y) {
    std::vector<Color32> row(256);
    sys.getDisplay().getScanline(y, row.data());
    return Display::color32To16(row[x]);
}

int main() {
    System sys;
    setup(sys);
    PPU& ppu = sys.getPPU();

    if (std::getenv("RQ_THROUGHPUT")) {   // engine speed with nothing held: queue a fine-scroll band's pieces and time the drain
        for (int i = 0; i < 64; i++) cell(ppu, ROW + 4 * i, i & 3, 8 + i % 60, 0);
        for (const int pieces : {1, 2, 4}) {
            runTo(sys, 100);
            uint64_t t0 = sys.getMasterCycleCount();
            int launched = 0;
            for (int rep = 0; rep < 4; rep++) {   // 4 bands' worth, topped up as space allows
                for (int p = 0; p < pieces; p++) {
                    while (ppu.readMemory(0x0360) + 33 > 128) sys.step();
                    const int rows = (p & 1) ? 3 : 5;
                    launch(ppu, ROW, 33, -3, 0, F_MODE2 | F_WRAP | F_CLIP, 0, rows);   // all in the past: nothing is held
                    launched += 33;
                }
            }
            while (ppu.readMemory(0x0360) != 0) sys.step();
            for (int i = 0; i < 400; i++) sys.step();
            std::printf("  %d piece(s) per band, 4 bands: %d cells in %llu cycles = %.1f cycles/cell\n", pieces, launched, (unsigned long long)(sys.getMasterCycleCount() - t0), double(sys.getMasterCycleCount() - t0 - 400) / launched);
        }
        return 0;
    }

    // ---- hold, geometry, left clip -----------------------------------------------------------------------------------
    std::printf("== a queued row waits for its rows, then draws exactly like TILEDRAW\n");
    cell(ppu, MAP + 0, 0, 1, 0); cell(ppu, MAP + 4, 1, 2, H); cell(ppu, MAP + 8, 2, 3, V | T); cell(ppu, MAP + 12, 3, 4, H | V);
    runTo(sys, 20);
    launch(ppu, MAP, 4, -5, 40, F_MODE2);
    check(ppu.readMemory(0x0360) == 4, "four cells pending after the launch");
    runTo(sys, 30);
    check(ppu.readMemory(0x0360) == 4, "still pending at scanline 30: row 47 is not in the window yet");
    runTo(sys, 70);
    check(ppu.readMemory(0x0360) == 0, "all dispatched once the window reached them");
    check((ppu.readMemory(0x0364) & 3) == 0, "neither late nor overflow");
    {
        const uint16_t attrs[4] = {0, H, V | T, H | V};
        const int bank[4] = {0, 1, 2, 3}, id[4] = {1, 2, 3, 4};
        int bad = 0, n = 0;
        for (int ty = 0; ty < 8; ty++) for (int x = 0; x < 27; x++) {
            const int k = (x + 5) / 8, tx = (x + 5) % 8;
            const int want = expect(bank[k], id[k], attrs[k], tx, ty);
            if (want < 0) continue;
            n++;
            if (got(sys, x, 40 + ty) != want) bad++;
        }
        check(bad == 0 && n > 150, "pixels match the reference (cells placed 8 apart from x = -5, flips, transparent cell)");
    }

    // ---- wrap, flip-x order, clip, transparent, flip-y ---------------------------------------------------------------------
    std::printf("== wrap inside a 64-cell row, flips, row clip\n");
    for (int i = 0; i < 64; i++) cell(ppu, ROW + 4 * i, i & 3, 8 + i % 60, (i % 3 == 0) ? H : 0);
    runTo(sys, 76);
    launch(ppu, ROW + 4 * 62, 4, 16, 80, F_MODE2 | F_WRAP);                       // cells 62, 63, 0, 1
    launch(ppu, ROW + 4 * 62, 4, 16, 96, F_MODE2 | F_WRAP | F_FLIPX);             // the same, mirrored: 1, 0, 63, 62 with h toggled
    launch(ppu, ROW, 3, 16, 112, F_MODE2 | F_CLIP, 114, 117);                      // only rows 114-116 of the tiles
    launch(ppu, ROW, 3, 16, 128, F_MODE2 | F_FLIPY | F_TRANSPARENT);
    runTo(sys, 160);
    {
        auto cellAttr = [](int i) { return static_cast<uint16_t>((i % 3 == 0) ? H : 0); };
        auto cellBank = [](int i) { return i & 3; };
        auto cellId = [](int i) { return 8 + i % 60; };
        int bad1 = 0, bad2 = 0, bad3 = 0, bad4 = 0, other = 0;
        const int order[4] = {62, 63, 0, 1};
        for (int ty = 0; ty < 8; ty++) for (int x = 16; x < 48; x++) {
            const int k = (x - 16) / 8, tx = (x - 16) % 8;
            { const int i = order[k]; if (got(sys, x, 80 + ty) != expect(cellBank(i), cellId(i), cellAttr(i), tx, ty)) bad1++; }
            { const int i = order[3 - k]; if (got(sys, x, 96 + ty) != expect(cellBank(i), cellId(i), cellAttr(i) ^ H, tx, ty)) bad2++; }
        }
        const uint16_t blank = got(sys, 200, 114);
        for (int ty = 0; ty < 8; ty++) for (int x = 16; x < 40; x++) {
            const int k = (x - 16) / 8, tx = (x - 16) % 8;
            const bool in = ty + 112 >= 114 && ty + 112 < 117;
            const uint16_t g = got(sys, x, 112 + ty);
            if (in) { if (g != expect(cellBank(k), cellId(k), cellAttr(k), tx, ty)) bad3++; }
            else if (g != blank) other++;
            const int w = expect(cellBank(k), cellId(k), cellAttr(k) ^ V | T, tx, ty);
            if (w >= 0 && got(sys, x, 128 + ty) != w) bad4++;
        }
        check(bad1 == 0, "WRAP: cells 62, 63, 0, 1 across the row end");
        check(bad2 == 0, "FLIPX: reversed cell order and each tile mirrored");
        check(bad3 == 0 && other == 0, "CLIP: only rows [114, 117) written, the rest of the tiles untouched");
        check(bad4 == 0, "FLIPY + TRANSPARENT flags apply to every cell");
    }

    // ---- late and overflow --------------------------------------------------------------------------------------------------
    std::printf("== late and overflow flags\n");
    runTo(sys, 200);
    launch(ppu, MAP, 1, 0, 50, F_MODE2);            // rows 50-57 left the window long ago
    for (int i = 0; i < 2000; i++) sys.step();
    check((ppu.readMemory(0x0364) & 1) == 1, "a row queued after its rows were latched sets the sticky late bit");
    w16(ppu, 0x0364, 0);
    check(ppu.readMemory(0x0364) == 0, "a write to $0364 clears the sticky bits");
    runTo(sys, 5);
    launch(ppu, ROW, 100, 0, 150, F_MODE2 | F_WRAP);
    launch(ppu, ROW, 40, 0, 160, F_MODE2 | F_WRAP);
    check(ppu.readMemory(0x0360) == 100 && (ppu.readMemory(0x0364) & 2), "a launch past 128 pending cells is dropped whole and sets overflow");
    launch(ppu, ROW, 28, 0, 170, F_MODE2 | F_WRAP);
    check(ppu.readMemory(0x0360) == 128 && !(ppu.readMemory(0x0364) & 1), "exactly 128 cells fit");

    // ---- a tile above row 0 ---------------------------------------------------------------------------------------------------
    std::printf("== signed y: a tile starting above row 0, clipped\n");
    for (int i = 0; i < 3 * 88740; i++) sys.step();   // let the overflow test's queue drain
    w16(ppu, 0x0364, 0);   // the overflow test's 100-cell row at one y was itself too much to land in time: its late bit is not this test's
    runTo(sys, 258);
    launch(ppu, MAP, 2, 24, -3, F_MODE2 | F_CLIP, 0, 5);
    runTo(sys, 30);
    {
        const uint16_t attrs[2] = {0, H};
        const int bank[2] = {0, 1}, id[2] = {1, 2};
        int bad = 0;
        for (int row = 0; row < 5; row++) for (int x = 24; x < 40; x++) {
            const int k = (x - 24) / 8, tx = (x - 24) % 8;
            if (got(sys, x, row) != expect(bank[k], id[k], attrs[k], tx, row + 3)) bad++;
        }
        check(bad == 0 && (ppu.readMemory(0x0364) & 1) == 0, "rows 0-4 show tile rows 3-7, and the queue did not flag it late");
    }

    // ---- queued across the frame boundary -------------------------------------------------------------------------------------------
    std::printf("== a row queued during this frame for the next frame's row 16\n");
    w16(ppu, 0x0364, 0);
    runTo(sys, 240);
    launch(ppu, MAP, 4, 100, 256 + 0, F_MODE2);   // the very top of the frame first: the queue is in order, a later row cannot overtake a held one
    launch(ppu, MAP, 4, 8, 256 + 16, F_MODE2);
    runTo(sys, 10);   // the next frame, rows 16-23 are not shown yet
    check(ppu.readMemory(0x0360) == 0 || ppu.readMemory(0x0360) == 4, "state sane early in the next frame");
    runTo(sys, 40);
    {
        const uint16_t attrs[4] = {0, H, V | T, H | V};
        const int bank[4] = {0, 1, 2, 3}, id[4] = {1, 2, 3, 4};
        int bad = 0, n = 0;
        for (int ty = 0; ty < 8; ty++) for (int x = 8; x < 40; x++) {
            const int k = (x - 8) / 8, tx = (x - 8) % 8;
            const int want = expect(bank[k], id[k], attrs[k], tx, ty);
            if (want < 0) continue;
            n++;
            if (got(sys, x, 16 + ty) != want) bad++;
        }
        check(bad == 0 && n > 100 && ppu.readMemory(0x0360) == 0, "it was held across the frame boundary and drew at the right place");
        int badTop = 0, nTop = 0;
        for (int ty = 0; ty < 8; ty++) for (int x = 100; x < 132; x++) {
            const int k = (x - 100) / 8, tx = (x - 100) % 8;
            const int want = expect(bank[k], id[k], attrs[k], tx, ty);
            if (want < 0) continue;
            nTop++;
            if (got(sys, x, ty) != want) badTop++;
        }
        check(badTop == 0 && nTop > 100, "and the top band (rows 0-7), queued the same way, is right too");
        check((ppu.readMemory(0x0364) & 1) == 0, "and was not flagged late");
    }

    // ---- the frame port ---------------------------------------------------------------------------------------------------------
    std::printf("== $0366 follows the hardware frame\n");
    runTo(sys, 256);
    const uint16_t f0 = r16(ppu, 0x0366);
    runTo(sys, 258);
    const uint16_t f1 = r16(ppu, 0x0366);
    runTo(sys, 5);
    check(static_cast<uint16_t>(f1 - f0) == 1 && r16(ppu, 0x0366) == f1, "bumped once, at scanline 257, and stays put through the next frame's first lines");

    // ---- open bus ------------------------------------------------------------------------------------------------------------
    std::printf("== write-ahead mode\n");
    for (int i = 0; i < 3 * 88740; i++) sys.step();
    ppu.writeMemory(0xC000, 0x12); ppu.writeMemory(0x8000, 0x34);
    check(ppu.readMemory(0xC000) == 0x12, "off: $C000 is ordinary RAM");
    ppu.writeMemory(0x0362, 1);
    check(ppu.readMemory(0xC000) == 0xFF && ppu.readMemory(0xE000) == 0xFF && ppu.readMemory(0xFFFF) == 0xFF, "on: $C000-$FFFF read open bus ($FF)");
    check(ppu.readMemory(0x8000) == 0x34 && ppu.readMemory(0xBFFF) == ppu.readMemory(0xBFFF), "on: everything below $C000 is untouched");
    ppu.writeMemory(0x0362, 0);
    check(ppu.readMemory(0xC000) == 0x12, "off again: the RAM is back");

    // ---- the batched executor stays state-identical with a queue running ---------------------------------------------------------
    std::printf("== batched vs per-cycle with queued rows (across a frame boundary)\n");
    System a, b;
    setup(a); setup(b);
    for (System* s : {&a, &b}) {
        for (int i = 0; i < 64; i++) cell(s->getPPU(), ROW + 4 * i, i & 3, 8 + i % 60, (i % 3 == 0) ? H : (i % 5 == 0) ? (V | T) : 0);
    }
    bool same = true;
    for (int chunk = 0; chunk < 12; chunk++) {
        for (System* s : {&a, &b}) {
            PPU& p = s->getPPU();
            launch(p, ROW + 4 * (chunk * 7 % 64), 33, -3 + chunk, 8 * chunk + (chunk > 7 ? 256 : 0), F_MODE2 | F_WRAP | ((chunk & 1) ? F_FLIPX : 0) | ((chunk % 3 == 0) ? F_CLIP : 0), 8 * chunk + 2, 8 * chunk + 6);
        }
        for (int i = 0; i < 40000; i++) a.step();
        b.runCycles(40000);
        if (a.getMasterCycleCount() != b.getMasterCycleCount()) { std::printf("    cycle counts differ at chunk %d (%llu vs %llu)\n", chunk, (unsigned long long)a.getMasterCycleCount(), (unsigned long long)b.getMasterCycleCount()); same = false; break; }
        if (a.stateHash() != b.stateHash()) { std::printf("    state differs at chunk %d\n", chunk); same = false; break; }
    }
    check(same, "12 checkpoints identical (queue pending across stalls, interrupts off, a frame boundary)");

    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
