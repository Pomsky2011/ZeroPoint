// zpa_play: load a ZPA sound pack's STL/SST banks into an APU and print what the
// MMP mixer plays on channel 0 for one sample, one right-channel value per line.
//
//   zpa_play <pack.zpa> <stl_index> <frames> [pitch]
//
// Channel 0 runs at pitch $1000 by default (exactly one stored sample per output
// frame), full volume, panned hard right, so each printed value is the stored
// sample itself. Used to check that what zpsnd bakes is what the mixer plays
// (ZPdevtools/scripts/test_zpsnd.py); the pack layout is in docs/apu/zpa.txt.
#include "apu.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <pack.zpa> <stl_index> <frames> [pitch]\n", argv[0]);
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 16 || b[0] != 'Z' || b[1] != 'P' || b[2] != 'A' || b[3] != '1') {
        std::fprintf(stderr, "%s: not a ZPA1 pack\n", argv[1]);
        return 1;
    }
    const size_t nstl = b[6] | (b[7] << 8);
    const size_t nsst = b[8] | (b[9] << 8);
    if (b.size() < 16 + nstl + nsst) {
        std::fprintf(stderr, "%s: truncated pack\n", argv[1]);
        return 1;
    }
    const int index = std::atoi(argv[2]);
    const int frames = std::atoi(argv[3]);
    const int pitch = argc > 4 ? std::strtol(argv[4], nullptr, 0) : 0x1000;

    APU apu;
    for (size_t i = 0; i < nstl; i++) apu.writeByte(0x7000 + i, b[16 + i]);
    for (size_t i = 0; i < nsst; i++) apu.writeByte(0x9000 + i, b[16 + nstl + i]);

    apu.writeByte(0x01, pitch >> 8);          // pitch high, then low
    apu.writeByte(0x00, pitch & 0xFF);
    apu.writeByte(0x20, 255);                 // volume
    apu.writeByte(0x30, 255);                 // pan: full right, so right = sample * 256
    const int stl = 0x7000 + index * 4;
    apu.writeByte(0x50, stl & 0xFF);          // STL address: low byte first
    apu.writeByte(0x51, stl >> 8);

    for (int i = 0; i < frames; i++) {
        apu.updateMMP();
        std::printf("%d\n", apu.getMixedSampleRight() / 256);
    }
    return 0;
}
