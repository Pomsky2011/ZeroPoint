// Test individual PPU instructions to find bugs
#include "ppu.h"
#include "display.h"
#include <iostream>
#include <iomanip>

using namespace ZeroPoint;

void testADD() {
    std::cout << "\n=== Test ADD Instruction ===\n";
    PPU ppu;
    Display display;
    ppu.setDisplay(&display);

    // Program: Test ADD R63, R1
    // Set R63 = 0xE000, R1 = 0x0002
    // Execute ADD R63, R1
    // Expected: R63 = 0xE002
    uint8_t program[] = {
        // Set R63 = 0xE000
        0xE0, 0x1BF,  // TARREG 0, LSB, R63
        0xE1, 0x00,   // SETBYTE 0, 0x00
        0xE0, 0x3BF,  // TARREG 0, MSB, R63
        0xE1, 0xE0,   // SETBYTE 0, 0xE0

        // Set R1 = 0x0002
        0xE0, 0x101,  // TARREG 0, LSB, R1
        0xE1, 0x02,   // SETBYTE 0, 0x02
        0xE0, 0x301,  // TARREG 0, MSB, R1
        0xE1, 0x00,   // SETBYTE 0, 0x00

        // ADD R63, R1 - opcode 0xA, operand: R63 << 6 | R1 = 0xFC1
        0xAF, 0xC1,

        // HLT
        0xE0, 0x1BE,  // TARREG 0, LSB, PC (R62)
        0xE1, 0x1A,   // SETBYTE 0, 0x1A (current PC)
        0xF7, 0x00,   // JMR
    };

    ppu.loadMicrocode(program, sizeof(program));
    ppu.start();

    for (int i = 0; i < 20; i++) {
        ppu.tick();
    }

    uint16_t r63 = ppu.getRegister(63);
    std::cout << "R63 after ADD: 0x" << std::hex << r63 << std::dec;
    if (r63 == 0xE002) {
        std::cout << " ✓ PASS\n";
    } else {
        std::cout << " ✗ FAIL (expected 0xE002)\n";
    }
}

void testMOVDP() {
    std::cout << "\n=== Test MOVDP Instruction ===\n";
    PPU ppu;
    Display display;
    ppu.setDisplay(&display);

    // Test: Set DP = 0xE000, R0 = 0xFFFF, execute MOVDP R0
    // Expected: Memory[0xE000] = 0xFF, Memory[0xE001] = 0xFF
    uint8_t program[] = {
        // Set DP (R63) = 0xE000
        0xE0, 0x1BF,  // TARREG 0, LSB, R63
        0xE1, 0x00,   // SETBYTE 0, 0x00
        0xE0, 0x3BF,  // TARREG 0, MSB, R63
        0xE1, 0xE0,   // SETBYTE 0, 0xE0

        // Set R0 = 0xFFFF
        0xE0, 0x100,  // TARREG 0, LSB, R0
        0xE1, 0xFF,   // SETBYTE 0, 0xFF
        0xE0, 0x300,  // TARREG 0, MSB, R0
        0xE1, 0xFF,   // SETBYTE 0, 0xFF

        // MOVDP R0 - Preset F opcode 3, suboperand = R0 << 2 = 0x00
        0xF3, 0x00,

        // HLT
        0xE0, 0x1BE,
        0xE1, 0x1C,
        0xF7, 0x00,
    };

    ppu.loadMicrocode(program, sizeof(program));
    ppu.start();

    for (int i = 0; i < 20; i++) {
        ppu.tick();
    }

    uint8_t byte0 = ppu.readMemory(0xE000);
    uint8_t byte1 = ppu.readMemory(0xE001);
    uint16_t value = byte0 | (byte1 << 8);

    std::cout << "Memory[0xE000-0xE001]: 0x" << std::hex << value << std::dec;
    if (value == 0xFFFF) {
        std::cout << " ✓ PASS\n";
    } else {
        std::cout << " ✗ FAIL (expected 0xFFFF, got 0x" << std::hex << value << std::dec << ")\n";
        std::cout << "  byte0=0x" << std::hex << (int)byte0 << ", byte1=0x" << (int)byte1 << std::dec << "\n";
    }
}

void testCMPandJMZ() {
    std::cout << "\n=== Test CMP and JMZ Instructions ===\n";
    PPU ppu;
    Display display;
    ppu.setDisplay(&display);

    // Test: Compare two equal values and jump if zero
    uint8_t program[] = {
        // Set R0 = 5
        0x30, 0x00,   // CLR R0
        0x80, 0x00,   // INC R0
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,

        // Set R1 = 5
        0x30, 0x01,   // CLR R1
        0x80, 0x01,   // INC R1
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,

        // CMP R0, R1 (should set zero flag)
        0x40, 0x01,   // opcode 4, operand: R0 << 6 | R1 = 0x001

        // Set PC to jump target (offset 32 = 0x20)
        0xE0, 0x1BE,  // TARREG 0, LSB, PC
        0xE1, 0x20,   // SETBYTE 0, 0x20
        0xE0, 0x3BE,  // TARREG 0, MSB, PC
        0xE1, 0x00,   // SETBYTE 0, 0x00

        // JMZ - should jump
        0x60, 0x00,   // opcode 6, bit 11 = 0 (JMZ)

        // If jump fails, set R2 = 99 (error marker)
        0x30, 0x02,   // CLR R2
        0xE0, 0x102,  // TARREG 0, LSB, R2
        0xE1, 0x63,   // SETBYTE 0, 99

        // Jump target (offset 32)
        // Set R2 = 42 (success marker)
        0x30, 0x02,   // CLR R2  (offset 32 = 0x20)
        0xE0, 0x102,  // TARREG 0, LSB, R2
        0xE1, 0x2A,   // SETBYTE 0, 42

        // HLT
        0xE0, 0x1BE,
        0xE1, 0x2A,
        0xF7, 0x00,
    };

    ppu.loadMicrocode(program, sizeof(program));
    ppu.start();

    for (int i = 0; i < 50; i++) {
        ppu.tick();
    }

    uint16_t r0 = ppu.getRegister(0);
    uint16_t r1 = ppu.getRegister(1);
    uint16_t r2 = ppu.getRegister(2);

    std::cout << "R0=" << r0 << ", R1=" << r1 << ", R2=" << r2;
    if (r2 == 42) {
        std::cout << " ✓ PASS (jump worked)\n";
    } else if (r2 == 99) {
        std::cout << " ✗ FAIL (jump didn't work)\n";
    } else {
        std::cout << " ✗ FAIL (unexpected R2 value)\n";
    }
}

int main() {
    std::cout << "=== PPU Instruction Tests ===\n";

    testADD();
    testMOVDP();
    testCMPandJMZ();

    std::cout << "\n=== Tests Complete ===\n";
    return 0;
}
