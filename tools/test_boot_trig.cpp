// test_boot_trig: run every vector from ZPbootROM/def88186/trig_model.py through a real
// COP #$F0-#$F6 on the real ZPbootROM, and compare bit for bit.
//
//   test_boot_trig <bootrom.bin> <trig_model.py>
//
// Also checks the calling contract (DB/D/SP/P preserved, the caller's M/X width doesn't
// matter, the reserved slots just return) and that the 35-byte default stub, which has no
// slot table, leaves COP #$F0 to the cartridge's own $00:FFE4 vector.
#include "system.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

struct Row {
    std::string name;
    uint16_t a, x, y, expA, expX;
};

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) failures++;
}

uint8_t sigFor(const std::string& n) {
    static const char* names[] = {"sin", "cos", "sincos", "atan2", "atan", "sqrt", "hypot"};
    for (int i = 0; i < 7; i++)
        if (n == names[i]) return 0xF0 + i;
    return 0;
}

constexpr uint32_t STUB = 0x800100;   // COP #sig at $80:0100, the instruction after it at $80:0102

struct Result {
    uint16_t a, x;
    uint64_t cycles;
    bool returned, preserved;
};

// Runs one service call with the given registers; flags are 16-bit unless eightBit.
Result call(System& sys, uint8_t sig, uint16_t a, uint16_t x, uint16_t y, bool eightBit) {
    CPU& cpu = sys.getCPU();
    cpu.writeByte(STUB, 0xAF);
    cpu.writeByte(STUB + 1, sig);
    cpu.writeByte(STUB + 2, 0x00);   // NOP
    CPUFlags f;
    f.M = f.X = eightBit;
    f.I = false;   // so we can see RTI restore it after COP sets it
    f.C = true;
    f.N = true;
    cpu.setFlags(f);
    cpu.setPB(0x80);
    cpu.setPC(0x0100);
    cpu.setDB(0x5A);
    cpu.setD(0x1234);
    cpu.setSP(0xF000);
    cpu.setA(a);
    cpu.setX(x);
    cpu.setY(y);
    const uint64_t c0 = cpu.getCycleCount();
    Result r{};
    for (int i = 0; i < 100000; i++) {
        cpu.step();
        if (cpu.getPB() == 0x80 && cpu.getPC() == 0x0102) { r.returned = true; break; }
    }
    r.a = cpu.getA();
    r.x = cpu.getX();
    r.cycles = cpu.getCycleCount() - c0;
    r.preserved = cpu.getDB() == 0x5A && cpu.getD() == 0x1234 && cpu.getSP() == 0xF000 &&
                  cpu.getPB() == 0x80 && cpu.getFlags().toByte() == f.toByte();
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <bootrom.bin> <trig_model.py>\n", argv[0]);
        return 2;
    }
    std::vector<Row> rows;
    {
        std::string cmd = std::string("python3 -I \"") + argv[2] + "\" --vectors";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) { std::fprintf(stderr, "cannot run %s\n", cmd.c_str()); return 1; }
        char name[32];
        unsigned a, x, y, ea, ex;
        while (std::fscanf(p, "%31s %x %x %x %x %x", name, &a, &x, &y, &ea, &ex) == 6)
            rows.push_back({name, (uint16_t)a, (uint16_t)x, (uint16_t)y, (uint16_t)ea, (uint16_t)ex});
        if (pclose(p) != 0 || rows.empty()) { std::fprintf(stderr, "model produced no vectors\n"); return 1; }
    }

    System sys;
    if (!sys.loadBootROM(argv[1])) { std::fprintf(stderr, "cannot load boot ROM\n"); return 1; }

    std::printf("boot ROM trig services: %zu vectors\n", rows.size());
    std::vector<std::string> kinds = {"sin", "cos", "sincos", "atan2", "atan", "sqrt", "hypot"};
    for (const auto& kind : kinds) {
        int n = 0, bad = 0, unret = 0, unpres = 0;
        uint64_t maxCycles = 0;
        for (const auto& r : rows) {
            if (r.name != kind) continue;
            // sin/cos/sincos/atan take the input in A; atan2/hypot in X (x) and Y (y);
            // sqrt takes A = low word, X = high word.
            uint16_t a = r.a, x = r.x, y = r.y;
            if (kind == "sqrt") x = r.x;   // the model puts the high word in x
            Result res = call(sys, sigFor(kind), a, x, y, false);
            n++;
            maxCycles = std::max(maxCycles, res.cycles);
            if (!res.returned) { unret++; continue; }
            if (!res.preserved) unpres++;
            const bool wantX = kind == "sincos";
            if (res.a != r.expA || (wantX && res.x != r.expX)) {
                if (++bad <= 5)
                    std::printf("    %s a=%04X x=%04X y=%04X: got A=%04X X=%04X, want A=%04X X=%04X\n", kind.c_str(), a, x,
                                y, res.a, res.x, r.expA, r.expX);
            }
        }
        char what[96];
        std::snprintf(what, sizeof what, "%-6s %4d vectors bit-exact (worst %llu cycles)", kind.c_str(), n,
                      (unsigned long long)maxCycles);
        check(n > 0 && bad == 0 && unret == 0, what);
        std::snprintf(what, sizeof what, "%-6s returns with DB/D/SP/P intact", kind.c_str());
        check(unpres == 0, what);
    }

    // An 8-bit caller (M=X=1) gets the same full-width A.
    {
        int bad = 0, n = 0;
        for (const auto& r : rows) {
            if (r.name != "sin" && r.name != "atan2") continue;
            uint16_t a = r.a, x = r.x, y = r.y;
            Result res = call(sys, sigFor(r.name), a, x, y, true);
            n++;
            if (!res.returned || res.a != r.expA || !res.preserved) bad++;
        }
        check(n > 0 && bad == 0, "8-bit callers (M=X=1) get the same result with P restored");
    }

    // $F8-$FE are reserved and return with registers untouched ($F7 is handoff.def's PPU load, tested by
    // test_boot_handoff).
    {
        bool ok = true;
        for (uint8_t sig = 0xF8; sig <= 0xFE; sig++) {
            Result res = call(sys, sig, 0x1111, 0x2222, 0x3333, false);
            ok = ok && res.returned && res.a == 0x1111 && res.x == 0x2222 && sys.getCPU().getY() == 0x3333 && res.preserved;
        }
        check(ok, "reserved $F8-$FE return immediately, registers untouched");
    }

    // The default 35-byte stub has no slot table: COP #$F0 must take the cartridge's vector.
    {
        System plain;
        CPU& cpu = plain.getCPU();
        cpu.writeByte(STUB, 0xAF);
        cpu.writeByte(STUB + 1, 0xF0);
        cpu.setPB(0x80);
        cpu.setPC(0x0100);
        cpu.setSP(0xF000);
        cpu.step();
        check(cpu.getPB() == 0x00, "with the default stub, COP #$F0 goes to the cartridge's $00:FFE4 vector");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
