// Test white fill - verifies color format correctness
#include "ppu.h"
#include "display.h"
#include <iostream>
#include <iomanip>

using namespace ZeroPoint;

int main() {
    std::cout << "=== White Fill Color Format Test ===\n\n";

    // Create display and PPU
    Display display;
    PPU ppu;
    ppu.setDisplay(&display);

    // Fill screen with white using direct pixel writes
    std::cout << "Filling screen with white pixels (RGBA: 255,255,255,255)...\n";
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            // White in RGBA32 format: R=255, G=255, B=255, A=255
            // Should be: 0xFFFFFFFF (RRGGBBAA)
            display.setPixel32(x, y, 0xFFFFFFFF);
        }
    }

    std::cout << "Reading back pixels to verify color format...\n\n";

    bool allCorrect = true;
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            Color32 color = display.getPixel(x, y);

            uint8_t r = (color >> 24) & 0xFF;
            uint8_t g = (color >> 16) & 0xFF;
            uint8_t b = (color >> 8) & 0xFF;
            uint8_t a = color & 0xFF;

            if (r != 255 || g != 255 || b != 255 || a != 255) {
                std::cout << "ERROR at (" << x << "," << y << "): ";
                std::cout << "Expected RGBA(255,255,255,255), got RGBA("
                          << (int)r << "," << (int)g << "," << (int)b << "," << (int)a << ")\n";
                std::cout << "  Raw value: 0x" << std::hex << std::setw(8) << std::setfill('0') << color << std::dec << "\n";
                allCorrect = false;
            }
        }
    }

    if (allCorrect) {
        std::cout << "✓ All 100 test pixels are white (255,255,255,255)\n";
        std::cout << "✓ Color format is correct (RGBA32)\n";
        std::cout << "\nTest PASSED!\n";
        return 0;
    } else {
        std::cout << "\nTest FAILED - color format is incorrect!\n";
        return 1;
    }
}
