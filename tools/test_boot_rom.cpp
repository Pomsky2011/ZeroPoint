// Regression test for the Boot ROM handoff path (System -> Boot ROM stub ->
// cartridge). Builds a tiny synthetic .zpb cartridge in a temp file so the
// test has no external fixture dependencies, then verifies:
//   1. Reset lands the CPU in the Boot ROM ($E0:0000), not the cartridge.
//   2. The stub reassembles the 24-bit entry point from $D80049-$D8004B
//      correctly (low/mid bytes are distinct here, so a swapped or dropped
//      byte would land the jump somewhere other than the loop instruction).
//   3. Cartridge code actually runs (writes its marker byte to Work RAM).
#include "system.h"
#include "rom.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

// Cartridge machine code, hand-assembled (cpuasm) from:
//   .org $0203
//   start:  LDA #$42
//           STA $800010
//   loop:   JMP loop
// #const immediates are always a fixed 3 bytes (opcode + 16-bit operand)
// regardless of M/X - see CPU::addrImmediate()'s comment in src/cpu.cpp.
static const uint8_t kCartCode[] = {
    0x37, 0x42, 0x00,             // LDA #$42
    0x55, 0x10, 0x00, 0x80,       // STA $800010
    0x0F, 0x0A, 0x02,             // JMP $020A (loop)
};
static const uint32_t kCartOrg = 0x0203;       // where kCartCode is placed
static const uint32_t kCartEntry = kCartOrg;   // low=0x03 mid=0x02 hi=0x00
static const uint16_t kExpectedLoopPC = 0x020A;

int main() {
    std::vector<uint8_t> payload(kCartOrg, 0x00);
    payload.insert(payload.end(), kCartCode, kCartCode + sizeof(kCartCode));

    ZPBHeader header{};
    std::memcpy(header.magic, "ZPB", 4);
    header.version = 1;
    header.reserved = 0;
    header.headerSize = sizeof(ZPBHeader);
    header.romSize = static_cast<uint32_t>(payload.size());
    header.entryPoint = kCartEntry;
    std::strncpy(header.title, "BOOTROM_TEST", sizeof(header.title));
    std::strncpy(header.developer, "TEST", sizeof(header.developer));

    const char* path = "test_boot_rom_fixture.zpb";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        out.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    }

    System system;
    bool ok = system.loadROM(path);
    std::remove(path);
    if (!ok) {
        std::printf("FAIL: could not load synthetic cartridge\n");
        return 1;
    }

    system.reset();
    if (system.getCPU().getPB() != 0xE0 || system.getCPU().getPC() != 0x0000) {
        std::printf("FAIL: reset did not land in Boot ROM ($E0:0000); got $%02X:%04X\n",
                     system.getCPU().getPB(), system.getCPU().getPC());
        return 1;
    }

    // 9 Boot ROM instructions, then the cartridge's LDA/STA/JMP.
    for (int i = 0; i < 20; i++) {
        system.getCPU().step();
    }

    uint8_t marker = system.getCPU().readByte(0x800010);
    ok = (system.getCPU().getPB() == 0x00) &&
         (system.getCPU().getPC() == kExpectedLoopPC) &&
         (marker == 0x42);

    if (!ok) {
        std::printf("FAIL: PB=$%02X PC=$%04X marker=$%02X (expect $00:%04X marker $42)\n",
                     system.getCPU().getPB(), system.getCPU().getPC(),
                     marker, kExpectedLoopPC);
        return 1;
    }

    std::printf("PASS: Boot ROM handed off to cartridge entry $00:%04X, marker written\n",
                kExpectedLoopPC);
    return 0;
}
