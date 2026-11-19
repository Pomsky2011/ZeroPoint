#include "idle_detect.h"
#include "cpu.h"

namespace ZeroPoint {

namespace {
constexpr uint64_t MAX_ITERATION_CYCLES = 4096;   // longer loops are not idle polls
constexpr uint32_t REJECT_COOLDOWN = 256;         // instructions before the next candidate after a reject

bool unwatchable(WatchClass c) {
    return c == WatchClass::PpuPort || c == WatchClass::PpuFb || c == WatchClass::Other;
}
}

IdleDetector::IdleDetector() {
    for (int i = 0; i < AccessLog::MAX; i++) { refAddr[i] = 0; refValue[i] = 0; }
}

bool IdleDetector::atSkipPoint(const CPU& cpu) const {
    return phase == Phase::Confirmed && skippable && !stale && iterCycles == 0 && cpu.PC == static_cast<uint16_t>(head) &&
           cpu.PB == (head >> 16);
}

bool IdleDetector::sameLoads() const {
    if (accessLog.overflow || accessLog.loads != refLoads) return false;
    for (int i = 0; i < refLoads; i++)
        if (accessLog.addr[i] != refAddr[i] || accessLog.value[i] != refValue[i] || accessLog.cycle[i] != refCycle[i]) return false;
    return true;
}

void IdleDetector::drop(CPU& cpu) {
    cpu.setAccessLog(nullptr);
    phase = Phase::Idle;
    cooldown = 0;
}

void IdleDetector::reject(CPU& cpu) {
    drop(cpu);
    cooldown = REJECT_COOLDOWN;
}

// A confirmed loop ended mid-iteration (it left the loop's range or took an interrupt). Why it ended is the self-check:
// the loop is a pure function of the bytes it reads, so it can only leave because one of them changed.
void IdleDetector::endConfirmed(CPU& cpu, bool interrupt) {
    if (interrupt) {
        st.interrupted++;
    } else {
        bool changed = accessLog.overflow;
        const int n = accessLog.loads < refLoads ? accessLog.loads : refLoads;
        for (int i = 0; i < n && !changed; i++)
            if (accessLog.addr[i] != refAddr[i] || accessLog.value[i] != refValue[i]) changed = true;
        if (changed) st.woke++;
        else st.unexplained++;
    }
    drop(cpu);
}

void IdleDetector::observe(CPU& cpu, uint32_t pcBefore, uint64_t cycles, uint64_t now) {
    const uint32_t pcAfter = (static_cast<uint32_t>(cpu.PB) << 16) | cpu.PC;

    if (phase == Phase::Idle) {
        if (cooldown) { cooldown--; return; }
        // A backward (or self) branch taken within a bank: a loop candidate whose head is where we landed.
        if (pcAfter <= pcBefore && (pcAfter >> 16) == (pcBefore >> 16) && cpu.getState() == CPUState::Running) {
            head = pcAfter;
            tail = pcBefore;
            sigAtHead = cpu.idleSignature();
            iterCycles = 0;
            iterInstr = 0;
            iterBegin = now;
            accessLog.clear();
            accessLog.cycleBase = 0;
            phase = Phase::First;
            cpu.setAccessLog(&accessLog);
        }
        return;
    }

    iterCycles += cycles;
    iterInstr++;
    accessLog.cycleBase = static_cast<uint32_t>(iterCycles);   // the next instruction starts this far into the iteration
    const bool interrupted = accessLog.writes != 0;
    const bool inRange = (pcAfter >> 16) == (head >> 16) && pcAfter >= head && pcAfter <= tail;

    if (cpu.getState() != CPUState::Running || iterCycles > MAX_ITERATION_CYCLES) {
        if (phase == Phase::Confirmed) endConfirmed(cpu, interrupted); else reject(cpu);
        return;
    }
    if (!inRange || (interrupted && phase != Phase::Confirmed) || accessLog.overflow) {
        if (phase == Phase::Confirmed) endConfirmed(cpu, interrupted);
        else reject(cpu);
        return;
    }
    if (interrupted) { endConfirmed(cpu, true); return; }   // Confirmed: a store/push/interrupt entry in the loop

    if (!(pcBefore == tail && pcAfter == head)) return;     // mid-iteration

    // One iteration complete.
    validSinceCycle = iterBegin;
    iterBegin = now;
    const CpuSignature sigNow = cpu.idleSignature();
    if (sigNow != sigAtHead) {
        if (phase != Phase::Confirmed) { reject(cpu); return; }
        // Only an interrupt line latching (an IRQ raised while I is set: the loop never takes it, it just stays
        // pending) can change a confirmed loop's state without it having read anything different.
        CpuSignature masked = sigNow;
        masked.flags = sigAtHead.flags;
        if (masked == sigAtHead && (sigNow.flags & 7) != (sigAtHead.flags & 7) && sameLoads()) { st.interrupted++; drop(cpu); }
        else endConfirmed(cpu, false);
        return;
    }
    switch (phase) {
        case Phase::First:
            refCycles = iterCycles;
            refInstr = iterInstr;
            refLoads = accessLog.loads;
            for (int i = 0; i < refLoads; i++) { refAddr[i] = accessLog.addr[i]; refValue[i] = accessLog.value[i]; refCycle[i] = accessLog.cycle[i]; }
            phase = Phase::Second;
            break;
        case Phase::Second:
            if (iterCycles == refCycles && iterInstr == refInstr && sameLoads()) {
                phase = Phase::Confirmed;
                stale = false;
                st.loopsConfirmed++;
                worst = WatchClass::Rom;
                skippable = true;
                ppuCount = 0;
                for (int i = 0; i < refLoads; i++) {
                    const WatchClass c = cpu.classifyLoad(refAddr[i]);
                    if (static_cast<uint8_t>(c) > static_cast<uint8_t>(worst)) worst = c;
                    if (c == WatchClass::PpuMem) {
                        // Skippable only if every PPU read is at one cycle: the PPU is then run ahead to the watched byte's
                        // change and the CPU resumes at the first read at or after it, so no read before it sees the future.
                        if (ppuCount > 0 && refCycle[i] != ppuOffset) skippable = false;
                        ppuOffset = refCycle[i];
                        ppuAddr[ppuCount++] = static_cast<uint16_t>(refAddr[i] & 0xFFFF);
                    } else if (c != WatchClass::Rom && c != WatchClass::WorkRam && c != WatchClass::Input && c != WatchClass::Timer) {
                        skippable = false;
                    }
                }
            } else {   // the first one was not the steady state: this one becomes the reference
                refCycles = iterCycles;
                refInstr = iterInstr;
                refLoads = accessLog.loads;
                for (int i = 0; i < refLoads; i++) { refAddr[i] = accessLog.addr[i]; refValue[i] = accessLog.value[i]; refCycle[i] = accessLog.cycle[i]; }
            }
            break;
        case Phase::Confirmed:
            if (iterCycles == refCycles && iterInstr == refInstr && sameLoads()) {
                stale = false;   // a real iteration read the same bytes as the reference: it is current again
                st.iterations++;
                st.cpuCycles += iterCycles;
                st.cpuCyclesByClass[static_cast<int>(worst)] += iterCycles;
                if (unwatchable(worst)) st.cpuCyclesUnwatchable += iterCycles;
            } else if (sameLoads()) {
                st.cycleDrift++;
                drop(cpu);
                return;
            } else {
                st.woke++;
                drop(cpu);
                return;
            }
            break;
        case Phase::Idle: break;
    }
    iterCycles = 0;
    iterInstr = 0;
    accessLog.clear();
    accessLog.cycleBase = 0;
}

} // namespace ZeroPoint
