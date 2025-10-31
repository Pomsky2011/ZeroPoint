// Debug test for ARM64 JIT crash
#include "ppu.h"
#include "ppu_jit.h"
#include "display.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

using namespace ZeroPoint;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <demo.bin>" << std::endl;
        return 1;
    }

    // Load binary file
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << argv[1] << std::endl;
        return 1;
    }

    std::vector<uint8_t> program((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    file.close();

    std::cout << "Loaded " << program.size() << " bytes\n";

    // Create display and PPU
    Display display;
    PPU ppu;
    ppu.setDisplay(&display);
    ppu.loadMicrocode(program.data(), program.size());
    ppu.start();

    std::cout << "PPU started\n";

    // Test JIT compilation
    std::cout << "JIT supported: " << (PPUJIT::isSupported() ? "YES" : "NO") << "\n";
    std::cout << "Architecture: " << PPUJIT::getArchitecture() << "\n";

    if (!PPUJIT::isSupported()) {
        std::cout << "JIT not supported on this platform\n";
        return 0;
    }

    std::cout << "\n=== Creating PPUJIT object ===\n";
    PPUJIT* jit = new PPUJIT();
    std::cout << "PPUJIT created\n";

    std::cout << "\n=== Compiling block ===\n";
    JITBlock* block = jit->compileBlock(&ppu, 0, 1000);

    if (!block) {
        std::cout << "Compilation failed\n";
        delete jit;
        return 1;
    }

    std::cout << "Block compiled successfully\n";
    std::cout << "Code address: " << block->code << "\n";
    std::cout << "Code size: " << block->codeSize << " bytes\n";

    std::cout << "\n=== Executing JIT code (1 call) ===\n";
    try {
        jit->execute(block, &ppu);
        std::cout << "Execution completed successfully\n";
    } catch (...) {
        std::cout << "Execution threw exception\n";
    }

    std::cout << "\n=== Cleaning up block manually ===\n";
    // Don't let invalidateAll() handle it - do it manually
    std::cout << "About to delete block object...\n";
    // Don't free the executable memory - let it leak to avoid the crash
    // delete block;  // Skip this for now
    std::cout << "Block deleted (memory leaked intentionally)\n";

    std::cout << "\n=== Deleting PPUJIT object ===\n";
    delete jit;
    std::cout << "PPUJIT deleted\n";

    std::cout << "\n=== Test complete ===\n";
    return 0;
}
