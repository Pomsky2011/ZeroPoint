// fuzz_idle_skip: the idle-poll skip (docs/idle-poll-skip.md) must be invisible. Every scenario is a guest made of
// polling loops (the shapes real firmware waits in) plus host events at exact master cycles that change what they
// poll, and it is run four ways that must be indistinguishable:
//
//   reference   skip off, runCycles() from stop to stop
//   per-cycle   skip off, System::step() one master cycle at a time (the definition of the machine)
//   skip        skip on, runCycles() cut into random slices
//   skip+vt     skip on, random slices, PPU/display catch-up on the worker thread
//
// The stops are every event, a cycle just before it, and random cycles in between (so a run is stopped in the
// middle of whatever the skip is doing), and System::stateHashParts() must match at every one of them.
//
//   fuzz_idle_skip <workdir> [first-seed] [count] [max-frames]
//   fuzz_idle_skip <workdir> --seed N        (one seed, verbose)
//
// Prints how much of the reference run the CPU actually spent in the loops, so a generator that stopped producing
// idle time can't pass quietly. Exits 1 at the first difference.
#include "system.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#ifdef _WIN32
#include <process.h>
#define ZP_GETPID _getpid
#else
#include <unistd.h>
#define ZP_GETPID getpid
#endif

using namespace ZeroPoint;

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x7654321ull) { next(); next(); }
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
    uint32_t below(uint32_t n) { return (uint32_t)(next() % n); }
    uint8_t byte() { return (uint8_t)(next() >> 24); }
};

// ---- the guest -------------------------------------------------------------------------------------------------
// A tiny assembler for the handful of DEF88186 encodings the loops need (checked against cpuasm/cpudisasm). The
// accumulator is 8 bits wide at reset, so nothing here uses an immediate: values come from memory.
struct Code {
    std::vector<uint8_t> b;
    uint32_t here() const { return (uint32_t)b.size(); }
    void a24(uint32_t a) { b.push_back(a & 0xFF); b.push_back((a >> 8) & 0xFF); b.push_back((a >> 16) & 0xFF); }
    void lda(uint32_t a) { b.push_back(0x3D); a24(a); }
    void sta(uint32_t a) { b.push_back(0x55); a24(a); }
    void cmp(uint32_t a) { b.push_back(0xAC); a24(a); }
    void inc() { b.push_back(0x69); }
    void nop() { b.push_back(0x00); }
    void cli() { b.push_back(0x7C); }
    void rti() { b.push_back(0x17); }
    void beq(uint32_t t) { b.push_back(0x0B); a24(t); }
    void bra(uint16_t t) { b.push_back(0x07); b.push_back(t & 0xFF); b.push_back(t >> 8); }
    // A forward branch/jump whose target is not known yet: returns the offset to patch.
    uint32_t beqFwd() { const uint32_t at = here(); beq(0); return at; }
    void patchBeq(uint32_t at, uint32_t t) { b[at + 1] = t & 0xFF; b[at + 2] = (t >> 8) & 0xFF; b[at + 3] = (t >> 16) & 0xFF; }
};

struct Range { uint32_t lo, hi; };

enum class Kind { Poke, Input, LoadPPU, PpuWriter, LoadAPU, Dma, Yank, Insert, SoftReset, HardReset };

struct Event {
    uint64_t cycle;
    Kind kind;
    uint32_t addr = 0;
    uint8_t value = 0;
    uint8_t in[3] = {0, 0, 0};
    std::vector<uint8_t> blob;
};

struct Scenario {
    std::vector<uint8_t> rom;
    std::vector<Range> loops;                  // where the polling loops are (for the coverage figure)
    std::vector<std::vector<uint8_t>> spare;
    std::vector<Event> events;
    uint64_t endCycle = 0;
    std::vector<uint32_t> polled;              // what the guest polls, so events can aim at it
    bool irq = false;
};

std::vector<uint8_t> makeImage(const std::vector<uint8_t>& payload, uint32_t entry) {
#pragma pack(push, 1)
    struct { char magic[4]; uint8_t version, reserved; uint16_t headerSize; uint32_t romSize, entry; char title[32]; char dev[16]; } h;
#pragma pack(pop)
    std::memset(&h, 0, sizeof h);
    std::memcpy(h.magic, "ZPB", 4);
    h.version = 1;
    h.headerSize = 64;
    h.romSize = (uint32_t)payload.size();
    h.entry = entry;
    std::strcpy(h.title, "idleskip");
    std::vector<uint8_t> out(sizeof h + payload.size());
    std::memcpy(out.data(), &h, sizeof h);
    std::memcpy(out.data() + sizeof h, payload.data(), payload.size());
    return out;
}

