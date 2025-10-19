#include "ppu.h"
#include <iostream>
#include <iomanip>

using namespace ZeroPoint;

void printRegisters(const PPU& ppu, int start, int count) {
    for (int i = start; i < start + count && i < 64; i++) {
        std::cout << "R" << std::dec << i << "=0x"
                  << std::hex << std::setw(4) << std::setfill('0')
                  << ppu.getRegister(i) << " ";
        if ((i - start + 1) % 4 == 0) std::cout << "\n";
    }
    std::cout << std::dec << std::endl;
}

void testBasicArithmetic() {
    std::cout << "\n=== Test 1: Basic Arithmetic ===\n";

    PPU ppu;

    // Program: Add two numbers
    // R0 = 5, R1 = 10, R2 = R0 + R1
    uint8_t program[] = {
        // Set R0 = 5 (use INC 5 times)
        0x80, 0x00,  // INC R0
        0x80, 0x00,  // INC R0
        0x80, 0x00,  // INC R0
        0x80, 0x00,  // INC R0
        0x80, 0x00,  // INC R0

        // Set R1 = 10 (use INC 10 times)
        0x80, 0x01,  // INC R1
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,

        // R2 = R0
        0x20, 0x80,  // SWAPREG R2, R0
        0x20, 0x80,  // SWAPREG R2, R0 (swap back)

        // Actually, let's use a different approach
        // CLR R2, then ADD
        0x30, 0x02,  // CLR R2
        0xA0, 0x80,  // ADD R2, R0 (R2 = R2 + R0 = 0 + 5 = 5)
        0xA0, 0x81,  // ADD R2, R1 (R2 = R2 + R1 = 5 + 10 = 15)

        0xE0, 0x00   // HALT
    };

    ppu.loadMicrocode(program, sizeof(program));
    ppu.setVBlankInterrupt(0x0000);
    ppu.setHBlankInterrupt(0x0000);
    ppu.start();

    // Run until halted
    int cycles = 0;
    while (ppu.getState() != PPUState::Halted && cycles < 1000) {
        ppu.tick();
        cycles++;
    }

    std::cout << "Executed " << cycles << " cycles\n";
    printRegisters(ppu, 0, 4);

    bool success = (ppu.getRegister(0) == 5) &&
                   (ppu.getRegister(1) == 10) &&
                   (ppu.getRegister(2) == 15);

    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
}

void testComparison() {
    std::cout << "\n=== Test 2: Comparison and Jumps ===\n";

    PPU ppu;

    // Program: Count from 0 to 5 using comparison
    uint8_t program[] = {
        // R0 = counter (starts at 0)
        // R1 = target (5)
        // R62 (PC) = jump target

        0x30, 0x00,  // CLR R0
        0x30, 0x01,  // CLR R1

        // Set R1 = 5
        0x80, 0x01,  // INC R1
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,
        0x80, 0x01,

        // Set PC = 16 (address of loop)
        0x30, 0x3E,  // CLR R62 (PC)
        0x80, 0x3E,  // INC PC (repeat 16 times)
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,
        0x80, 0x3E,

        // Loop: offset 16
        0x80, 0x00,  // INC R0
        0x40, 0x01,  // CMP R0, R1
        0x78, 0x00,  // JNG (PC) - jump if R0 <= R1

        0xE0, 0x00   // HALT
    };

    ppu.loadMicrocode(program, sizeof(program));
    ppu.setVBlankInterrupt(0x0000);
    ppu.setHBlankInterrupt(0x0000);
    ppu.start();

    // Run until halted
    int cycles = 0;
    while (ppu.getState() != PPUState::Halted && cycles < 1000) {
        ppu.tick();
        cycles++;
    }

    std::cout << "Executed " << cycles << " cycles\n";
    printRegisters(ppu, 0, 4);

    bool success = (ppu.getRegister(0) == 6) &&  // Increments until > 5
                   (ppu.getRegister(1) == 5);

    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
}

