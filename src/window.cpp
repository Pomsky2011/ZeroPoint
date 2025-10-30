#include "window.h"
#include <iostream>

namespace ZeroPoint {

Window::Window(int scale)
    : window(nullptr)
    , renderer(nullptr)
    , texture(nullptr)
    , scale(scale)
    , quit(false)
{
}

Window::~Window() {
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

bool Window::init() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << "\n";
        return false;
    }

    window = SDL_CreateWindow(
        "ZeroPoint",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        FB_WIDTH * scale,
        FULL_HEIGHT * scale,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        std::cerr << "Window creation failed: " << SDL_GetError() << "\n";
        return false;
    }

    // Use accelerated rendering without vsync for maximum performance
    // The emulator controls frame timing, not the display
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << "\n";
        return false;
    }

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        FB_WIDTH,
        FULL_HEIGHT
    );

    if (!texture) {
        std::cerr << "Texture creation failed: " << SDL_GetError() << "\n";
        return false;
    }

    return true;
}

void Window::render(const Display& display) {
    // Allocate pixel buffer (SDL_UpdateTexture is faster than LockTexture on some drivers)
    uint32_t pixels[FB_WIDTH * FULL_HEIGHT];

    // Read scanline-by-scanline with DIRECT conversion to SDL ARGB format
    // This eliminates the double color conversion (was: RGBA16->RGBA32->ARGB)
    for (int y = 0; y < FULL_HEIGHT; y++) {
        // Get scanline directly in SDL format (single conversion)
        display.getScanlineSDL(y, &pixels[y * FB_WIDTH]);
    }

    // Update texture (driver-optimized upload path)
    SDL_UpdateTexture(texture, nullptr, pixels, FB_WIDTH * sizeof(uint32_t));

    // Clear and render
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

void Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            quit = true;
        } else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                quit = true;
            }
        }
    }
}

uint32_t Window::convertColor(Color16 color) const {
    // Format: BBBBBGGGGGRRRRR-
    // Extract 5-bit components (ignore bit 0)
    uint8_t r5 = (color >> 1) & 0x1F;
    uint8_t g5 = (color >> 6) & 0x1F;
    uint8_t b5 = (color >> 11) & 0x1F;

    // Convert 5-bit to 8-bit by scaling
    uint8_t r8 = (r5 * 255) / 31;
    uint8_t g8 = (g5 * 255) / 31;
    uint8_t b8 = (b5 * 255) / 31;

    // Return RGBA8888 format (0xRRGGBBAA)
    return (r8 << 24) | (g8 << 16) | (b8 << 8) | 0xFF;
}

} // namespace ZeroPoint
