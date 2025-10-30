#include "ppu.h"
#include "display.h"
#include <iostream>

using namespace ZeroPoint;

int main() {
    std::cout << "=== Framebuffer Write Test ===\n";
    
    Display display;
    PPU ppu;
    ppu.setDisplay(&display);
    
    // Directly write to PPU memory at 0xE000
    ppu.writeMemory(0xE000, 0xFF);
    ppu.writeMemory(0xE001, 0xFF);
    
    // Read back from display
    Color32 pixel = display.getPixel(0, 0);
    uint8_t r = (pixel >> 24) & 0xFF;
    uint8_t g = (pixel >> 16) & 0xFF;
    uint8_t b = (pixel >> 8) & 0xFF;
    uint8_t a = pixel & 0xFF;
    
    std::cout << "Wrote 0xFFFF to PPU[0xE000]\n";
    std::cout << "Read from display.getPixel(0,0): RGBA(" << (int)r << "," << (int)g << "," << (int)b << "," << (int)a << ")\n";
    std::cout << "Raw: 0x" << std::hex << pixel << std::dec << "\n";
    
    if (r >= 248 && g >= 248 && b >= 248) {
        std::cout << "✓ Framebuffer write works!\n";
        return 0;
    } else {
        std::cout << "✗ Framebuffer write failed!\n";
        return 1;
    }
}
