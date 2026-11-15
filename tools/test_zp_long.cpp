// test_zp_long: run libzp's 32-bit helpers (ZPdevtools/lib/zp_long.asm) against the host's 32-bit arithmetic.
//
//   test_zp_long <cpuasm> <zp_long.asm>
//
// The library is assembled with cpuasm, loaded into Work RAM and each helper is called the way the compilers
// call it: operands in $80:2C00 (left) and $80:2C04 (right), result in A (low) and X (high). Edge values are
// crossed with each other, plus pseudo-random pairs; shift counts run 0..40.
#include "system.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

int failures = 0;
long checks = 0;

const char* const kHelpers[] = {"__zp_ladd", "__zp_lsub", "__zp_lmul", "__zp_land", "__zp_lor", "__zp_lxor",
                                "__zp_lshl", "__zp_llsr", "__zp_lasr", "__zp_ludiv", "__zp_lumod",
                                "__zp_lsdiv", "__zp_lsmod", "__zp_lcmpu", "__zp_lcmps"};
enum { ADD, SUB, MUL, AND, OR, XOR, SHL, LSR, ASR, UDIV, UMOD, SDIV, SMOD, CMPU, CMPS };
constexpr uint16_t THUNK = 0x0200;   // thunk i = JSR helper at THUNK + 8*i; the return lands 3 bytes on

void put32(CPU& cpu, uint32_t addr, uint32_t v) {
    for (int i = 0; i < 4; i++) cpu.writeByte(addr + i, (v >> (8 * i)) & 0xFF);
}

struct Out { uint32_t value; bool returned, preserved; };

Out call(System& sys, int which, uint32_t l, uint32_t r) {
    CPU& cpu = sys.getCPU();
    put32(cpu, 0x802C00, l);
    put32(cpu, 0x802C04, r);
    CPUFlags f;
    f.M = f.X = false;
    cpu.setFlags(f);
    cpu.setPB(0x80);
    cpu.setDB(0x5A);
    cpu.setD(0x1234);
    cpu.setSP(0xF000);
    cpu.setA(0xAAAA);
    cpu.setX(0xBBBB);
    cpu.setY(0xCCCC);
    cpu.setPC(THUNK + 8 * which);
    const uint16_t ret = THUNK + 8 * which + 3;
    Out o{};
    for (int i = 0; i < 2000000; i++) {
        cpu.step();
        if (cpu.getPB() == 0x80 && cpu.getPC() == ret) { o.returned = true; break; }
    }
    o.value = (uint32_t)cpu.getA() | ((uint32_t)cpu.getX() << 16);
    o.preserved = cpu.getDB() == 0x5A && cpu.getD() == 0x1234 && cpu.getSP() == 0xF000 && cpu.getPB() == 0x80;
    return o;
}

