// The Qt frontend has its own main(); keep SDL.h from renaming it to SDL_main.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "audiooutput.h"
#include "system.h"
#include <algorithm>
#include <iostream>

namespace {
// The frontend's timer runs the console in bursts, so a little queue is normal.
// Past this much the device has stalled or the console ran ahead; drop the
// backlog rather than let audio drift further and further behind the picture.
constexpr Uint32 MAX_QUEUED_BYTES = ZeroPoint::System::AUDIO_SAMPLE_RATE * 2 * sizeof(int16_t) / 4;   // 0.25 s
}

AudioOutput::AudioOutput()
    : device(0)
    , subsystemUp(false)
    , volume(80)
    , enabled(true)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL audio init failed: " << SDL_GetError() << " (continuing without audio)\n";
        return;
    }
    subsystemUp = true;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = ZeroPoint::System::AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;

    // No callback: push mode, same as Window.
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (device == 0) {
        std::cerr << "Audio device open failed: " << SDL_GetError() << " (continuing without audio)\n";
        return;
    }
    SDL_PauseAudioDevice(device, 0);
}

AudioOutput::~AudioOutput()
{
    if (device != 0) {
        SDL_CloseAudioDevice(device);
    }
    if (subsystemUp) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void AudioOutput::queue(const int16_t *interleaved, int frameCount)
{
    if (device == 0 || !enabled || frameCount <= 0) {
        return;
    }

    const Uint32 queued = SDL_GetQueuedAudioSize(device);
    if (primed && queued == 0) {
        underruns++;
        const auto now = std::chrono::steady_clock::now();
        if (now - lastReport >= std::chrono::seconds(1)) {
            lastReport = now;
            std::cerr << "audio: the device queue ran dry (" << underruns
                      << " so far): the emulator fell behind the wall clock\n";
        }
    }
    if (queued > MAX_QUEUED_BYTES) {
        SDL_ClearQueuedAudio(device);
    }
    primed = true;

    const size_t count = static_cast<size_t>(frameCount) * 2;
    const int gain = volume;   // one read: the GUI thread can change it mid-call
    if (gain >= 100) {
        SDL_QueueAudio(device, interleaved, static_cast<Uint32>(count * sizeof(int16_t)));
        return;
    }
    scaled.resize(count);
    for (size_t i = 0; i < count; i++) {
        scaled[i] = static_cast<int16_t>(interleaved[i] * gain / 100);
    }
    SDL_QueueAudio(device, scaled.data(), static_cast<Uint32>(count * sizeof(int16_t)));
}

void AudioOutput::flush()
{
    primed = false;
    if (device != 0) {
        SDL_ClearQueuedAudio(device);
    }
}

void AudioOutput::setVolume(int percent)
{
    volume = std::clamp(percent, 0, 100);
}

void AudioOutput::setEnabled(bool on)
{
    enabled = on;
    if (!on) {
        flush();
    }
}
