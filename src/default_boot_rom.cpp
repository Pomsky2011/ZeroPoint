#include "default_boot_rom.h"

namespace ZeroPoint {

// Assembled (cpuasm) from examples/boot-rom/boot.asm. 35 bytes:
//   LDA #$10            ; opcode for JMP long
//   STA $800000
//   LDA $D80049          ; cartridge entry point, low byte
//   STA $800001
//   LDA $D8004A          ; cartridge entry point, mid byte
//   STA $800002
//   LDA $D8004B          ; cartridge entry point, bank byte
//   STA $800003
//   JMP $800000          ; execute the JMP long we just wrote
// #const immediates are always a fixed 3 bytes (opcode + 16-bit operand)
// regardless of M/X - see CPU::addrImmediate()'s comment in src/cpu.cpp -
// hence LDA #$10 below being 3 bytes (37 10 00), not 2.
const uint8_t kDefaultBootROM[] = {
    0x37, 0x10, 0x00,
    0x55, 0x00, 0x00, 0x80,
    0x3D, 0x49, 0x00, 0xD8,
    0x55, 0x01, 0x00, 0x80,
    0x3D, 0x4A, 0x00, 0xD8,
    0x55, 0x02, 0x00, 0x80,
    0x3D, 0x4B, 0x00, 0xD8,
    0x55, 0x03, 0x00, 0x80,
    0x10, 0x00, 0x00, 0x80,
};
const size_t kDefaultBootROMSize = sizeof(kDefaultBootROM);

} // namespace ZeroPoint
