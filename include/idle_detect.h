#ifndef ZEROPOINT_IDLE_DETECT_H
#define ZEROPOINT_IDLE_DETECT_H

// Idle-poll detector (phase 1 of docs/idle-poll-skip.md). Watches the CPU's instruction stream for a loop that is
// closed (one iteration leaves every bit of CPU state where it started and costs the same cycles each time),
// read-only (no store, push, or interrupt entry) and stable (the same bytes read each time), and counts what a skip
// would have elided. It changes nothing in the machine: it is host-side bookkeeping, never hashed.

#include <cstdint>

namespace ZeroPoint {

// What the CPU did to memory since clear(). The CPU fills it while one is attached (CPU::setAccessLog); a store, push
// or interrupt entry all show up as writes, a load is kept (up to MAX of them) with the byte it returned.
struct AccessLog {
    static constexpr int MAX = 8;
    uint32_t addr[MAX];
    uint8_t value[MAX];
    int loads = 0;
    bool overflow = false;      // more loads than fit
    uint32_t writes = 0;

    void clear() { loads = 0; overflow = false; writes = 0; }
    void addLoad(uint32_t a, uint8_t v) {
        if (loads < MAX) { addr[loads] = a; value[loads] = v; loads++; }
        else overflow = true;
    }
};

// Every piece of CPU state that decides what the next instruction does (registers, flags, hardware loop counters,
// latched interrupts, run state). Equal before and after an iteration is the "closed" half of the contract.
struct CpuSignature {
    uint16_t A, X, Y, SP, D, PC, loopCounter, loopStart;
    uint8_t PB, DB, P, state, flags;   // flags: irq/nmi/abort pending, boot-service latch
    bool operator==(const CpuSignature& o) const {
        return A == o.A && X == o.X && Y == o.Y && SP == o.SP && D == o.D && PC == o.PC &&
               loopCounter == o.loopCounter && loopStart == o.loopStart && PB == o.PB && DB == o.DB &&
               P == o.P && state == o.state && flags == o.flags;
    }
    bool operator!=(const CpuSignature& o) const { return !(*this == o); }
};

// Whether the value at an address can be given a timestamp for when it changes (the "Watchable addresses" table).
enum class WatchClass : uint8_t {
    Rom,         // cartridge bytes in the ungated code region: never change while it stays inserted
    WorkRam,     // changes through writeByte/DMA/host only
    ApuRam,
    PpuMem,      // PPU memory that is not a port or the framebuffer
    Timer,       // $D80051 timer status, $D80058 IRQ status
    Input,       // controller ports
    PpuPort,     // derived from live PPU/display state: not watchable yet
    PpuFb,       // framebuffer / open bus: not watchable yet
    Other,       // anything else (unknown I/O, gated cartridge data, unmapped)
    COUNT
};

struct IdleStats {
    uint64_t loopsConfirmed = 0;        // times a loop reached "two identical iterations"
    uint64_t iterations = 0;            // identical iterations seen after confirmation (what a skip would elide)
    uint64_t cpuCycles = 0;             // ... their CPU cycles (x4 for master cycles)
    uint64_t cpuCyclesByClass[static_cast<int>(WatchClass::COUNT)] = {};   // by the worst class the loop reads
    uint64_t cpuCyclesUnwatchable = 0;  // cycles in loops that read PpuPort/PpuFb/Other (not skippable yet)
    uint64_t skips = 0;                 // times a skip was applied (phase 2)
    uint64_t skippedIterations = 0;
    uint64_t skippedCpuCycles = 0;      // CPU cycles those iterations stood for (x4 for master cycles)
    uint64_t woke = 0;                  // a confirmed loop ended because a loaded byte changed (the expected exit)
    uint64_t interrupted = 0;           // ... or because an interrupt was taken (also expected)
    uint64_t unexplained = 0;           // ended with every loaded byte equal: the loop was not a pure function (a bug)
    uint64_t cycleDrift = 0;            // same loads, same state, different cycle cost
};

class CPU;

class IdleDetector {
public:
    IdleDetector();

    // Call before attaching: the log the CPU should fill. Valid for the detector's lifetime.
    AccessLog* log() { return &accessLog; }
    bool tracking() const { return phase != Phase::Idle; }

    // After one CPU instruction. `pcBefore` is PB:PC before it; `cycles` its cost in CPU cycles. `cpu` supplies the
    // signature and the address classification, and gets the access log attached while a loop is tracked.
    // `now` is the master cycle the instruction ran at.
    void observe(CPU& cpu, uint32_t pcBefore, uint64_t cycles, uint64_t now);

    // Phase 2. True when the CPU is exactly at the start of a fresh iteration of a confirmed loop whose loads are all
    // of a class phase 2 can wake exactly (ROM, work RAM, controller ports, timer status).
    bool atSkipPoint(const CPU& cpu) const;
    // A lower bound on the cycle of the first load of the last iteration that was actually executed and compared. Any
    // event before it is already reflected in what that iteration read.
    uint64_t validSince() const { return validSinceCycle; }
    uint64_t iterationCycles() const { return refCycles; }
    uint64_t iterationInstructions() const { return refInstr; }
    void noteSkipped(uint64_t iterations) {
        st.skips++; st.skippedIterations += iterations; st.skippedCpuCycles += iterations * refCycles;
    }

    // The machine changed under the detector (reset, cartridge swap, the switch turned off): forget everything.
    void drop(CPU& cpu);

    const IdleStats& stats() const { return st; }
    void resetStats() { st = IdleStats(); }

private:
    enum class Phase : uint8_t { Idle, First, Second, Confirmed };

    AccessLog accessLog;
    Phase phase = Phase::Idle;
    uint32_t head = 0, tail = 0;     // PB:PC of the loop's first instruction and of its closing branch
    CpuSignature sigAtHead{};
    uint64_t iterCycles = 0;         // CPU cycles of the iteration in progress
    uint64_t iterBegin = 0;          // master cycle of the instruction that opened the iteration in progress
    uint64_t validSinceCycle = 0;
    uint64_t iterInstr = 0;          // ... and its instruction count
    // The reference iteration (the first complete one, then confirmed by the second)
    uint64_t refCycles = 0;
    uint64_t refInstr = 0;
    bool skippable = false;          // every load of the confirmed loop is of a class phase 2 can wake exactly
    int refLoads = 0;
    uint32_t refAddr[AccessLog::MAX];
    uint8_t refValue[AccessLog::MAX];
    WatchClass worst = WatchClass::Rom;
    uint32_t cooldown = 0;           // instructions to ignore backward branches for, after rejecting a loop
    IdleStats st;

    bool sameLoads() const;
    void reject(CPU& cpu);
    void endConfirmed(CPU& cpu, bool interrupt);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_IDLE_DETECT_H
