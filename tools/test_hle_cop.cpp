// test_hle_cop: the native Boot ROM (System's HLE, docs/boot-hle.md) against the real thing.
//
//   test_hle_cop <bootrom.bin> <v1.zpb> <v3.zpb> <unsigned.zpb> <workdir>
//
// <v1.zpb>/<v3.zpb> are zpbuild-signed ROMs (trailer v1, and v3 with --codesize so there are data chunks);
// tools/test_hle_cop.sh builds them. The math services are covered by test_boot_trig --hle; this covers
// the rest:
//   * boot: a good signature runs, and strict mode halts on every way a ROM can be wrong (bad code byte,
//     bad header, bad signature, unsigned, malformed trailer); --no-verify's setVerifySignatures(false) boots anything
//   * COP #$FF: a v3 data chunk reads as poison until verified, verifies only if its bytes match the
//     signed manifest, and a tampered chunk stays poison while its neighbours verify
//   * COP #$F7: the PPU ends up with the same bytes as under the real Boot ROM (and the real ROM's
//     cycle cost is printed, for the constants in src/system.cpp)
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

std::vector<uint8_t> slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void spit(const std::string& path, const std::vector<uint8_t>& d) {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(d.data()), (std::streamsize)d.size());
}

uint32_t rd32(const std::vector<uint8_t>& d, size_t o) {
    return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24);
}

// Boots `rom` (HLE) and returns the System. verify = strict signature check on/off.
struct Booted {
    System sys;
    bool loaded = false;
};
void boot(Booted& b, const std::string& rom, bool verify) {
    b.sys.setVerifySignatures(verify);
    b.loaded = b.sys.loadROM(rom);
    b.sys.powerOn();
}

bool running(System& sys, uint32_t entry) {
    CPU& cpu = sys.getCPU();
    return !cpu.isHalted() && cpu.getPB() == ((entry >> 16) & 0xFF) && cpu.getPC() == (entry & 0xFFFF);
}

constexpr uint32_t STUB = 0x800100;   // COP #sig at $80:0100, the instruction after it at $80:0102

