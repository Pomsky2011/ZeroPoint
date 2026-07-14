#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include "apu.h"

void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " <program.bin> [max_cycles]\n";
    std::cerr << "\nLoads and executes an APU program.\n";
    std::cerr << "program.bin: Binary file containing APU instructions\n";
    std::cerr << "max_cycles:  Maximum cycles to run (default: 1,000,000)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const char* binFile = argv[1];
    uint64_t maxCycles = (argc >= 3) ? std::stoull(argv[2]) : 1000000;

    // Load binary file
    std::ifstream file(binFile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << binFile << "\n";
        return 1;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "Error: Cannot read file: " << binFile << "\n";
        return 1;
    }

    file.close();

    std::cout << "Loaded " << size << " bytes from " << binFile << "\n";

    // Create APU and load ROM
    APU apu;
    apu.loadROM(buffer.data(), size, 0x8000);  // Load to BIOS region
    apu.setPC(0x8000);  // Start at BIOS

    std::cout << "Starting APU execution...\n";
    std::cout << "Max cycles: " << maxCycles << "\n";
    std::cout << "Press Ctrl+C to stop\n";
    std::cout << "----------------------------------------\n";

    // Run
    uint64_t startCycles = apu.getCycleCount();
    uint64_t lastReportCycles = startCycles;

    while (apu.getCycleCount() - startCycles < maxCycles && !apu.isHalted()) {
        apu.step();

        // Report progress every 100,000 cycles
        if (apu.getCycleCount() - lastReportCycles >= 100000) {
            std::cout << "Cycles: " << apu.getCycleCount()
                      << "  Instructions: " << apu.getInstructionCount()
                      << "  PC: 0x" << std::hex << apu.getPC() << std::dec << "\n";
            lastReportCycles = apu.getCycleCount();
        }
    }

    std::cout << "----------------------------------------\n";
    std::cout << "Execution stopped.\n";
    std::cout << "Total cycles: " << apu.getCycleCount() << "\n";
    std::cout << "Total instructions: " << apu.getInstructionCount() << "\n";
    std::cout << "Final PC: 0x" << std::hex << apu.getPC() << std::dec << "\n";
    std::cout << "Halted: " << (apu.isHalted() ? "Yes" : "No") << "\n";

    // Print register state
    std::cout << "\nRegister State (R0-R15):\n";
    for (int i = 0; i < 16; i++) {
        std::cout << "R" << i << ": 0x" << std::hex
                  << static_cast<int>(apu.getRegister(i)) << std::dec;
        if ((i + 1) % 4 == 0) {
            std::cout << "\n";
        } else {
            std::cout << "  ";
        }
    }

    return 0;
}