uint32_t pollTarget(Rng& r) {
    if (r.below(3) == 0) return 0xB00000 + 0x0400 + r.below(0x1C00);   // a third of the polls are PPU memory a program writes
    switch (r.below(8)) {
        case 0: case 1: return 0xB00000 + 0x0400 + r.below(0x1C00);                       // PPU memory a program or the host writes
        case 2: { static const uint16_t sp[] = {0x00E0, 0x00E1, 0x034A, 0x0360, 0x0364, 0x00F0};   // derived PPU state
                  return 0xB00000 + sp[r.below(6)]; }
        case 3: return 0xB00000 + 0xE000 + r.below(0x2000);                                // the rolling framebuffer
        case 4: return 0xD80051;                                                           // timer status
        case 5: return 0x800100 + r.below(0x40);                                           // work RAM: host, DMA, the guest itself
        case 6: return 0xD80030 + r.below(3);                                              // controller port
        default: return r.below(2) ? 0xA00000 + r.below(0x400) : 0xD80058;                 // APU window / IRQ status
    }
}

Scenario makeScenario(uint64_t seed, int maxFrames) {
    Rng r(seed);
    Scenario sc;
    Code c;
    sc.irq = r.below(3) == 0;
    // ISR: acknowledge whatever the timers/interrupt controller latched, count it, return. Placed at $0F00.
    const uint32_t isr = 0x0F00;
    if (sc.irq) c.cli();
    const int phases = 1 + (int)r.below(4);
    std::vector<uint32_t> phaseStart;
    std::vector<uint32_t> jumpFix;
    for (int p = 0; p < phases; p++) {
        phaseStart.push_back(c.here());
        const uint32_t a = pollTarget(r), a2 = pollTarget(r);
        const uint32_t var = 0x800200 + p;                      // CMP's other operand
        sc.polled.push_back(a);
        const uint32_t lo = c.here();
        switch (r.below(5)) {
            case 0:                                              // spin while zero
                c.lda(a); c.beq(lo); break;
            case 1: {                                            // spin while nonzero
                c.lda(a); const uint32_t fwd = c.beqFwd(); c.bra((uint16_t)lo); c.patchBeq(fwd, c.here()); break; }
            case 2:                                              // spin while equal to a variable
                c.lda(a); c.cmp(var); c.beq(lo); break;
            case 3:                                              // padded: a longer period
                c.nop(); c.lda(a); c.nop(); c.nop(); c.beq(lo + 1); break;
            default: {                                           // two polls: spin while the first is zero or the second is nonzero
                c.lda(a); const uint32_t fwd = c.beqFwd(); c.lda(a2); const uint32_t fwd2 = c.beqFwd();
                c.bra((uint16_t)lo); c.patchBeq(fwd, lo); c.patchBeq(fwd2, c.here()); sc.polled.push_back(a2); break; }
        }
        sc.loops.push_back({lo, c.here()});
        // The action after a loop: count the exit, and sometimes touch something another loop (or the machine) sees.
        c.sta(0x800060 + p);                                     // the last value polled: a late exit shows as a different one
        c.lda(0x800030 + p); c.inc(); c.sta(0x800030 + p);
        if (r.below(2)) {
            static const uint32_t out[] = {0xD80051, 0x800100, 0x800101, 0xB00500, 0xA00010, 0xD80058};
            c.sta(out[r.below(6)]);
        }
        jumpFix.push_back(c.here());
        c.bra(0);                                                // patched to the next phase below
    }
    for (int p = 0; p < phases; p++) {
        const uint32_t next = phaseStart[(p + 1) % phases];
        c.b[jumpFix[p] + 1] = next & 0xFF; c.b[jumpFix[p] + 2] = next >> 8;
    }
    sc.rom.assign(65536, 0x00);
    std::memcpy(sc.rom.data(), c.b.data(), c.b.size());
    Code h;
    h.lda(0xD80051); h.sta(0xD80051); h.lda(0xD80058); h.sta(0xD80058);
    h.lda(0x800040); h.inc(); h.sta(0x800040); h.rti();
    std::memcpy(sc.rom.data() + isr, h.b.data(), h.b.size());
    sc.rom[0xFFFE] = isr & 0xFF; sc.rom[0xFFFF] = isr >> 8;
    for (int i = 0; i < 2; i++) {
        std::vector<uint8_t> payload(4096 + r.below(8192));
        for (auto& b : payload) b = r.byte() & 0xFE;
        sc.spare.push_back(makeImage(payload, 0));
    }
    sc.spare.push_back(makeImage(sc.rom, 0));   // [2]: the guest itself, so a yank or swap can put the loops back

    const uint64_t frames = 2 + r.below((uint32_t)maxFrames);
    sc.endCycle = frames * System::CYCLES_PER_FRAME;
    const int n = 8 + (int)r.below(60);
    for (int i = 0; i < n; i++) {
        Event e;
        e.cycle = r.next() % sc.endCycle;
        switch (r.below(26)) {
            case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: {   // a PPU program that keeps rewriting a byte a loop polls
                e.kind = Kind::PpuWriter;
                uint32_t target = 0xB00400 + r.below(0x1C00);
                for (uint32_t a : sc.polled) if ((a >> 16) == 0xB0 && (a & 0xFFFF) < 0xC000 && r.below(2)) target = a;
                e.addr = target & 0xFFFF;
                e.blob = {(uint8_t)0, (uint8_t)0, (uint8_t)r.below(60)};   // [0..1] delay before the first store, [2] padding between stores
                { const uint32_t d = 1 + r.below(r.below(4) == 0 ? 600 : 6000); e.blob[0] = (uint8_t)d; e.blob[1] = (uint8_t)(d >> 8); }
                break; }
            case 0: case 1: case 2: case 3: case 4: {            // change something a loop polls
                e.kind = Kind::Poke;
                e.addr = sc.polled[r.below((uint32_t)sc.polled.size())];
                e.value = r.below(3) == 0 ? 0 : r.byte();
                break; }
            case 5: e.kind = Kind::Poke; e.addr = 0x800200 + r.below(4); e.value = r.byte(); break;   // CMP's variable
            case 6: case 7:                                      // timers / interrupt controller
                e.kind = Kind::Poke; e.addr = 0xD80050 + (r.below(4) == 0 ? 9 : r.below(3)); e.value = r.byte(); break;
            case 8: case 9: e.kind = Kind::Input; e.in[0] = r.byte(); e.in[1] = r.byte(); e.in[2] = r.byte(); break;
            case 10: e.kind = Kind::Poke; e.addr = 0xB00000 + r.below(0x10000); e.value = r.byte(); break;
            case 11:
                e.kind = Kind::Poke;
                if (r.below(2)) { e.addr = 0xD80000; e.value = 1 + r.below(2); } else { e.addr = 0xD80010; e.value = (uint8_t)(1u << r.below(3)); }
                break;
            case 12: {                                            // a DMA aimed at what the loops watch
                e.kind = Kind::Dma;
                e.blob.resize(9);
                for (auto& b : e.blob) b = r.byte();
                static const uint8_t banks[] = {0x00, 0x80, 0xA0, 0xB0};
                e.blob[3] = banks[r.below(4)];
                e.blob[6] = banks[r.below(4)];
                e.blob[7] = (uint8_t)(1 + r.below(4));
                break; }
            case 13: e.kind = Kind::LoadPPU; e.blob.resize(16 + r.below(512)); for (auto& b : e.blob) b = r.byte(); break;
            case 14: e.kind = Kind::LoadAPU; e.blob.resize(16 + r.below(512)); for (auto& b : e.blob) b = r.byte(); break;
            default:                                              // the rare disruptive ones
                switch (r.below(4)) {
                    case 0: {                                     // the slot is empty for a while, then the guest is back
                        e.kind = Kind::Yank;
                        Event back;
                        back.kind = Kind::Insert; back.addr = 2;
                        back.cycle = e.cycle + 1 + r.below(30000);
                        if (back.cycle < sc.endCycle) sc.events.push_back(std::move(back));
                        break; }
                    case 1: e.kind = Kind::Insert; e.addr = r.below(4) == 0 ? r.below(2) : 2; break;   // usually the same code again
                    case 2: e.kind = Kind::SoftReset; break;
                    default: e.kind = Kind::HardReset; break;
                }
                break;
        }
        sc.events.push_back(std::move(e));
    }
    for (size_t i = 1; i < sc.events.size(); i++)
        for (size_t j = i; j > 0 && sc.events[j].cycle < sc.events[j - 1].cycle; j--) std::swap(sc.events[j], sc.events[j - 1]);
    return sc;
}

