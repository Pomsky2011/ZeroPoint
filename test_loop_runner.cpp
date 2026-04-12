#include "ppu.h"
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace ZeroPoint;

int main() {
    PPU ppu;

    // Load the test program
    std::ifstream file("../ZPdevtools/test_loop.bin", std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open test_loop.bin\n";
        return 1;
    }

    // Read entire file
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    uint8_t* program = new uint8_t[size];
    file.read((char*)program, size);
    file.close();

    std::cout << "Loaded " << size << " bytes\n";

    // Load and start
    ppu.loadMicrocode(program, size);
    ppu.start();
    delete[] program;

    // Run with detailed tracing
    int cycles = 0;
    uint16_t lastPC = 0;
    int samePC_count = 0;

    while (ppu.getState() != PPUState::Halted && cycles < 1000) {
        uint16_t currentPC = ppu.getExecutionPointer();
        uint16_t r10 = ppu.getRegister(10);
        uint16_t r15 = ppu.getRegister(15);
        uint16_t r62 = ppu.getRegister(62);

        // Print every iteration or when PC changes
        if (cycles < 50 || currentPC != lastPC) {
            std::cout << "Cycle " << std::dec << cycles
                      << ": PC=0x" << std::hex << std::setw(4) << std::setfill('0') << currentPC
                      << " R10=" << std::dec << r10
                      << " R15=" << r15
                      << " R62=0x" << std::hex << std::setw(4) << std::setfill('0') << r62
                      << "\n";
        }

        // Detect infinite loop
        if (currentPC == lastPC) {
            samePC_count++;
            if (samePC_count > 20) {
                std::cout << "\nERROR: Stuck at PC=0x" << std::hex << currentPC << "!\n";
                break;
            }
        } else {
            samePC_count = 0;
        }

        lastPC = currentPC;
        ppu.tick();
        cycles++;
    }

    std::cout << "\nFinal state after " << std::dec << cycles << " cycles:\n";
    std::cout << "R10 (COUNTER) = " << ppu.getRegister(10) << "\n";
    std::cout << "R15 (TARGET) = " << ppu.getRegister(15) << "\n";
    std::cout << "R62 (PC) = 0x" << std::hex << ppu.getRegister(62) << "\n";
    std::cout << "Execution pointer = 0x" << ppu.getExecutionPointer() << "\n";
    std::cout << "State: " << (ppu.getState() == PPUState::Halted ? "HALTED" : "RUNNING") << "\n";

    bool success = (ppu.getRegister(10) == 6);
    std::cout << "\nResult: " << (success ? "PASS" : "FAIL") << "\n";

    return success ? 0 : 1;
}
