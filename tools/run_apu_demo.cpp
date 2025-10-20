#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <SDL2/SDL.h>
#include "apu.h"

const int SAMPLE_RATE = 48000;
const int AUDIO_BUFFER_SIZE = 4096;

struct AudioContext {
    APU* apu;
    bool running;
};

void audioCallback(void* userdata, Uint8* stream, int len) {
    AudioContext* ctx = static_cast<AudioContext*>(userdata);
    int16_t* output = reinterpret_cast<int16_t*>(stream);
    int samples = len / 4;  // 2 channels × 2 bytes per sample

    for (int i = 0; i < samples; i++) {
        // Run APU for appropriate number of cycles per sample
        // At 4.2 MHz and 48kHz sample rate: 4,200,000 / 48,000 = 87.5 cycles/sample
        ctx->apu->run(88);

        // Update MMP and get mixed audio
        ctx->apu->updateMMP();
        int16_t left = ctx->apu->getMixedSampleLeft();
        int16_t right = ctx->apu->getMixedSampleRight();

        output[i * 2] = left;
        output[i * 2 + 1] = right;
    }
}

void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " <program.bin>\n";
    std::cerr << "\nRuns an APU program with audio output.\n";
    std::cerr << "program.bin: Binary file containing APU instructions\n";
    std::cerr << "\nControls:\n";
    std::cerr << "  ESC or Q: Quit\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    const char* binFile = argv[1];

    // Load binary file
    std::ifstream file(binFile, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << binFile << "\n";
        return 1;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "Error: Cannot read file: " << binFile << "\n";
        return 1;
    }

    file.close();

    std::cout << "Loaded " << size << " bytes from " << binFile << "\n";

    // Initialize SDL
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Create APU and load ROM
    APU apu;
    apu.loadROM(buffer.data(), size, 0x8000);  // Load to BIOS region
    apu.setPC(0x8000);  // Start at BIOS

    // Setup audio
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;  // Stereo
    want.samples = AUDIO_BUFFER_SIZE;
    want.callback = audioCallback;

    AudioContext ctx;
    ctx.apu = &apu;
    ctx.running = true;
    want.userdata = &ctx;

    SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audioDevice == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    std::cout << "Audio device opened:\n";
    std::cout << "  Sample rate: " << have.freq << " Hz\n";
    std::cout << "  Channels: " << static_cast<int>(have.channels) << "\n";
    std::cout << "  Buffer size: " << have.samples << " samples\n";

    // Start audio playback
    SDL_PauseAudioDevice(audioDevice, 0);

    std::cout << "\nAPU running with audio output.\n";
    std::cout << "Press ESC or Q to quit.\n";
    std::cout << "----------------------------------------\n";

    // Main loop (just wait for quit)
    SDL_Event event;
    bool running = true;
    uint64_t lastPrintTime = SDL_GetTicks();

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                    running = false;
                }
            }
        }

        // Print status every second
        uint64_t now = SDL_GetTicks();
        if (now - lastPrintTime >= 1000) {
            std::cout << "Cycles: " << apu.getCycleCount()
                      << "  Instructions: " << apu.getInstructionCount()
                      << "  PC: 0x" << std::hex << apu.getPC() << std::dec
                      << "  Halted: " << (apu.isHalted() ? "Yes" : "No") << "\r" << std::flush;
            lastPrintTime = now;
        }

        if (apu.isHalted()) {
            // If halted, just sleep
            SDL_Delay(100);
        } else {
            SDL_Delay(10);
        }
    }

    std::cout << "\n----------------------------------------\n";
    std::cout << "Stopping APU...\n";

    // Cleanup
    ctx.running = false;
    SDL_CloseAudioDevice(audioDevice);
    SDL_Quit();

    std::cout << "Total cycles: " << apu.getCycleCount() << "\n";
    std::cout << "Total instructions: " << apu.getInstructionCount() << "\n";

    return 0;
}
