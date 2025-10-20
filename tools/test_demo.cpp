// Simple demo test without SDL
// Just runs the PPU and reports results

#include "ppu.h"
#include "display.h"
#include <iostream>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char* argv[]) {
    std::cout << "test_demo starting...\n" << std::flush;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <demo.bin>" << std::endl;
        return 1;
    }

    const char* filename = argv[1];
    std::cout << "Opening file: " << filename << "\n" << std::flush;

    // Load binary file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return 1;
    }

    std::vector<uint8_t> program((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    file.close();

    std::cout << "Loaded " << program.size() << " bytes from " << filename << "\n" << std::flush;

    // Create display and PPU
    std::cout << "Creating display...\n" << std::flush;
    Display display;
    display.setRenderMode(RenderMode::RGBA32);

    std::cout << "Creating PPU...\n" << std::flush;
    PPU ppu;
    ppu.setDisplay(&display);

    // Load program
    std::cout << "Loading microcode...\n" << std::flush;
    ppu.loadMicrocode(program.data(), program.size());
    // Interrupt addresses are now in R59 (VBlank) and R60 (HBlank)
    // Set to 0 to disable interrupts for this demo

    std::cout << "Starting PPU...\n" << std::flush;
    ppu.start();

    std::cout << "PPU started, state: " << (int)ppu.getState() << "\n" << std::flush;

    // Run until halted
    int cycles = 0;
    const int MAX_CYCLES = 10000000;  // 10M cycles for pixel I/O demos

    // HLT detection
    const int HISTORY_SIZE = 20;  // Need 2× max loop length
    uint16_t pcHistory[HISTORY_SIZE] = {0};
    int historyIndex = 0;

    std::cout << "Entering main loop...\n" << std::flush;

    while (ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        ppu.tick();
        cycles++;

        // Store PC after execution
        pcHistory[historyIndex] = ppu.getExecutionPointer();
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;

        // Debug first 20 cycles
        if (cycles <= 20) {
            std::cout << "Cycle " << cycles << ": PC=" << ppu.getExecutionPointer();
            if (cycles == 12) {
                std::cout << " [history:";
                for (int i = 0; i < HISTORY_SIZE; i++) {
                    std::cout << " " << pcHistory[i];
                }
                std::cout << ", idx=" << historyIndex << "]";
            }
            std::cout << "\n" << std::flush;
        }

        // Check for HLT pattern (repeating PC sequence of 5-8 instructions)
        if (cycles >= HISTORY_SIZE) {
            // Try different loop lengths (5-8 instructions)
            for (int loopLen = 5; loopLen <= 8; loopLen++) {
                bool isRepeating = true;
                for (int i = 0; i < loopLen && i < HISTORY_SIZE/2; i++) {
                    int recentIndex = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
                    int olderIndex = (historyIndex - 1 - i - loopLen + HISTORY_SIZE * 2) % HISTORY_SIZE;
                    if (pcHistory[recentIndex] != pcHistory[olderIndex]) {
                        isRepeating = false;
                        break;
                    }
                }
                if (isRepeating) {
                    std::cout << "Detected HLT pattern (loop length " << loopLen << ") at cycle " << cycles << "\n" << std::flush;
                    goto exit_loop;
                }
            }
        }

        if (cycles % 100000 == 0) {
            std::cout << "Cycles: " << cycles << ", PC=" << ppu.getExecutionPointer()
                      << " R10=" << ppu.getRegister(10) << " R11=" << ppu.getRegister(11)
                      << " R12=" << ppu.getRegister(12) << "\n" << std::flush;
        }
    }
exit_loop:

    std::cout << "\nDemo completed after " << cycles << " cycles\n";
    std::cout << "Final state: " << (int)ppu.getState() << "\n";

    // Sample pixels across the screen to verify gradient
    const Color32* fb = display.getFramebuffer32();

    std::cout << "\nSampling pixels (format: X,Y -> R,G,B,A):\n";

    // Sample various points (including tile test positions)
    int samples[][2] = {{0,0}, {10,10}, {50,50}, {64,64}, {65,65}, {70,70}, {100,100}, {200,200}, {255,255}};

    for (auto& pt : samples) {
        int x = pt[0], y = pt[1];
        Color32 pixel = fb[y * 256 + x];
        uint8_t r = (pixel >> 24) & 0xFF;
        uint8_t g = (pixel >> 16) & 0xFF;
        uint8_t b = (pixel >> 8) & 0xFF;
        uint8_t a = pixel & 0xFF;

        std::cout << "  (" << x << "," << y << ") -> R=" << (int)r
                  << " G=" << (int)g << " B=" << (int)b << " A=" << (int)a;

        // For gradient_demo: R should = Y, G should = 128, B should = X
        std::cout << " (expected: R=" << y << " G=128 B=" << x << " A=255)\n";
    }

    return 0;
}
