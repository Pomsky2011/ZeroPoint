// zp_run_c: run a compiled C test image in the emulator and print what it recorded.
//
//   zp_run_c <bootrom.bin|none> <image.bin>
//
// The image is loaded at $80:0200 and run with PB = DB = $80 and the stack in bank $80 (the CPU's stack bank), so
// code, data and stack share Work RAM. The whole System is clocked (not just the CPU), so DMA, timers and the
// other peripherals the SDK drives actually run. The test's
// rec() (ZPdevtools/tests/cc/rec.asm) appends 16-bit words at $80:8100 and bumps the byte count at $80:8000; this prints
// them as signed decimals, space separated, on the last line of output (System prints a banner first), once the count stops changing (main returned or hung). Exit status 1 if
// the program never recorded anything. main returning (SP rising above its starting $F000) also ends the run. With ZP_TRACE=1 the last 60 instructions (PC, registers, SP) are printed to
// stderr at the end - the quickest way to see where a hung program is. ZP_STOP=<hex pc> also stops the first time the
// PC (in bank $80) reaches that address, so the trace ends on the way in. With ZP_TRACE set it also stops the moment
// the PC leaves the loaded image (a run-off-the-end or a wild return).
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <string>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <bootrom.bin|none> <image.bin>\n", argv[0]);
        return 2;
    }
    System sys;
    if (std::string(argv[1]) != "none" && !sys.loadBootROM(argv[1])) return 2;
    std::ifstream f(argv[2], std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), {});
    CPU& cpu = sys.getCPU();
    for (size_t i = 0; i < img.size(); i++) cpu.writeByte(0x800200 + i, img[i]);
    cpu.writeByte(0x808000, 0);
    cpu.writeByte(0x808001, 0);
    cpu.setPB(0x80);
    cpu.setDB(0x80);
    cpu.setSP(0xF000);
    cpu.setPC(0x0200);
    uint16_t last = 0xFFFF;
    long idle = 0;
    const bool trace = std::getenv("ZP_TRACE") != nullptr;
    const long stopPc = std::getenv("ZP_STOP") ? std::strtol(std::getenv("ZP_STOP"), nullptr, 16) : -1;
    std::deque<std::string> ring;
    uint64_t lastCycles = cpu.getCycleCount();
    for (long i = 0; i < 400000000L; i++) {
        sys.step();   // one master cycle; the rest of this loop runs once per retired CPU instruction
        if (cpu.getCycleCount() == lastCycles) continue;
        lastCycles = cpu.getCycleCount();
        if (trace) {
            char b[96];
            std::snprintf(b, sizeof b, "%02X:%04X op=%02X A=%04X X=%04X Y=%04X SP=%04X", cpu.getPB(), cpu.getPC(),
                          cpu.readByte((cpu.getPB() << 16) | cpu.getPC()), cpu.getA(), cpu.getX(), cpu.getY(), cpu.getSP());
            ring.push_back(b);
            if (ring.size() > 60) ring.pop_front();
        }
        if (stopPc >= 0 && cpu.getPB() == 0x80 && cpu.getPC() == stopPc) break;
        if (cpu.getSP() > 0xF000) break;   // main returned
        if (trace && (cpu.getPB() != 0x80 || cpu.getPC() < 0x0200 || cpu.getPC() >= 0x0200 + img.size())) break;
        const uint16_t n = cpu.readByte(0x808000) | (cpu.readByte(0x808001) << 8);
        if (n != last) { last = n; idle = 0; }
        else if (++idle > 3000000) break;
    }
    for (const auto& l : ring) std::fprintf(stderr, "%s\n", l.c_str());
    for (int i = 0; i < last / 2; i++)
        std::printf("%d ", (int)(int16_t)(cpu.readByte(0x808100 + 2 * i) | (cpu.readByte(0x808101 + 2 * i) << 8)));
    std::printf("\n");
    return last ? 0 : 1;
}
