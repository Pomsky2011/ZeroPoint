#include "display.h"
#include "window.h"
#include <iostream>

using namespace ZeroPoint;

int main() {
    Display display;
    Window window(2); // 2x scale

    std::cout << "ZeroPoint Emulator\n";
    std::cout << "==================\n";
    std::cout << "Press ESC to exit\n\n";

    if (!window.init()) {
        std::cerr << "Failed to initialize window\n";
        return 1;
    }

    // Fill framebuffer with a test pattern
    // Create a simple gradient
    for (int y = 0; y < FULL_HEIGHT; y++) {
        for (int x = 0; x < FB_WIDTH; x++) {
            // Simple pattern: red increases with x, blue with y
            uint16_t red = (x * 31 / 255) << 1;
            uint16_t blue = (y * 31 / 255) << 11;
            display.setPixel(x, y, red | blue);
        }
    }

    std::cout << "Window created. Running emulation...\n";

    // Main loop
    while (!window.shouldClose()) {
        window.pollEvents();

        // Run one complete frame (261 scanlines * 340 pixels)
        for (int i = 0; i < TOTAL_SCANLINES * TOTAL_PIXELS_PER_LINE; i++) {
            display.tick();
        }

        // Render the framebuffer
        window.render(display);
    }

    std::cout << "Shutting down...\n";

    return 0;
}