void fail(const char* name, uint32_t l, uint32_t r, uint32_t got, uint32_t want) {
    if (failures++ < 12)
        std::printf("  FAIL %s(%08X, %08X): got %08X, want %08X\n", name, l, r, got, want);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <cpuasm> <zp_long.asm>\n", argv[0]);
        return 2;
    }
    const std::string cpuasm = std::filesystem::absolute(argv[1]).string();
    const std::string libAsm = std::filesystem::absolute(argv[2]).string();
    const std::string work = (std::filesystem::temp_directory_path() / "zp_long_test").string();
    std::filesystem::create_directories(work);
    {
        std::ofstream a(work + "/lib.asm");
        a << ".org $0200\n.bank $80\n";
        for (const char* h : kHelpers) a << "thunk_" << h << ": JSR " << h << "\n NOP\n NOP\n NOP\n NOP\n NOP\n";
        a << ".include \"" << libAsm << "\"\n";
    }
    if (std::system(("cd " + work + " && \"" + cpuasm + "\" lib.asm lib.bin > asm.log 2>&1").c_str()) != 0) {
        std::fprintf(stderr, "cpuasm failed, see %s/asm.log\n", work.c_str());
        return 1;
    }
    std::vector<uint8_t> bin;
    {
        std::ifstream f(work + "/lib.bin", std::ios::binary);
        bin.assign(std::istreambuf_iterator<char>(f), {});
    }
    std::printf("zp_long assembled: %zu bytes\n", bin.size());

    System sys;
    for (size_t i = 0; i < bin.size(); i++) sys.getCPU().writeByte(0x800200 + i, bin[i]);

    std::vector<uint32_t> edge = {0, 1, 2, 3, 7, 255, 256, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x10001, 100000, 65537,
                                  0x7FFFFFFF, 0x80000000u, 0x80000001u, 0xFFFFFFFFu, 0xFFFFFFFEu, 0xDEADBEEFu,
                                  0x12345678, 0xFFFF0000u, 0x0000FFFFu, 0x40000000, 0xC0000000u};
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    for (uint32_t a : edge)
        for (uint32_t b : edge) pairs.push_back({a, b});
    uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed; };
    for (int i = 0; i < 1500; i++) {
        uint32_t a = rnd(), b = rnd();
        if (i % 3 == 0) b >>= 16;          // small divisors / counts too
        if (i % 5 == 0) a >>= 8;
        pairs.push_back({a, b});
    }

    long pairsDone = 0;
    for (auto [l, r] : pairs) {
        pairsDone++;
        const int32_t sl = (int32_t)l, sr = (int32_t)r;
        auto chk = [&](int which, uint32_t want) {
            Out o = call(sys, which, l, r);
            checks++;
            if (!o.returned) { fail(kHelpers[which], l, r, 0xDEAD0000u, want); return; }
            if (!o.preserved) fail(kHelpers[which], l, r, 0xBAD0BAD0u, want);
            if (o.value != want) fail(kHelpers[which], l, r, o.value, want);
        };
        chk(ADD, l + r);
        chk(SUB, l - r);
        chk(MUL, l * r);
        chk(AND, l & r);
        chk(OR, l | r);
        chk(XOR, l ^ r);
        if (r != 0) {
            chk(UDIV, l / r);
            chk(UMOD, l % r);
            if (!(sl == INT32_MIN && sr == -1)) {
                chk(SDIV, (uint32_t)(sl / sr));
                chk(SMOD, (uint32_t)(sl % sr));
            }
        }
    }
    // compares return only A: check them separately (X is not part of the result)
    for (auto [l, r] : pairs) {
        const int32_t sl = (int32_t)l, sr = (int32_t)r;
        Out u = call(sys, CMPU, l, r), s = call(sys, CMPS, l, r);
        checks += 2;
        const uint16_t wu = l < r ? 0xFFFF : l > r ? 1 : 0, ws = sl < sr ? 0xFFFF : sl > sr ? 1 : 0;
        if ((uint16_t)u.value != wu) fail("__zp_lcmpu", l, r, u.value & 0xFFFF, wu);
        if ((uint16_t)s.value != ws) fail("__zp_lcmps", l, r, s.value & 0xFFFF, ws);
    }
    for (uint32_t v : edge)
        for (uint32_t n = 0; n <= 40; n++) {
            Out a = call(sys, SHL, v, n), b = call(sys, LSR, v, n), c = call(sys, ASR, v, n);
            checks += 3;
            const uint32_t wl = n >= 32 ? 0 : v << n, wr = n >= 32 ? 0 : v >> n;
            const uint32_t wa = n >= 32 ? ((int32_t)v < 0 ? 0xFFFFFFFFu : 0) : (uint32_t)((int32_t)v >> n);
            if (a.value != wl) fail("__zp_lshl", v, n, a.value, wl);
            if (b.value != wr) fail("__zp_llsr", v, n, b.value, wr);
            if (c.value != wa) fail("__zp_lasr", v, n, c.value, wa);
        }
    // division by zero: quotient $FFFFFFFF, remainder = dividend (documented), and it must not hang
    {
        Out q = call(sys, UDIV, 12345, 0), m = call(sys, UMOD, 12345, 0);
        checks += 2;
        if (!q.returned || q.value != 0xFFFFFFFFu) fail("__zp_ludiv", 12345, 0, q.value, 0xFFFFFFFFu);
        if (!m.returned || m.value != 12345u) fail("__zp_lumod", 12345, 0, m.value, 12345u);
    }

    std::printf("%ld checks over %ld operand pairs: %s\n", checks, pairsDone, failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
