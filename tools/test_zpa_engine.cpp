// test_zpa_engine: run a ZPA sound pack through an APU BIOS sequencer headlessly
// and print the MMP register writes per engine tick, in the same text format as
// `zpsnd.py trace` (the golden trace), so the two can simply be diffed.
//
//   test_zpa_engine <bios.bin> <pack.zpa> <ticks> [TICK=VALUE ...] [--timing]
//
// It plays the part of the Boot ROM: DMAs the STL/SST/score images into the APU,
// writes the header (magic last), zeroes the mailbox, and lets the BIOS run.
// TICK=VALUE writes VALUE to the CPU mailbox ($1000) at the start of that tick.
// The engine publishes its tick counter at $0207/$0208 (docs/apu/zpa.txt), which
// is how writes are attributed to ticks. The run stops after <ticks> ticks or
// when the engine ENDs and the BIOS is back at its entry point.
//
// --timing prints the engine's real tick period (APU cycles) to stderr; --timing-all every tick's.
#include "apu.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr uint16_t TICK_ADDR = 0x0207;      // engine tick counter, little-endian
constexpr uint16_t MAILBOX = 0x1000;
constexpr uint16_t STATUS = 0x1800;
constexpr uint16_t ENGINE_BASE = 0x8100;    // anything at or above this is engine code

struct Tracer {
    APU* apu = nullptr;
    int tick = 0;
    uint8_t lo[0x80] = {};
    std::vector<std::string> lines;

    void emit(const char* field, int ch, int value) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "t%04d ch%-2d %-5s %d", tick, ch, field, value);
        lines.push_back(buf);
    }

    void onWrite(uint16_t a, uint8_t v) {
        if (a == STATUS) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "t%04d POST  %d", tick, v);
            lines.push_back(buf);
        } else if (a < 0x20) {                      // pitch: low then high
            if (a & 1) emit("pitch", a / 2, (v << 8) | lo[a - 1]);
            else lo[a] = v;
        } else if (a < 0x30) {
            emit("vol", a - 0x20, v);
        } else if (a < 0x40) {
            emit("pan", a - 0x30, v);
        } else if (a >= 0x50 && a < 0x70) {         // STL address: low then high
            if (a & 1) emit("stl", (a - 0x50) / 2, (v << 8) | lo[a - 1]);
            else lo[a] = v;
        }
    }
};

void hook(void* ctx, uint16_t a, uint8_t v) { static_cast<Tracer*>(ctx)->onWrite(a, v); }

