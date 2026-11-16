#ifndef ZEROPOINT_WINDOW_H
#define ZEROPOINT_WINDOW_H

#include "display.h"
#include "cpu.h"
#include "system.h"
#include <SDL2/SDL.h>
#include <chrono>
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

    // Player 2, same layout. Default bindings: i/j/k/l = direction (adjacent pairs give diagonals, all four = center),
    // 7/8/9/0 = buttons 1-4, u/o = big-left/big-right, n/m = little-left/little-right, backspace = menu, p = pause.
    // Unplugged (all zero, no CTRL_CONNECTION) unless `plugged` - the frontend's --two-players.
    void getPlayer2Input(uint8_t& direction, uint8_t& control, uint8_t& buttons, bool plugged) const;

    // Host actions on the cartridge slot and the reset buttons, set by key presses: F1 soft reset, F2 hard reset
    // (power cycle), F3 yank the cartridge, F4 Stop 'n' Swop. takeHotkeys() returns the bits pressed since the last
    // call and clears them.
    enum Hotkey : unsigned { HOTKEY_SOFT_RESET = 1, HOTKEY_HARD_RESET = 2, HOTKEY_YANK = 4, HOTKEY_SWAP = 8 };
    unsigned takeHotkeys() { unsigned h = hotkeys; hotkeys = 0; return h; }

    // Push interleaved stereo 16-bit PCM (as produced by System::getAudioBuffer())
    // to the audio device. No-op if the device failed to open. Safe to call
    // with an empty/zero-length buffer.
    void queueAudio(const int16_t* interleaved, int frameCount);

    // How many times queueAudio() found the device's queue already empty after audio had started (the speakers
    // played silence). Also logged to stderr, at most once a second.
    unsigned long audioUnderrunCount() const { return audioUnderruns; }

private:
    // Convert ZeroPoint color format to SDL RGBA
    uint32_t convertColor(Color16 color) const;

    unsigned hotkeys = 0;
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    SDL_AudioDeviceID audioDevice;
    bool audioPrimed = false;
    unsigned long audioUnderruns = 0;
    std::chrono::steady_clock::time_point audioLastReport{};

    int scale;
    bool quit;
};

} // namespace ZeroPoint

#endif // ZEROPOINT_WINDOW_H
