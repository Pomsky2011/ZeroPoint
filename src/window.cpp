#include "window.h"
#include <iostream>

namespace ZeroPoint {

Window::Window(int scale)
    : window(nullptr)
    , renderer(nullptr)
    , texture(nullptr)
    , audioDevice(0)
    , scale(scale)
    , quit(false)
{
}

Window::~Window() {
    if (audioDevice != 0) {
        SDL_CloseAudioDevice(audioDevice);
    }
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
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
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

    // No callback: this is push-mode audio. Frontends drive System at their
    // own pace and hand us the resulting samples via queueAudio() each
    // frame; SDL_QueueAudio() drains them to the device on its own thread.
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = System::AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;

    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDevice == 0) {
        std::cerr << "Audio device open failed: " << SDL_GetError() << " (continuing without audio)\n";
    } else {
        SDL_PauseAudioDevice(audioDevice, 0);
    }

    return true;
}

void Window::queueAudio(const int16_t* interleaved, int frameCount) {
    if (audioDevice == 0 || frameCount <= 0) {
        return;
    }
    SDL_QueueAudio(audioDevice, interleaved, static_cast<Uint32>(frameCount) * 2 * sizeof(int16_t));
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

void Window::getPlayerInput(uint8_t& direction, uint8_t& control, uint8_t& buttons) const {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    bool up = keys[SDL_SCANCODE_UP];
    bool down = keys[SDL_SCANCODE_DOWN];
    bool left = keys[SDL_SCANCODE_LEFT];
    bool right = keys[SDL_SCANCODE_RIGHT];

    direction = 0;
    control = PlayerInput::CTRL_CONNECTION;

    if (up && down && left && right) {
        control |= PlayerInput::CTRL_CENTER;
    } else if (up && left) {
        direction = PlayerInput::DIR_UPLEFT;
    } else if (up && right) {
        direction = PlayerInput::DIR_UPRIGHT;
    } else if (down && left) {
        direction = PlayerInput::DIR_DOWNLEFT;
    } else if (down && right) {
        direction = PlayerInput::DIR_DOWNRIGHT;
    } else if (up) {
        direction = PlayerInput::DIR_UP;
    } else if (down) {
        direction = PlayerInput::DIR_DOWN;
    } else if (left) {
        direction = PlayerInput::DIR_LEFT;
    } else if (right) {
        direction = PlayerInput::DIR_RIGHT;
    }

    if (keys[SDL_SCANCODE_A]) control |= PlayerInput::CTRL_BIGLEFT;
    if (keys[SDL_SCANCODE_F]) control |= PlayerInput::CTRL_BIGRIGHT;
    if (keys[SDL_SCANCODE_S]) control |= PlayerInput::CTRL_LITTLELEFT;
    if (keys[SDL_SCANCODE_D]) control |= PlayerInput::CTRL_LITTLERIGHT;
    if (keys[SDL_SCANCODE_RSHIFT]) control |= PlayerInput::CTRL_MENU;
    if (keys[SDL_SCANCODE_RETURN]) control |= PlayerInput::CTRL_PAUSE;

    buttons = 0;
    if (keys[SDL_SCANCODE_Z]) buttons |= PlayerInput::BTN_1;
    if (keys[SDL_SCANCODE_X]) buttons |= PlayerInput::BTN_2;
    if (keys[SDL_SCANCODE_C]) buttons |= PlayerInput::BTN_3;
    if (keys[SDL_SCANCODE_V]) buttons |= PlayerInput::BTN_4;
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
