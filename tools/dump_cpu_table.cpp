// Measures what every DEF88186 opcode really does and prints it as TSV (one row per opcode).
//
//   bin/dump_cpu_table > ../ZPdevtools/spec/DEF88186-measured.tsv
//
// Each opcode runs from random registers, flags and memory on a CPU in the legacy flat-memory mode (a plain 16 MiB
// array: no I/O, no bank gating, no bus contention), with the instruction at $00:8000. Banks $00 and $80 (code,
// direct page, pointers; the stack) are filled with random bytes before every run. From many runs per (M,X) mode:
//   len     bytes the instruction occupies: the CPU logs every read, and the leading reads at the opcode's own address
//           are its fetches (an immediate's operand is read through the data path, so PC's advance counts too)
//   cycles  every distinct cost seen, per mode (page crossings and taken branches show up as extra values)
//   flags   P bits that ever changed        regs  A X Y S(P) D B(DB) that ever changed
//   dsp     distinct SP deltas (stack bytes pushed -, pulled +)      mem  r = read data, w = wrote memory
//   flow    PC didn't land on the next instruction (jump/branch/call/return/vector)
//   end     run state afterwards (R running, H halted, W waiting)
// ZPdevtools' scripts/gen_cpu_docs.py joins this with spec/DEF88186.csv. The emulator is frozen at CORE_REVISION 10,
// so the output only changes if the CPU does.

#include "cpu.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ZeroPoint;

struct Rng {
    uint64_t s;
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return static_cast<uint32_t>(s >> 16);
    }
};

static const int TRIALS = 128;   // per mode
static const uint32_t CODE = 0x8000;

struct Regs {
    uint16_t A, X, Y, SP, D, PC;
    uint8_t PB, DB, P;
};

struct Run {
    Regs pre, post;
    uint64_t cycles;
    CPUState state;
    int fetched;      // leading reads at CODE, CODE+1, ...
    bool read;        // read data (the instruction's own bytes don't count)
    bool wrote;
};

static Regs capture(const CPU& c) {
    return Regs{c.getA(), c.getX(), c.getY(), c.getSP(), c.getD(), c.getPC(), c.getPB(), c.getDB(), c.getFlags().toByte()};
}

// One run of `op` from the state `seed` describes. `nearOperand` points operands at the same page as the code so taken
// branches are seen without a page crossing.
static Run run(CPU& cpu, std::vector<uint8_t>& mem, AccessLog& log, int op, uint64_t seed, bool M, bool X,
               bool nearOperand) {
    Rng r{seed};
    for (uint32_t bank : {0x00u, 0x80u})
        for (uint32_t i = 0; i < 0x10000; i++) mem[(bank << 16) | i] = static_cast<uint8_t>(r.next());

    mem[CODE] = static_cast<uint8_t>(op);
    if (nearOperand) {
        mem[CODE + 1] = 0x10; mem[CODE + 2] = 0x80; mem[CODE + 3] = 0x00; mem[CODE + 4] = 0x00;
    } else {
        mem[CODE + 1] = static_cast<uint8_t>(r.next());
        mem[CODE + 2] = static_cast<uint8_t>(r.next() & 0x7F);   // operands stay below $8000, clear of the code
        mem[CODE + 3] = static_cast<uint8_t>((r.next() & 1) ? 0x80 : 0x00);
        mem[CODE + 4] = static_cast<uint8_t>(r.next() & 0x7F);
    }
    if (op == 0x67 || op == 0x68) mem[CODE + 1] = mem[CODE + 2] = 0;   // MVN/MVP bank bytes: bank $00 to bank $00

    cpu.reset();
    cpu.setA(r.next()); cpu.setX(r.next()); cpu.setY(r.next());
    cpu.setSP(0x0100 | (r.next() & 0xFF)); cpu.setD(r.next());
    cpu.setPC(CODE); cpu.setPB(0);
    cpu.setDB(static_cast<uint8_t>((r.next() & 1) ? 0x80 : 0x00));
    CPUFlags f;
    f.fromByte(static_cast<uint8_t>(r.next()));
    f.M = M; f.X = X;
    cpu.setFlags(f);

    Run out{};
    out.pre = capture(cpu);
    log.clear();
    cpu.step();
    out.post = capture(cpu);
    out.cycles = cpu.getCycleCount();
    out.state = cpu.getState();
    out.wrote = log.writes > 0;
    while (out.fetched < log.loads && log.addr[out.fetched] == CODE + out.fetched) out.fetched++;
    out.read = log.overflow;
    for (int i = 0; i < log.loads; i++)
        if (log.addr[i] < CODE || log.addr[i] >= CODE + 5) out.read = true;
    return out;
}

