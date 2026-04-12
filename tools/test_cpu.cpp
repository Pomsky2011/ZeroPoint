#include "cpu.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>

using namespace ZeroPoint;

void printCPUState(const CPU& cpu) {
    std::cout << "CPU State:" << std::endl;
    std::cout << "  A:  0x" << std::hex << std::setw(4) << std::setfill('0') << cpu.getA() << std::endl;
    std::cout << "  X:  0x" << std::hex << std::setw(4) << std::setfill('0') << cpu.getX() << std::endl;
    std::cout << "  Y:  0x" << std::hex << std::setw(4) << std::setfill('0') << cpu.getY() << std::endl;
    std::cout << "  SP: 0x" << std::hex << std::setw(4) << std::setfill('0') << cpu.getSP() << std::endl;
    std::cout << "  D:  0x" << std::hex << std::setw(4) << std::setfill('0') << cpu.getD() << std::endl;
    std::cout << "  PC: 0x" << std::hex << std::setw(4) << std::setfill('0') << cpu.getPC() << std::endl;
    std::cout << "  PB: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)cpu.getPB() << std::endl;
    std::cout << "  DB: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)cpu.getDB() << std::endl;

    CPUFlags flags = cpu.getFlags();
    std::cout << "  Flags: "
              << (flags.N ? "N" : "-")
              << (flags.V ? "V" : "-")
              << (flags.M ? "M" : "-")
              << (flags.X ? "X" : "-")
              << (flags.D ? "D" : "-")
              << (flags.I ? "I" : "-")
              << (flags.Z ? "Z" : "-")
              << (flags.C ? "C" : "-") << std::endl;

    std::cout << "  Cycles: " << std::dec << cpu.getCycleCount() << std::endl;
    std::cout << "  Instructions: " << std::dec << cpu.getInstructionCount() << std::endl;
    std::cout << std::endl;
}

int main() {
    std::cout << "DEF88186 CPU Test" << std::endl;
    std::cout << "=================" << std::endl << std::endl;

    // Create CPU
    CPU cpu;

    // Create memory (64KB for testing)
    std::vector<uint8_t> memory(65536, 0);
    cpu.setMemory(memory.data(), memory.size());

    // Test 1: Simple LDA immediate
    std::cout << "Test 1: LDA #$42 (8-bit mode)" << std::endl;
    std::cout << "-------------------------------" << std::endl;

    // Reset CPU
    cpu.reset();

    // Write program: LDA #$42, HLT
    memory[0] = 0x37;  // LDA immediate
    memory[1] = 0x42;  // Value
    memory[2] = 0xFF;  // HLT

    printCPUState(cpu);

    // Run until halt
    for (int i = 0; i < 10 && !cpu.isHalted(); i++) {
        cpu.step();
    }

    printCPUState(cpu);

    if (cpu.getA() == 0x42) {
        std::cout << "✓ Test 1 PASSED: A = 0x42" << std::endl;
    } else {
        std::cout << "✗ Test 1 FAILED: Expected A = 0x42, got 0x" << std::hex << cpu.getA() << std::endl;
    }
    std::cout << std::endl;

    // Test 2: LDA absolute
    std::cout << "Test 2: LDA $1000 (absolute addressing)" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    cpu.reset();

    // Write value at address $1000
    memory[0x1000] = 0x99;

    // Write program: LDA $1000, HLT
    memory[0] = 0x38;  // LDA absolute
    memory[1] = 0x00;  // Address low
    memory[2] = 0x10;  // Address high
    memory[3] = 0xFF;  // HLT

    printCPUState(cpu);

    // Run until halt
    for (int i = 0; i < 10 && !cpu.isHalted(); i++) {
        cpu.step();
    }

    printCPUState(cpu);

    if (cpu.getA() == 0x99) {
        std::cout << "✓ Test 2 PASSED: A = 0x99" << std::endl;
    } else {
        std::cout << "✗ Test 2 FAILED: Expected A = 0x99, got 0x" << std::hex << cpu.getA() << std::endl;
    }
    std::cout << std::endl;

    // Test 3: ADC
    std::cout << "Test 3: ADC #$10 (8-bit addition)" << std::endl;
    std::cout << "-----------------------------------" << std::endl;

    cpu.reset();

    // Write program: LDA #$30, ADC #$10, HLT
    memory[0] = 0x37;  // LDA immediate
    memory[1] = 0x30;  // Value
    memory[2] = 0xB6;  // ADC immediate
    memory[3] = 0x10;  // Value
    memory[4] = 0xFF;  // HLT

    printCPUState(cpu);

    // Run until halt
    for (int i = 0; i < 10 && !cpu.isHalted(); i++) {
        cpu.step();
    }

    printCPUState(cpu);

    if ((cpu.getA() & 0xFF) == 0x40) {
        std::cout << "✓ Test 3 PASSED: A = 0x40 (0x30 + 0x10)" << std::endl;
    } else {
        std::cout << "✗ Test 3 FAILED: Expected A = 0x40, got 0x" << std::hex << (cpu.getA() & 0xFF) << std::endl;
    }
    std::cout << std::endl;

    // Test 4: Hardware LOOP
    std::cout << "Test 4: LOOP/LPEND (hardware loop)" << std::endl;
    std::cout << "-----------------------------------" << std::endl;

    cpu.reset();

    // Write program:
    // LDA #$00
    // LOOP #$0005
    // ADC #$01
    // LPEND
    // HLT
    memory[0] = 0x37;  // LDA immediate
    memory[1] = 0x00;  // Value = 0
    memory[2] = 0x13;  // LOOP
    memory[3] = 0x05;  // Count low
    memory[4] = 0x00;  // Count high (5 iterations)
    memory[5] = 0xB6;  // ADC immediate
    memory[6] = 0x01;  // Value = 1
    memory[7] = 0x14;  // LPEND
    memory[8] = 0xFF;  // HLT

    printCPUState(cpu);

    // Run until halt (max 100 instructions to prevent infinite loop)
    for (int i = 0; i < 100 && !cpu.isHalted(); i++) {
        cpu.step();
    }

    printCPUState(cpu);

    if ((cpu.getA() & 0xFF) == 0x05) {
        std::cout << "✓ Test 4 PASSED: A = 0x05 (looped 5 times, added 1 each time)" << std::endl;
    } else {
        std::cout << "✗ Test 4 FAILED: Expected A = 0x05, got 0x" << std::hex << (cpu.getA() & 0xFF) << std::endl;
    }
    std::cout << std::endl;

    // Test 5: SDB (Set Data Bank)
    std::cout << "Test 5: SDB #$02 (set data bank)" << std::endl;
    std::cout << "----------------------------------" << std::endl;

    cpu.reset();

    // Write program: SDB #$02, HLT
    memory[0] = 0x1B;  // SDB
    memory[1] = 0x02;  // Bank = 2
    memory[2] = 0xFF;  // HLT

    printCPUState(cpu);

    // Run until halt
    for (int i = 0; i < 10 && !cpu.isHalted(); i++) {
        cpu.step();
    }

    printCPUState(cpu);

    if (cpu.getDB() == 0x02) {
        std::cout << "✓ Test 5 PASSED: DB = 0x02" << std::endl;
    } else {
        std::cout << "✗ Test 5 FAILED: Expected DB = 0x02, got 0x" << std::hex << (int)cpu.getDB() << std::endl;
    }
    std::cout << std::endl;

    std::cout << "=================" << std::endl;
    std::cout << "All tests complete!" << std::endl;

    return 0;
}
