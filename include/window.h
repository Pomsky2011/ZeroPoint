#ifndef ZEROPOINT_WINDOW_H
#define ZEROPOINT_WINDOW_H

#include "display.h"
#include "cpu.h"
#include "system.h"
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

    // Player 1 input, computed from the current keyboard state. Default
    // bindings: arrows = direction (an adjacent pair gives a diagonal; all
    // four held at once has no directional meaning, so it doubles as the
    // D-pad's center-click), z/x/c/v = buttons 1-4, a/s/d/f = big-left/
    // little-left/little-right/big-right, right shift = menu, enter = pause.
    // See cpu.h PlayerInput:: for the register bit layout.
    void getPlayerInput(uint8_t& direction, uint8_t& control, uint8_t& buttons) const;

    // Push interleaved stereo 16-bit PCM (as produced by System::getAudioBuffer())
    // to the audio device. No-op if the device failed to open. Safe to call
    // with an empty/zero-length buffer.
    void queueAudio(const int16_t* interleaved, int frameCount);

private:
    // Convert ZeroPoint color format to SDL RGBA
    uint32_t convertColor(Color16 color) const;

    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_AudioDeviceID audioDevice;

    int scale;
    bool quit;
};

} // namespace ZeroPoint

#endif // ZEROPOINT_WINDOW_H