std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <bios.bin> <pack.zpa> <ticks> [TICK=VALUE ...] [--timing]\n", argv[0]);
        return 2;
    }
    const auto bios = slurp(argv[1]);
    const auto pack = slurp(argv[2]);
    const int maxTicks = std::atoi(argv[3]);
    std::map<int, int> mailbox;
    bool timing = false, timingAll = false;
    for (int i = 4; i < argc; i++) {
        if (!std::strcmp(argv[i], "--timing")) timing = true;
        else if (!std::strcmp(argv[i], "--timing-all")) timing = timingAll = true;
        else if (const char* eq = std::strchr(argv[i], '='))
            mailbox[std::atoi(argv[i])] = std::strtol(eq + 1, nullptr, 0);
    }
    if (bios.empty()) { std::fprintf(stderr, "%s: cannot read BIOS image\n", argv[1]); return 1; }
    if (pack.size() < 16 + 32 || std::memcmp(pack.data(), "ZPA1", 4) != 0) {
        std::fprintf(stderr, "%s: not a ZPA1 pack\n", argv[2]);
        return 1;
    }
    const size_t nstl = pack[6] | (pack[7] << 8);
    const size_t nsst = pack[8] | (pack[9] << 8);
    const size_t nscore = pack[10] | (pack[11] << 8);
    if (pack.size() < 16 + nstl + nsst + nscore + 32) { std::fprintf(stderr, "truncated pack\n"); return 1; }

    APU apu;
    Tracer tr;
    tr.apu = &apu;
    apu.loadBIOS(bios.data(), bios.size());
    apu.reset();

    // The Boot ROM's upload: STL, SST, score, then the header with the magic last.
    const uint8_t* p = pack.data() + 16;
    for (size_t i = 0; i < nstl; i++) apu.writeByte(0x7000 + i, p[i]);
    p += nstl;
    for (size_t i = 0; i < nsst; i++) apu.writeByte(0x9000 + i, p[i]);
    p += nsst;
    for (size_t i = 0; i < nscore; i++) apu.writeByte(0x3020 + i, p[i]);
    p += nscore;
    for (int i = 1; i < 32; i++) apu.writeByte(0x3000 + i, p[i]);
    apu.writeByte(MAILBOX, 0);
    apu.writeByte(0x3000, p[0]);

    apu.setWriteHook(hook, &tr);

    auto applyMailbox = [&](int t) {
        auto it = mailbox.begin();
        while (it != mailbox.end() && it->first <= t) {
            apu.writeByte(MAILBOX, it->second);
            it = mailbox.erase(it);
        }
    };
    applyMailbox(0);

    bool started = false;
    uint16_t ring[48] = {};
    unsigned ringPos = 0;
    int lastTick = -1;
    uint64_t lastCycles = 0;
    std::vector<uint64_t> periods;
    const uint64_t guard = (uint64_t)(maxTicks + 400) * 40000ull;   // a hung BIOS must not hang the test
    while (apu.getCycleCount() < guard) {
        ring[ringPos++ % 48] = apu.getPC();
        apu.step();
        if (apu.isHalted()) break;
        const uint16_t pc = apu.getPC();
        if (!started) {
            if (pc >= ENGINE_BASE && pc < 0x8F00) started = true;
            else continue;
        } else if (pc == 0x8000) {              // the engine's END hands control back to the BIOS entry
            char b[32];
            std::snprintf(b, sizeof b, "t%04d END", tr.tick);
            tr.lines.push_back(b);
            break;
        }
        const int t = apu.readByte(TICK_ADDR) | (apu.readByte(TICK_ADDR + 1) << 8);
        if (t != lastTick) {
            if (lastTick >= 0 && timing) periods.push_back(apu.getCycleCount() - lastCycles);
            lastTick = t;
            lastCycles = apu.getCycleCount();
            tr.tick = t;
            if (t >= maxTicks) break;
            applyMailbox(t);
        }
    }
    for (const auto& l : tr.lines) std::puts(l.c_str());
    if (apu.isHalted()) {
        std::fprintf(stderr, "last PCs:");
        for (unsigned i = 0; i < 48; i++) std::fprintf(stderr, " %04X", ring[(ringPos + i) % 48]);
        std::fprintf(stderr, "\n");
    }
    if (timing || apu.isHalted())
        std::fprintf(stderr, "stopped: tick=%d pc=$%04X sp=$%04X halted=%d cycles=%llu\n", tr.tick, apu.getPC(),
                     apu.getSP(), apu.isHalted() ? 1 : 0, (unsigned long long)apu.getCycleCount());

    if (timing && periods.size() > 2) {
        std::vector<uint64_t> v(periods.begin() + 1, periods.end());
        uint64_t sum = 0;
        for (auto x : v) sum += x;
        if (timingAll)
            for (size_t i = 0; i < v.size(); i++) std::fprintf(stderr, "period t%04zu %llu\n", i + 1, (unsigned long long)v[i]);
        std::fprintf(stderr, "tick period (APU cycles): n=%zu min=%llu max=%llu avg=%.1f (ideal 35422.6)\n",
                     v.size(), (unsigned long long)*std::min_element(v.begin(), v.end()),
                     (unsigned long long)*std::max_element(v.begin(), v.end()), (double)sum / v.size());
    }
    return 0;
}
