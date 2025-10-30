#include "ppu.h"
#include "display.h"
#include <iostream>

using namespace ZeroPoint;

int main() {
    std::cout << "=== Framebuffer Write Test v2 ===\n";
    
    Display display;
    PPU ppu;
    ppu.setDisplay(&display);
    
    std::cout << "Display mode: " << (display.getRenderMode() == RenderMode::RGBA16 ? "16-bit" : "32-bit") << "\n";
    
    // Directly write to PPU memory at 0xE000
    std::cout << "Writing 0xFF to PPU[0xE000]...\n";
    ppu.writeMemory(0xE000, 0xFF);
    std::cout << "Writing 0xFF to PPU[0xE001]...\n";
    ppu.writeMemory(0xE001, 0xFF);
    
    // Read back from PPU memory
    uint8_t byte0 = ppu.readMemory(0xE000);
    uint8_t byte1 = ppu.readMemory(0xE001);
    std::cout << "Read back from PPU memory: 0x" << std::hex << (int)byte1 << (int)byte0 << std::dec << "\n";
    
    // Read from display framebuffer directly
    const uint8_t* fbBytes = reinterpret_cast<const uint8_t*>(display.getFramebuffer16());
    std::cout << "Direct framebuffer read [0]: 0x" << std::hex << (int)fbBytes[0] << std::dec << "\n";
    std::cout << "Direct framebuffer read [1]: 0x" << std::hex << (int)fbBytes[1] << std::dec << "\n";
    
    // Read back from display.getPixel
    Color32 pixel = display.getPixel(0, 0);
    uint8_t r = (pixel >> 24) & 0xFF;
    uint8_t g = (pixel >> 16) & 0xFF;
    uint8_t b = (pixel >> 8) & 0xFF;
    uint8_t a = pixel & 0xFF;
    
    std::cout << "display.getPixel(0,0): RGBA(" << (int)r << "," << (int)g << "," << (int)b << "," << (int)a << ")\n";
    std::cout << "Raw: 0x" << std::hex << pixel << std::dec << "\n";
    
    if (fbBytes[0] == 0xFF && fbBytes[1] == 0xFF) {
        std::cout << "✓ Framebuffer HAS the data!\n";
        if (r >= 248 && g >= 248 && b >= 248) {
            std::cout << "✓ getPixel works!\n";
            return 0;
        } else {
            std::cout << "✗ getPixel doesn't read correctly!\n";
            return 1;
        }
    } else {
        std::cout << "✗ Framebuffer write failed!\n";
        return 1;
    }
}
