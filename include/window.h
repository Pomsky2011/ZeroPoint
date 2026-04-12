#ifndef ZEROPOINT_WINDOW_H
#define ZEROPOINT_WINDOW_H

#include "display.h"
#include <SDL2/SDL.h>
#include <cstdint>

namespace ZeroPoint {

class Window {
public:
    Window(int scale = 2);
    ~Window();

    // Initialize SDL and create window
    bool init();

    // Render the framebuffer to the window
    void render(const Display& display);

    // Check if window should close
    bool shouldClose() const { return quit; }

    // Process events
    void pollEvents();

private:
    // Convert ZeroPoint color format to SDL RGBA
    uint32_t convertColor(Color16 color) const;

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;

    int scale;
    bool quit;
};

} // namespace ZeroPoint

#endif // ZEROPOINT_WINDOW_H
