// Demo runner for PPU .bin files
// Loads a 65536-byte demo and runs it with display output

#include "ppu.h"
#include "display.h"
#include "window.h"
#include <iostream>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <demo.bin>" << std::endl;
        std::cerr << "  Runs a PPU microcode demo with SDL display" << std::endl;
        return 1;
    }

    const char* filename = argv[1];

    // Load binary file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return 1;
    }

    std::vector<uint8_t> program((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    file.close();

    std::cout << "Loaded " << program.size() << " bytes from " << filename << "\n";

    // Create display and PPU
    Display display;
    PPU ppu;

    // Connect PPU to display for memory-mapped I/O
    ppu.setDisplay(&display);

    // Load program into PPU
    ppu.loadMicrocode(program.data(), program.size());
    // Interrupt addresses are now in R59 (VBlank) and R60 (HBlank)
    // Set to 0 to disable interrupts for this demo

    // Create window
    Window window(2);  // 2x scale
    if (!window.init()) {
        return 1;
    }

    // Start PPU
    ppu.start();

    std::cout << "PPU started, state: " << (int)ppu.getState() << "\n";
    std::cout << "Running demo... Press ESC to quit\n";

    // Main loop
    int cycles = 0;
    const int MAX_CYCLES = 50000000;  // 50M cycles max (enough for slow pixel I/O)

    // HLT detection: HLT expands to a 5-instruction loop
    // Track PC history to detect repeating patterns
    const int HISTORY_SIZE = 10;
    uint16_t pcHistory[HISTORY_SIZE] = {0};
    int historyIndex = 0;

    while (!window.shouldClose() && ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        // Tick display (advances scanlines, sets blank status)
        display.tick();

        // Update PPU blank status from display
        ppu.setBlankStatus(display.isVBlank(), display.isHBlank());

        // Execute one PPU instruction
        ppu.tick();
        cycles++;

        // Store PC after execution
        pcHistory[historyIndex] = ppu.getExecutionPointer();
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;

        // Check for HLT pattern (repeating PC sequence)
        if (cycles >= HISTORY_SIZE) {
            // Check if the last 5 PCs match the 5 before that
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
                std::cout << "Detected HLT pattern (5-instruction repeating loop)\n";
                std::cout << "PC sequence: ";
                for (int i = 4; i >= 0; i--) {
                    int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
                    std::cout << pcHistory[idx] << " ";
                }
                std::cout << "\n";
                break;
            }
        }

        // Debug: Print PC for first 20 cycles
        if (cycles <= 20) {
            std::cout << "Cycle " << cycles << ": PC=" << ppu.getExecutionPointer() << "\n";
        }

        // Render every 1000 cycles for visual feedback
        if (cycles % 1000 == 0) {
            window.render(display);
            window.pollEvents();
        }

        // Print progress
        if (cycles % 100000 == 0) {
            std::cout << "Executed " << cycles << " cycles, scanline=" << display.getCurrentScanline()
                      << " R10=" << ppu.getRegister(10) << " (CURRENT_LINE)\n";
        }
    }

    // Final render
    window.render(display);

    std::cout << "Demo completed after " << cycles << " cycles\n";
    std::cout << "Final state: ";

    switch (ppu.getState()) {
        case PPUState::Halted:
            std::cout << "HALTED\n";
            break;
        case PPUState::Running:
            std::cout << "RUNNING (hit max cycles)\n";
            break;
        default:
            std::cout << "OTHER\n";
            break;
    }

    // Keep window open until user closes it
    std::cout << "Showing result. Press ESC or close window to exit...\n";
    while (!window.shouldClose()) {
        window.pollEvents();
        SDL_Delay(16);
    }

    return 0;
}
