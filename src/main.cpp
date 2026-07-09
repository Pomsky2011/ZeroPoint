#define SDL_MAIN_HANDLED
#include "system.h"
#include "display.h"
#include "window.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using namespace ZeroPoint;

// One display frame = every pixel of every scanline. In the System model the
// display advances one tick per master clock cycle, so this is also the number
// of master cycles per frame.
static constexpr uint64_t CYCLES_PER_FRAME =
    static_cast<uint64_t>(TOTAL_SCANLINES) * TOTAL_PIXELS_PER_LINE;

// Draw a static gradient so the windowed app is still useful without a ROM.
static void fillTestPattern(Display& display) {
    for (int y = 0; y < FULL_HEIGHT; y++) {
        for (int x = 0; x < FB_WIDTH; x++) {
            uint16_t red = (x * 31 / 255) << 1;
            uint16_t blue = (y * 31 / 255) << 11;
            display.setPixel(x, y, red | blue);
        }
    }
}

// Fallback path (no ROM): present the test pattern, no CPU/PPU/APU execution.
static int runTestPattern(Window& window) {
    Display display;
    fillTestPattern(display);
    std::cout << "No ROM provided - showing test pattern. "
                 "Pass a .rom file to run the emulator.\n";
    while (!window.shouldClose()) {
        window.pollEvents();
        window.render(display);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return 0;
}

int main(int argc, char** argv) {
    std::cout << "ZeroPoint Emulator\n";
    std::cout << "==================\n";
    std::cout << "Usage: zeropoint_sdl [rom.rom] [--dev]\n";
    std::cout << "Press ESC to exit\n\n";

    std::string romPath;
    bool devMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--dev") == 0) {
            devMode = true;
        } else if (argv[i][0] != '-') {
            romPath = argv[i];
        }
    }

    Window window(2);  // 2x scale
    if (!window.init()) {
        std::cerr << "Failed to initialize window\n";
        return 1;
    }

    // No ROM: fall back to the static test pattern.
    if (romPath.empty()) {
        return runTestPattern(window);
    }

    // Full-system mode: drive CPU, PPU, APU, DMA, and display through System.
    System system;
    if (devMode) {
        system.setDevMode(true);
    }
    if (!system.loadROM(romPath)) {
        std::cerr << "Failed to load ROM: " << romPath << "\n";
        return 1;
    }
    system.reset();

    std::cout << "Running '" << system.getROMTitle() << "'...\n";

    // Run one display frame worth of master cycles, then render. Sleep to a
    // ~60 Hz target so we don't spin the host CPU at 100%.
    const auto frameDuration = std::chrono::microseconds(16667);
    while (!window.shouldClose() && !system.getCPU().isHalted()) {
        auto frameStart = std::chrono::steady_clock::now();

        window.pollEvents();

        for (uint64_t i = 0; i < CYCLES_PER_FRAME; i++) {
            if (system.getCPU().isHalted()) break;
            system.step();
        }

        window.render(system.getDisplay());

        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
    }

    if (system.getCPU().isHalted()) {
        std::cout << "CPU halted after " << system.getMasterCycleCount()
                  << " master cycles.\n";
    }
    std::cout << "Shutting down...\n";
    return 0;
}
