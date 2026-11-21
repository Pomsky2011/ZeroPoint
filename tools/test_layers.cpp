// Harness for the two-layer + sprite microcode v2 (ZPdevtools/ucode/layers, docs/ppu/layers.md), which runs on the PPU's
// tile-row queue. Runs the generated program on a live raster for a few frames per case and compares the whole latched
// frame with a per-pixel reference renderer (BG0, BG1, sprites, then the window layer), and checks the queue never flagged a late or an
// overflowed row: the "PPU must never lag" requirement is that bit staying clear.
//   test_layers <layers.bin> [seed]
//   LAYERS_SWEEP=1 test_layers ...   also sweeps the sprite load to find where the frame stops fitting
#include "system.h"
#include "zp_layers_window.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using namespace ZeroPoint;

struct Scene {
    uint16_t pal[256];
    uint8_t tiles[4][64][64];
    uint16_t map[2][64 * 64][2];   // word0 = tile (low byte; the high byte is junk on purpose), word1 = bank|attrs|$1000
    std::vector<ZPL_Sprite> sprites;
    std::vector<uint16_t> cells;   // sprite cells, 2 words each, at ZPL_SPRCELLS
    std::vector<ZPL_Box> boxes;    // the window layer's boxes, their cells (2 words each) in the pool at ZPL_WINCELLS, and the glass fill cell
    std::vector<uint16_t> wcells;
    unsigned glassMode = ZPL_GLASS_AVERAGE;
    uint16_t fill[2] = {0, 0};
    unsigned char wintab[ZPL_WIN_TABLE_BYTES] = {0};   // what zpl_build_window made of the boxes: the reference draws from it
    bool hasWin = false;
};

static void w16(PPU& ppu, uint16_t addr, uint16_t v) { ppu.writeMemory(addr, v & 0xFF); ppu.writeMemory(addr + 1, v >> 8); }

static Scene makeScene(std::mt19937& rng) {
    Scene s;
    for (int i = 0; i < 256; i++) {
        uint16_t c = static_cast<uint16_t>(rng() & 0xFFFE);
        s.pal[i] = (i % 16 == 0) ? c : (c | 1);          // index 0, 16, 32.. are alpha 0: transparent under the T attribute
    }
    for (int b = 0; b < 4; b++) for (int t = 0; t < 64; t++) for (int p = 0; p < 64; p++) {
        int idx = rng() & 0xFF;
        if (b < 2 && idx % 16 == 0) idx++;                // banks 0-1 are opaque tiles, 2-3 have see-through pixels
        s.tiles[b][t][p] = static_cast<uint8_t>(idx);
    }
    for (int l = 0; l < 2; l++) for (int c = 0; c < 64 * 64; c++) {
        const int bank = l == 0 ? rng() % 2 : rng() % 4;
        uint16_t attr = static_cast<uint16_t>(bank | 0x1000);
        if (rng() & 1) attr |= 0x08;                      // hflip
        if (rng() & 1) attr |= 0x10;                      // vflip
        if (l == 1 && (rng() & 1)) attr |= 0x04;          // transparent (BG0 stays opaque so every pixel is defined)
        s.map[l][c][0] = static_cast<uint16_t>((rng() & 0xFF00) | (rng() % 64));
        s.map[l][c][1] = attr;
    }
    return s;
}

// sprites with at most `tileBudget` cells in total, random sizes, positions (partly off-screen), flips and cells
static void addSprites(Scene& s, std::mt19937& rng, int count, int tileBudget, bool priorities = false) {
    s.sprites.clear(); s.cells.clear();
    int used = 0;
    for (int i = 0; i < count; i++) {
        ZPL_Sprite sp;
        sp.w = static_cast<unsigned short>(1 + rng() % 4); sp.h = static_cast<unsigned short>(1 + rng() % 4);
        if (used + sp.w * sp.h > tileBudget) break;
        sp.x = static_cast<short>(int(rng() % 300) - 30); sp.y = static_cast<short>(int(rng() % 290) - 30);
        sp.flags = static_cast<unsigned short>((rng() & 1 ? ZPL_SPR_HFLIP : 0) | (rng() & 1 ? ZPL_SPR_VFLIP : 0));
        if (priorities) { static const unsigned short prio[3] = {0, ZPL_SPR_PRIO_MID, ZPL_SPR_PRIO_BACK}; sp.flags |= prio[rng() % 3]; }
        sp.cells = static_cast<unsigned short>(ZPL_SPRCELLS + 4 * used);
        for (unsigned c = 0; c < sp.w * sp.h; c++) {
            uint16_t attr = static_cast<uint16_t>((rng() % 4) | 0x1000);
            if (rng() & 1) attr |= 0x08;
            if (rng() & 1) attr |= 0x10;
            if (rng() % 3) attr |= 0x04;                  // mostly transparent: sprites overlap what is under them
            s.cells.push_back(static_cast<uint16_t>((rng() & 0xFF00) | (rng() % 64)));
            s.cells.push_back(attr);
        }
        used += sp.w * sp.h;
        s.sprites.push_back(sp);
    }
}

static int tilePixel(const Scene& s, uint16_t w0, uint16_t w1, int tx, int ty, bool* transparent) {
    const int px = (w1 & 0x08) ? 7 - tx : tx, py = (w1 & 0x10) ? 7 - ty : ty;
    const uint16_t color = s.pal[s.tiles[w1 & 3][w0 & 0xFF][py * 8 + px]];
    *transparent = (w1 & 0x04) && !(color & 1);
    return color;
}

static unsigned rd16(const unsigned char* p) { return p[0] | (p[1] << 8); }