// A PPU program: R63 (DP) = addr, R2 = delay; count R2 down (so the byte stays still while a CPU loop settles and starts being
// skipped), then forever: R1++, store R1 (a word) at (DP), `pad` INCs, jump. R62 (PC) is the jump target both times.
// Words are big-endian: opcode in the top nibble (see PPU::decodeOp).
std::vector<uint8_t> ppuWriterProgram(uint16_t addr, uint16_t delay, unsigned pad) {
    std::vector<uint16_t> w;
    w.push_back(0xE03F); w.push_back(0xE400 | (addr & 0xFF));            // 0: tarreg t0 low -> R63; setbyte t0 lo
    w.push_back(0xE0BF); w.push_back(0xE400 | (addr >> 8));              // 2: tarreg t0 high -> R63; setbyte t0 hi
    w.push_back(0xE102); w.push_back(0xE500 | (delay & 0xFF));           // 4: tarreg t1 low -> R2; setbyte t1 lo
    w.push_back(0xE182); w.push_back(0xE500 | (delay >> 8));             // 6: tarreg t1 high -> R2; setbyte t1 hi
    w.push_back(0xE23E); w.push_back(0xE600 | 20);                       // 8: tarreg t2 low -> R62; setbyte t2 = the delay loop (byte 20)
    w.push_back(0x9002);                                                 // 10: delay: dec R2
    w.push_back(0x4083);                                                 // 11: cmp R2, R3 (R3 stays 0)
    w.push_back(0x7000);                                                 // 12: jnz -> R62
    w.push_back(0xE600 | 28);                                            // 13: setbyte t2 = the store loop (byte 28)
    w.push_back(0x8001);                                                 // 14: loop: inc R1
    w.push_back(0xF304);                                                 // 15: movdp R1
    for (unsigned i = 0; i < pad; i++) w.push_back(0x8002);              // inc R2
    w.push_back(0xF700);                                                 // jmr
    std::vector<uint8_t> out;
    for (uint16_t x : w) { out.push_back((uint8_t)(x >> 8)); out.push_back((uint8_t)x); }
    return out;
}

