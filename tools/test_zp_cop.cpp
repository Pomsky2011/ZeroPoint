// test_zp_cop: run libzp's zp_cop (ZPdevtools/lib/zp_cop.asm) against the real ZPbootROM and check it is a faithful
// way to reach every COP signature.
//
//   test_zp_cop <bootrom.bin> <trig_model.py> <cpuasm> <zp_cop.asm>
//
// The library is assembled with cpuasm, loaded into Work RAM and called the way compiled C calls it: A = sig,
// X = a, Y = x, 4th argument (y) pushed on the stack, JSR. Every vector from trig_model.py (the bit-exact
// reference) goes through it, and so do the calling-contract checks: DB/D/SP and the caller's M/X/flags survive,
// the reserved signatures hand registers back untouched, and a DB clobbered by a service is put back.
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ZeroPoint;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

struct Row {
    std::string name;
    uint16_t a, x, y, expA, expX;
};

const char* const kWrappers[] = {"zp_cop32", "zp_cop3", "zp_sin", "zp_cos", "zp_sincos", "zp_atan2", "zp_atan", "zp_sqrt", "zp_hypot",
                                 "zp_boot_ppu_load", "zp_verify_chunk"};
constexpr uint16_t THUNK = 0x0200;     // thunk i = JSR <name> at THUNK + 8*i (0 = zp_cop32); the return lands 3 bytes on
constexpr uint16_t PUSH_PC = 0x0100;   // PHA
constexpr uint16_t CALL_PC = THUNK;    // thunk 0: JSR zp_cop32
constexpr uint16_t RET_PC = THUNK + 3; // the NOP after the JSR

struct Result {
    uint16_t a, x;
    bool returned, preserved;
    uint8_t db;
};

// One C-style call: push y (4th argument), then JSR zp_cop with A = sig, X = a, Y = x. `dbBefore` is the caller's
// DB; the stub must hand it back.
Result call(System& sys, uint8_t sig, uint16_t a, uint16_t x, uint16_t y, bool eightBit, uint8_t dbBefore = 0x5A) {
    CPU& cpu = sys.getCPU();
    cpu.writeByte(0x800100, 0x2B);                         // PHA
    CPUFlags f;
    f.M = f.X = false;
    f.I = false;
    f.C = true;
    f.N = true;
    cpu.setFlags(f);
    cpu.setPB(0x80);
    cpu.setDB(dbBefore);
    cpu.setD(0x1234);
    cpu.setSP(0xF000);
    cpu.setA(y);
    cpu.setPC(PUSH_PC);
    cpu.step();                                            // push the 4th argument
    const uint16_t spAfterPush = cpu.getSP();

    CPUFlags g = f;
    g.M = g.X = eightBit;
    cpu.setFlags(g);
    cpu.setA(sig);
    cpu.setX(a);
    cpu.setY(x);
    cpu.setPC(CALL_PC);
    Result r{};
    for (int i = 0; i < 200000; i++) {
        cpu.step();
        if (cpu.getPB() == 0x80 && cpu.getPC() == RET_PC) { r.returned = true; break; }
    }
    r.a = cpu.getA();
    r.x = cpu.getX();
    r.db = cpu.getDB();
    r.preserved = cpu.getDB() == dbBefore && cpu.getD() == 0x1234 && cpu.getSP() == spAfterPush &&
                  cpu.getPB() == 0x80 && cpu.getFlags().toByte() == g.toByte();
    return r;
}

// Calls typed wrapper `w` with A, X, Y as the C ABI hands them over.
Result callWrapper(System& sys, int w, uint16_t a, uint16_t x, uint16_t y, bool eightBit = false, uint8_t dbBefore = 0x5A) {
    CPU& cpu = sys.getCPU();
    CPUFlags f;
    f.M = f.X = eightBit;
    f.I = false;
    f.C = true;
    f.N = true;
    cpu.setFlags(f);
    cpu.setPB(0x80);
    cpu.setDB(dbBefore);
    cpu.setD(0x1234);
    cpu.setSP(0xF000);
    cpu.setA(a);
    cpu.setX(x);
    cpu.setY(y);
    cpu.setPC(THUNK + 8 * w);
    const uint16_t retPc = THUNK + 8 * w + 3;
    Result r{};
    for (int i = 0; i < 200000; i++) {
        cpu.step();
        if (cpu.getPB() == 0x80 && cpu.getPC() == retPc) { r.returned = true; break; }
    }
    r.a = cpu.getA();
    r.x = cpu.getX();
    r.db = cpu.getDB();
    r.preserved = cpu.getDB() == dbBefore && cpu.getD() == 0x1234 && cpu.getSP() == 0xF000 && cpu.getPB() == 0x80 &&
                  cpu.getFlags().toByte() == f.toByte();
    return r;
}