// a box on the window grid with random cells: solid boxes get opaque tiles (banks 0-1, no T), overlays any tile and sometimes their own T
static void addBox(Scene& s, std::mt19937& rng, int col, int row, int w, int h, int kind) {
    ZPL_Box b;
    b.col = static_cast<unsigned short>(col); b.row = static_cast<unsigned short>(row);
    b.w = static_cast<unsigned short>(w); b.h = static_cast<unsigned short>(h); b.kind = static_cast<unsigned short>(kind);
    b.cells = static_cast<unsigned short>(ZPL_WINCELLS + 2 * s.wcells.size());
    if (kind != ZPL_BOX_GLASS) for (int c = 0; c < w * h; c++) {
        uint16_t attr = static_cast<uint16_t>((kind == ZPL_BOX_SOLID ? rng() % 2 : rng() % 4) | 0x1000);
        if (rng() & 1) attr |= 0x08;
        if (rng() & 1) attr |= 0x10;
        if (kind == ZPL_BOX_OVERLAY && (rng() & 1)) attr |= 0x04;
        s.wcells.push_back(static_cast<uint16_t>((rng() & 0xFF00) | (rng() % 64)));
        s.wcells.push_back(attr);
    }
    if (ZPL_WINCELLS + 2 * s.wcells.size() > ZPL_WINCELLS_END) { std::fprintf(stderr, "window cell pool is full\n"); std::exit(1); }
    s.boxes.push_back(b);
}

static void clearWindow(Scene& s) { s.boxes.clear(); s.wcells.clear(); s.hasWin = false; }

// what the blitter does to a pixel under the glass fill at translucency 15: PPU::blendColors, and only over a destination that is not all-zero
static uint16_t glassPixel(uint16_t src16, uint16_t dst16, unsigned mode) {
    const uint32_t src = Display::color16To32(src16), dst = Display::color16To32(dst16);
    if (dst == 0) return src16;
    const int sr = (src >> 24) & 0xFF, sg = (src >> 16) & 0xFF, sb = (src >> 8) & 0xFF, sa = src & 0xFF;
    const int dr = (dst >> 24) & 0xFF, dg = (dst >> 16) & 0xFF, db = (dst >> 8) & 0xFF;
    int r, g, b;
    switch (mode) {
        case 1: r = (sr + dr) >> 1; g = (sg + dg) >> 1; b = (sb + db) >> 1; break;
        case 2: r = dr > sr ? dr - sr : 0; g = dg > sg ? dg - sg : 0; b = db > sb ? db - sb : 0; break;
        default: r = std::min(255, sr + dr); g = std::min(255, sg + dg); b = std::min(255, sb + db); break;
    }
    const uint32_t out = (static_cast<uint32_t>((r * 255) >> 8) << 24) | (static_cast<uint32_t>((g * 255) >> 8) << 16) |
                         (static_cast<uint32_t>((b * 255) >> 8) << 8) | static_cast<uint32_t>((sa * 255) >> 8);
    return Display::color32To16(out);
}

// the window pass, band by band from the table the PPU gets: the glass fill (blended over what is there), then the ink over it
static void drawWindow(const Scene& s, std::vector<uint16_t>& frame) {
    if (!s.hasWin) return;
    const unsigned mode = rd16(s.wintab);
    for (int b = 0; b < ZPL_BANDS; b++) {
        const unsigned char* rec = s.wintab + ZPL_WIN_HDR_BYTES + b * ZPL_WIN_REC_BYTES;
        const unsigned icnt = rd16(rec), icol = rd16(rec + 2), isrc = rd16(rec + 4), iflg = rd16(rec + 6), gcnt = rd16(rec + 8), gcol = rd16(rec + 10);
        for (unsigned k = 0; k < gcnt; k++) for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
            bool t;
            const uint16_t src = static_cast<uint16_t>(tilePixel(s, s.fill[0], s.fill[1], tx, ty, &t));
            uint16_t& px = frame[(8 * b + ty) * 256 + 8 * (gcol + k) + tx];
            px = glassPixel(src, px, mode);
        }
        for (unsigned k = 0; k < icnt; k++) {
            const unsigned ci = (isrc - ZPL_WINCELLS) / 2 + 2 * k;
            uint16_t w1 = s.wcells[ci + 1];
            if (iflg & ZPL_F_TRANSPARENT) w1 |= 0x04;
            for (int ty = 0; ty < 8; ty++) for (int tx = 0; tx < 8; tx++) {
                bool t;
                const uint16_t color = static_cast<uint16_t>(tilePixel(s, s.wcells[ci], w1, tx, ty, &t));
                if (!t) frame[(8 * b + ty) * 256 + 8 * (icol + k) + tx] = color;
            }
        }
    }
}

static bool g_l0t = false;   // ZPL_CTRL_L0FLAGS = T: BG0 is transparent too (the default is opaque)

static void drawLayer(const Scene& s, std::vector<uint16_t>& frame, int l, const int sc[4]) {
    for (int sy = 0; sy < 256; sy++) for (int sx = 0; sx < 256; sx++) {
        const int wx = (sx + sc[2 * l]) & 511, wy = (sy + sc[2 * l + 1]) & 511;
        const int c = (wy >> 3) * 64 + (wx >> 3);
        uint16_t w1 = s.map[l][c][1];
        if (l == 0 && g_l0t) w1 |= 0x04;                       // the queue's TRANSPARENT flag, whatever the cell's own T bit says
        bool t;
        const uint16_t color = static_cast<uint16_t>(tilePixel(s, s.map[l][c][0], w1, wx & 7, wy & 7, &t));
        if (!t) frame[sy * 256 + sx] = color;
    }
}