void apply(System& sys, const Event& e, const std::vector<std::vector<uint8_t>>& spare) {
    switch (e.kind) {
        case Kind::Poke: sys.getCPU().writeByte(e.addr, e.value); break;
        case Kind::Input: sys.getCPU().setPlayerInput(e.in[0], e.in[1], e.in[2]); break;
        case Kind::Yank: sys.ejectCartridge(); break;
        case Kind::Insert: sys.insertCartridge(spare[e.addr]); break;
        case Kind::SoftReset: sys.softReset(); break;
        case Kind::HardReset: sys.hardReset(); break;
        case Kind::LoadPPU: sys.loadPPUProgram(e.blob.data(), e.blob.size(), 0, 0); break;
        case Kind::PpuWriter: { const std::vector<uint8_t> p = ppuWriterProgram((uint16_t)e.addr, (uint16_t)(e.blob[0] | (e.blob[1] << 8)), e.blob[2]); sys.loadPPUProgram(p.data(), p.size(), 0, 0); break; }
        case Kind::LoadAPU: sys.loadAPUProgram(e.blob.data(), e.blob.size(), 0); break;
        case Kind::Dma:
            sys.getCPU().writeByte(0xD80020, 0);
            for (uint8_t b : e.blob) sys.getCPU().writeByte(0xD80021, b);
            break;
    }
}

// ---- the runs ---------------------------------------------------------------------------------------------------
enum class Mode { Reference, PerCycle, Skip, SkipVThread };
const char* modeName(Mode m) {
    switch (m) { case Mode::Reference: return "reference"; case Mode::PerCycle: return "per-cycle";
                 case Mode::Skip: return "skip"; default: return "skip+vthread"; }
}

struct Stop { uint64_t cycle; int event; };   // event < 0: just a place to look

