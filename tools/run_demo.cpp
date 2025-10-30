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

    // Detailed profiling
    int64_t ppuExecutionTimeNs = 0;
    int64_t displayTickTimeNs = 0;
    int64_t renderTimeNs = 0;

    // Main loop
    int cycles = 0;
    const int MAX_CYCLES = 50000000;  // 50M cycles max (enough for slow pixel I/O)

    // Frame timing: 1,119,600 + 2/3 PPU cycles per frame
    // PPU clock: 67,108,864 Hz (2^26 Hz)
    // At 67,108,864 Hz: 1,119,600.666... cycles = ~16.68 ms per frame (59.94 Hz NTSC)
    //
    // Scanline timing: 4289.65772669 cycles/scanline (261 scanlines = 1,119,600.666... cycles)
    // Pixel timing: 4289.65772669 / 340 = 12.616640372617647... cycles/pixel
    //
    // Integer-based fractional timing to avoid floating point errors:
    // 12.616640372617647 PPU cycles per display tick (pixel)
    // 1 PPU cycle = 1/12.616640372617647 = 0.0792452830188679... display ticks
    // Scale by 1,000,000,000: add 79,245,283 per PPU cycle
    // When accumulator >= 1,000,000,000, tick display once and subtract 1,000,000,000

    int64_t displayCycleAccumulator = 0;
    const int64_t DISPLAY_TICK_THRESHOLD = 1000000000;  // 1 billion (scale factor)
    const int64_t DISPLAY_TICK_INCREMENT = 79245283;    // Per PPU cycle

    int renderCycleCounter = 0;
    int frameCount = 0;
    // Frame 1 & 2: 1,119,601 cycles, Frame 3: 1,119,600 cycles
    int CYCLES_PER_RENDER = 1119601;

    while (!window.shouldClose() && ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        // Execute PPU in large batches (JIT or interpreter)
        int batchSize = (useJIT && jitBlock) ? 10 : 1000;  // JIT does 1000 cycles per call, so 10 calls = 10k cycles

        auto ppuStart = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < batchSize && ppu.getState() == PPUState::Running; i++) {
            if (useJIT && jitBlock) {
                // Execute 1000 PPU cycles via JIT (batched within call)
                jit.execute(jitBlock, &ppu);
                cycles += 1000;
                displayCycleAccumulator += DISPLAY_TICK_INCREMENT * 1000;
                renderCycleCounter += 1000;
            } else {
                // Execute 1 PPU cycle via interpreter
                ppu.tick();
                cycles++;
                displayCycleAccumulator += DISPLAY_TICK_INCREMENT;
                renderCycleCounter++;
            }
        }
        auto ppuEnd = std::chrono::high_resolution_clock::now();
        ppuExecutionTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(ppuEnd - ppuStart).count();

        // Update display in batches (integer-based fractional timing, no floating point drift)
        auto displayStart = std::chrono::high_resolution_clock::now();
        while (displayCycleAccumulator >= DISPLAY_TICK_THRESHOLD) {
            display.tick();
            displayCycleAccumulator -= DISPLAY_TICK_THRESHOLD;
        }
        auto displayEnd = std::chrono::high_resolution_clock::now();
        displayTickTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(displayEnd - displayStart).count();

        // Update blank status after batch
        ppu.setBlankStatus(display.isVBlank(), display.isHBlank());

        // Render once per frame (~57.16 Hz) to avoid bottleneck
        if (renderCycleCounter >= CYCLES_PER_RENDER) {
            auto renderStart = std::chrono::high_resolution_clock::now();
            window.render(display);
            window.pollEvents();
            auto renderEnd = std::chrono::high_resolution_clock::now();
            renderTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(renderEnd - renderStart).count();
            renderCycleCounter = 0;
            frameCount++;

            // Alternate frame lengths: 1,119,601, 1,119,601, 1,119,600 (repeating)
            // This maintains exactly 1,119,600 + 2/3 cycles per frame on average
            CYCLES_PER_RENDER = (frameCount % 3 == 0) ? 1119600 : 1119601;
        }

        // Print progress with performance stats
        if (cycles - lastReportCycles >= 1000000) {  // Report every 1M cycles
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime).count();
            int cyclesDone = cycles - lastReportCycles;
            double mhz = (cyclesDone / 1000000.0) / (elapsed / 1000.0);  // Mega = million

            std::cout << "Cycles: " << cycles << " | Scanline: " << display.getCurrentScanline()
                      << " | Speed: " << mhz << " MHz (target: 67.108864 MHz)\n";

            lastReportCycles = cycles;
            lastReportTime = now;
        }
    }

    // Final render
    window.render(display);

    // Final performance report
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    double avgMhz = (cycles / 1000000.0) / (totalTime / 1000.0);  // Mega = million

    std::cout << "\n=== Demo Complete ===\n";
    std::cout << "Total cycles: " << cycles << "\n";
    std::cout << "Total time: " << totalTime << " ms\n";
    std::cout << "Average speed: " << avgMhz << " MHz (target: 67.108864 MHz)\n";
    std::cout << "Efficiency: " << (avgMhz / 67.108864 * 100.0) << "%\n";
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

    // Profiling breakdown
    double ppuTimeMs = ppuExecutionTimeNs / 1000000.0;
    double displayTimeMs = displayTickTimeNs / 1000000.0;
    double renderTimeMs = renderTimeNs / 1000000.0;
    double totalProfiledMs = ppuTimeMs + displayTimeMs + renderTimeMs;

    std::cout << "\n=== Performance Breakdown ===\n";
    std::cout << "PPU execution:   " << ppuTimeMs << " ms (" << (ppuTimeMs / totalTime * 100.0) << "%)\n";
    std::cout << "Display ticks:   " << displayTimeMs << " ms (" << (displayTimeMs / totalTime * 100.0) << "%)\n";
    std::cout << "Rendering:       " << renderTimeMs << " ms (" << (renderTimeMs / totalTime * 100.0) << "%)\n";
    std::cout << "Other overhead:  " << (totalTime - totalProfiledMs) << " ms (" << ((totalTime - totalProfiledMs) / totalTime * 100.0) << "%)\n";

    // Keep window open until user closes it
    std::cout << "Showing result. Press ESC or close window to exit...\n";
    while (!window.shouldClose()) {
        window.pollEvents();
        SDL_Delay(16);
    }

    return 0;
}