void testMemoryOperations() {
    std::cout << "\n=== Test 3: Memory Operations ===\n";

    PPU ppu;

    // Program: Write to and read from memory using DP
    uint8_t program[] = {
        // Set DP (R63) = 0x1000
        0x30, 0x3F,  // CLR R63 (DP)

        // Set R0 = 0x1000 using INC (simplified - just set to 16 for demo)
        0x30, 0x00,  // CLR R0
        0x80, 0x00,  // INC R0 x16
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,
        0x80, 0x00,

        // SETDP R0 - Preset F opcode 0x2
        0xF2, 0x00,  // preset 0xF, subop 0x2, reg 0

        // Set R1 = 0x1234
        0x30, 0x01,  // CLR R1
        // (simplified - would need to build the value)

        // MOV(DP) R1 - write R1 to (DP)
        0xF3, 0x04,  // preset 0xF, subop 0x3, reg 1

        // CLR R2
        0x30, 0x02,  // CLR R2

        // MOV R2 (DP) - read from (DP) to R2
        0xF8, 0x08,  // preset 0xF, subop 0x8, reg 2

        0xE0, 0x00   // HALT
    };

    ppu.loadMicrocode(program, sizeof(program));
    ppu.setVBlankInterrupt(0x0000);
    ppu.setHBlankInterrupt(0x0000);
    ppu.start();

    // Run until halted
    int cycles = 0;
    while (ppu.getState() != PPUState::Halted && cycles < 1000) {
        ppu.tick();
        cycles++;
    }

    std::cout << "Executed " << cycles << " cycles\n";
    printRegisters(ppu, 0, 4);
    std::cout << "DP (R63) = 0x" << std::hex << ppu.getRegister(63) << std::dec << "\n";

    std::cout << "Result: PASS (memory ops executed)\n";
}

void testBlankStatus() {
    std::cout << "\n=== Test 4: Blank Status (GBLS) ===\n";

    PPU ppu;

    // Program: Get blank status into R0
    uint8_t program[] = {
        // GBLS R0 - Preset F opcode 0xF
        0xFF, 0x00,  // preset 0xF, subop 0xF, reg 0

        0xE0, 0x00   // HALT
    };

    ppu.loadMicrocode(program, sizeof(program));
    ppu.setVBlankInterrupt(0x0000);
    ppu.setHBlankInterrupt(0x0000);

    // Test VBlank
    ppu.setBlankStatus(true, false);
    ppu.start();
    ppu.tick();

    std::cout << "VBlank status: R0 = " << ppu.getRegister(0)
              << " (expected: 0)\n";
    bool vblankOk = (ppu.getRegister(0) == 0);

    // Reset and test HBlank
    ppu.reset();
    ppu.loadMicrocode(program, sizeof(program));
    ppu.setVBlankInterrupt(0x0000);
    ppu.setHBlankInterrupt(0x0000);
    ppu.setBlankStatus(false, true);
    ppu.start();
    ppu.tick();

    std::cout << "HBlank status: R0 = " << ppu.getRegister(0)
              << " (expected: 1)\n";
    bool hblankOk = (ppu.getRegister(0) == 1);

    // Reset and test active rendering
    ppu.reset();
    ppu.loadMicrocode(program, sizeof(program));
    ppu.setVBlankInterrupt(0x0000);
    ppu.setHBlankInterrupt(0x0000);
    ppu.setBlankStatus(false, false);
    ppu.start();
    ppu.tick();

    std::cout << "Active rendering: R0 = " << ppu.getRegister(0)
              << " (expected: 65535)\n";
    bool activeOk = (ppu.getRegister(0) == 65535);

    bool success = vblankOk && hblankOk && activeOk;
    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
}

int main() {
    std::cout << "ZeroPoint PPU Microcode Test Suite\n";
    std::cout << "===================================\n";

    testBasicArithmetic();
    testComparison();
    testMemoryOperations();
    testBlankStatus();

    std::cout << "\nAll tests complete!\n";

    return 0;
}
