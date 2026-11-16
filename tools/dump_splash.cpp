// Boots a boot ROM + cartridge headlessly and dumps latched output frames as raw RGBA bytes (R,G,B,A per pixel,
// 256x256), for looking at the splash without a display (convert with tools/splash_frames.py).
//   dump_splash <bootrom.bin> <cart.zpb> <outprefix> <frame> [<frame>...]
// Writes <outprefix>_<frame>.rgba for each requested frame number (0-based count of stepFrame() calls).
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace ZeroPoint;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <bootrom.bin> <cart.zpb> <outprefix> <frame> [<frame>...]\n", argv[0]);
        return 2;
    }
    std::vector<int> frames;
    for (int i = 4; i < argc; i++) frames.push_back(std::atoi(argv[i]));
    const int last = *std::max_element(frames.begin(), frames.end());

    System sys;
    if (!sys.loadBootROM(argv[1]) || !sys.loadROM(argv[2])) { std::fprintf(stderr, "cannot load\n"); return 1; }
    sys.powerOn();

    std::vector<Color32> row(FB_WIDTH);
    for (int f = 0; f <= last; f++) {
        sys.stepFrame();
        if (std::find(frames.begin(), frames.end(), f) == frames.end()) continue;
        char path[512];
        std::snprintf(path, sizeof path, "%s_%d.rgba", argv[3], f);
        FILE* out = std::fopen(path, "wb");
        if (!out) { std::fprintf(stderr, "cannot write %s\n", path); return 1; }
        for (int y = 0; y < 256; y++) {
            if (!sys.getDisplay().getScanline(y, row.data())) std::fill(row.begin(), row.end(), 0u);
            for (Color32 px : row) {
                const uint8_t b[4] = {uint8_t(px >> 24), uint8_t(px >> 16), uint8_t(px >> 8), uint8_t(px)};
                std::fwrite(b, 1, 4, out);
            }
        }
        std::fclose(out);
        std::printf("wrote %s\n", path);
    }
    return 0;
}
