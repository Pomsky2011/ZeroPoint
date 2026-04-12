// Test white fill demo - verifies PPU fills framebuffer correctly
#include "ppu.h"
#include "display.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

int main() {
    std::cout << "=== PPU White Fill Demo Test ===\n\n";

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
    ppu.start();

    std::cout << "Running PPU demo...\n";

    // Run until HLT detected (repeating PC pattern)
    const int MAX_CYCLES = 20000;
    const int HISTORY_SIZE = 10;
    uint16_t pcHistory[HISTORY_SIZE] = {0};
    int historyIndex = 0;
    int cycles = 0;

    while (ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        display.tick();
        ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
        ppu.tick();
        cycles++;

        pcHistory[historyIndex] = ppu.getExecutionPointer();
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;

        // Check for HLT pattern
        if (cycles >= HISTORY_SIZE) {
            bool isRepeating = true;
            for (int i = 0; i < 5; i++) {
                int recentIndex = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
                int olderIndex = (historyIndex - 6 - i + HISTORY_SIZE) % HISTORY_SIZE;
                if (pcHistory[recentIndex] != pcHistory[olderIndex]) {
                    isRepeating = false;
                    break;
                }
            }
            if (isRepeating) {
                std::cout << "Detected HLT after " << cycles << " cycles\n";
                break;
            }
        }
    }

    std::cout << "Demo completed after " << cycles << " cycles\n\n";

    // Verify framebuffer is filled with white (0xFFFF in 16-bit mode)
    std::cout << "Verifying framebuffer contents...\n";

    // Sample pixels from different areas
    struct TestPoint { int x, y; };
    TestPoint testPoints[] = {
        {0, 0}, {127, 127}, {255, 255},  // Corners/center
        {50, 50}, {100, 100}, {200, 200}, // Various positions
        {10, 10}, {240, 240}, {128, 128}
    };

    int whitePixels = 0;
    int totalPixels = sizeof(testPoints) / sizeof(TestPoint);
    bool allWhite = true;

    for (const auto& pt : testPoints) {
        Color32 color = display.getPixel(pt.x, pt.y);
        uint8_t r = (color >> 24) & 0xFF;
        uint8_t g = (color >> 16) & 0xFF;
        uint8_t b = (color >> 8) & 0xFF;
        uint8_t a = color & 0xFF;

        // In 16-bit mode, white is 0xFFFF which converts to ~255,255,255,255
        // Check if it's close to white
        bool isWhite = (r >= 248 && g >= 248 && b >= 248 && a >= 248);

        if (isWhite) {
            whitePixels++;
            std::cout << "  (" << pt.x << "," << pt.y << "): WHITE ✓ ";
        } else {
            std::cout << "  (" << pt.x << "," << pt.y << "): RGBA("
                      << (int)r << "," << (int)g << "," << (int)b << "," << (int)a << ") ✗ ";
            allWhite = false;
        }
        std::cout << " [0x" << std::hex << std::setw(8) << std::setfill('0') << color << std::dec << "]\n";
    }

    std::cout << "\n";
    std::cout << "White pixels: " << whitePixels << "/" << totalPixels << "\n";

    if (allWhite) {
        std::cout << "\n✓ Screen is filled with white!\n";
        std::cout << "✓ PPU demo test PASSED!\n";
        return 0;
    } else {
        std::cout << "\n✗ Screen is NOT all white!\n";
        std::cout << "✗ PPU demo test FAILED!\n";
        return 1;
    }
}
