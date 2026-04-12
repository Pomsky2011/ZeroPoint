// Simple JIT test without SDL
// Tests JIT compilation and execution, reports performance

#include "ppu.h"
#include "ppu_jit.h"
#include "display.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <cstring>

using namespace ZeroPoint;

int main(int argc, char* argv[]) {
    std::cout << "test_jit starting..." << std::endl;
    std::cout.flush();

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <demo.bin> [--jit]" << std::endl;
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

    std::cout << "Parsed arguments" << std::endl;

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
    ppu.setDisplay(&display);

    // Load program into PPU
    ppu.loadMicrocode(program.data(), program.size());

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

    std::cout << "\nRunning test...\n";
    std::cout << "JIT mode: " << (useJIT && jitBlock ? "ENABLED" : "DISABLED") << "\n\n";

    // Performance tracking
    auto startTime = std::chrono::high_resolution_clock::now();

    // Main loop - run for limited cycles
    int cycles = 0;
    const int MAX_CYCLES = 1000000;  // 1M cycles for quick test

    int64_t displayCycleAccumulator = 0;
    const int64_t DISPLAY_TICK_THRESHOLD = 1000000000;
    const int64_t DISPLAY_TICK_INCREMENT = 79245283;

    while (ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        if (useJIT && jitBlock) {
            // Execute 1000 PPU cycles via JIT
            jit.execute(jitBlock, &ppu);
            cycles += 1000;
            displayCycleAccumulator += DISPLAY_TICK_INCREMENT * 1000;
        } else {
            // Execute 1 PPU cycle via interpreter
            ppu.tick();
            cycles++;
            displayCycleAccumulator += DISPLAY_TICK_INCREMENT;
        }

        // Update display
        while (displayCycleAccumulator >= DISPLAY_TICK_THRESHOLD) {
            display.tick();
            displayCycleAccumulator -= DISPLAY_TICK_THRESHOLD;
        }

        // Update blank status
        ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
    }

    // Final performance report
    auto endTime = std::chrono::high_resolution_clock::now();
    auto totalTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    double avgMhz = (cycles / 1000000.0) / (totalTime / 1000.0);

    std::cout << "\n=== Test Complete ===\n";
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

    // Clean up: intentionally leak the JIT block to avoid QEMU munmap issues
    // The memory will be freed when the process exits anyway

    return 0;
}
