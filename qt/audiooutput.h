#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <atomic>
#include <cstdint>
#include <vector>

// Push-mode audio device for the Qt frontend: the same SDL_QueueAudio setup as
// ZeroPoint::Window::queueAudio (stereo S16 at System::AUDIO_SAMPLE_RATE), but
// with only SDL's audio subsystem initialised - no window or renderer.
class AudioOutput
{
public:
    AudioOutput();
    ~AudioOutput();

    AudioOutput(const AudioOutput &) = delete;
    AudioOutput &operator=(const AudioOutput &) = delete;

    // False if no device could be opened; every other call is then a no-op.
    bool isOpen() const { return device != 0; }

    // Interleaved stereo 16-bit PCM, as System::getAudioBuffer() produces it.
    // Scaled by the volume, dropped while muted. Safe with frameCount == 0.
    void queue(const int16_t *interleaved, int frameCount);

    // Throw away anything still waiting on the device (stop/reset/new ROM).
    void flush();

    // 0-100. Linear gain on the samples, so 100 is the mixer's output untouched.
    void setVolume(int percent);
    void setEnabled(bool on);

private:
    uint32_t device;               // SDL_AudioDeviceID
    bool subsystemUp;
    std::atomic<int> volume;       // set by the GUI thread, read by whichever thread calls queue()
    std::atomic<bool> enabled;
    std::vector<int16_t> scaled;
};

#endif // AUDIOOUTPUT_H
