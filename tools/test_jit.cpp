// PPU throughput test without SDL. Runs the batched interpreter and reports
// speed. (--jit is a work-in-progress no-op that runs the same batched path.)

#include "ppu.h"
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

    // The PPU always runs the batched interpreter (PPU::runBatch): whole
    // instructions with multi-cycle stalls collapsed, 1.2x-4x faster than
    // ticking cycle by cycle and provably state-identical.
    if (enableJIT) {
        std::cout << "--jit: native code generation is a work in progress; "
                     "running the batched interpreter (already the default).\n";
    }

    std::cout << "\nRunning test...\n";
    std::cout << "Execution: batched interpreter\n\n";

    // Performance tracking
    auto startTime = std::chrono::high_resolution_clock::now();

    // Main loop - run for limited cycles
    int cycles = 0;
    const int MAX_CYCLES = 1000000;  // 1M cycles for quick test

    int64_t displayCycleAccumulator = 0;
    const int64_t DISPLAY_TICK_THRESHOLD = 1000000000;
    const int64_t DISPLAY_TICK_INCREMENT = 79245283;

    while (ppu.getState() == PPUState::Running && cycles < MAX_CYCLES) {
        // Batched interpreter: run up to a 1000-cycle quantum (clamped so we
        // stop exactly at MAX_CYCLES). runBatch returns cycles actually consumed.
        int want = MAX_CYCLES - cycles;
        if (want > 1000) want = 1000;
        uint32_t ran = ppu.runBatch((uint32_t)want);
        if (ran == 0) break;  // halted / no progress
        cycles += (int)ran;
        displayCycleAccumulator += DISPLAY_TICK_INCREMENT * (int64_t)ran;

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

    return 0;
}