// Runs `COP #sig` with the given registers; returns the CPU cycles it took (0 if it never came back).
uint64_t runCop(System& sys, uint8_t sig, uint16_t a, uint16_t x, uint16_t y, bool maskIRQ = false) {
    CPU& cpu = sys.getCPU();
    cpu.writeByte(STUB, 0xAF);
    cpu.writeByte(STUB + 1, sig);
    cpu.writeByte(STUB + 2, 0x00);
    CPUFlags f;
    f.M = f.X = false;
    f.I = maskIRQ;   // a started PPU raises blank IRQs, and there is no cartridge handler to take them
    cpu.setFlags(f);
    cpu.setPB(0x80);
    cpu.setPC(0x0100);
    cpu.setDB(0x80);
    cpu.setSP(0xF000);
    cpu.setA(a);
    cpu.setX(x);
    cpu.setY(y);
    const uint64_t c0 = cpu.getCycleCount();
    for (uint64_t i = 0; i < 50000000ull; i++) {
        sys.step();
        if (cpu.getPB() == 0x80 && cpu.getPC() == 0x0102) return cpu.getCycleCount() - c0;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s <bootrom.bin> <v1.zpb> <v3.zpb> <unsigned.zpb> <workdir>\n", argv[0]);
        return 2;
    }
    const std::string bootrom = argv[1], v1 = argv[2], v3 = argv[3], unsignedRom = argv[4], work = argv[5];
    std::vector<uint8_t> v1d = slurp(v1), v3d = slurp(v3);
    if (v1d.size() < 400 || v3d.size() < 400) { std::fprintf(stderr, "cannot read signed ROMs\n"); return 1; }
    const uint32_t entry = rd32(v1d, 12);
    const uint32_t payload = rd32(v1d, 8);
    const uint32_t codeSize = rd32(v3d, 64 + rd32(v3d, 8) + 296);

    std::printf("HLE boot: signature verification\n");
    {
        Booted b;
        boot(b, v1, true);
        check(b.loaded && running(b.sys, entry), "v1 signed ROM boots and lands on its entry point");
        check(b.sys.getCPU().getA() == ((entry >> 16) & 0xFF) && b.sys.getCPU().readByte(0x800000) == 0x10,
              "leaves the stub's side effects (JMP-long trampoline in WRAM, A = entry bank)");
    }
    {
        Booted b;
        boot(b, v3, true);
        check(b.loaded && running(b.sys, rd32(v3d, 12)), "v3 signed ROM (code + manifest) boots");
    }
    struct Tamper { const char* what; const char* src; size_t off; };
    const Tamper bad[] = {
        {"v1: one payload byte flipped", "v1", 64 + 5},
        {"v1: last payload byte flipped", "v1", 64 + payload - 1},
        {"v1: header entry point changed", "v1", 12},
        {"v1: header title changed", "v1", 20},
        {"v1: signature byte flipped", "v1", 64 + payload + 40 + 7},
        {"v3: a code byte flipped", "v3", 64 + 3},
        {"v3: manifest entry flipped", "v3", 64 + payload + 300 + 5},
        {"v3: codeSize field changed", "v3", 64 + payload + 296},
    };
    for (const Tamper& t : bad) {
        std::vector<uint8_t> d = std::string(t.src) == "v1" ? v1d : v3d;
        d[t.off] ^= 0x01;
        spit(work + "/tampered.zpb", d);
        Booted b;
        boot(b, work + "/tampered.zpb", true);
        check(b.loaded && b.sys.getCPU().isHalted(), t.what);
        Booted open;
        boot(open, work + "/tampered.zpb", false);
        if (std::string(t.what).find("header entry") == std::string::npos && std::string(t.what).find("codeSize") == std::string::npos)
            check(!open.sys.getCPU().isHalted(), "  ...and --no-verify still runs it");
    }
    {
        std::vector<uint8_t> d = v1d;
        d.resize(d.size() - 8);   // trailer cut short
        spit(work + "/tampered.zpb", d);
        Booted b;
        boot(b, work + "/tampered.zpb", true);
        check(b.loaded ? b.sys.getCPU().isHalted() : true, "v1: truncated trailer is refused (or doesn't even load)");
    }
    {
        Booted b;
        boot(b, unsignedRom, true);
        check(b.loaded && b.sys.getCPU().isHalted(), "unsigned ROM halts in strict mode");
        Booted open;
        boot(open, unsignedRom, false);
        check(open.loaded && !open.sys.getCPU().isHalted() && open.sys.getCPU().getPB() == 0, "unsigned ROM boots with verification off");
    }

    std::printf("COP #$FF: lazy chunk verification (v3)\n");
    {
        const uint32_t chunks = (payload - codeSize + 16383) / 16384;
        const uint32_t addr0 = codeSize;                 // first data byte, bank $00
        std::vector<uint8_t> good(v3d.begin() + 64 + codeSize, v3d.begin() + 64 + codeSize + 32);
        Booted b;
        boot(b, v3, true);
        CPU& cpu = b.sys.getCPU();
        check(chunks >= 2, "test ROM has at least two data chunks");
        check(cpu.readByte(addr0) == 0xDE && cpu.readByte(addr0 + 1) == 0xAF, "an unverified chunk reads as poison (DE AF)");
        runCop(b.sys, 0xFF, 0, 0, 0);
        check(cpu.readByte(addr0) == good[0] && cpu.readByte(addr0 + 31) == good[31], "COP #$FF X=0 verifies chunk 0: real bytes appear");
        check(cpu.readByte(addr0 + 16384) == 0xDE, "chunk 1 is still poison");
        runCop(b.sys, 0xFF, 0, 0x7FFF, 0);
        runCop(b.sys, 0xFF, 0, 0xFFFF, 0);
        check(cpu.readByte(addr0 + 16384) == 0xDE, "out-of-range chunk indices do nothing");
        runCop(b.sys, 0xFF, 0, 1, 0);
        check(cpu.readByte(addr0 + 16384) == v3d[64 + addr0 + 16384], "COP #$FF X=1 verifies chunk 1");
    }
    {
        // Tamper chunk 0's data only: the boot still passes (data isn't hashed at boot), chunk 0 never
        // verifies, chunk 1 does. The cartridge can't read the forged bytes.
        std::vector<uint8_t> d = v3d;
        d[64 + codeSize + 9] ^= 0x40;
        spit(work + "/tampered.zpb", d);
        Booted b;
        boot(b, work + "/tampered.zpb", true);
        CPU& cpu = b.sys.getCPU();
        check(!cpu.isHalted(), "v3 with a tampered data chunk still boots (data is checked lazily)");
        runCop(b.sys, 0xFF, 0, 0, 0);
        runCop(b.sys, 0xFF, 0, 1, 0);
        check(cpu.readByte(codeSize + 9) == 0xDE || cpu.readByte(codeSize + 9) == 0xAF, "the tampered chunk never verifies: it keeps reading as poison");
        check(cpu.readByte(codeSize + 16384) == d[64 + codeSize + 16384], "the untouched chunk next to it does verify");
    }

    std::printf("COP #$F7: PPU load, HLE vs the real Boot ROM\n");
    {
        // A PPU program: ppuasm's HLT (10 bytes) at address 0, zeros through the MMIO pages, then a pattern.
        static const uint8_t hlt[10] = {0xe0, 0x3e, 0xe1, 0xbe, 0xe4, 0x00, 0xe5, 0x00, 0xf7, 0x00};
        std::vector<uint8_t> prog(256 * 256, 0);
        std::copy(hlt, hlt + 10, prog.begin());
        for (size_t i = 0x400; i < prog.size(); i++) prog[i] = (uint8_t)(i * 37 + (i >> 8) * 11 + 5);

        struct Run { uint64_t cycles; uint16_t a; PPUState state; std::vector<uint8_t> mem; };
        const uint32_t pageCounts[] = {1, 4, 5, 16, 64, 0};
        // One System at a time, to keep memory down (test_determinism covers several at once).
        auto runAll = [&](bool realRom) {
            std::vector<Run> out;
            System sys;
            if (realRom && !sys.loadBootROM(bootrom)) { std::fprintf(stderr, "cannot load boot ROM\n"); std::exit(1); }
            sys.reset();
            for (size_t i = 0; i < prog.size(); i++) sys.getCPU().writeByte(0x900000 + i, prog[i]);   // $90:0000..$90:FFFF, clear of the stack at $80:EFxx
            for (uint32_t pages : pageCounts) {
                Run r;
                r.cycles = runCop(sys, 0xF7, 0x90, 0x0000, (uint16_t)pages, true);
                r.a = sys.getCPU().getA();
                r.state = sys.getPPU().getState();
                uint32_t n = std::min<uint32_t>((pages ? pages : 256) * 256, 0xE000);
                // $00E0-$00E1 is the live scanline register: it reads differently whenever you look.
                for (uint32_t i = 0; i < n; i++) r.mem.push_back(i == 0xE0 || i == 0xE1 ? 0 : sys.getPPU().readMemory((uint16_t)i));
                out.push_back(std::move(r));
            }
            return out;
        };
        std::vector<Run> real = runAll(true), hle = runAll(false);
        for (size_t k = 0; k < real.size(); k++) {
            uint32_t pages = pageCounts[k] ? pageCounts[k] : 256;
            char what[160];
            std::snprintf(what, sizeof what, "pages=%-3u same PPU bytes, A=0, started   (real ROM %llu cycles, HLE charged %llu)",
                          pages, (unsigned long long)real[k].cycles, (unsigned long long)hle[k].cycles);
            if (real[k].mem != hle[k].mem || real[k].a != hle[k].a || real[k].state != hle[k].state) {
                size_t d = 0;
                while (d < real[k].mem.size() && d < hle[k].mem.size() && real[k].mem[d] == hle[k].mem[d]) d++;
                std::printf("    A real=%04X hle=%04X, PPU state real=%d hle=%d, first byte diff at $%04zX (real %02X, hle %02X)\n",
                            real[k].a, hle[k].a, (int)real[k].state, (int)hle[k].state, d,
                            d < real[k].mem.size() ? real[k].mem[d] : 0, d < hle[k].mem.size() ? hle[k].mem[d] : 0);
            }
            check(real[k].cycles && hle[k].cycles && real[k].mem == hle[k].mem && real[k].a == 0 && hle[k].a == 0 &&
                      real[k].state == hle[k].state, what);
        }
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
