// fuzz_ppu: random PPU programs (self-modifying writes, interrupts, blank-line changes, resets mixed in) driven
// through either PPU::tick() one cycle at a time or PPU::tickMany() in random chunks, printing PPU::stateHash()
// after every chunk. Two uses:
//   - tick vs tickmany of the same build must print identical streams (tickMany's contract);
//   - the same seed built against two versions of the PPU (e.g. before/after changing the instruction decoder)
//     must print identical streams.
//
//   fuzz_ppu <seed> <chunks> <tick|tickmany>
#include "display.h"
#include "ppu.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s <seed> <chunks> <tick|tickmany>\n", argv[0]); return 2; }
    const uint32_t seed = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    const int chunks = std::atoi(argv[2]);
    const bool many = !std::strcmp(argv[3], "tickmany");

    std::mt19937 rng(seed);                                   // one stream, consumed identically by both modes
    auto rnd = [&](uint32_t n) { return static_cast<uint32_t>(rng() % n); };

    Display display;
    PPU ppu;
    ppu.setDisplay(&display);

    auto loadRandomProgram = [&]() {
        ppu.reset();
        std::vector<uint8_t> code(0x2000);
        for (size_t i = 0; i + 1 < code.size(); i += 2) {
            // bias towards the interesting opcodes, but keep every one reachable
            const uint16_t op = rnd(16);
            const uint16_t operand = (rnd(8) == 0) ? rnd(0x1000) : (rnd(0x40) << 6) | rnd(0x40);
            const uint16_t w = (op << 12) | operand;
            code[i] = w >> 8;
            code[i + 1] = w & 0xFF;
        }
        ppu.loadMicrocode(code.data(), code.size(), 0x0100);
        for (int r = 0; r < 64; r++) ppu.setRegister(r, static_cast<uint16_t>(rnd(0x2200)));
        ppu.setRegister(59, 0x0100 + 2 * rnd(0x800));          // vblank / hblank vectors into the code
        ppu.setRegister(60, 0x0100 + 2 * rnd(0x800));
        ppu.setRegister(61, 0x2000 + rnd(0x100));              // SP: stack above the code
        ppu.writeMemory(0x00F0, 0x30);                          // VOC: vblank + hblank interrupts on
        ppu.setExecutionPointer(0x0100 + 2 * rnd(0x800));
        ppu.start();
    };
    loadRandomProgram();

    bool vb = false, hb = false;
    for (int c = 0; c < chunks; c++) {
        if (ppu.getState() != PPUState::Running) loadRandomProgram();
        const uint32_t r = rnd(100);
        if (r < 12) { vb = rnd(2); hb = rnd(2); ppu.setBlankStatus(vb, hb); }
        if (r >= 12 && r < 24) {                                   // external write: data, or into the code
            const uint16_t addr = (rnd(2) ? 0x0100 + rnd(0x2000) : rnd(0x400));
            ppu.writeMemory(addr, static_cast<uint8_t>(rnd(256)));
        }
        if (r >= 24 && r < 27) ppu.raiseCpuInterrupt(0x0100 + 2 * rnd(0x800));
        if (r >= 27 && r < 30) ppu.setRegister(rnd(64), static_cast<uint16_t>(rnd(65536)));
        const uint32_t n = 1 + rnd(rnd(4) == 0 ? 2000 : 60);
        if (many) {
            ppu.tickMany(n);
        } else {
            for (uint32_t i = 0; i < n; i++) ppu.tick();
        }
        std::printf("%016llx\n", static_cast<unsigned long long>(ppu.stateHash()));
    }
    return 0;
}