std::vector<Stop> makeStops(const Scenario& sc, Rng& r) {
    std::vector<Stop> stops;
    for (size_t i = 0; i < sc.events.size(); i++) {
        const uint64_t c = sc.events[i].cycle;
        if (c > 0 && r.below(2)) stops.push_back({c - 1 - r.below(3), -1});     // a cycle or two before: the skip's edge
        stops.push_back({c, (int)i});
    }
    const int cuts = 20 + (int)r.below(60);
    for (int i = 0; i < cuts; i++) stops.push_back({r.next() % sc.endCycle, -1});
    stops.push_back({sc.endCycle, -1});
    // Stable by cycle, a plain look before the event that shares its cycle.
    for (size_t i = 1; i < stops.size(); i++)
        for (size_t j = i; j > 0 && (stops[j].cycle < stops[j - 1].cycle ||
                                      (stops[j].cycle == stops[j - 1].cycle && stops[j].event < 0 && stops[j - 1].event >= 0)); j--)
            std::swap(stops[j], stops[j - 1]);
    return stops;
}

void advanceTo(System& sys, uint64_t target, Mode mode, Rng& cut) {
    while (sys.getMasterCycleCount() < target && !sys.getCPU().isHalted()) {
        const uint64_t left = target - sys.getMasterCycleCount();
        if (mode == Mode::PerCycle) sys.step();
        else if (mode == Mode::Reference) sys.runCycles(left);
        else sys.runCycles(std::min<uint64_t>(left, 1 + cut.below(cut.below(4) == 0 ? 400000 : 3000)));
    }
}

using Parts = std::vector<std::pair<const char*, uint64_t>>;

struct Coverage { uint64_t cpuInstr = 0, looks = 0, inLoop = 0, exits = 0, isr = 0; bool halted = false; };
Coverage g_cov;
IdleStats g_idle;   // the phase 1 detector's counters, summed over the skip-mode runs
void addIdle(const IdleStats& s) {
    g_idle.loopsConfirmed += s.loopsConfirmed; g_idle.iterations += s.iterations; g_idle.cpuCycles += s.cpuCycles;
    g_idle.cpuCyclesUnwatchable += s.cpuCyclesUnwatchable; g_idle.woke += s.woke; g_idle.interrupted += s.interrupted;
    g_idle.unexplained += s.unexplained; g_idle.ppuRuns += s.ppuRuns; g_idle.ppuHits += s.ppuHits; g_idle.ppuAlignedHits += s.ppuAlignedHits; g_idle.skips += s.skips; g_idle.skippedIterations += s.skippedIterations; g_idle.cycleDrift += s.cycleDrift;
    for (int i = 0; i < static_cast<int>(WatchClass::COUNT); i++) g_idle.cpuCyclesByClass[i] += s.cpuCyclesByClass[i];
}

std::vector<Parts> run(const Scenario& sc, const std::vector<Stop>& stops, const std::string& rom, Mode mode, uint64_t seed) {
    void* storage = ::operator new(sizeof(System), std::align_val_t(alignof(System)));
    System* sys = new (storage) System();
    sys->setVerifySignatures(false);
    if (!sys->loadROM(rom)) { std::fprintf(stderr, "cannot load %s\n", rom.c_str()); std::exit(2); }
    sys->powerOn();
    sys->setIdlePollSkip(mode == Mode::Skip || mode == Mode::SkipVThread);
    if (mode == Mode::SkipVThread) sys->setVideoThread(true);
    Rng cut(seed ^ 0xBADC0DE);
    std::vector<Parts> out;
    Coverage cov;
    for (const Stop& s : stops) {
        advanceTo(*sys, s.cycle, mode, cut);
        if (s.event >= 0) apply(*sys, sc.events[s.event], sc.spare);
        out.push_back(sys->stateHashParts());
        if (mode == Mode::Reference) {
            cov.looks++;
            const uint16_t pc = sys->getCPU().getPC();
            if (sys->getCPU().getPB() == 0)
                for (const Range& l : sc.loops) if (pc >= l.lo && pc < l.hi) { cov.inLoop++; break; }
        }
    }
    if (mode == Mode::Reference) {
        cov.cpuInstr = sys->getCPU().getInstructionCount();
        cov.halted = sys->getCPU().isHalted();
        for (int p = 0; p < 4; p++) cov.exits += sys->getCPU().readByte(0x800030 + p);
        cov.isr = sys->getCPU().readByte(0x800040);
        g_cov = cov;
    }
    if (mode == Mode::Skip) addIdle(sys->idleStats());
    sys->~System();
    ::operator delete(storage, std::align_val_t(alignof(System)));
    return out;
}