// the sprites of one priority (0 above both layers, 1 between them, 2 behind BG0), array order: later on top
static void drawSprites(const Scene& s, std::vector<uint16_t>& frame, unsigned prio) {
    for (const ZPL_Sprite& sp : s.sprites) {
        const unsigned p = (sp.flags & ZPL_SPR_PRIO_MASK) == ZPL_SPR_PRIO_BACK ? 2 : (sp.flags & ZPL_SPR_PRIO_MASK) == ZPL_SPR_PRIO_MID ? 1 : 0;
        if (p != prio) continue;
        for (int py = 0; py < sp.h * 8; py++) for (int px = 0; px < sp.w * 8; px++) {
            const int sy = sp.y + py, sx = sp.x + px;
            if (sy < 0 || sy >= 256 || sx < 0 || sx >= 256) continue;
            const int cx = (sp.flags & ZPL_SPR_HFLIP) ? sp.w - 1 - px / 8 : px / 8, cy = (sp.flags & ZPL_SPR_VFLIP) ? sp.h - 1 - py / 8 : py / 8;
            const int ci = (cy * sp.w + cx) * 2 + (sp.cells - ZPL_SPRCELLS) / 2;
            uint16_t w1 = s.cells[ci + 1];
            if (sp.flags & ZPL_SPR_HFLIP) w1 ^= 0x08;
            if (sp.flags & ZPL_SPR_VFLIP) w1 ^= 0x10;
            w1 |= 0x04;   // every sprite piece carries the queue's TRANSPARENT flag, whatever the cell's own T bit says
            bool t;
            const uint16_t color = static_cast<uint16_t>(tilePixel(s, s.cells[ci], w1, px & 7, py & 7, &t));
            if (!t) frame[sy * 256 + sx] = color;
        }
    }
}

// z-order, bottom to top: sprites behind BG0, BG0, sprites between, BG1, sprites above, the window layer
static std::vector<uint16_t> reference(const Scene& s, const int sc[4]) {
    std::vector<uint16_t> frame(256 * 256, 0);
    drawSprites(s, frame, 2);
    drawLayer(s, frame, 0, sc);
    drawSprites(s, frame, 1);
    drawLayer(s, frame, 1, sc);
    drawSprites(s, frame, 0);
    drawWindow(s, frame);
    return frame;
}

static void uploadTile(PPU& ppu, int bank, int id, const uint8_t* px) {
    static const uint8_t mini[] = {0xFD, 0x64, 0xF1, 0x6A, 0xF7, 0x00};   // SETTILEBANK R25, SETTILE R26 mode 2, JMR (parked)
    ppu.loadMicrocode(mini, sizeof mini, 0);
    ppu.setRegister(25, static_cast<uint16_t>(bank));
    ppu.setRegister(26, static_cast<uint16_t>(id));
    ppu.setRegister(62, 4);
    ppu.setExecutionPointer(0);
    if (ppu.getState() != PPUState::Running) ppu.start();
    for (int i = 0; i < 12; i++) ppu.tick();
    for (int i = 0; i < 64; i++) ppu.writeMemory(0x0300 + i, px[i]);
}

static void loadScene(System& sys, const Scene& s) {
    PPU& ppu = sys.getPPU();
    for (int i = 0; i < 256; i++) w16(ppu, ZPL_PAL + 2 * i, s.pal[i]);
    for (int l = 0; l < 2; l++) for (int c = 0; c < 64 * 64; c++) {
        w16(ppu, (l ? ZPL_MAP1 : ZPL_MAP0) + 4 * c, s.map[l][c][0]);
        w16(ppu, (l ? ZPL_MAP1 : ZPL_MAP0) + 4 * c + 2, s.map[l][c][1]);
    }
}

// Like a cart: the frame block (scroll words + sprite stream) is written into the idle one of two buffers, then LISTBASE flips to it.
static uint16_t g_nextBase = ZPL_LISTS;

static unsigned long g_budget = ZPL_BAND_BUDGET;   // what the list builder admits per band; the sweep varies it
static unsigned long g_bandBudget[ZPL_BANDS];      // what is left of it for sprites once the window has taken its share
static uint16_t g_nextWin = ZPL_WIN0;
static uint16_t g_winBase = 0;                     // what setControl flips WINBASE to: 0 = no window

// Window layer: the pool cells, the glass fill row, then the table into the idle one of two buffers. Returns the box-bands dropped.
static unsigned loadWindow(System& sys, Scene& s) {
    PPU& ppu = sys.getPPU();
    s.hasWin = !s.boxes.empty();
    for (int b = 0; b < ZPL_BANDS; b++) g_bandBudget[b] = g_budget;
    if (!s.hasWin) { g_winBase = 0; return 0; }
    for (size_t i = 0; i < s.wcells.size(); i++) w16(ppu, ZPL_WINCELLS + 2 * static_cast<uint16_t>(i), s.wcells[i]);
    for (int c = 0; c < ZPL_WIN_COLS; c++) { w16(ppu, ZPL_GLFILL + 4 * c, s.fill[0]); w16(ppu, ZPL_GLFILL + 4 * c + 2, s.fill[1]); }
    const unsigned dropped = zpl_build_window(s.boxes.data(), static_cast<unsigned>(s.boxes.size()), s.glassMode, g_budget, s.wintab, g_bandBudget);
    for (unsigned k = 0; k < ZPL_WIN_TABLE_BYTES; k++) ppu.writeMemory(static_cast<uint16_t>(g_nextWin + k), s.wintab[k]);
    g_winBase = g_nextWin;
    g_nextWin = g_nextWin == ZPL_WIN0 ? ZPL_WIN1 : ZPL_WIN0;
    return dropped;
}

