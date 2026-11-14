// zpb_wav: run a ROM in the whole System (CPU, PPU, APU, DMA - what the frontends run) and write the audio
// it plays to a WAV, headlessly.
//
//   zpb_wav <rom.zpb> <seconds> <out.wav> [--boot bootrom.bin]
//
// <seconds> is console time (a System frame is ~1.3 ms, not 1/60 s). Prints the audio's peak, when it was last
// audible, and where the APU's sequencer is, so a script can check that a cartridge really got a score
// playing: it exits non-zero if nothing was ever audible.
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

using namespace ZeroPoint;

namespace {

bool writeWav(const char* path, const std::vector<int16_t>& lr, uint32_t rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) f.put((char)(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { f.put((char)v); f.put((char)(v >> 8)); };
    const uint32_t bytes = (uint32_t)(lr.size() * 2);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    f.write(reinterpret_cast<const char*>(lr.data()), bytes);
    return (bool)f;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <rom.zpb> <seconds> <out.wav> [--boot bootrom.bin]\n", argv[0]);
        return 2;
    }
    const double seconds = std::atof(argv[2]);
    const char* bootPath = nullptr;
    for (int i = 4; i + 1 < argc; i++)
        if (!std::strcmp(argv[i], "--boot")) bootPath = argv[i + 1];

    System system;
    if (bootPath && !system.loadBootROM(bootPath)) { std::fprintf(stderr, "cannot load boot ROM\n"); return 1; }
    if (!system.loadROM(argv[1])) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    system.powerOn();

    const double frameSeconds = System::CYCLES_PER_FRAME / 68011355.0;
    const uint64_t frames = (uint64_t)(seconds / frameSeconds);
    std::vector<int16_t> pcm;
    for (uint64_t f = 0; f < frames; f++) {
        system.stepFrame();
        const auto& a = system.getAudioBuffer();
        pcm.insert(pcm.end(), a.begin(), a.end());
        system.clearAudioBuffer();
    }

    int peak = 0;
    size_t lastAudible = 0;
    for (size_t i = 0; i < pcm.size(); i++) {
        const int v = std::abs((int)pcm[i]);
        peak = std::max(peak, v);
        if (v > 200) lastAudible = i;
    }
    if (!writeWav(argv[3], pcm, System::AUDIO_SAMPLE_RATE)) { std::fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
    APU& apu = system.getAPU();
    std::printf("%s: %.2f s of audio, peak %d of 32767, last audible at %.2f s, CPU %s, APU pc=$%04X, engine %s\n",
                argv[3], pcm.size() / 2.0 / System::AUDIO_SAMPLE_RATE, peak,
                lastAudible / 2.0 / System::AUDIO_SAMPLE_RATE, system.getCPU().isHalted() ? "halted" : "running",
                apu.getPC(), apu.hleEngineActive() ? "playing" : "idle");
    return peak > 1000 ? 0 : 1;
}
