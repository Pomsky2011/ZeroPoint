#define SDL_MAIN_HANDLED
#include "system.h"
#include "display.h"
#include "window.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using namespace ZeroPoint;

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
    std::cout << "Usage: zeropoint_sdl [rom.rom] [--dev] [--scale N] [--boot boot.bin]\n";
    std::cout << "Press ESC to exit\n\n";

    std::string romPath;
    std::string bootPath;
    bool devMode = false;
    int scale = 2;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--dev") == 0) {
            devMode = true;
        } else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = std::atoi(argv[++i]);
            if (scale <= 0) {
                std::cerr << "Invalid --scale value, using 2x\n";
                scale = 2;
            }
        } else if (std::strcmp(argv[i], "--boot") == 0 && i + 1 < argc) {
            bootPath = argv[++i];
        } else if (argv[i][0] != '-') {
            romPath = argv[i];
        }
    }

    Window window(scale);
    if (!window.init()) {
        std::cerr << "Failed to initialize window\n";
        return 1;
    }

    // No ROM and no alternate Boot ROM: fall back to the static test pattern.
    if (romPath.empty() && bootPath.empty()) {
        return runTestPattern(window);
    }

    // Full-system mode: drive CPU, PPU, APU, DMA, and display through System.
    System system;
    if (devMode) {
        system.setDevMode(true);
    }
    if (!bootPath.empty() && !system.loadBootROM(bootPath)) {
        std::cerr << "Failed to load Boot ROM: " << bootPath << "\n";
        return 1;
    }

    // A Boot ROM payload with no cartridge (e.g. a hardware demo) runs on
    // its own - only load/require a ROM if one was actually given.
    if (romPath.empty()) {
        system.powerOn();
        std::cout << "Running Boot ROM demo (no cartridge)...\n";
        while (!window.shouldClose()) {
            window.pollEvents();
            system.stepFrame();
            window.render(system.getDisplay());
        }
        return 0;
    }

    if (!system.loadROM(romPath)) {
        std::cerr << "Failed to load ROM: " << romPath << "\n";
        return 1;
    }
    system.powerOn();

    std::cout << "Running '" << system.getROMTitle() << "'...\n";

    // Run one display frame worth of master cycles, then render. Sleep to a
    // ~60 Hz target so we don't spin the host CPU at 100%.
    const auto frameDuration = std::chrono::microseconds(16667);
    while (!window.shouldClose() && !system.getCPU().isHalted()) {
        auto frameStart = std::chrono::steady_clock::now();

        window.pollEvents();

        uint8_t direction, control, buttons;
        window.getPlayerInput(direction, control, buttons);
        system.getCPU().setPlayerInput(direction, control, buttons);

        system.stepFrame();

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