std::string g_romFile;
void removeRomFile() { if (!g_romFile.empty()) std::remove(g_romFile.c_str()); }

// Run one scenario four ways and compare the machine at every stop.
bool checkScenario(const Scenario& sc, const std::vector<Stop>& stops, uint64_t seed, const std::string& work, bool verbose, Coverage& total) {
    if (g_romFile.empty()) { g_romFile = work + "/idleskip_" + std::to_string((long)ZP_GETPID()) + ".zpb"; std::atexit(removeRomFile); }
    const std::vector<uint8_t> image = makeImage(sc.rom, 0);
    FILE* f = std::fopen(g_romFile.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", g_romFile.c_str()); std::exit(2); }
    std::fwrite(image.data(), 1, image.size(), f);
    std::fclose(f);

    const std::vector<Parts> ref = run(sc, stops, g_romFile, Mode::Reference, seed);
    total.cpuInstr += g_cov.cpuInstr; total.looks += g_cov.looks; total.inLoop += g_cov.inLoop;
    total.exits += g_cov.exits; total.isr += g_cov.isr; total.halted |= g_cov.halted;
    if (verbose)
        std::printf("seed %llu: %zu loops, %zu events, %zu stops, %llu cycles, irq=%d | %llu CPU instructions, in a loop at %llu of %llu stops, %llu loop exits, %llu ISR runs%s\n",
                    (unsigned long long)seed, sc.loops.size(), sc.events.size(), stops.size(), (unsigned long long)sc.endCycle, sc.irq,
                    (unsigned long long)g_cov.cpuInstr, (unsigned long long)g_cov.inLoop, (unsigned long long)g_cov.looks,
                    (unsigned long long)g_cov.exits, (unsigned long long)g_cov.isr, g_cov.halted ? " (HALTED)" : "");
    bool ok = true;
    for (Mode m : {Mode::PerCycle, Mode::Skip, Mode::SkipVThread}) {
        const std::vector<Parts> got = run(sc, stops, g_romFile, m, seed);
        if (got.size() != ref.size()) { std::printf("  FAIL seed %llu %s: %zu checkpoints vs %zu\n", (unsigned long long)seed, modeName(m), got.size(), ref.size()); ok = false; continue; }
        for (size_t i = 0; i < ref.size(); i++) {
            std::string bad;
            for (size_t k = 0; k < ref[i].size() && k < got[i].size(); k++)
                if (ref[i][k].second != got[i][k].second) bad += std::string(bad.empty() ? "" : ", ") + ref[i][k].first;
            if (!bad.empty()) {
                std::printf("  FAIL seed %llu %s: stop %zu (cycle %llu%s) differs in: %s\n", (unsigned long long)seed, modeName(m), i,
                            (unsigned long long)stops[i].cycle, stops[i].event >= 0 ? ", just after an event" : "", bad.c_str());
                ok = false;
                break;
            }
        }
    }
    return ok;
}

bool fuzzOne(uint64_t seed, int maxFrames, const std::string& work, bool verbose, Coverage& total) {
    const Scenario sc = makeScenario(seed, maxFrames);
    Rng sr(seed ^ 0x5709);
    const std::vector<Stop> stops = makeStops(sc, sr);
    return checkScenario(sc, stops, seed, work, verbose, total);
}

// The boundary case, on purpose: one loop polling PPU memory, one PPU program that changes that byte after a delay, and the
// program loaded at 96 consecutive cycles so the change lands on every alignment against the loop's reads (period 48 master
// cycles), including exactly on one. Two stops per run: the load, and well after the change, so the whole stretch between
// is one slice in which the loop is learned, skipped under the PPU watch and left.
bool sweepOne(unsigned i, unsigned delay, unsigned pad, const std::string& work, Coverage& total) {
    Scenario sc;
    Code c;
    const uint32_t a = 0xB00800;
    const uint32_t lo = c.here();
    c.lda(a); c.beq(lo);
    sc.loops.push_back({lo, c.here()});
    c.sta(0x800060);
    c.lda(0x800030); c.inc(); c.sta(0x800030);
    const uint32_t spin = c.here();
    c.bra((uint16_t)spin);
    sc.rom.assign(65536, 0x00);
    std::memcpy(sc.rom.data(), c.b.data(), c.b.size());
    sc.spare.push_back(makeImage(sc.rom, 0));
    sc.spare.push_back(makeImage(sc.rom, 0));
    sc.spare.push_back(makeImage(sc.rom, 0));
    sc.polled.push_back(a);
    const uint64_t load = 20000 + i;
    Event e;
    e.cycle = load; e.kind = Kind::PpuWriter; e.addr = a & 0xFFFF;
    e.blob = {(uint8_t)delay, (uint8_t)(delay >> 8), (uint8_t)pad};
    sc.events.push_back(e);
    sc.endCycle = load + 12000 + 4 * delay;
    const std::vector<Stop> stops = {{load, 0}, {sc.endCycle - 2, -1}};
    return checkScenario(sc, stops, 1000 + i, work, false, total);
}

void printSummary() {
    static const char* const classNames[] = {"rom", "workram", "apuram", "ppumem", "timer", "input", "ppuport", "ppufb", "other"};
    std::printf("detector (skip runs): %llu loops confirmed, %llu identical iterations after that = %llu CPU cycles (%llu in loops reading an unwatchable address); ended by: %llu changed byte, %llu interrupt, %llu cycle drift, %llu unexplained\n",
                (unsigned long long)g_idle.loopsConfirmed, (unsigned long long)g_idle.iterations, (unsigned long long)g_idle.cpuCycles,
                (unsigned long long)g_idle.cpuCyclesUnwatchable, (unsigned long long)g_idle.woke, (unsigned long long)g_idle.interrupted,
                (unsigned long long)g_idle.cycleDrift, (unsigned long long)g_idle.unexplained);
    std::printf("  skips: %llu (%llu iterations); PPU watch runs: %llu, watch hits: %llu (%llu exactly on a poll read)\n", (unsigned long long)g_idle.skips,
                (unsigned long long)g_idle.skippedIterations, (unsigned long long)g_idle.ppuRuns, (unsigned long long)g_idle.ppuHits, (unsigned long long)g_idle.ppuAlignedHits);
    std::printf("  cycles by worst class read:");
    for (int i = 0; i < static_cast<int>(WatchClass::COUNT); i++) std::printf(" %s=%llu", classNames[i], (unsigned long long)g_idle.cpuCyclesByClass[i]);
    std::printf("\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <workdir> [first-seed] [count] [max-frames] | --seed N\n", argv[0]); return 2; }
    const std::string work = argv[1];
    uint64_t first = 1, count = 20;
    int maxFrames = 4;
    bool verbose = false;
    if (argc >= 3 && !std::strcmp(argv[2], "--sweep")) {
        Coverage tot;
        uint64_t bad = 0;
        for (unsigned delay : {1500u, 2203u}) for (unsigned pad : {0u, 7u})
            for (unsigned i = 0; i < 96; i++) if (!sweepOne(i, delay, pad, work, tot)) { bad++; break; }
        std::printf("boundary sweep (96 load cycles x 2 delays x 2 paddings): %s\n", bad ? "FAILED" : "all identical");
        printSummary();
        return bad ? 1 : 0;
    }
    if (argc >= 4 && !std::strcmp(argv[2], "--seed")) { first = std::strtoull(argv[3], nullptr, 0); count = 1; verbose = true; }
    else {
        if (argc > 2) first = std::strtoull(argv[2], nullptr, 0);
        if (argc > 3) count = std::strtoull(argv[3], nullptr, 0);
        if (argc > 4) maxFrames = std::atoi(argv[4]);
    }
    Coverage total;
    uint64_t failed = 0;
    for (uint64_t s = first; s < first + count; s++) {
        if (!fuzzOne(s, maxFrames, work, verbose, total)) { failed++; break; }
    }
    std::printf("idle-skip fuzz: %llu seeds, %llu CPU instructions, in a polling loop at %llu of %llu stops (%.0f%%), %llu loop exits, %llu ISR runs%s: %s\n",
                (unsigned long long)count, (unsigned long long)total.cpuInstr, (unsigned long long)total.inLoop, (unsigned long long)total.looks,
                total.looks ? 100.0 * total.inLoop / total.looks : 0.0, (unsigned long long)total.exits, (unsigned long long)total.isr,
                total.halted ? " (some guests HALTED)" : "", failed ? "FAILED" : "all identical");
    printSummary();
    if (g_idle.unexplained) { std::printf("detector self-check FAILED: loops ended with every loaded byte unchanged\n"); failed++; }
    return failed ? 1 : 0;
}