static unsigned loadSprites(System& sys, Scene& s) {
    PPU& ppu = sys.getPPU();
    for (size_t i = 0; i < s.cells.size(); i++) w16(ppu, ZPL_SPRCELLS + 2 * static_cast<uint16_t>(i), s.cells[i]);
    static unsigned char lists[ZPL_BANDS][ZPL_LIST_STRIDE];
    unsigned long uniform[ZPL_BANDS];
    for (int b = 0; b < ZPL_BANDS; b++) uniform[b] = g_budget;
    const unsigned dropped = zpl_build_lists_bands(s.sprites.data(), static_cast<unsigned>(s.sprites.size()), lists, g_winBase ? g_bandBudget : uniform);
    static unsigned char stream[4096];
    const unsigned n = zpl_pack_stream(lists, stream, sizeof stream - ZPL_STREAM_OFF);   // the PPU reads the packed stream after the scroll words
    if (n == 0) { std::fprintf(stderr, "sprite stream does not fit\n"); std::exit(1); }
    for (unsigned k = 0; k < n; k++) ppu.writeMemory(static_cast<uint16_t>(g_nextBase + ZPL_STREAM_OFF + k), stream[k]);
    return dropped;
}

static int failures = 0;

static void setControl(PPU& ppu, const int sc[4], int sprites) {
    for (int i = 0; i < 4; i++) w16(ppu, g_nextBase + ZPL_SCROLL_OFF + 2 * i, static_cast<uint16_t>(sc[i]));   // scroll lives in the frame block with the sprite lists
    w16(ppu, ZPL_CTRL_NCOLS, 33);
    w16(ppu, ZPL_CTRL_SPREN, static_cast<uint16_t>(sprites));
    w16(ppu, ZPL_CTRL_L1FLAGS, 0);
    w16(ppu, ZPL_CTRL_L0FLAGS, g_l0t ? ZPL_F_TRANSPARENT : 0);
    w16(ppu, ZPL_CTRL_WINBASE, g_winBase);
    w16(ppu, ZPL_CTRL_LISTBASE, g_nextBase);                       // flip to the block just filled
    g_nextBase = g_nextBase == ZPL_LISTS ? ZPL_LISTS2 : ZPL_LISTS;   // and fill the other one next time
    w16(ppu, ZPL_CTRL_RUN, 1);   // the PPU idles (serving tile uploads) until told to run
}

// run frames, then compare the last full latched frame; returns the mismatching pixel count
static int runCase(System& sys, const Scene& scene, const int sc[4], int frames, bool verbose, int* lateOut = nullptr) {
    PPU& ppu = sys.getPPU();
    for (int f = 0; f < frames; f++) {
        sys.runCycles(System::CYCLES_PER_FRAME);
        if (verbose) std::printf("      frame %d: status %d, pending %u, EP=$%04X\n", f, ppu.readMemory(0x0364), ppu.readMemory(0x0360), ppu.getExecutionPointer());
    }
    for (int g = 0; sys.getDisplay().getCurrentScanline() != 258 && g < 2 * 88740; g++) sys.step();
    const uint8_t status = ppu.readMemory(0x0364);
    if (lateOut) *lateOut = status;
    const std::vector<uint16_t> want = reference(scene, sc);
    int bad = 0, shown = 0;
    std::vector<Color32> row(256);
    int badBand[32] = {0};
    for (int y = 0; y < 256; y++) {
        sys.getDisplay().getScanline(y, row.data());
        for (int x = 0; x < 256; x++) {
            if (Display::color32To16(row[x]) != want[y * 256 + x]) {
                bad++; badBand[y / 8]++;
                if (verbose && shown++ < 14) {
                    int covers = 0;
                    for (const ZPL_Sprite& sp : scene.sprites) covers += (x >= sp.x && x < sp.x + sp.w * 8 && y >= sp.y && y < sp.y + sp.h * 8);
                    std::printf("      y=%d x=%d want %04X got %04X (sprites covering this pixel: %d)\n", y, x, want[y * 256 + x], Display::color32To16(row[x]), covers);
                }
            }
        }
    }
    if (bad && verbose) {
        std::printf("      bad pixels per band:");
        for (int b = 0; b < 32; b++) if (badBand[b]) std::printf(" %d:%d", b, badBand[b]);
        std::printf("\n");
    }
    return bad;
}

