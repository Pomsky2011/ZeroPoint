// Demo runner for PPU .bin files
// Loads a 65536-byte demo and runs it with display output

#include "ppu.h"
#include "ppu_jit.h"
#include "display.h"
#include "window.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

using namespace ZeroPoint;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <demo.bin> [--jit]" << std::endl;
        std::cerr << "  Runs a PPU microcode demo with SDL display" << std::endl;
        std::cerr << "  --jit: Enable experimental JIT compilation" << std::endl;
        return 1;
    }

    const char* filename = argv[1];
    bool enableJIT = false;

    // Check for --jit flag
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--jit") == 0) {
            enableJIT = true;
        }
    }

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

    // Initialize JIT if requested and supported
    PPUJIT jit;
    bool useJIT = false;
    JITBlock* jitBlock = nullptr;

    if (enableJIT) {
        if (PPUJIT::isSupported()) {
            std::cout << "JIT compilation enabled (" << PPUJIT::getArchitecture() << ")\n";
            jitBlock = jit.compileBlock(&ppu, 0, 1000);
            if (jitBlock) {
                useJIT = true;
                std::cout << "JIT compilation successful\n";
            } else {
                std::cout << "JIT compilation failed, falling back to interpreter\n";
            }
        } else {
            std::cout << "JIT not supported on this platform, using interpreter\n";
        }
    } else {
        std::cout << "JIT disabled (use --jit to enable), using interpreter\n";
    }

    std::cout << "Running demo... Press ESC to quit\n";

    // Main loop
    int cycles = 0;
    const int MAX_CYCLES = 50000000;  // 50M cycles max (enough for slow pixel I/O)

    // NTSC timing: ~60 Hz, 261 scanlines per frame
    // Each scanline: 340 pixels (256 visible + 84 H-Blank)
    // Total cycles per frame: 261 * 340 = 88,740 cycles
    // At 64 MHz: 88,740 cycles = ~1.387 ms per frame

    int displayCycleCounter = 0;
    const int CYCLES_PER_DISPLAY_TICK = 13;  // Update display every 13 PPU cycles (NTSC timing: 67.1MHz PPU / 5.3MHz pixel clock)

    int renderCycleCounter = 0;
    const int CYCLES_PER_RENDER = 88740;  // Render once per NTSC frame (60 Hz)

    while (!window.shouldClose() && ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        // Execute PPU (JIT or interpreter)
        if (useJIT && jitBlock) {
            // Execute 1000 PPU cycles via JIT
            jit.execute(jitBlock, &ppu);
            cycles += 1000;
            displayCycleCounter += 1000;
            renderCycleCounter += 1000;
        } else {
            // Execute 1 PPU cycle via interpreter
            ppu.tick();
            cycles++;
            displayCycleCounter++;
            renderCycleCounter++;
        }

        // Update display at scanline rate (much slower than PPU)
        if (displayCycleCounter >= CYCLES_PER_DISPLAY_TICK) {
            display.tick();
            ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
            displayCycleCounter = 0;
        }

        // Render once per frame (60 Hz) to avoid bottleneck
        if (renderCycleCounter >= CYCLES_PER_RENDER) {
            window.render(display);
            window.pollEvents();
            renderCycleCounter = 0;
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
