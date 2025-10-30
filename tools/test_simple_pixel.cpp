// Test simple pixel drawing
#include "ppu.h"
#include "display.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

int main() {
    std::cout << "=== Simple White Pixel Test ===\n\n";

    // Load simple_white_pixel.bin
    const char* filename = "/Users/alexanderwhite/Documents/Code/ZPdevtools/examples/ppu/simple_white_pixel.bin";
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
    ppu.start();

    std::cout << "Running PPU...\n";

    // Run until HLT
    const int MAX_CYCLES = 1000;
    int cycles = 0;

    while (ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        display.tick();
        ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
        ppu.tick();
        cycles++;

        // Show register state at key points
        if (cycles == 10 || cycles == 20 || cycles == 30) {
            std::cout << "Cycle " << cycles << ": ";
            std::cout << "R5=" << ppu.getRegister(5) << " ";
            std::cout << "R6=" << ppu.getRegister(6) << " ";
            std::cout << "DP=" << std::hex << ppu.getRegister(63) << std::dec << "\n";
        }
    }

    std::cout << "Completed after " << cycles << " cycles\n\n";

    // Check if pixel at (10, 10) is white
    Color32 pixel = display.getPixel(10, 10);
    uint8_t r = (pixel >> 24) & 0xFF;
    uint8_t g = (pixel >> 16) & 0xFF;
    uint8_t b = (pixel >> 8) & 0xFF;
    uint8_t a = pixel & 0xFF;

    std::cout << "Pixel at (10, 10): RGBA(" << (int)r << "," << (int)g << "," << (int)b << "," << (int)a << ")\n";
    std::cout << "Raw value: 0x" << std::hex << std::setw(8) << std::setfill('0') << pixel << std::dec << "\n";

    if (r >= 248 && g >= 248 && b >= 248 && a >= 248) {
        std::cout << "\n✓ Pixel is WHITE!\n";
        std::cout << "✓ Test PASSED!\n";
        return 0;
    } else {
        std::cout << "\n✗ Pixel is NOT white!\n";
        std::cout << "✗ Test FAILED!\n";

        // Check memory-mapped I/O region to see what was written
        std::cout << "\nMemory-mapped I/O region:\n";
        for (int addr = 0x0100; addr <= 0x010A; addr += 2) {
            uint8_t low = ppu.readMemory(addr);
            uint8_t high = ppu.readMemory(addr + 1);
            std::cout << "  [0x" << std::hex << addr << "]: 0x" << std::setw(2) << (int)low << (int)high << std::dec << "\n";
        }

        return 1;
    }
}
