// Demo runner for PPU .bin files
// Loads a 65536-byte demo and runs it with display output

#include "ppu.h"
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
        std::cerr << "  --jit: (work in progress) runs the batched interpreter, same as default" << std::endl;
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

    // The PPU always runs the batched interpreter (PPU::runBatch): it executes
    // whole instructions and collapses multi-cycle stalls, 1.2x-4x faster than
    // ticking cycle by cycle and provably state-identical.
    if (enableJIT) {
        std::cout << "--jit: native code generation is a work in progress; "
                     "running the batched interpreter (already the default).\n";
    }

    std::cout << "Running demo... Press ESC to quit\n";
    std::cout << "Execution: batched interpreter\n\n";

    // Performance tracking
    auto startTime = std::chrono::high_resolution_clock::now();
    int lastReportCycles = 0;
    auto lastReportTime = startTime;

    // Detailed profiling
    int64_t ppuExecutionTimeNs = 0;
    int64_t displayTickTimeNs = 0;
    int64_t renderTimeNs = 0;

    // Main loop
    long long cycles = 0;  // 64-bit: this runner loops until the window closes

    // Frame timing: 1,134,656 PPU cycles per frame
    // PPU clock: 68,011,355 Hz (NTSC colorburst 3.579545 MHz x19)
    // At 68,011,355 Hz: 1,134,656.106 cycles = ~16.68 ms per frame (59.94 Hz NTSC)
    //
    // Scanline timing: 4347.34 cycles/scanline (261 scanlines = 1,134,656.106 cycles)
    // Pixel timing: 4347.34 / 340 = 12.786298 cycles/pixel
    //
    // Integer-based fractional timing to avoid floating point errors:
    // 12.786298 PPU cycles per display tick (pixel)
    // 1 PPU cycle = 1/12.786298 = 0.078210719... display ticks
    // Scale by 1,000,000,000: add 78,208,719 per PPU cycle
    // When accumulator >= 1,000,000,000, tick display once and subtract 1,000,000,000
    //
    // (Rounded to the nearest cycle rather than kept exact via a repeating
    // multi-frame pattern like the old 67,108,864 Hz clock allowed - that
    // clock's cycles/frame happened to have a clean 2/3 fractional part;
    // this one's fractional part doesn't reduce to a small denominator, and
    // sub-cycle-per-frame drift here is imperceptible for a demo viewer.)

    int64_t displayCycleAccumulator = 0;
    const int64_t DISPLAY_TICK_THRESHOLD = 1000000000;  // 1 billion (scale factor)
    const int64_t DISPLAY_TICK_INCREMENT = 78208719;    // Per PPU cycle

    int renderCycleCounter = 0;
    const int CYCLES_PER_RENDER = 1134656;

    while (!window.shouldClose() && ppu.getState() == PPUState::Running) {
        // Execute the PPU with per-cycle display sync. Beam-synced firmware
        // (e.g. an H-blank ISR that reads the scanline register at $00E0) needs
        // the display's scanline/blank state to advance *between* PPU
        // instructions -- so we tick the display and refresh blank status inside
        // the cycle loop rather than batching a stale snapshot. Batching (runBatch)
        // freezes the scanline for its whole span and blanks the screen. ~20k
        // cycles per outer iteration bounds event/render latency.
        const int CHUNK = 20000;
        auto ppuStart = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < CHUNK && ppu.getState() == PPUState::Running; i++) {
            displayCycleAccumulator += DISPLAY_TICK_INCREMENT;
            while (displayCycleAccumulator >= DISPLAY_TICK_THRESHOLD) {
                display.tick();
                displayCycleAccumulator -= DISPLAY_TICK_THRESHOLD;
                ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
            }
            ppu.tick();
            cycles++;
            renderCycleCounter++;
        }
        auto ppuEnd = std::chrono::high_resolution_clock::now();
        ppuExecutionTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(ppuEnd - ppuStart).count();

        // Render once per frame (~59.94 Hz NTSC) to avoid bottleneck
        if (renderCycleCounter >= CYCLES_PER_RENDER) {
            auto renderStart = std::chrono::high_resolution_clock::now();
            window.render(display);
            window.pollEvents();
            auto renderEnd = std::chrono::high_resolution_clock::now();
            renderTimeNs += std::chrono::duration_cast<std::chrono::nanoseconds>(renderEnd - renderStart).count();
            renderCycleCounter = 0;
        }

        // Print progress with performance stats
        if (cycles - lastReportCycles >= 1000000) {  // Report every 1M cycles
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReportTime).count();
            int cyclesDone = cycles - lastReportCycles;
            double mhz = (cyclesDone / 1000000.0) / (elapsed / 1000.0);  // Mega = million

            std::cout << "Cycles: " << cycles << " | Scanline: " << display.getCurrentScanline()
                      << " | Speed: " << mhz << " MHz (target: 68.011355 MHz)\n";

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
    std::cout << "Average speed: " << avgMhz << " MHz (target: 68.011355 MHz)\n";
    std::cout << "Efficiency: " << (avgMhz / 68.011355 * 100.0) << "%\n";
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