static int modeOf(const std::map<int, int>& hist) {
    int best = 0, bestCount = -1;
    for (auto [v, n] : hist)
        if (n > bestCount) { best = v; bestCount = n; }
    return best;
}

static std::string joinSet(const std::set<int>& s) {
    if (s.size() > 6) return "var";   // MVN/MVP, TCS: depends on a register value
    std::string out;
    for (int c : s) {
        if (!out.empty()) out += "/";
        out += std::to_string(c);
    }
    return out;
}

int main() {
    std::vector<uint8_t> mem(1u << 24);
    CPU cpu;
    cpu.setMemory(mem.data(), mem.size());
    AccessLog log;
    cpu.setAccessLog(&log);

    std::printf("op\tlen\tc_m1x1\tc_m1x0\tc_m0x1\tc_m0x0\tflags\tregs\tdsp\tmem\tflow\tend\n");

    for (int op = 0; op < 256; op++) {
        std::set<int> cycles[4];      // index: m*2 + x
        std::set<int> dsps;
        std::map<int, int> fetchedHist, deltaHist;
        uint8_t flagUnion = 0;
        bool regA = false, regX = false, regY = false, regSP = false, regD = false, regDB = false;
        bool anyRead = false, anyWrite = false, sawHalt = false, sawWait = false;
        std::vector<Regs> landed;

        for (int mode = 0; mode < 4; mode++) {
            const bool M = (mode >> 1) & 1, X = mode & 1;
            for (int t = 0; t < TRIALS; t++) {
                const uint64_t seed = 0x9E3779B97F4A7C15ull * (static_cast<uint64_t>(op) * 1000 + mode * 100 + t + 1);
                const Run s = run(cpu, mem, log, op, seed, M, X, (t & 1) != 0);

                cycles[mode].insert(static_cast<int>(s.cycles));
                anyRead |= s.read;
                anyWrite |= s.wrote;
                if (s.state == CPUState::Halted) sawHalt = true;
                if (s.state == CPUState::Waiting) sawWait = true;
                flagUnion |= static_cast<uint8_t>(s.post.P ^ s.pre.P);
                regA |= s.post.A != s.pre.A; regX |= s.post.X != s.pre.X; regY |= s.post.Y != s.pre.Y;
                regSP |= s.post.SP != s.pre.SP; regD |= s.post.D != s.pre.D; regDB |= s.post.DB != s.pre.DB;
                dsps.insert(static_cast<int16_t>(s.post.SP - s.pre.SP));
                landed.push_back(s.post);
                fetchedHist[s.fetched]++;
                const int delta = static_cast<int>(s.post.PC) - static_cast<int>(CODE);
                if (s.post.PB == 0 && delta >= 1 && delta <= 5) deltaHist[delta]++;
            }
        }
        // A data read that happens to land on the byte after the instruction would inflate one run's count; the
        // most common value is the real one. An immediate's operand isn't a fetch, so PC's advance can be longer.
        int len = modeOf(fetchedHist);
        if (!deltaHist.empty()) len = std::max(len, modeOf(deltaHist));
        bool flow = false;
        for (const Regs& p : landed)
            if (p.PB != 0 || p.PC != static_cast<uint16_t>(CODE + len)) flow = true;

        const char* flagNames = "NVMXDIZC";
        std::string flags;
        for (int b = 0; b < 8; b++) flags += (flagUnion & (0x80 >> b)) ? flagNames[b] : '.';
        std::string regs;
        if (regA) regs += 'A';
        if (regX) regs += 'X';
        if (regY) regs += 'Y';
        if (regSP) regs += 'S';
        if (regD) regs += 'D';
        if (regDB) regs += 'B';
        if (regs.empty()) regs = "-";
        std::string mm;
        if (anyRead) mm += 'r';
        if (anyWrite) mm += 'w';
        if (mm.empty()) mm = "-";

        std::printf("%02X\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%c\n", op, len, joinSet(cycles[3]).c_str(),
                    joinSet(cycles[2]).c_str(), joinSet(cycles[1]).c_str(), joinSet(cycles[0]).c_str(), flags.c_str(),
                    regs.c_str(), joinSet(dsps).c_str(), mm.c_str(), flow ? "Y" : "-",
                    sawHalt ? 'H' : (sawWait ? 'W' : 'R'));
    }
    return 0;
}
