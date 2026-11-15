// Headless harness for the APU ADPCM decoder (ZPdevtools scripts/gen_adpcm_apu.py; driven by its scripts/test_adpcm.py): loads the program and tables into a bare
// APU, plays the CPU's part (keeps the input ring filled through the $1000/$1800 mailbox, wrapping the ADPCM stream
// the way the real feeder wraps the ROM) and the mixer's part (one updateMMP() per 48 kHz tick, which is what
// advances the stream's RDPTR), and records every byte the decoder writes into the stream ring.
//
//   test_adpcm_apu <code.bin> <tables.bin> <input.bin> <entry hex> <out.bin> [--fast] [--bytes N] [--max-seconds S]
//                  [--in-page0 HEX] [--in-pages N] [--out-page0 HEX] [--out-pages N]
//
// input.bin is whole 256-byte ADPCM pages; the track loops over all of them. Stops after N recorded bytes (two
// per frame). Prints instructions, cycles and the instructions per frame the decoder actually used.
#include "apu.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "cannot read %s\n", path); std::exit(2); }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

struct Capture {
    std::vector<uint8_t> bytes;
    uint64_t limit = 0;
    uint16_t lo = 0x3000, hi = 0x3800;     // the stream ring
};

static void onWrite(void* ctx, uint16_t address, uint8_t value) {
    Capture* c = static_cast<Capture*>(ctx);
    if (address >= c->lo && address < c->hi && c->bytes.size() < c->limit) c->bytes.push_back(value);
}

int main(int argc, char** argv) {
    if (argc < 6) { std::fprintf(stderr, "usage: %s code.bin tables.bin input.bin entry out.bin [--fast] [--bytes N] [--max-seconds S]\n", argv[0]); return 2; }
    const std::vector<uint8_t> code = readFile(argv[1]), tables = readFile(argv[2]), input = readFile(argv[3]);
    const uint16_t entry = static_cast<uint16_t>(std::strtoul(argv[4], nullptr, 16));
    bool fast = false;
    uint64_t wantBytes = 2 * 4096;
    double maxSeconds = 30;
    int inPage0 = 0x38, inPages = 56, outPage0 = 0x30, outPages = 8;
    for (int i = 6; i < argc; i++) {
        if (!std::strcmp(argv[i], "--fast")) fast = true;
        else if (!std::strcmp(argv[i], "--bytes") && i + 1 < argc) wantBytes = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--max-seconds") && i + 1 < argc) maxSeconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--in-page0") && i + 1 < argc) inPage0 = static_cast<int>(std::strtol(argv[++i], nullptr, 16));
        else if (!std::strcmp(argv[i], "--in-pages") && i + 1 < argc) inPages = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out-page0") && i + 1 < argc) outPage0 = static_cast<int>(std::strtol(argv[++i], nullptr, 16));
        else if (!std::strcmp(argv[i], "--out-pages") && i + 1 < argc) outPages = std::atoi(argv[++i]);
    }
    const size_t totalPages = input.size() / 256;
    if (!totalPages) { std::fprintf(stderr, "input.bin has no whole page\n"); return 2; }

    APU apu;
    apu.loadROM(code.data(), code.size(), 0x0200);
    apu.loadROM(tables.data(), tables.size(), 0x9000);
    apu.setFastMode(fast);
    apu.setPC(entry);
    Capture cap;
    cap.limit = wantBytes;
    cap.lo = static_cast<uint16_t>(outPage0 << 8);
    cap.hi = static_cast<uint16_t>((outPage0 + outPages) << 8);
    apu.setWriteHook(onWrite, &cap);

    // the CPU's part
    const int IN_PAGES = inPages;
    size_t src = 0;           // next ADPCM page to send
    int wr = 0;               // next input ring page to fill
    auto feed = [&]() {
        const int rd = apu.readByte(0x1800);
        while ((wr + 1) % IN_PAGES != rd) {
            for (int i = 0; i < 256; i++) apu.writeByte(inPage0 * 256 + wr * 256 + i, input[src * 256 + i]);
            src = (src + 1) % totalPages;
            wr = (wr + 1) % IN_PAGES;
            apu.writeByte(0x1000, static_cast<uint8_t>(wr));
        }
    };
    feed();

    const uint64_t clockHz = fast ? 8501420ull : 4250710ull;
    uint64_t mixAcc = 0, lastCycles = 0, steps = 0;
    const uint64_t maxCycles = static_cast<uint64_t>(maxSeconds * clockHz);
    const uint64_t i0 = apu.getInstructionCount();
    while (cap.bytes.size() < wantBytes && apu.getCycleCount() < maxCycles && !apu.isHalted()) {
        apu.step();
        const uint64_t now = apu.getCycleCount();
        mixAcc += (now - lastCycles) * 48000;
        lastCycles = now;
        while (mixAcc >= clockHz) { mixAcc -= clockHz; apu.updateMMP(); }
        if ((++steps & 0xFF) == 0) feed();
    }
    std::ofstream(argv[5], std::ios::binary).write(reinterpret_cast<const char*>(cap.bytes.data()), cap.bytes.size());
    const uint64_t instr = apu.getInstructionCount() - i0;
    const double frames = cap.bytes.size() / 2.0;
    std::printf("captured %zu bytes (%.0f frames) in %llu instructions, %llu cycles (%.2f s of APU time)%s\n", cap.bytes.size(), frames,
                (unsigned long long)instr, (unsigned long long)apu.getCycleCount(), apu.getCycleCount() / double(clockHz), apu.isHalted() ? " [APU HALTED]" : "");
    if (frames > 0) std::printf("%.1f instructions per frame\n", instr / frames);
    return cap.bytes.size() >= wantBytes ? 0 : 1;
}