uint8_t sigFor(const std::string& n) {
    static const char* names[] = {"sin", "cos", "sincos", "atan2", "atan", "sqrt", "hypot"};
    for (int i = 0; i < 7; i++)
        if (n == names[i]) return 0xF0 + i;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <bootrom.bin | --hle> <trig_model.py> <cpuasm> <zp_cop.asm>\n", argv[0]);
        return 2;
    }
    const std::string cpuasm = std::filesystem::absolute(argv[3]).string();   // we cd into the work dir below
    const std::string libAsm = std::filesystem::absolute(argv[4]).string();
    const std::string work = (std::filesystem::temp_directory_path() / "zp_cop_test").string();
    std::filesystem::create_directories(work);
    {
        std::ofstream asmf(work + "/lib.asm");
        // cpuasm writes one contiguous image, so the thunks come first and the library follows them.
        asmf << ".org $0200\n.bank $80\n";
        for (const char* w : kWrappers) asmf << "thunk_" << w << ": JSR " << w << "\n NOP\n NOP\n NOP\n NOP\n NOP\n";   // 8 bytes each
        asmf << ".include \"" << libAsm << "\"\n";
    }
    if (std::system(("cd " + work + " && \"" + cpuasm + "\" lib.asm lib.bin > asm.log 2>&1").c_str()) != 0) {
        std::fprintf(stderr, "cpuasm failed, see %s/asm.log\n", work.c_str());
        return 1;
    }
    std::vector<uint8_t> lib;
    {
        std::ifstream f(work + "/lib.bin", std::ios::binary);
        lib.assign(std::istreambuf_iterator<char>(f), {});
    }
    std::printf("zp_cop assembled: %zu bytes\n", lib.size());

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
    // "--hle" in place of the image runs the same wrappers against System's native Boot ROM services.
    if (std::string(argv[1]) != "--hle" && !sys.loadBootROM(argv[1])) { std::fprintf(stderr, "cannot load boot ROM\n"); return 1; }
    for (size_t i = 0; i < lib.size(); i++) sys.getCPU().writeByte(0x800200 + i, lib[i]);

    // Every reference vector through zp_cop, bit for bit. The model's a/x/y are the service's A/X/Y.
    for (const char* kind : {"sin", "cos", "sincos", "atan2", "atan", "sqrt", "hypot"}) {
        int n = 0, bad = 0, unret = 0, unpres = 0;
        for (const auto& r : rows) {
            if (r.name != kind) continue;
            Result res = call(sys, sigFor(kind), r.a, r.x, r.y, false);
            n++;
            if (!res.returned) { unret++; continue; }
            if (!res.preserved) unpres++;
            const bool wantX = std::string(kind) == "sincos";
            if (res.a != r.expA || (wantX && res.x != r.expX)) {
                if (++bad <= 3)
                    std::printf("    %s a=%04X x=%04X y=%04X: got A=%04X X=%04X, want A=%04X X=%04X\n", kind, r.a, r.x,
                                r.y, res.a, res.x, r.expA, r.expX);
            }
        }
        check(n > 0 && bad == 0 && unret == 0, std::string(kind) + ": " + std::to_string(n) + " vectors bit-exact through zp_cop");
        check(unpres == 0, std::string(kind) + ": DB/D/SP/PB and the caller's M/X/flags intact");
    }

    // The typed wrappers, with arguments in A/X/Y exactly as the C ABI passes them, against the same vectors.
    {
        auto wrapperIndex = [](const char* name) {
            for (int i = 0; i < (int)(sizeof kWrappers / sizeof *kWrappers); i++)
                if (std::string(name) == kWrappers[i]) return i;
            return -1;
        };
        for (const char* kind : {"sin", "cos", "sincos", "atan2", "atan", "sqrt", "hypot"}) {
            const int w = wrapperIndex((std::string("zp_") + kind).c_str());
            int n = 0, bad = 0, unpres = 0;
            for (const auto& r : rows) {
                if (r.name != kind) continue;
                // C(y, x): A = y, X = x.  C(x, y): A = x, X = y.  sqrt(lo, hi): A = lo, X = hi.  The rest take A.
                uint16_t a = r.a, x = 0;
                const std::string k = kind;
                if (k == "atan2") { a = r.y; x = r.x; }
                else if (k == "hypot") { a = r.x; x = r.y; }
                else if (k == "sqrt") { a = r.a; x = r.x; }
                Result res = callWrapper(sys, w, a, x, 0);
                n++;
                if (!res.preserved) unpres++;
                if (!res.returned || res.a != r.expA || (k == "sincos" && res.x != r.expX)) bad++;
            }
            check(n > 0 && bad == 0, std::string("zp_") + kind + "(): " + std::to_string(n) + " vectors bit-exact via its C-ABI wrapper");
            check(unpres == 0, std::string("zp_") + kind + "(): DB/D/SP/PB and the caller's M/X/flags intact");
        }
        // zp_cop3(sig, a, x): the three-register generic C uses; y is 0. Same vectors for the services that
        // don't take Y, plus the reserved signatures.
        {
            const int w3 = wrapperIndex("zp_cop3");
            int n = 0, bad = 0, unpres = 0;
            for (const auto& r : rows) {
                if (r.name == "atan2" || r.name == "hypot") continue;   // need Y
                uint16_t a = r.a, x = r.name == "sqrt" ? r.x : 0;
                Result res = callWrapper(sys, w3, sigFor(r.name), a, x);
                n++;
                if (!res.preserved) unpres++;
                if (!res.returned || res.a != r.expA || (r.name == "sincos" && res.x != r.expX)) bad++;
            }
            check(n > 0 && bad == 0, "zp_cop3(): " + std::to_string(n) + " sin/cos/sincos/atan/sqrt vectors bit-exact");
            check(unpres == 0, "zp_cop3(): DB/D/SP/PB and the caller's M/X/flags intact");
            bool ok = true;
            for (unsigned sig = 0xF8; sig <= 0xFE; sig++) {
                Result res = callWrapper(sys, w3, sig, 0x1111, 0x2222);
                ok = ok && res.returned && res.a == 0x1111 && res.x == 0x2222 && res.preserved;
            }
            check(ok, "zp_cop3(): reserved $F8-$FE return with A/X untouched");
        }
        // 8-bit callers and the PPU-load / verify wrappers' register plumbing.
        Result r8 = callWrapper(sys, wrapperIndex("zp_sin"), 0x4000, 0, 0, true);
        check(r8.returned && r8.a == 0x7FFF && r8.preserved, "zp_sin from an 8-bit (M=X=1) caller: same answer, P restored");
        Result rv = callWrapper(sys, wrapperIndex("zp_verify_chunk"), 0, 0, 0, false, 0x5A);
        check(rv.returned && rv.db == 0x5A, "zp_verify_chunk returns with DB restored");
    }

    // An 8-bit (M=X=1) caller gets the same full-width answer and its flags back.
    {
        int bad = 0, n = 0;
        for (const auto& r : rows) {
            if (r.name != "sin" && r.name != "atan2") continue;
            Result res = call(sys, sigFor(r.name), r.a, r.x, r.y, true);
            n++;
            if (!res.returned || res.a != r.expA || !res.preserved) bad++;
        }
        check(n > 0 && bad == 0, "8-bit callers (M=X=1) get the same result with P restored");
    }

    // $F8-$FE: registers come back untouched (A and X are what zp_cop returns).
    {
        bool ok = true;
        for (unsigned sig = 0xF8; sig <= 0xFE; sig++) {
            Result res = call(sys, (uint8_t)sig, 0x1111, 0x2222, 0x3333, false);
            ok = ok && res.returned && res.a == 0x1111 && res.x == 0x2222 && res.preserved;
        }
        check(ok, "reserved $F8-$FE return with A/X untouched");
    }

    // The caller's DB comes back whatever it was.
    {
        bool ok = true;
        for (unsigned db : {0x00u, 0x80u, 0xE0u, 0x5Au}) {
            Result res = call(sys, 0xF0, 0x4000, 0, 0, false, (uint8_t)db);
            ok = ok && res.returned && res.db == db;
        }
        check(ok, "DB $00/$80/$E0/$5A restored after the call");
    }

    // COP #$FF (verify_chunk) leaves DB at the Boot ROM's value: zp_cop must put the caller's back. With no
    // signed cartridge loaded there is no manifest, so the service has nothing to verify and just returns.
    {
        Result res = call(sys, 0xFF, 0, 0, 0, false, 0x5A);
        check(res.returned && res.db == 0x5A, "COP #$FF returns and DB is restored (verify_chunk clobbers it)");
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
