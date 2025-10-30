// Debug version - shows what's happening during execution
#include "ppu.h"
#include "display.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

int main() {
    std::cout << "=== PPU White Fill Demo Debug Test ===\n\n";

    // Load white_fill.bin
    const char* filename = "/Users/alexanderwhite/Documents/Code/ZPdevtools/examples/ppu/white_fill.bin";
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open " << filename << std::endl;
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

    // Load program
    ppu.loadMicrocode(program.data(), program.size());

    std::cout << "Display mode: " << (display.getRenderMode() == RenderMode::RGBA16 ? "16-bit" : "32-bit") << "\n";

    ppu.start();

    std::cout << "Running PPU demo...\n";

    // Run with debug output
    const int MAX_CYCLES = 10000;
    int cycles = 0;
    int fillLoopCycles = 0;
    bool inFillLoop = false;

    while (ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        display.tick();
        ppu.setBlankStatus(display.isVBlank(), display.isHBlank());

        uint16_t pc = ppu.getExecutionPointer();

        // Track when we're in the fill loop (PC >= 38)
        if (pc >= 38 && pc <= 44 && !inFillLoop) {
            inFillLoop = true;
            std::cout << "Entered fill loop at cycle " << cycles << "\n";
            std::cout << "  R0 (white value): 0x" << std::hex << ppu.getRegister(0) << std::dec << "\n";
            std::cout << "  DP (start addr): 0x" << std::hex << ppu.getRegister(63) << std::dec << "\n";
            std::cout << "  R2 (end addr): 0x" << std::hex << ppu.getRegister(2) << std::dec << "\n";
        }

        if (inFillLoop) {
            fillLoopCycles++;

            // Show first few fill operations
            if (fillLoopCycles <= 5 || fillLoopCycles % 500 == 0) {
                std::cout << "  Fill cycle " << fillLoopCycles << ": DP=0x"
                          << std::hex << ppu.getRegister(63) << std::dec << "\n";
            }
        }

        ppu.tick();
        cycles++;

        // Check for HLT
        if (cycles >= 10 && pc == ppu.getExecutionPointer()) {
            std::cout << "Detected halt at cycle " << cycles << ", PC=" << pc << "\n";
            break;
        }

        if (cycles > 100 && cycles % 100 == 0 && pc == 44) {
            // We're stuck in the fill loop, check state
            std::cout << "Still in fill loop at cycle " << cycles << "\n";
            std::cout << "  DP: 0x" << std::hex << ppu.getRegister(63) << std::dec << "\n";
            if (cycles >= 2000) {
                std::cout << "Breaking - seems stuck\n";
                break;
            }
        }
    }

    std::cout << "\nDemo completed after " << cycles << " cycles\n";
    std::cout << "Fill loop ran for " << fillLoopCycles << " cycles\n";

    // Show final register state
    std::cout << "\nFinal register values:\n";
    std::cout << "  R0 (white): 0x" << std::hex << ppu.getRegister(0) << std::dec << "\n";
    std::cout << "  R1 (step): 0x" << std::hex << ppu.getRegister(1) << std::dec << "\n";
    std::cout << "  R2 (end): 0x" << std::hex << ppu.getRegister(2) << std::dec << "\n";
    std::cout << "  DP (R63): 0x" << std::hex << ppu.getRegister(63) << std::dec << "\n";

    // Check a few pixels
    std::cout << "\nSample framebuffer pixels:\n";
    for (int addr = 0xE000; addr <= 0xE010; addr += 2) {
        uint8_t low = ppu.readMemory(addr);
        uint8_t high = ppu.readMemory(addr + 1);
        uint16_t value = (high << 8) | low;
        std::cout << "  [0x" << std::hex << addr << "]: 0x" << std::setw(4) << std::setfill('0') << value << std::dec << "\n";
    }

    return 0;
}
