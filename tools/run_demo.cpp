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
#include <chrono>

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
    std::cout << "JIT mode: " << (useJIT && jitBlock ? "ENABLED" : "DISABLED") << "\n\n";

    // Performance tracking
    auto startTime = std::chrono::high_resolution_clock::now();
    int lastReportCycles = 0;
    auto lastReportTime = startTime;

    // Main loop
    int cycles = 0;
    const int MAX_CYCLES = 50000000;  // 50M cycles max (enough for slow pixel I/O)

    // NTSC timing: ~60 Hz, 261 scanlines per frame
    // Each scanline: 340 pixels (256 visible + 84 H-Blank)
    // Total cycles per frame: 261 * 340 = 88,740 cycles
    // At 64 MHz: 88,740 cycles = ~1.387 ms per frame

    int displayCycleCounter = 0;
    const int CYCLES_PER_DISPLAY_TICK = 13;  // Display ticks at 5.3 MHz (64 MHz / 13)

    int renderCycleCounter = 0;
    const int CYCLES_PER_RENDER = 88740;  // Render once per NTSC frame (60 Hz)

    while (!window.shouldClose() && ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        // Execute PPU in large batches (JIT or interpreter)
        int batchSize = (useJIT && jitBlock) ? 10000 : 1000;  // Larger batches for better performance

        for (int i = 0; i < batchSize && ppu.getState() == PPUState::Running; i++) {
            if (useJIT && jitBlock) {
                // Execute 100 PPU cycles via JIT (batched within batch)
                jit.execute(jitBlock, &ppu);
                cycles += 100;
                displayCycleCounter += 100;
                renderCycleCounter += 100;
                i += 99;  // Skip ahead since we did 100 cycles
            } else {
                // Execute 1 PPU cycle via interpreter
                ppu.tick();
                cycles++;
                displayCycleCounter++;
                renderCycleCounter++;
            }
        }

        // Update display in batches (batch the display ticks)
        while (displayCycleCounter >= CYCLES_PER_DISPLAY_TICK) {
            display.tick();
            displayCycleCounter -= CYCLES_PER_DISPLAY_TICK;
        }

        // Update blank status after batch
        ppu.setBlankStatus(display.isVBlank(), display.isHBlank());

        // Render once per frame (60 Hz) to avoid bottleneck
        if (renderCycleCounter >= CYCLES_PER_RENDER) {
            window.render(display);
            window.pollEvents();
            renderCycleCounter = 0;
        }

        // Print progress with performance stats
        if (cycles - lastReportCycles >= 1000000) {  // Report every 1M cycles
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime).count();
            int cyclesDone = cycles - lastReportCycles;
            double mhz = (cyclesDone / 1000.0) / (elapsed / 1000.0);

            std::cout << "Cycles: " << cycles << " | Scanline: " << display.getCurrentScanline()
                      << " | Speed: " << mhz << " MHz (target: 64 MHz)\n";

            lastReportCycles = cycles;
            lastReportTime = now;
        }
    }

    // Final render
    window.render(display);

    // Final performance report
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    double avgMhz = (cycles / 1000.0) / (totalTime / 1000.0);

    std::cout << "\n=== Demo Complete ===\n";
    std::cout << "Total cycles: " << cycles << "\n";
    std::cout << "Total time: " << totalTime << " ms\n";
    std::cout << "Average speed: " << avgMhz << " MHz (target: 64 MHz)\n";
    std::cout << "Efficiency: " << (avgMhz / 64.0 * 100.0) << "%\n";
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