// the window scenes: disjoint row ranges, so no band holds two ink spans or two glass spans
static void buildWindowScene(Scene& s, std::mt19937& rng, int id) {
    auto span = [&](int maxw, int* col, int* w) { *w = 1 + static_cast<int>(rng() % maxw); *col = static_cast<int>(rng() % (ZPL_WIN_COLS + 1 - *w)); };
    int col, w;
    s.fill[0] = static_cast<uint16_t>(rng() % 64);
    s.fill[1] = static_cast<uint16_t>(0x1000 | (rng() % 2) | ((rng() & 1) ? 0x08 : 0) | ((rng() & 1) ? 0x10 : 0));   // an opaque tile, no T
    s.glassMode = ZPL_GLASS_AVERAGE;
    switch (id) {
        case 1: span(19, &col, &w); addBox(s, rng, col, 2, w, 4, ZPL_BOX_SOLID);        // 21 rows of at most 19 cells: the cell pool holds 408
                span(19, &col, &w); addBox(s, rng, col, 9, w, 8, ZPL_BOX_SOLID);
                span(19, &col, &w); addBox(s, rng, col, 22, w, 9, ZPL_BOX_SOLID); break;
        case 2: addBox(s, rng, 0, 1, 1, 3, ZPL_BOX_SOLID); addBox(s, rng, 31, 5, 1, 2, ZPL_BOX_SOLID); addBox(s, rng, 0, 9, 32, 3, ZPL_BOX_SOLID);
                addBox(s, rng, 30, 14, 2, 2, ZPL_BOX_SOLID); addBox(s, rng, 0, 18, 2, 2, ZPL_BOX_SOLID); addBox(s, rng, 1, 22, 31, 3, ZPL_BOX_SOLID);
                addBox(s, rng, 0, 26, 31, 3, ZPL_BOX_SOLID); break;
        case 3: span(4, &col, &w); addBox(s, rng, col, 3, w, 3, ZPL_BOX_OVERLAY);
                span(4, &col, &w); addBox(s, rng, col, 10, w, 5, ZPL_BOX_OVERLAY);
                span(4, &col, &w); addBox(s, rng, col, 20, w, 8, ZPL_BOX_OVERLAY); break;
        case 4: case 5: case 6:
                s.glassMode = static_cast<unsigned>(id - 3);
                span(3, &col, &w); addBox(s, rng, col, 3, w, 4, ZPL_BOX_GLASS);
                span(3, &col, &w); addBox(s, rng, col, 12, w, 3, ZPL_BOX_GLASS); break;
        case 7: s.glassMode = ZPL_GLASS_AVERAGE;                      // text on glass: the same cells, both kinds (needs more budget than the shipped one)
                addBox(s, rng, 6, 4, 3, 3, ZPL_BOX_GLASS); addBox(s, rng, 6, 4, 3, 3, ZPL_BOX_OVERLAY); break;
        case 8: s.glassMode = ZPL_GLASS_ADD;
                span(20, &col, &w); addBox(s, rng, col, 2, w, 4, ZPL_BOX_SOLID);
                span(3, &col, &w); addBox(s, rng, col, 8, w, 2, ZPL_BOX_OVERLAY);
                span(3, &col, &w); addBox(s, rng, col, 12, w, 2, ZPL_BOX_GLASS); break;
        default: break;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <layers.bin> [seed]\n", argv[0]); return 2; }
    const unsigned seed = argc > 2 ? std::atoi(argv[2]) : 1;
    std::vector<uint8_t> code;
    if (FILE* f = std::fopen(argv[1], "rb")) {
        uint8_t buf[4096]; size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) code.insert(code.end(), buf, buf + n);
        std::fclose(f);
    } else { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const bool verbose = std::getenv("LAYERS_VERBOSE") != nullptr;

    std::mt19937 rng(seed);
    Scene scene = makeScene(rng);
    System sys;
    sys.getDisplay().setRenderMode(RenderMode::RGBA16);
    PPU& ppu = sys.getPPU();
    for (int b = 0; b < 4; b++) for (int t = 0; t < 64; t++) uploadTile(ppu, b, t, scene.tiles[b][t]);
    loadScene(sys, scene);
    addSprites(scene, rng, 0, 0);
    const int zero[4] = {0, 0, 0, 0};
    setControl(ppu, zero, 0);
    loadSprites(sys, scene);
    ppu.loadMicrocode(code.data(), code.size(), 0);   // palette is loaded by the microcode itself, after the tile uploads
    ppu.setExecutionPointer(0);

    struct Case { const char* name; int sc[4]; int sprites; int count; int budget; };
    const Case cases[] = {
        {"BG only, tile-aligned scroll",            {0, 0, 0, 0},          0, 0, 0},
        {"BG only, fine X and Y on both layers",    {3, 3, 5, 6},          0, 0, 0},
        {"BG only, big scroll, wraps both axes",    {457, 301, 130, 509},  0, 0, 0},
        {"BG only, different phases per layer",     {19, 1, 250, 7},       0, 0, 0},
        {"sprites, fine scroll",                    {3, 3, 5, 6},          1, 14, 40},
        {"sprites, aligned scroll",                 {0, 0, 0, 0},          1, 14, 40},
        {"sprites, wrapping scroll",                {457, 301, 130, 509},  1, 20, 60},
    };
    for (const Case& c : cases) {
        unsigned dropped = 0;
        clearWindow(scene); loadWindow(sys, scene);
        for (int tries = 0; tries < 60; tries++) {   // the reference draws every sprite, so judge scenes the budget admits whole
            addSprites(scene, rng, c.count, c.budget);
            dropped = loadSprites(sys, scene);
            if (dropped == 0) break;
        }
        setControl(ppu, c.sc, c.sprites);
        // The harness rewrites the one buffer the PPU is walking (a cart double-buffers): let the transition frames pass, then judge.
        for (int f = 0; f < 2; f++) sys.runCycles(System::CYCLES_PER_FRAME);
        w16(ppu, 0x0364, 0);
        int status = 0;
        if (verbose) std::printf("   [before case: EP=$%04X REC=$%04X NREC=%u ORIG=%u frames=%u pending=%u]\n", ppu.getExecutionPointer(), ppu.getRegister(44), ppu.getRegister(45), ppu.getRegister(53), ppu.readMemory(ZPL_CTRL_FRAMES) | (ppu.readMemory(ZPL_CTRL_FRAMES + 1) << 8), ppu.readMemory(0x0360));
        const int bad = runCase(sys, scene, c.sc, 3, verbose, &status);
        const bool ok = bad == 0 && (status & 3) == 0 && dropped == 0;
        std::printf("  %s: %s (%d sprites, %u pieces dropped; %d wrong pixels; late/overflow bits %d; frames produced %u)\n", ok ? "ok  " : "FAIL", c.name,
                    static_cast<int>(scene.sprites.size()), dropped, bad, status & 3, ppu.readMemory(ZPL_CTRL_FRAMES) | (ppu.readMemory(ZPL_CTRL_FRAMES + 1) << 8));
        if (!ok) failures++;
        w16(ppu, 0x0364, 0);
    }

    {   // the window layer: boxes of each kind over every scroll, with and without sprites (which it covers), against the per-pixel reference
        struct WinCase { const char* name; int sc[4]; int scene; int sprites; int sprBudget; unsigned long budget; };
        const WinCase wcases[] = {
            {"window: solid boxes, fine scroll",              {3, 3, 5, 6},         1, 0, 0, 0},
            {"window: solid boxes, aligned scroll",           {0, 0, 0, 0},         1, 0, 0, 0},
            {"window: solid boxes, wrapping scroll",          {457, 301, 130, 509}, 1, 0, 0, 0},
            {"window: solid at the screen edges, fine scroll", {3, 3, 5, 6},        2, 0, 0, 0},
            {"window: solid at the screen edges, aligned",    {0, 0, 0, 0},         2, 0, 0, 0},
            {"window: solid at the screen edges, X only",     {5, 0, 3, 0},         2, 0, 0, 0},
            {"window: overlay boxes",                         {3, 3, 5, 6},         3, 0, 0, 0},
            {"window: glass, average",                        {3, 3, 5, 6},         4, 0, 0, 0},
            {"window: glass, subtract",                       {3, 3, 5, 6},         5, 0, 0, 0},
            {"window: glass, add",                            {0, 0, 0, 0},         6, 0, 0, 0},
            {"window: text on glass, budget 2000",            {0, 0, 0, 0},         7, 0, 0, 2000},
            {"window: all three kinds",                       {3, 3, 5, 6},         8, 0, 0, 0},
            {"window over sprites, fine scroll",              {3, 3, 5, 6},         8, 1, 14, 0},
            {"window over sprites, aligned scroll",           {0, 0, 0, 0},         1, 1, 14, 0},
            {"window over sprites, wrapping scroll",          {457, 301, 130, 509}, 3, 1, 14, 0},
        };
        for (const WinCase& c : wcases) {
            unsigned wdropped = 0, dropped = 0;
            for (int tries = 0; tries < 60; tries++) {
                g_budget = c.budget ? c.budget : ZPL_BAND_BUDGET;
                clearWindow(scene); buildWindowScene(scene, rng, c.scene);
                wdropped = loadWindow(sys, scene);
                addSprites(scene, rng, c.sprites ? 20 : 0, c.sprBudget);
                dropped = loadSprites(sys, scene);
                if (wdropped == 0 && dropped == 0) break;
            }
            setControl(ppu, c.sc, c.sprites);
            for (int f = 0; f < 2; f++) sys.runCycles(System::CYCLES_PER_FRAME);
            w16(ppu, 0x0364, 0);
            int status = 0;
            const int bad = runCase(sys, scene, c.sc, 3, verbose, &status);
            const bool ok = bad == 0 && (status & 3) == 0 && dropped == 0 && wdropped == 0;
            std::printf("  %s: %s (%d boxes, %u box-bands dropped, %d sprites, %u pieces dropped; %d wrong pixels; late/overflow bits %d)\n", ok ? "ok  " : "FAIL", c.name,
                        static_cast<int>(scene.boxes.size()), wdropped, static_cast<int>(scene.sprites.size()), dropped, bad, status & 3);
            if (!ok) failures++;
            w16(ppu, 0x0364, 0);
        }
        g_budget = ZPL_BAND_BUDGET;
        clearWindow(scene); loadWindow(sys, scene);
    }

    std::vector<unsigned long> budgetList = {ZPL_BAND_BUDGET};   // LAYERS_BUDGET=a,b,c: the saturation tests run at each
    if (const char* list = std::getenv("LAYERS_BUDGET")) { budgetList.clear(); for (const char* q = list; *q;) { budgetList.push_back(std::strtoul(q, const_cast<char**>(&q), 10)); if (*q == ',') q++; } }

    for (size_t wbi = 0; wbi < budgetList.size(); wbi++) {   // saturation with a window: whatever the window shape, the sprites the builder still admits never make a band late
        g_budget = budgetList[wbi];
        std::printf("== saturation with the window layer at a per-band budget of %lu, fine scroll on both layers\n", g_budget);
        static const int scrolls[][4] = {{3, 3, 5, 6}, {0, 0, 0, 0}, {1, 7, 2, 4}, {7, 1, 6, 2}, {5, 5, 0, 3}};
        const int variants[][3] = {{2, 4, 0}, {4, 2, 0}, {8, 1, 5}, {4, 4, 0}, {6, 4, 3}, {16, 4, 3}, {16, 1, 7}};
        const char* shapeNames[] = {"solid 28x12", "solid full-width strip", "solid 1-wide column", "overlay 4 + glass 3", "solid 2-wide + overlay", "overlay and glass on the same rows", "solid 12x32 (tall)"};
        int lateRuns = 0;
        for (int shape = 0; shape < 7; shape++) for (const auto& sc : scrolls) for (const auto& v : variants) {
            clearWindow(scene);
            scene.fill[0] = 3; scene.fill[1] = 0x1000; scene.glassMode = ZPL_GLASS_AVERAGE;
            switch (shape) {
                case 0: addBox(scene, rng, 2, 10, 28, 12, ZPL_BOX_SOLID); break;
                case 1: addBox(scene, rng, 0, 24, 32, 8, ZPL_BOX_SOLID); break;
                case 2: addBox(scene, rng, 7, 0, 1, 32, ZPL_BOX_SOLID); break;
                case 3: addBox(scene, rng, 5, 4, 4, 6, ZPL_BOX_OVERLAY); addBox(scene, rng, 9, 12, 3, 6, ZPL_BOX_GLASS); break;
                case 4: addBox(scene, rng, 3, 2, 2, 10, ZPL_BOX_SOLID); addBox(scene, rng, 20, 20, 4, 8, ZPL_BOX_OVERLAY); break;
                case 5: addBox(scene, rng, 4, 8, 3, 8, ZPL_BOX_GLASS); addBox(scene, rng, 4, 8, 3, 8, ZPL_BOX_OVERLAY); break;
                default: addBox(scene, rng, 6, 0, 12, 32, ZPL_BOX_SOLID); break;
            }
            loadWindow(sys, scene);
            scene.sprites.clear(); scene.cells.clear();
            for (int c = 0; c < 4; c++) { scene.cells.push_back(static_cast<uint16_t>(c + 1)); scene.cells.push_back(static_cast<uint16_t>((c % 4) | 0x1000)); }
            for (int band = 0; band < 32; band++) for (int k = 0; k < v[0]; k++) {
                ZPL_Sprite sp;
                sp.w = static_cast<unsigned short>(v[1]); sp.h = 1;
                sp.x = static_cast<short>((k * (256 / v[0] - 4)) % 224); sp.y = static_cast<short>(8 * band + v[2]);
                sp.flags = 0; sp.cells = ZPL_SPRCELLS;
                scene.sprites.push_back(sp);
            }
            const unsigned dropped = loadSprites(sys, scene);
            setControl(ppu, sc, 1);
            for (int f = 0; f < 4; f++) sys.runCycles(System::CYCLES_PER_FRAME);
            w16(ppu, 0x0364, 0);
            for (int f = 0; f < 4; f++) sys.runCycles(System::CYCLES_PER_FRAME);
            const int status = ppu.readMemory(0x0364);
            if (status & 3) std::printf("  LATE: window %s, %d sprites of %d tiles per band at +%d, scroll %d,%d,%d,%d: %u pieces dropped; bits %d\n", shapeNames[shape], v[0], v[1], v[2], sc[0], sc[1], sc[2], sc[3], dropped, status & 3);
            lateRuns += status & 3 ? 1 : 0;
            w16(ppu, 0x0364, 0);
        }
        const bool ok = lateRuns == 0;
        std::printf("  %s: saturated bands under a window never go late at %lu\n", ok ? "ok  " : "FAIL", g_budget);
        if (!ok && wbi == 0) failures++;
        clearWindow(scene); loadWindow(sys, scene);
        g_budget = ZPL_BAND_BUDGET;
    }

    {   // saturation at the shipped budget, then (LAYERS_BUDGET=1) a sweep of the budget to find where it stops fitting
        const std::vector<unsigned long>& budgets = budgetList;
        for (size_t bi = 0; bi < budgets.size(); bi++) {
            g_budget = budgets[bi];
            std::printf("== saturation at a per-band budget of %lu channel-cycles, fine scroll on both layers\n", g_budget);
            static int scrolls[64][4] = {{3, 3, 5, 6}, {0, 0, 0, 0}, {1, 7, 2, 4}, {7, 1, 6, 2}, {5, 5, 0, 3}};
            size_t nScrolls = 5;
            if (std::getenv("LAYERS_ALLPHASES")) {   // every (fine Y of BG0, fine Y of BG1) pair: the background's cost depends on nothing else
                nScrolls = 0;
                for (int o0 = 0; o0 < 8; o0++) for (int o1 = 0; o1 < 8; o1++) { scrolls[nScrolls][0] = 3; scrolls[nScrolls][1] = 16 + o0; scrolls[nScrolls][2] = 5; scrolls[nScrolls][3] = 40 + o1; nScrolls++; }
            }
            int lateRuns = 0;
            // {sprites per band, width in tiles, y offset inside the band}: all share one 4-cell array, so nothing but the budget limits them
            const int variants[][3] = {{2, 4, 0}, {2, 4, 3}, {4, 2, 0}, {8, 1, 5}, {4, 4, 0}, {6, 4, 3}, {16, 4, 3}, {16, 1, 7}};
            for (size_t si = 0; si < nScrolls; si++) for (const auto& v : variants) {
                const int* sc = scrolls[si];
                if (std::getenv("LAYERS_ALLPHASES") && !(v[0] == 16 || v[0] == 6 || (v[0] == 4 && v[1] == 4))) continue;
                scene.sprites.clear(); scene.cells.clear();
                for (int c = 0; c < 4; c++) { scene.cells.push_back(static_cast<uint16_t>(c + 1)); scene.cells.push_back(static_cast<uint16_t>((c % 4) | 0x1000)); }
                for (int band = 0; band < 32; band++) for (int k = 0; k < v[0]; k++) {
                    ZPL_Sprite sp;
                    sp.w = static_cast<unsigned short>(v[1]); sp.h = 1;
                    sp.x = static_cast<short>((k * (256 / v[0] - 4)) % 224); sp.y = static_cast<short>(8 * band + v[2]);
                    sp.flags = 0; sp.cells = ZPL_SPRCELLS;
                    scene.sprites.push_back(sp);
                }
                const unsigned dropped = loadSprites(sys, scene);
                setControl(ppu, sc, 1);
                for (int f = 0; f < 4; f++) sys.runCycles(System::CYCLES_PER_FRAME);
                w16(ppu, 0x0364, 0);
                for (int f = 0; f < 4; f++) sys.runCycles(System::CYCLES_PER_FRAME);
                const int status = ppu.readMemory(0x0364);
                if (status & 3) std::printf("  LATE: %2d sprites of %d tiles per band at +%d, scroll %d,%d,%d,%d: %u pieces dropped; bits %d\n", v[0], v[1], v[2], sc[0], sc[1], sc[2], sc[3], dropped, status & 3);
                lateRuns += status & 3 ? 1 : 0;
                w16(ppu, 0x0364, 0);
            }
            const bool ok = lateRuns == 0;
            std::printf("  %s: saturated bands never go late at %lu\n", ok ? "ok  " : "FAIL", g_budget);
            if (!ok && bi == 0) failures++;
        }
        g_budget = ZPL_BAND_BUDGET;
    }

    {   // sprite priority: behind BG0, between the layers, above both, against the per-pixel reference. BG0 gets the see-through tiles first, and L0FLAGS = T
        // (g_l0t) makes them see-through: only then does a sprite behind BG0 show at all (with BG0 opaque it must vanish, which the reference checks too)
        for (int c = 0; c < 64 * 64; c++) scene.map[0][c][1] = static_cast<uint16_t>((scene.map[0][c][1] & ~3) | (rng() % 4));
        loadScene(sys, scene);
        struct PrioCase { const char* name; int sc[4]; bool l0t; int winScene; int sprites; int tiles; };   // sprites/tiles: fewer under a window, which takes its share of the band budget
        const PrioCase pcases[] = {
            {"priority: BG0 opaque, fine scroll",              {3, 3, 5, 6},         false, 0, 24, 70},
            {"priority: BG0 opaque, aligned scroll",           {0, 0, 0, 0},         false, 0, 24, 70},
            {"priority: BG0 transparent, fine scroll",         {3, 3, 5, 6},         true,  0, 24, 70},
            {"priority: BG0 transparent, aligned scroll",      {0, 0, 0, 0},         true,  0, 24, 70},
            {"priority: BG0 transparent, wrapping scroll",     {457, 301, 130, 509}, true,  0, 24, 70},
            {"priority: BG0 transparent, different phases",    {19, 1, 250, 7},      true,  0, 24, 70},
            {"priority: under a solid window, fine scroll",    {3, 3, 5, 6},         true,  1, 16, 40},
            {"priority: under overlay and glass",              {3, 3, 5, 6},         true,  8, 10, 25},
        };
        for (const PrioCase& c : pcases) {
            unsigned dropped = 0, wdropped = 0;
            g_l0t = c.l0t;
            for (int tries = 0; tries < 60; tries++) {
                clearWindow(scene);
                if (c.winScene) buildWindowScene(scene, rng, c.winScene);
                wdropped = loadWindow(sys, scene);
                addSprites(scene, rng, c.sprites, c.tiles, true);
                dropped = loadSprites(sys, scene);
                if (dropped == 0 && wdropped == 0) break;
            }
            setControl(ppu, c.sc, 1);
            for (int f = 0; f < 2; f++) sys.runCycles(System::CYCLES_PER_FRAME);
            w16(ppu, 0x0364, 0);
            int status = 0;
            const int bad = runCase(sys, scene, c.sc, 3, verbose, &status);
            const bool ok = bad == 0 && (status & 3) == 0 && dropped == 0 && wdropped == 0;
            std::printf("  %s: %s (%d sprites, %u pieces dropped; %d wrong pixels; late/overflow bits %d)\n", ok ? "ok  " : "FAIL", c.name,
                        static_cast<int>(scene.sprites.size()), dropped, bad, status & 3);
            if (!ok) failures++;
            w16(ppu, 0x0364, 0);
        }
        // saturated bands with the pieces spread over the three priorities: the extra passes must not make a band late
        std::printf("== saturation with the pieces spread over the three priorities, BG0 transparent, fine scroll on both layers\n");
        g_l0t = true;
        clearWindow(scene); loadWindow(sys, scene);
        static const int scrolls[][4] = {{3, 3, 5, 6}, {0, 0, 0, 0}, {1, 7, 2, 4}, {7, 1, 6, 2}, {5, 5, 0, 3}};
        const int variants[][3] = {{2, 4, 0}, {4, 2, 0}, {8, 1, 5}, {4, 4, 0}, {6, 4, 3}, {16, 4, 3}, {16, 1, 7}};
        int lateRuns = 0;
        for (const auto& sc : scrolls) for (const auto& v : variants) {
            scene.sprites.clear(); scene.cells.clear();
            for (int c = 0; c < 4; c++) { scene.cells.push_back(static_cast<uint16_t>(c + 1)); scene.cells.push_back(static_cast<uint16_t>((c % 4) | 0x1000)); }
            for (int band = 0; band < 32; band++) for (int k = 0; k < v[0]; k++) {
                static const unsigned short prio[3] = {0, ZPL_SPR_PRIO_MID, ZPL_SPR_PRIO_BACK};
                ZPL_Sprite sp;
                sp.w = static_cast<unsigned short>(v[1]); sp.h = 1;
                sp.x = static_cast<short>((k * (256 / v[0] - 4)) % 224); sp.y = static_cast<short>(8 * band + v[2]);
                sp.flags = prio[k % 3]; sp.cells = ZPL_SPRCELLS;
                scene.sprites.push_back(sp);
            }
            const unsigned dropped = loadSprites(sys, scene);
            setControl(ppu, sc, 1);
            for (int f = 0; f < 4; f++) sys.runCycles(System::CYCLES_PER_FRAME);
            w16(ppu, 0x0364, 0);
            for (int f = 0; f < 4; f++) sys.runCycles(System::CYCLES_PER_FRAME);
            const int status = ppu.readMemory(0x0364);
            if (status & 3) std::printf("  LATE: %2d sprites of %d tiles per band at +%d, scroll %d,%d,%d,%d: %u pieces dropped; bits %d\n", v[0], v[1], v[2], sc[0], sc[1], sc[2], sc[3], dropped, status & 3);
            lateRuns += status & 3 ? 1 : 0;
            w16(ppu, 0x0364, 0);
        }
        std::printf("  %s: saturated bands with mixed priorities never go late\n", lateRuns == 0 ? "ok  " : "FAIL");
        if (lateRuns) failures++;
        g_l0t = false;
    }

    if (std::getenv("LAYERS_SWEEP")) {
        std::printf("== sprite load sweep (fine scroll on both layers): where the frame stops fitting\n");
        const int sc[4] = {3, 3, 5, 6};
        for (int budget = 20; budget <= 260; budget += 20) {
            int lateFrames = 0, wrong = 0; unsigned droppedTotal = 0;
            for (int trial = 0; trial < 3; trial++) {
                addSprites(scene, rng, 200, budget);
                droppedTotal += loadSprites(sys, scene);
                setControl(ppu, sc, 1);
                int status = 0;
                wrong += runCase(sys, scene, sc, 4, false, &status) > 0;
                lateFrames += status & 1;
                w16(ppu, 0x0364, 0);
            }
            std::printf("  %3d sprite cells: %d/3 trials had wrong pixels, late bit in %d/3, %u pieces dropped by the per-band cap\n", budget, wrong, lateFrames, droppedTotal);
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
