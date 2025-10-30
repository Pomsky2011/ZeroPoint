#include "ppu.h"
#include "display.h"
#include <iostream>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

int main() {
    Display display;
    PPU ppu;
    ppu.setDisplay(&display);
    
    // Load and run white_fill
    std::ifstream file("/Users/alexanderwhite/Documents/Code/ZPdevtools/examples/ppu/white_fill.bin", std::ios::binary);
    std::vector<uint8_t> program((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ppu.loadMicrocode(program.data(), program.size());
    ppu.start();
    
    // Run until done
    for (int i = 0; i < 100000 && ppu.getState() == PPUState::Running; i++) {
        display.tick();
        ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
        ppu.tick();
    }
    
    // Check how many scanlines are white
    int whiteScanlines = 0;
    for (int y = 0; y < 256; y++) {
        bool scanlineWhite = true;
        for (int x = 0; x < 256; x += 10) {  // Sample every 10 pixels
            Color32 c = display.getPixel(x, y);
            uint8_t r = (c >> 24) & 0xFF;
            if (r < 248) {  // Not white
                scanlineWhite = false;
                break;
            }
        }
        if (scanlineWhite) whiteScanlines++;
    }
    
    std::cout << "White scanlines: " << whiteScanlines << " / 256\n";
    
    // Check memory mapping
    std::cout << "\nPPU memory at 0xE000-0xE010:\n";
    for (int addr = 0xE000; addr <= 0xE010; addr += 2) {
        uint8_t low = ppu.readMemory(addr);
        uint8_t high = ppu.readMemory(addr + 1);
        std::cout << "  [0x" << std::hex << addr << "]: 0x" << (int)high << (int)low << std::dec << "\n";
    }
    
    std::cout << "\nPPU memory at 0xEFF0-0xF000:\n";
    for (int addr = 0xEFF0; addr <= 0xF000; addr += 2) {
        uint8_t low = ppu.readMemory(addr);
        uint8_t high = ppu.readMemory(addr + 1);
        std::cout << "  [0x" << std::hex << addr << "]: 0x" << (int)high << (int)low << std::dec << "\n";
    }
    
    return 0;
}
