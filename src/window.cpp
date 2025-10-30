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

    // Use accelerated rendering with Metal backend on macOS
    // SDL_RENDERER_ACCELERATED uses Metal on macOS (via MoltenVK internally)
    // Add SDL_RENDERER_TARGETTEXTURE for direct GPU memory access
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!renderer) {
        std::cerr << "Renderer creation failed: " << SDL_GetError() << "\n";
        return false;
    }

    // Print renderer info to verify we're using Metal
    SDL_RendererInfo info;
    SDL_GetRendererInfo(renderer, &info);
    std::cout << "SDL Renderer: " << info.name << "\n";

    texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,  // Match our LUT output format (ARGB)
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
    // Use SDL_UpdateTexture (faster than LockTexture on macOS drivers)
    // Allocate buffer on stack (fast, avoids heap allocation overhead)
    uint32_t pixels[FB_WIDTH * FULL_HEIGHT];

    // Convert all scanlines to SDL ARGB format
    for (int y = 0; y < FULL_HEIGHT; y++) {
        display.getScanlineSDL(y, &pixels[y * FB_WIDTH]);
    }

    // Upload to GPU (driver-optimized DMA path)
    SDL_UpdateTexture(texture, nullptr, pixels, FB_WIDTH * sizeof(uint32_t));

    // Render (no clear needed, texture contains full frame)
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
