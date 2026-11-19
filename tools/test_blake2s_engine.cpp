// test_blake2s_engine: the BLAKE2s engine ($D80060-$D8009F, docs/blake2s-engine.md).
//
//   test_blake2s_engine <cartridge.zpb>      (any unsigned cartridge; test_movie writes a.zpb)
//
//   * the digest of [SRC, SRC+LEN) equals a host BLAKE2s of the same bytes, for several ranges (empty, 1 byte, 64 K, odd lengths)
//   * BUSY lasts exactly blocks * BLAKE2S_CYCLES_PER_BLOCK CPU cycles, the digest reads 0 until DONE, registers lock while busy
//   * a range outside the cartridge, or with the slot empty, is rejected (ERR) and returns no digest
//   * a yank or a reset drops a job in flight
//   * the engine's state is part of the machine hash (determinism)
#include "boot_hle/zpblake2s.h"
#include "system.h"
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

constexpr uint32_t REG = 0xD80060;

void spin(System& sys) {   // park the CPU in a loop in work RAM so its cycle counter keeps running
    CPU& cpu = sys.getCPU();
    cpu.writeByte(0x800100, 0x07); cpu.writeByte(0x800101, 0x00); cpu.writeByte(0x800102, 0x01);
    CPUFlags f = cpu.getFlags();
    f.I = true;
    cpu.setFlags(f);
    cpu.setPB(0x80);
    cpu.setPC(0x0100);
}

void start(CPU& cpu, uint32_t src, uint32_t len) {
    for (int i = 0; i < 3; i++) cpu.writeByte(REG + i, (src >> (8 * i)) & 0xFF);
    for (int i = 0; i < 4; i++) cpu.writeByte(REG + 4 + i, (len >> (8 * i)) & 0xFF);
    cpu.writeByte(REG + 8, 0x01);
}

bool digestIs(CPU& cpu, const unsigned char* want) {
    for (int i = 0; i < 32; i++) if (cpu.readByte(REG + 0x20 + i) != want[i]) return false;
    return true;
}

bool digestZero(CPU& cpu) {
    for (int i = 0; i < 32; i++) if (cpu.readByte(REG + 0x20 + i) != 0) return false;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <cartridge.zpb>\n", argv[0]); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    System sys;
    sys.setVerifySignatures(false);
    if (!sys.loadROMImage(image)) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    sys.powerOn();
    CPU& cpu = sys.getCPU();
    spin(sys);

    std::printf("digests\n");
    const struct { uint32_t src, len; } ranges[] = {{0, 0}, {0, 1}, {0, 64}, {0, 65}, {0x10, 1000}, {0, 0x10000}, {0x1234, 0x20001}};
    for (const auto& r : ranges) {
        std::vector<unsigned char> bytes(r.len);
        for (uint32_t i = 0; i < r.len; i++) bytes[i] = cpu.readByte(r.src + i);   // unsigned cartridge: nothing is gated
        unsigned char want[32];
        zp_blake2s_buf(bytes.data(), r.len, want);
        start(cpu, r.src, r.len);
        const uint64_t blocks = r.len ? (r.len + 63) / 64 : 1;
        const uint64_t cost = blocks * CPU::BLAKE2S_CYCLES_PER_BLOCK;
        char what[96];
        std::snprintf(what, sizeof what, "src %06X len %u: busy right after start, digest hidden", r.src, r.len);
        check((cpu.readByte(REG + 9) & 3) == 1 && digestZero(cpu), what);
        const uint64_t c0 = cpu.getCycleCount();
        while ((cpu.readByte(REG + 9) & 1) && cpu.getCycleCount() - c0 < cost * 2 + 1000) sys.runCycles(4);
        const uint64_t waited = cpu.getCycleCount() - c0;
        std::snprintf(what, sizeof what, "src %06X len %u: done, digest matches host BLAKE2s", r.src, r.len);
        check((cpu.readByte(REG + 9) & 3) == 2 && digestIs(cpu, want), what);
        std::snprintf(what, sizeof what, "src %06X len %u: took ~%llu CPU cycles (waited %llu)", r.src, r.len,
                      (unsigned long long)cost, (unsigned long long)waited);
        check(waited + 8 >= cost && waited <= cost + 8, what);
    }

    std::printf("registers\n");
    start(cpu, 0, 100000);
    cpu.writeByte(REG + 0, 0x55);   // ignored while busy
    check(cpu.readByte(REG + 0) == 0, "SRC is locked while the job runs");
    check((cpu.readByte(REG + 9) & 1) == 1, "still busy");
    cpu.writeByte(REG + 8, 0x80);
    check(cpu.readByte(REG + 9) == 0 && digestZero(cpu), "reset cancels it (idle, no digest)");

    std::printf("rejects\n");
    start(cpu, 0x7FFFF0, 0x100);
    check((cpu.readByte(REG + 9) & 7) == 4 && digestZero(cpu), "a range running off the end of the cartridge window is rejected");
    start(cpu, 0xE00000, 16);
    check((cpu.readByte(REG + 9) & 4) != 0, "a range outside banks $00-$7F is rejected");

    std::printf("slot and reset\n");
    start(cpu, 0, 100000);
    sys.ejectCartridge();
    check(cpu.readByte(REG + 9) == 0, "a yank drops the job");
    start(cpu, 0, 16);
    check((cpu.readByte(REG + 9) & 4) != 0, "an empty slot cannot be hashed");
    sys.insertCartridge(image);
    spin(sys);
    start(cpu, 0, 100000);
    sys.hardReset();
    check(cpu.readByte(REG + 9) == 0, "a reset drops the job");

    std::printf("determinism\n");
    {
        System a, b;
        a.setVerifySignatures(false); b.setVerifySignatures(false);
        a.loadROMImage(image); b.loadROMImage(image);
        a.powerOn(); b.powerOn();
        spin(a); spin(b);
        check(a.machineHash() == b.machineHash(), "identical machines hash alike");
        start(a.getCPU(), 0, 4096);
        check(a.machineHash() != b.machineHash(), "a started job is in the machine hash");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
