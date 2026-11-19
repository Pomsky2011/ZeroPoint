// fuzz_queue: differential fuzz of the PPU's tile-row queue and blit channels under the batched clock. Two Systems get
// the same random schedule - a random PPU program, random row launches ($0350 port) near the beam, render-mode flips,
// interrupts, stretches from a few cycles to a frame - one stepped a master cycle at a time (step()), the other
// batched (runCycles(), which goes through PPU::tickMany and its skipped stretches). Their state hashes (PPU, display,
// everything) must match after every chunk, and the final frame must too.
//
//   fuzz_queue <first-seed> <last-seed> [chunks]
#include "system.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace ZeroPoint;

static const uint16_t PAL = 0x5000, MAP = 0x6000;

static void w16(PPU& ppu, uint16_t addr, uint16_t v) { ppu.writeMemory(addr, v & 0xFF); ppu.writeMemory(addr + 1, v >> 8); }
static uint16_t r16(const PPU& ppu, uint16_t addr) { return ppu.readMemory(addr) | (ppu.readMemory(addr + 1) << 8); }

// a palette (every 16th entry see-through) and a few tiles in each bank, loaded the way a program does: SETTILE then the 64 pixel bytes at $0300
static void setup(System& sys, std::mt19937& rng) {
    sys.getDisplay().setRenderMode(RenderMode::RGBA16);
    PPU& ppu = sys.getPPU();
    for (int i = 0; i < 256; i++) {
        const uint16_t c = static_cast<uint16_t>(rng());
        w16(ppu, PAL + 2 * i, (i % 16 == 0) ? (c & ~1) : (c | 1));
    }
    static const uint8_t mini[] = {0xFD, 0x64, 0xF1, 0x6A, 0xF7, 0x00};          // SETTILEBANK R25, SETTILE R26 mode 2, JMR (parked at 4)
    for (int bank = 0; bank < 4; bank++) for (int id = 0; id < 24; id++) {
        ppu.loadMicrocode(mini, sizeof mini, 0);
        ppu.setRegister(25, static_cast<uint16_t>(bank)); ppu.setRegister(26, static_cast<uint16_t>(id));
        ppu.setRegister(62, 4);
        ppu.setExecutionPointer(0);
        if (ppu.getState() != PPUState::Running) ppu.start();
        for (int i = 0; i < 12; i++) ppu.tick();
        for (int p = 0; p < 64; p++) ppu.writeMemory(0x0300 + p, static_cast<uint8_t>(rng()));
    }
    static const uint8_t pal[] = {0xF2, 0x08, 0xF6, 0x00, 0xF7, 0x00};          // SETDP R2, PALETTE256, JMR (parked at 4)
    ppu.loadMicrocode(pal, sizeof pal, 0);
    ppu.setRegister(2, PAL); ppu.setRegister(62, 4);
    ppu.setExecutionPointer(0);
    for (int i = 0; i < 400; i++) ppu.tick();
    for (int i = 0; i < 0x400; i++) ppu.writeMemory(static_cast<uint16_t>(MAP + i), static_cast<uint8_t>(rng()));   // random cells
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <first-seed> <last-seed> [chunks]\n", argv[0]); return 2; }
    const unsigned first = static_cast<unsigned>(std::strtoul(argv[1], nullptr, 10));
    const unsigned last = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
    const int chunks = argc > 3 ? std::atoi(argv[3]) : 40;

    int bad = 0;

    // Directed: a program does the launch itself, after a few padding instructions (so the batched clock has cycles
    // "owed" when it does). The padding is SETDP R2 repeated; the launch is SETDP R2 / MOVDP R3 storing R3 = 1 at R2.
    //   kind 0: SETRENDMOD to 16-bit with a cell held on its rows in RGBA32 (an 8-row window; 16 rows frees it at once)
    //   kind 1: a row launch ($0360) for a cell whose rows are already free: it is dispatched the cycle after the store
    //   kind 2: a line blit ($0348): it completes a fixed time after the store
    //   kind 3: kind 0 with the mode change made by a store to the VOC ($00F0 = $30) instead of SETRENDMOD
    // In each the batched clock must see the effect at the cycle the instruction runs, not when it next looks.
    {
        std::mt19937 s1(99), s2(99);
        System a, b;
        setup(a, s1); setup(b, s2);
        System* both[2] = {&a, &b};
        std::mt19937 rng(7);
        auto rnd = [&](uint32_t n) { return static_cast<uint32_t>(rng() % n); };
        int started[4] = {0, 0, 0, 0};
        for (int trial = 0; trial < 600; trial++) {
            const int kind = trial % 4;
            const bool modeFlip = kind == 0 || kind == 3;
            const uint32_t pad = 1 + rnd(8);
            std::vector<uint8_t> prog;
            for (uint32_t i = 0; i < pad; i++) { prog.push_back(0xF2); prog.push_back(0x08); }   // SETDP R2
            if (kind == 0) { prog.push_back(0xF4); prog.push_back(0x00); }                       // SETRENDMOD 16-bit
            else { prog.push_back(0xF3); prog.push_back(0x0C); }                                  // MOVDP R3
            prog.push_back(0xF7); prog.push_back(0x00);                                           // JMR (R62: parked on itself)
            const uint16_t park = static_cast<uint16_t>(prog.size() - 2);
            for (System* s : both) {
                PPU& p = s->getPPU();
                p.writeMemory(0x00F0, modeFlip ? 0xB0 : 0x30);   // RGBA32 for kind 0, else RGBA16; interrupts on
                p.loadMicrocode(prog.data(), prog.size(), 0);
                p.setRegister(62, park);
                p.setExecutionPointer(park);
                if (p.getState() != PPUState::Running) p.start();
            }
            const uint32_t settle = 4000 + rnd(2000);   // lets the previous trial's rows drain
            for (uint32_t i = 0; i < settle; i++) a.step();
            b.runCycles(settle);
            const Display& d = a.getDisplay();
            const uint16_t cells = static_cast<uint16_t>(1 + rnd(3));
            int y = 0;
            if (modeFlip) {   // a mode change restarts the row space (windowStart = 0): rows 8-15 are held by 8 rows, free in 16
                for (System* s : both) { s->getPPU().writeMemory(0x00F0, 0x30); s->getPPU().writeMemory(0x00F0, 0xB0); }
                y = 1 + static_cast<int>(rnd(8));
            } else if (kind == 1) {   // the last row the window has reached: free right now
                int64_t r1 = d.getFrameRowOffset();
                while (d.rowIsClear(r1)) r1++;
                y = static_cast<int>(r1 - 8 - d.getFrameRowOffset());
            }
            const uint16_t lineCount = static_cast<uint16_t>(8 + rnd(120)), lineX = static_cast<uint16_t>(rnd(200)), lineY = static_cast<uint16_t>(d.getCurrentScanline() > 40 ? d.getCurrentScanline() - 20 : 100);
            for (System* s : both) {
                PPU& p = s->getPPU();
                if (kind == 2) {
                    w16(p, 0x0340, MAP); w16(p, 0x0342, lineX); w16(p, 0x0344, lineY); w16(p, 0x0346, lineCount);
                    p.setRegister(2, 0x0348); p.setRegister(3, 1);
                } else {
                    w16(p, 0x0350, MAP); w16(p, 0x0352, cells); w16(p, 0x0354, 0);
                    w16(p, 0x0356, static_cast<uint16_t>(y)); w16(p, 0x0358, 0); w16(p, 0x035A, 0); w16(p, 0x035C, 0x20);
                    w16(p, 0x035E, r16(p, 0x0366));
                    if (kind == 1) { p.setRegister(2, 0x0360); p.setRegister(3, 1); }
                    else {   // kinds 0 and 3: the launch is external, the program only flips the mode
                        w16(p, 0x0360, 1);
                        if (kind == 3) { p.setRegister(2, 0x00F0); p.setRegister(3, 0x0030); }
                    }
                }
                p.setExecutionPointer(0);
            }
            started[kind]++;
            const uint32_t n = 1 + rnd(300);
            for (uint32_t i = 0; i < n; i++) a.step();
            b.runCycles(n);
            if (a.stateHash() != b.stateHash()) {
                std::printf("directed kind %d trial %d (pad %u): state differs after %u cycles\n", kind, trial, pad, n);
                bad++;
                break;
            }
        }
        std::printf("directed launches: %d SETRENDMOD, %d row, %d line, %d VOC-store trials\n", started[0], started[1], started[2], started[3]);
    }

    for (unsigned seed = first; seed <= last; seed++) {
        std::mt19937 rng(seed);
        auto rnd = [&](uint32_t n) { return static_cast<uint32_t>(rng() % n); };

        System a, b;
        {   // the same setup for both: replay the same random stream
            std::mt19937 s1(seed * 7 + 1), s2(seed * 7 + 1);
            setup(a, s1); setup(b, s2);
        }
        System* both[2] = {&a, &b};
        std::vector<uint8_t> code(0x1000);
        auto loadProgram = [&]() {
            for (size_t i = 0; i + 1 < code.size(); i += 2) {
                const uint16_t op = rnd(16);
                const uint16_t operand = (rnd(8) == 0) ? rnd(0x1000) : (rnd(0x40) << 6) | rnd(0x40);
                uint16_t w = (op << 12) | operand;
                if (rnd(24) == 0) w = static_cast<uint16_t>(0xF400 | (rnd(2) ? 0x80 : 0));   // SETRENDMOD: moves the window under a held cell
                code[i] = w >> 8;
                code[i + 1] = w & 0xFF;
            }
            uint16_t regs[64];
            for (int r = 0; r < 64; r++) regs[r] = static_cast<uint16_t>(rnd(0x2200));
            regs[59] = 0x0400 + 2 * rnd(0x800); regs[60] = 0x0400 + 2 * rnd(0x800); regs[61] = 0x2000 + rnd(0x100);
            const uint16_t ep = 0x0400 + 2 * rnd(0x800);
            for (System* s : both) {
                PPU& p = s->getPPU();
                p.reset();
                p.loadMicrocode(code.data(), code.size(), 0x0400);
                for (int r = 0; r < 64; r++) p.setRegister(r, regs[r]);
                p.writeMemory(0x00F0, 0x30);   // vblank + hblank interrupts on
                p.setExecutionPointer(ep);
                p.start();
            }
        };
        loadProgram();

        bool same = true;
        for (int c = 0; c < chunks && same; c++) {
            if (a.getPPU().getState() != PPUState::Running) loadProgram();
            if (rnd(100) < 75) {   // launch a row, mostly near the beam, sometimes far ahead or long past
                const uint16_t src = static_cast<uint16_t>(MAP + 4 * rnd(64));
                const uint16_t count = static_cast<uint16_t>(1 + rnd(rnd(3) == 0 ? 130 : 40));
                const uint16_t x = static_cast<uint16_t>(static_cast<int>(rnd(300)) - 20);
                const int y = static_cast<int>(rnd(rnd(4) == 0 ? 600 : 300)) - 20;
                const uint16_t flags = static_cast<uint16_t>((rnd(2) ? 0x01 : 0) | (rnd(2) ? 0x02 : 0) | (rnd(2) ? 0x04 : 0) | (rnd(2) ? 0x08 : 0) | (rnd(4) << 4) | (rnd(2) ? 0x40 : 0));
                const uint16_t clip0 = static_cast<uint16_t>(y + static_cast<int>(rnd(8)));
                const uint16_t clip1 = static_cast<uint16_t>(clip0 + rnd(9));
                const uint16_t ahead = static_cast<uint16_t>(rnd(4) == 0 ? rnd(3) : 0);
                for (System* s : both) {
                    PPU& p = s->getPPU();
                    w16(p, 0x0350, src); w16(p, 0x0352, count); w16(p, 0x0354, x); w16(p, 0x0356, static_cast<uint16_t>(y));
                    w16(p, 0x0358, clip0); w16(p, 0x035A, clip1); w16(p, 0x035C, flags);
                    w16(p, 0x035E, static_cast<uint16_t>(r16(p, 0x0366) + ahead));
                    w16(p, 0x0360, 1);
                }
            }
            const uint32_t r = rnd(100);
            if (r < 12) {   // flip the render mode / rolling mode: the window the queue waits on changes under it
                const uint8_t v = static_cast<uint8_t>(0x30 | (rnd(4) << 6));
                for (System* s : both) s->getPPU().writeMemory(0x00F0, v);
            } else if (r < 20) {
                const uint16_t vec = static_cast<uint16_t>(0x0400 + 2 * rnd(0x800));
                for (System* s : both) s->getPPU().raiseCpuInterrupt(vec);
            } else if (r < 26) {   // a stray write into the cells or the code
                const uint16_t addr = rnd(2) ? static_cast<uint16_t>(MAP + rnd(0x400)) : static_cast<uint16_t>(0x0400 + rnd(0x1000));
                const uint8_t v = static_cast<uint8_t>(rnd(256));
                for (System* s : both) s->getPPU().writeMemory(addr, v);
            }
            const uint32_t n = 1 + rnd(rnd(5) == 0 ? 90000 : rnd(3) == 0 ? 3000 : 120);
            for (uint32_t i = 0; i < n; i++) a.step();
            b.runCycles(n);
            if (a.getMasterCycleCount() != b.getMasterCycleCount()) {
                std::printf("seed %u chunk %d: cycle counts differ (%llu vs %llu)\n", seed, c,
                            (unsigned long long)a.getMasterCycleCount(), (unsigned long long)b.getMasterCycleCount());
                same = false;
            } else if (a.stateHash() != b.stateHash()) {
                std::printf("seed %u chunk %d: state differs after %u cycles\n", seed, c, n);
                same = false;
            }
        }
        if (!same) bad++;
    }
    std::printf("%d mismatches in seeds %u..%u\n", bad, first, last);
    return bad ? 1 : 0;
}
