#include "system.h"
#include "default_boot_rom.h"
#include <iostream>
#include <fstream>
#include <vector>

namespace ZeroPoint {

// Static member initialization
System* System::currentSystem = nullptr;

System::System()
    : cpu(), ppu(), apu(), dma(), display(),
      romLoaded(false), entryPoint(0),
      masterCycleCount(0),
      nextTimerEventCycle(UINT64_MAX), timersLastUpdateCycle(0),
      cpuCycleBudget(0), apuCycleBudget(0),
      audioSampleAccum(0),
      vblankIRQEnabled(false), hblankIRQEnabled(false),
      lastVBlank(false), lastHBlank(false),
      devMode(false)
{
    // Initialize timer state
    timers.control = 0x00;
    timers.status = 0x00;
    timers.intEnable = 0x00;
    timers.vblankCounter = 0;
    timers.hblankCounter = 0;
    timers.secondCounter = 0;
    timers.quarterSecCounter = 0;
    timers.eighthSecCounter = 0;
    timers.k1024Counter = 0;
    timers.microCounter = 0;
    timers.vblank60Counter = 0;
    // Set current system for DMA callbacks
    currentSystem = this;

    // Connect components
    cpu.setPPU(&ppu);
    cpu.setAPU(&apu);
    cpu.setDisplay(&display);
    cpu.setDMA(&dma);
    cpu.setSystem(this);
    ppu.setDisplay(&display);

    // Setup memory map
    // Allocate Work RAM (banks $80-$9F, 2 MB)
    cpu.allocateRAM(0x80, 32);

    // Allocate Shadow Work RAM (banks $BE-$BF, 128 KB)
    cpu.allocateRAM(0xBE, 2);

    // Map PPU and APU windows
    cpu.mapPPUWindow(0xB0);  // Bank $B0 = PPU memory
    cpu.mapAPUWindow(0xA0);  // Bank $A0 = APU memory

    // Setup I/O registers at bank $D8
    cpu.setupIORegisters();

    // Map the Boot ROM (bank $E0). Hardware reset lands here; the stub reads
    // the cartridge entry point System::loadROM() records and hands off.
    cpu.loadBootROM(kDefaultBootROM, kDefaultBootROMSize);

    // Connect DMA to memory system
    dma.setMemoryCallbacks(dmaReadCallback, dmaWriteCallback);
    dma.setPrivilegeQuery(dmaPrivilegeQuery);
    dma.setCompleteCallback(dmaCompleteCallback);

    std::cout << "ZeroPoint System initialized\n";
    std::cout << "  Work RAM:   Banks $80-$9F (2 MB)\n";
    std::cout << "  Shadow RAM: Banks $BE-$BF (128 KB)\n";
    std::cout << "  PPU Window: Bank $B0\n";
    std::cout << "  APU Window: Bank $A0\n";
    std::cout << "  I/O Regs:   Bank $D8\n";
    std::cout << "  Boot ROM:   Bank $E0\n";
    std::cout << "  DMA:        16 channels, 34 MHz\n";
}

System::~System() {
}

void System::reset() {
    cpu.reset();
    ppu.reset();
    apu.reset();
    dma.reset();

    masterCycleCount = 0;
    nextTimerEventCycle = UINT64_MAX;   // no timers enabled -> nothing scheduled
    timersLastUpdateCycle = 0;
    cpuCycleBudget = 0;
    apuCycleBudget = 0;
    audioSampleAccum = 0;
    audioBuffer.clear();

    intc.reset();

    lastVBlank = false;
    lastHBlank = false;

    // Reset timers
    timers.control = 0x00;
    timers.status = 0x00;
    timers.intEnable = 0x00;
    timers.vblankCounter = 0;
    timers.hblankCounter = 0;
    timers.secondCounter = 0;
    timers.quarterSecCounter = 0;
    timers.eighthSecCounter = 0;
    timers.k1024Counter = 0;
    timers.microCounter = 0;
    timers.vblank60Counter = 0;

    // Connect the cartridge interface now that the hard-reset above has
    // finished (register/execution state only - RAM is untouched). Cheap to
    // check every reset(); only actually maps once per loadROM() call.
    if (romLoaded && !cartridgeMapped) {
        cpu.loadROM(pendingRomData.data(), pendingRomData.size(), 0x00);
        cpu.loadSignedROMMetadata(pendingSignedMetadata.empty() ? nullptr : pendingSignedMetadata.data(),
                                   pendingSignedMetadata.size());
        // Runtime chunk-verification gating (see CPU::configureDataGating) -
        // only trailer version 3 has a manifest to gate against. Done once
        // here, in lockstep with the metadata mapping above, not on every
        // reset: CPU::reset() re-locks all chunks itself on each subsequent
        // reset without needing this re-run.
        if (pendingTrailerVersion == 3) {
            cpu.configureDataGating(pendingCodeSize, pendingChunkCount);
        } else {
            cpu.clearDataGating();
        }
        cartridgeMapped = true;
    }

    // With a Boot ROM mapped, cpu.reset() above already landed PC/PB at the
    // Boot ROM ($E0:0000), which reads the cartridge entry point recorded
    // below and jumps there itself. Without one, jump straight to it.
    if (romLoaded) {
        if (!cpu.hasBootROM()) {
            cpu.setPC(entryPoint & 0xFFFF);
            cpu.setPB((entryPoint >> 16) & 0xFF);
        }
        std::cout << "System reset - Entry point: $" << std::hex
                  << (int)((entryPoint >> 16) & 0xFF) << ":" << (entryPoint & 0xFFFF)
                  << std::dec << "\n";
    }
}

void System::powerOn() {
    // Real hardware holds reset while power rails and clocks settle, with
    // the cartridge/expansion bus left disconnected the whole time - only
    // once reset is released does it become safe to expose. loadROM() has
    // already cached the cartridge (pendingRomData) without mapping it, so
    // the bus is still disconnected at this point regardless of how long
    // ago loadROM() was called. This emulator has nothing meaningful to
    // simulate happening during that delay (deterministic, zero-init'd
    // state throughout - see POWER_ON_DELAY_CYCLES), so it isn't spent here;
    // reset() does the hard 0 + reset-signal part and connects the
    // cartridge right after, matching the real sequencing.
    reset();
}

bool System::loadROM(const std::string& filename) {
    ROM rom;
    if (!rom.load(filename)) {
        std::cerr << "Error loading ROM: " << rom.getError() << "\n";
        return false;
    }

    romTitle = rom.getTitle();
    romDeveloper = rom.getDeveloper();
    entryPoint = rom.getEntryPoint();

    // Cache the cartridge bytes but don't map them into CPU memory yet -
    // powerOn() connects the cartridge interface only after the hard-reset
    // settle sequence. (Calling reset() directly instead of powerOn() maps
    // it immediately below, for callers that don't want the power-on delay.)
    pendingRomData.assign(rom.getData(), rom.getData() + rom.getSize());
    cartridgeMapped = false;

    // Same "not mapped until reset()" deferral as pendingRomData - see the
    // cartridge-bus comment below. Empty (and bank $E2 cleared at reset)
    // for an unsigned ROM.
    pendingSignedMetadata.clear();
    pendingTrailerVersion = 0;
    pendingCodeSize = 0;
    pendingChunkCount = 0;
    if (rom.isSigned()) {
        pendingSignedMetadata.assign(rom.getRawHeader(), rom.getRawHeader() + ROM::RAW_HEADER_SIZE);
        const std::vector<uint8_t>& trailer = rom.getTrailer();
        pendingSignedMetadata.insert(pendingSignedMetadata.end(), trailer.begin(), trailer.end());
        pendingTrailerVersion = rom.getTrailerVersion();
        pendingCodeSize = rom.getCodeSize();
        pendingChunkCount = rom.getChunkCount();
    }

    romLoaded = true;

    // Record the entry point for the Boot ROM to read (or for reset() to
    // jump to directly, if no Boot ROM is mapped). Safe to do now - it's
    // just bookkeeping, independent of whether the cartridge bus is
    // actually connected yet.
    cpu.setCartridgeEntryPoint(entryPoint);

    std::cout << "Loaded ROM: " << romTitle << "\n";
    std::cout << "  Developer: " << romDeveloper << "\n";
    std::cout << "  Size: " << rom.getSize() << " bytes\n";
    std::cout << "  Entry: $" << std::hex << entryPoint << std::dec << "\n";

    return true;
}

bool System::loadBootROM(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error loading Boot ROM: cannot open " << filename << "\n";
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (data.empty()) {
        std::cerr << "Error loading Boot ROM: " << filename << " is empty\n";
        return false;
    }

    cpu.loadBootROM(data.data(), data.size());
    std::cout << "Loaded Boot ROM: " << filename << " (" << data.size() << " bytes)\n";
    return true;
}

bool System::loadAPUBios(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error loading APU BIOS: cannot open " << filename << "\n";
        return false;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    if (data.empty()) {
        std::cerr << "Error loading APU BIOS: " << filename << " is empty\n";
        return false;
    }

    apu.loadBIOS(data.data(), data.size());
    std::cout << "Loaded APU BIOS: " << filename << " (" << data.size() << " bytes)\n";
    return true;
}

// Number of subchip cycles granted after raising the code-switch signal, so a
// running program reaches an instruction boundary and services/acknowledges the
// notification before its code memory is overwritten.
static constexpr int CODE_SWITCH_ACK_CYCLES = 256;

void System::loadPPUProgram(const uint8_t* code, size_t length,
                            uint16_t entry, uint16_t switchVector) {
    // 1. If a program is running, interrupt it so it knows the swap is coming,
    //    and give it a bounded window to reach a boundary and react.
    if (switchVector != 0 && ppu.getState() == PPUState::Running) {
        ppu.raiseCpuInterrupt(switchVector);
        for (int i = 0; i < CODE_SWITCH_ACK_CYCLES; i++) {
            ppu.tick();
        }
    }

    // 2. Halt so the PPU can't execute half-overwritten code. reset() returns it
    //    to WaitingForStart and clears its memory.
    ppu.reset();

    // 3. Upload the new microcode at offset 0.
    ppu.loadMicrocode(code, length, 0);

    // 4. Restart at the new entry point.
    ppu.setExecutionPointer(entry);
    ppu.start();

    std::cout << "[boot] PPU program loaded (" << length << " bytes, entry $"
              << std::hex << entry << std::dec << ")\n";
}

void System::loadAPUProgram(const uint8_t* code, size_t length, uint16_t entry) {
    // 1. Signal the APU via its CPU->APU input port that a code switch is coming,
    //    and let a running program observe/acknowledge it.
    apu.writeDEF88186Input(APU_CODESWITCH_PORT, APU_CODESWITCH_SIGNAL);
    if (!apu.isHalted()) {
        for (int i = 0; i < CODE_SWITCH_ACK_CYCLES; i++) {
            apu.step();
        }
    }

    // 2. Halt so the APU can't execute half-overwritten code.
    apu.setHalted(true);

    // 3. Upload the new program at address 0.
    apu.loadROM(code, length, 0);

    // 4. Restart at the new entry point.
    apu.setPC(entry);
    apu.setHalted(false);

    std::cout << "[boot] APU program loaded (" << length << " bytes, entry $"
              << std::hex << entry << std::dec << ")\n";
}

void System::step() {
    // Read the display's blank state once (post-tick) and hand it to both
    // consumers instead of recomputing isVBlank()/isHBlank() four times per
    // master cycle.
    bool currentVBlank, currentHBlank;
    tickComponents(currentVBlank, currentHBlank);
    checkInterrupts(currentVBlank, currentHBlank);
    updateTimers();
    masterCycleCount++;
}

void System::run(uint64_t cycles) {
    for (uint64_t i = 0; i < cycles; i++) {
        step();

        // Stop if CPU is halted
        if (cpu.isHalted()) {
            std::cout << "CPU halted after " << i << " cycles\n";
            break;
        }
    }
}

void System::stepFrame() {
    for (uint64_t i = 0; i < CYCLES_PER_FRAME; i++) {
        if (cpu.isHalted()) break;
        step();
    }
}

void System::tickComponents(bool& outVBlank, bool& outHBlank) {
    // Integer-based cycle pattern (repeats every 16 master cycles)
    // Each component runs on specific cycles:
    // - PPU:     every cycle (0-15)
    // - Display: every cycle (0-15)
    // - DMA:     every 2 cycles (1, 3, 5, 7, 9, 11, 13, 15)
    // - CPU:     every 4 cycles (3, 7, 11, 15)
    // - APU:     every 16 cycles (15)

    int cyclePhase = masterCycleCount % 16;

    // PPU runs every cycle
    ppu.tick();

    // Display ticks every cycle
    display.tick();

    // DMA runs every 2 cycles (on odd cycles)
    if (cyclePhase % 2 == 1) {
        dma.tick();
    }

    // CPU runs at master/4 (~17 MHz). Accrue one CPU cycle of budget every
    // 4 master cycles, then execute whole instructions while budget remains,
    // debiting each instruction's real cycle cost. This makes a 7-cycle
    // instruction actually take 7 CPU cycles worth of wall-clock pacing
    // instead of running as fast as a 2-cycle one.
    if (cyclePhase % 4 == 3 && cpu.getState() == CPUState::Running) {
        cpuCycleBudget++;
    }
    while (cpuCycleBudget > 0 && cpu.getState() == CPUState::Running) {
        uint64_t before = cpu.cycleCount;
        cpu.step();
        cpuCycleBudget -= static_cast<int64_t>(cpu.cycleCount - before);
    }

    // APU runs at master/16 (~4.25 MHz). Same cycle-debited pacing; each APU
    // instruction costs 4 APU cycles, so it now runs at its rated ~1 MIPS
    // instead of ~4x too fast.
    if (cyclePhase == 15 && !apu.isHalted()) {
        apuCycleBudget++;
    }
    while (apuCycleBudget > 0 && !apu.isHalted()) {
        uint64_t before = apu.getCycleCount();
        apu.step();
        apuCycleBudget -= static_cast<int64_t>(apu.getCycleCount() - before);
    }

    // Sample the display's blank state once, now that it has ticked. Feed it to
    // the PPU and return it so checkInterrupts() doesn't recompute it.
    outVBlank = display.isVBlank();
    outHBlank = display.isHBlank();
    ppu.setBlankStatus(outVBlank, outHBlank);

    // MMP mixer runs independently of the APU's CPU (it's a hardware
    // sequencer driven by channel registers, not fetch/decode/execute), so
    // sample it on its own schedule regardless of apu.isHalted().
    audioSampleAccum += AUDIO_SAMPLE_RATE;
    if (audioSampleAccum >= TIMER_SECOND_PERIOD) {
        audioSampleAccum -= TIMER_SECOND_PERIOD;
        apu.updateMMP();
        audioBuffer.push_back(apu.getMixedSampleLeft());
        audioBuffer.push_back(apu.getMixedSampleRight());
    }
}

void System::signalIRQ(IRQSource src) {
    // Latch the source for ISR discrimination, then assert the CPU line only
    // if the controller's top-level mask allows this source through.
    intc.raise(src);
    if (intc.isEnabled(src)) {
        cpu.triggerIRQ();
    }
}

void System::checkInterrupts(bool currentVBlank, bool currentHBlank) {
    // Fast path: with both blank IRQs disabled and no blank period to fall out
    // of, no edge can fire and there's nothing to clear. Keep the last-state
    // latches in sync so enabling an IRQ mid-blank doesn't fabricate an edge.
    if (!vblankIRQEnabled && !hblankIRQEnabled && !lastVBlank && !lastHBlank) [[likely]] {
        lastVBlank = currentVBlank;
        lastHBlank = currentHBlank;
        return;
    }

    // Detect V-Blank rising edge
    if (vblankIRQEnabled && currentVBlank && !lastVBlank) {
        // Route V-Blank through the interrupt controller
        signalIRQ(IRQSource::VBlank);
        // Pause DMA during interrupt
        dma.triggerInterrupt();
    }

    // Detect H-Blank rising edge
    if (hblankIRQEnabled && currentHBlank && !lastHBlank) {
        // Route H-Blank through the interrupt controller
        signalIRQ(IRQSource::HBlank);
        // Pause DMA during interrupt
        dma.triggerInterrupt();
    }

    // Clear DMA interrupt flag when blanking periods end
    if (!currentVBlank && !currentHBlank && (lastVBlank || lastHBlank)) {
        dma.clearInterrupt();
    }

    lastVBlank = currentVBlank;
    lastHBlank = currentHBlank;
}

void System::setDevMode(bool enabled) {
    devMode = enabled;
    // Write to DEV_MODE I/O register ($D80048)
    cpu.writeByte(0xD80048, enabled ? 0x01 : 0x00);

    if (enabled) {
        std::cout << "Dev mode ENABLED - Boot ROM will enter debug loop\n";
    } else {
        std::cout << "Dev mode DISABLED - Boot ROM will jump to entry point\n";
    }
}

// Static description of the 8 timers: enable-bit mask + period, in the register
// bit order 0bVHSQETAR. The matching counters live in the timers struct and are
// paired up by index in the two helpers below.
namespace {
struct TimerDef { uint8_t mask; uint64_t period; };
constexpr TimerDef TIMER_DEFS[8] = {
    {0x80, System::TIMER_VBLANK_PERIOD},       // V  V-blank
    {0x40, System::TIMER_HBLANK_PERIOD},       // H  H-blank
    {0x20, System::TIMER_SECOND_PERIOD},       // S  1s
    {0x10, System::TIMER_QUARTER_SEC_PERIOD},  // Q  1/4s
    {0x08, System::TIMER_EIGHTH_SEC_PERIOD},   // E  1/8s
    {0x04, System::TIMER_K1024_PERIOD},        // T  1/1024s
    {0x02, System::TIMER_MICRO_PERIOD},        // A  ~1ms
    {0x01, System::TIMER_VBLANK60_PERIOD},     // R  60 V-blanks
};
} // namespace

// Gather pointers to the counters in the same order as TIMER_DEFS.
static void collectTimerCounters(decltype(System::timers)& t, uint64_t* out[8]) {
    out[0] = &t.vblankCounter;    out[1] = &t.hblankCounter;
    out[2] = &t.secondCounter;    out[3] = &t.quarterSecCounter;
    out[4] = &t.eighthSecCounter; out[5] = &t.k1024Counter;
    out[6] = &t.microCounter;     out[7] = &t.vblank60Counter;
}

void System::rescheduleTimers() {
    // nextTimerEventCycle = timersLastUpdateCycle + (smallest cycles-to-fire
    // across enabled timers). Each counter is < its period (we reset on hit),
    // so period-counter is the number of increments until that timer fires.
    uint64_t* counters[8];
    collectTimerCounters(timers, counters);

    uint64_t best = UINT64_MAX;
    for (int i = 0; i < 8; ++i) {
        if (timers.control & TIMER_DEFS[i].mask) {
            uint64_t remaining = TIMER_DEFS[i].period - *counters[i];
            if (remaining < best) best = remaining;
        }
    }
    nextTimerEventCycle = (best == UINT64_MAX)
                              ? UINT64_MAX
                              : timersLastUpdateCycle + best;
}

void System::fireDueTimers() {
    uint64_t* counters[8];
    collectTimerCounters(timers, counters);

    // Advance every enabled counter by the cycles elapsed since we last touched
    // them. By construction of nextTimerEventCycle this brings exactly the
    // soonest timer(s) up to their period (never past it), so no fire is missed.
    uint64_t elapsed = masterCycleCount - timersLastUpdateCycle;
    for (int i = 0; i < 8; ++i) {
        if (!(timers.control & TIMER_DEFS[i].mask)) continue;
        *counters[i] += elapsed;
        if (*counters[i] >= TIMER_DEFS[i].period) {
            *counters[i] = 0;
            timers.status |= TIMER_DEFS[i].mask;
            if (timers.intEnable & TIMER_DEFS[i].mask) {
                signalIRQ(IRQSource::Timer);
            }
        }
    }
    timersLastUpdateCycle = masterCycleCount;
    rescheduleTimers();
}

void System::updateTimers() {
    // Fast path: nothing is due (and the no-timers-enabled case parks
    // nextTimerEventCycle at UINT64_MAX, so this always skips). One compare
    // instead of eight counter updates per master cycle.
    if (masterCycleCount < nextTimerEventCycle) [[likely]] {
        return;
    }
    fireDueTimers();
}

void System::setTimerControl(uint8_t value) {
    // Bring the currently-enabled counters current through the *previous* master
    // cycle before switching the enabled set, so continuing timers keep exact
    // phase and any newly-enabled timer starts counting from this cycle. This
    // catch-up cannot fire a timer: nextTimerEventCycle >= masterCycleCount, so
    // fewer than "remaining" increments elapse here.
    if (masterCycleCount > 0) {
        uint64_t through = masterCycleCount - 1;
        if (through > timersLastUpdateCycle) {
            uint64_t catchup = through - timersLastUpdateCycle;
            uint64_t* counters[8];
            collectTimerCounters(timers, counters);
            for (int i = 0; i < 8; ++i) {
                if (timers.control & TIMER_DEFS[i].mask) {
                    *counters[i] += catchup;
                }
            }
            timersLastUpdateCycle = through;
        }
    } else {
        timersLastUpdateCycle = 0;
    }

    timers.control = value;
    rescheduleTimers();
}

// Static DMA callback wrappers
uint8_t System::dmaReadCallback(uint32_t address, bool privileged) {
    if (currentSystem) {
        return currentSystem->cpu.readByteForDMA(address, privileged);
    }
    return 0xFF;
}

void System::dmaWriteCallback(uint32_t address, uint8_t value) {
    if (currentSystem) {
        currentSystem->cpu.writeByteForDMA(address, value);
    }
}

bool System::dmaPrivilegeQuery() {
    return currentSystem && currentSystem->cpu.getPB() == 0xE0;
}

void System::dmaCompleteCallback(uint8_t /*channel*/) {
    if (currentSystem) {
        currentSystem->signalIRQ(IRQSource::DMA);
    }
}

} // namespace ZeroPoint
