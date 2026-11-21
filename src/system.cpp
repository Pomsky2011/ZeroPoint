#include "system.h"
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>

namespace ZeroPoint {

// Static member initialization

System::System()
    : cpu(), ppu(), apu(), dma(), display(),
      romLoaded(false), entryPoint(0),
      masterCycleCount(0),
      nextTimerEventCycle(UINT64_MAX), timersLastUpdateCycle(0),
      cpuCycleBudget(0), apuCycleBudget(0),
      audioSampleAccum(0),
      nextAudioCycle(audioCyclesUntilSample(0)),
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

    // Connect components
    cpu.setPPU(&ppu);
    cpu.setAPU(&apu);
    cpu.setDisplay(&display);
    cpu.setDMA(&dma);
    cpu.setSystem(this);
    ppu.setDisplay(&display);

    // No APU BIOS image yet: stand in for it natively. loadAPUBios() switches
    // to the real, assembled BIOS (see APU::enableHLEBios()).
    apu.enableHLEBios(true);

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

    // No Boot ROM image by default: the boot ROM is HLE'd natively (isBootHLE(), src/system.cpp's
    // hle* functions, docs/boot-hle.md) until loadBootROM() maps a real one into bank $E0.
    cpu.setServiceHook([this](uint8_t sig) { return hleService(sig); });

    // Connect DMA to memory system
    dma.setCallbackContext(this);
    dma.setMemoryCallbacks(dmaReadCallback, dmaWriteCallback);
    dma.setPrivilegeQuery(dmaPrivilegeQuery);
    dma.setBusContendedQuery(dmaBusContendedQuery);
    dma.setCompleteCallback(dmaCompleteCallback);

    std::cout << "ZeroPoint System initialized\n";
    std::cout << "  Work RAM:   Banks $80-$9F (2 MB)\n";
    std::cout << "  Shadow RAM: Banks $BE-$BF (128 KB)\n";
    std::cout << "  PPU Window: Bank $B0\n";
    std::cout << "  APU Window: Bank $A0\n";
    std::cout << "  I/O Regs:   Bank $D8\n";
    std::cout << "  Boot ROM:   HLE (Bank $E0 when loadBootROM() maps a real one)\n";
    std::cout << "  DMA:        16 channels, 34 MHz\n";
}

System::~System() {
    stopVideoThread();
}

void System::reset() {
    cpu.reset();
    ppu.reset();
    apu.reset();
    dma.reset();

    masterCycleCount = 0;
    videoCycle = 0;
    {
        std::lock_guard<std::mutex> lock(vtMutex);   // the worker is idle between runCycles() calls
        vtRequest = 0;
    }
    vtDone = 0;
    videoPublished = 0;
    videoLazy = false;
    videoAccessed = false;
    nextTimerEventCycle = UINT64_MAX;   // no timers enabled -> nothing scheduled
    timersLastUpdateCycle = 0;
    idleDetect.drop(cpu);
    lastTimerFireCycle = UINT64_MAX;
    cpuCycleBudget = 0;
    apuCycleBudget = 0;
    audioSampleAccum = 0;
    nextAudioCycle = audioCyclesUntilSample(0);   // from master cycle 0
    audioBuffer.clear();
    audioDigest = 1469598103934665603ull;

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
        mapCartridgeNow();
    }

    // With a real Boot ROM mapped, cpu.reset() above already landed PC/PB at the
    // Boot ROM ($E0:0000), which verifies the cartridge and jumps to the entry
    // point recorded below itself. Without one, the HLE does that natively.
    if (romLoaded) {
        if (!cpu.hasBootROM()) hleBootCartridge();
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
    return cacheROM(rom);
}

bool System::loadROMImage(const std::vector<uint8_t>& image) {
    ROM rom;
    if (!rom.loadMemory(image.data(), image.size())) {
        std::cerr << "Error loading ROM: " << rom.getError() << "\n";
        return false;
    }
    return cacheROM(rom);
}

bool System::cacheROM(const ROM& rom) {
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

    // Identity of what is in the slot, for stateHash (header + payload + trailer, i.e. the file).
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&h](const uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull; };
        mix(rom.getRawHeader(), ROM::RAW_HEADER_SIZE);
        mix(rom.getData(), rom.getSize());
        mix(rom.getTrailer().data(), rom.getTrailer().size());
        cartridgeDigest = h;
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

void System::mapCartridgeNow() {
    cpu.mapCartridge(pendingRomData.data(), pendingRomData.size(), pendingSignedMetadata.data(),
                     pendingSignedMetadata.size(), pendingTrailerVersion, pendingCodeSize, pendingChunkCount);
    cartridgeMapped = true;
}

bool System::insertCartridge(const std::vector<uint8_t>& image) {
    ROM rom;
    if (!rom.loadMemory(image.data(), image.size())) {
        std::cerr << "Error inserting cartridge: " << rom.getError() << "\n";
        return false;
    }
    cacheROM(rom);   // also what is on the bus from now on: no reset, no boot, no verify
    idleDetect.drop(cpu);
    mapCartridgeNow();
    return true;
}

void System::ejectCartridge() {
    idleDetect.drop(cpu);
    cpu.ejectCartridge();
    pendingRomData.clear();
    pendingSignedMetadata.clear();
    pendingTrailerVersion = 0;
    pendingCodeSize = 0;
    pendingChunkCount = 0;
    cartridgeMapped = false;
    romLoaded = false;
    entryPoint = 0;
    cartridgeDigest = 0;
    romTitle.clear();
    romDeveloper.clear();
    cpu.setCartridgeEntryPoint(0);
}

void System::hardReset() {
    // A power cycle: RAM, video, and the cartridge bus (the cartridge itself stays in the slot, but off the bus until
    // reset() reconnects it, exactly like powerOn()).
    cpu.clearRAM();
    display.reset();
    cpu.ejectCartridge();
    cartridgeMapped = false;
    reset();
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
    if (!loadBootROMImage(data)) return false;
    std::cout << "Loaded Boot ROM: " << filename << " (" << data.size() << " bytes)\n";
    return true;
}

bool System::loadBootROMImage(const std::vector<uint8_t>& data) {
    if (data.empty()) return false;
    cpu.loadBootROM(data.data(), data.size());
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
    if (!loadAPUBiosImage(data)) return false;
    std::cout << "Loaded APU BIOS: " << filename << " (" << data.size() << " bytes)\n";
    return true;
}

bool System::loadAPUBiosImage(const std::vector<uint8_t>& data) {
    if (data.empty()) return false;
    apu.loadBIOS(data.data(), data.size());
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
    bool currentVBlank = false, currentHBlank = false;
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
    runCycles(CYCLES_PER_FRAME);
}

uint64_t System::quietCycles(uint64_t& displayQuiet) const {
    // Everything tickComponents()/step() does besides ppu.tick() and display.tick()'s increment, and the first
    // cycle at which it can matter. "quiet" is how many cycles come before the first such one.
    if (!dma.tickIsNoOp()) { displayQuiet = 0; return 0; }
    displayQuiet = static_cast<uint64_t>(display.quietTicks());
    return quietCyclesNonVideo();
}

uint64_t System::quietCyclesNonVideo() const {
    // The cycle numbers below are masterCycleCount values. Callers have checked that DMA is idle.
    const uint64_t c = masterCycleCount;
    uint64_t q = UINT64_MAX;

    // The CPU/APU run an instruction on the cycle their budget first goes positive. Budget accrues by 1 on every
    // 4th (CPU) / 16th (APU) cycle and an instruction debits its cost, so after an instruction the next one is
    // not due until budget climbs back above 0: the accrual cycles in between do nothing but count, and
    // advanceQuiet() adds them in bulk. (Stopping at every accrual instead ran ~cost full steps per instruction.)
    if (cpu.getState() == CPUState::Running) {
        if (cpuCycleBudget > 0) return 0;                 // an instruction is due now
        const uint64_t need = static_cast<uint64_t>(1 - cpuCycleBudget);          // accruals until budget > 0
        const uint64_t firstAccrual = (3 - (c & 3)) & 3;  // budget accrues when c % 4 == 3
        const uint64_t due = firstAccrual + 4 * (need - 1);
        if (due < q) q = due;
    }
    if (!apu.isHalted()) {
        if (apuCycleBudget > 0) return 0;
        const uint64_t need = static_cast<uint64_t>(1 - apuCycleBudget);
        const uint64_t period = apu.isFastMode() ? 8 : 16;
        const uint64_t firstAccrual = (period - 1) - (c & (period - 1));   // ... c % period == period - 1
        const uint64_t due = firstAccrual + period * (need - 1);
        if (due < q) q = due;
    }
    if (nextTimerEventCycle != UINT64_MAX) {              // updateTimers() fires when c >= nextTimerEventCycle
        const uint64_t toTimer = nextTimerEventCycle > c ? nextTimerEventCycle - c : 0;
        if (toTimer < q) q = toTimer;
    }
    // Audio: a sample is emitted on the cycle the accumulator reaches a full second's worth.
    const uint64_t toSample = nextAudioCycle > c ? nextAudioCycle - c : 0;
    if (toSample < q) q = toSample;
    return q;
}

void System::advanceNonVideo(uint64_t cycles) {
    // advanceQuiet() without the video chips: the budget accruals and audio accumulation of `cycles` skipped
    // cycles, and the clock itself.
    const uint64_t c = masterCycleCount;
    const unsigned apuShift = apu.isFastMode() ? 3 : 4;   // the APU period (8 or 16 master cycles) as a shift
    if (cpu.getState() == CPUState::Running)
        cpuCycleBudget += static_cast<int64_t>((c + cycles) / 4 - c / 4);
    if (!apu.isHalted())
        apuCycleBudget += static_cast<int64_t>(((c + cycles) >> apuShift) - (c >> apuShift));
    audioSampleAccum += AUDIO_SAMPLE_RATE * cycles;
    masterCycleCount += cycles;
}

void System::tickVideoTo(uint64_t targetCycle) {
    // Tick the PPU and display for the master cycles videoCycle .. targetCycle - 1, in step()'s order (ppu, then
    // display, then the blank status hand-off to the PPU). Between display edges the blank lines are constant, so
    // those stretches go through PPU::tickMany; the edge ticks (H-blank start, end of line) and any tick where
    // the PPU's idea of the blank lines is out of date (power-on, after a PPU reset) run one cycle at a time.
    uint64_t vc = videoCycle.load(std::memory_order_relaxed);
    while (vc < targetCycle) {
        uint64_t q = 0;
        if (ppu.blankStatusIs(display.isVBlank(), display.isHBlank())) {
            q = static_cast<uint64_t>(display.quietTicks());
            if (q > targetCycle - vc) q = targetCycle - vc;
        }
        if (q > 0) {
            const uint32_t done = ppu.tickMany(static_cast<uint32_t>(q));   // fewer than q only when a watch fired
            display.advanceQuiet(static_cast<int>(done));
            vc += done;
        } else {
            ppu.tick();
            display.tick();
            ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
            vc++;
        }
        videoCycle.store(vc, std::memory_order_relaxed);
        if (ppu.watchFired()) [[unlikely]] break;   // idle-poll skip: stopped right after the cycle that changed a watched byte
    }
}

void System::syncVideo(uint64_t targetCycle) {
    if (videoThread.joinable()) waitVideoIdle();
    tickVideoTo(targetCycle);
    const uint64_t vc = videoCycle.load(std::memory_order_relaxed);
    if (vc > videoPublished) videoPublished = vc;   // accesses this frequent keep the stretches below VIDEO_CHUNK
}

void System::setVideoThread(bool on) {
    videoThreadWanted = on && std::thread::hardware_concurrency() >= 2;
    if (!videoThreadWanted) stopVideoThread();
}

bool System::videoThreadDefault() {
    const char* env = std::getenv("ZEROPOINT_VIDEO_THREAD");
    return !(env && env[0] == '0');
}

bool System::idlePollSkipDefault() {
    const char* env = std::getenv("ZEROPOINT_IDLE_SKIP");
    return !(env && env[0] == '0');
}

bool System::verifySignaturesDefault() {
    const char* env = std::getenv("ZEROPOINT_NO_VERIFY");
    return !(env && env[0] == '1');
}

void System::startVideoThread() {
    if (videoThread.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(vtMutex);
        vtQuit = false;
    }
    videoThread = std::thread(&System::videoThreadMain, this);
}

void System::stopVideoThread() {
    if (!videoThread.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(vtMutex);
        vtQuit = true;
    }
    vtWake.notify_one();
    videoThread.join();
}

void System::videoThreadMain() {
    std::unique_lock<std::mutex> lock(vtMutex);
    for (;;) {
        vtWake.wait(lock, [this] { return vtQuit || videoCycle.load(std::memory_order_relaxed) < vtRequest; });
        if (vtQuit) return;
        const uint64_t target = vtRequest;
        lock.unlock();
        tickVideoTo(target);
        vtDone.store(target, std::memory_order_release);
        lock.lock();                 // the runner checks vtDone under this mutex before it sleeps: no lost wake-up
        vtIdle.notify_all();
    }
}

void System::waitVideoIdle() {
    const uint64_t request = vtRequest;   // only this thread ever writes it
    for (int spin = 0; spin < 2000; spin++) {
        if (vtDone.load(std::memory_order_acquire) == request) return;
    }
    std::unique_lock<std::mutex> lock(vtMutex);
    vtIdle.wait(lock, [&] { return vtDone.load(std::memory_order_acquire) == request; });
}

void System::publishVideo(uint64_t targetCycle) {
    {
        std::lock_guard<std::mutex> lock(vtMutex);
        vtRequest = targetCycle;
    }
    videoPublished = targetCycle;
    vtWake.notify_one();
}

void System::advanceQuiet(uint64_t cycles, bool foldEventTick) {
    // Budget accruals that tickComponents() would have made on the skipped cycles [c, c + cycles): one per
    // cycle with c % 4 == 3 (CPU, if running) / c % 16 == 15 (APU, if not halted). By construction none of them
    // makes the budget positive: that cycle is the event cycle after this stretch.
    const uint64_t c = masterCycleCount;
    const unsigned apuShift = apu.isFastMode() ? 3 : 4;   // the APU period (8 or 16 master cycles) as a shift
    if (cpu.getState() == CPUState::Running)
        cpuCycleBudget += static_cast<int64_t>((c + cycles) / 4 - c / 4);
    if (!apu.isHalted())
        apuCycleBudget += static_cast<int64_t>(((c + cycles) >> apuShift) - (c >> apuShift));
    const uint64_t ticks = cycles + (foldEventTick ? 1 : 0);   // the event cycle's own ppu/display tick
    ppu.tickMany(static_cast<uint32_t>(ticks));
    display.advanceQuiet(static_cast<int>(ticks));
    audioSampleAccum += AUDIO_SAMPLE_RATE * cycles;
    masterCycleCount += cycles;
}

void System::runCycles(uint64_t cycles) {
    const uint64_t target = masterCycleCount + cycles;
    if (idlePollSkip) idleDetect.drop(cpu);   // the host may have touched anything since the last call: re-learn the loop
    while (masterCycleCount < target && !cpu.isHalted()) {
        if (!vblankIRQEnabled && !hblankIRQEnabled && dma.tickIsNoOp()) {
            // Nothing can make the CPU need the video chips except its own accesses to them (which sync them
            // first, see syncVideoForAccess()): no blank IRQ to deliver on a display edge and no DMA ticking
            // alongside. Run the CPU/APU/timers/audio events ahead and let the PPU/display catch up in long
            // stretches.
            if (!videoLazy) {
                videoLazy = true;
                videoCycle = masterCycleCount;
                if (videoThreadWanted) {
                    startVideoThread();
                    {
                        std::lock_guard<std::mutex> lock(vtMutex);   // the worker is idle: nothing to do yet
                        vtRequest = masterCycleCount;
                    }
                    vtDone.store(masterCycleCount, std::memory_order_release);
                }
                videoPublished = masterCycleCount;
            }
            runAhead(target);
        } else {
            leaveRunAhead();
            lockstepEvent(target);
        }
    }
    leaveRunAhead();
    cpu.serialSettle();   // get the debug UART's finished bytes to the host (the guest's view doesn't depend on this)
}

void System::leaveRunAhead() {
    if (!videoLazy) return;
    syncVideo(masterCycleCount);
    videoLazy = false;
    // What checkInterrupts() would have left behind: the display's blank lines after the last tick.
    lastVBlank = display.isVBlank();
    lastHBlank = display.isHBlank();
}

void System::runAhead(uint64_t target) {
    // Events (a CPU/APU instruction, a timer, an audio sample) in order, state-identical to step()ing through
    // them except that nothing here ticks the video chips: whatever the CPU/APU do that depends on them goes
    // through syncVideoForAccess(), which ticks them through this very cycle first, as step() would have.
    while (masterCycleCount < target && !cpu.isHalted() && dma.tickIsNoOp()) {
        runCpuBurst(target);
        if (masterCycleCount >= target || cpu.isHalted() || !dma.tickIsNoOp()) continue;
        const uint64_t left = target - masterCycleCount;
        const uint64_t q = quietCyclesNonVideo();
        if (q >= left) {
            advanceNonVideo(left);
            return;
        }
        advanceNonVideo(q);
        bool currentVBlank = false, currentHBlank = false;
        videoAccessed = false;
        tickRest(static_cast<int>(masterCycleCount % 16), currentVBlank, currentHBlank, false);
        if (videoAccessed) {
            // step() hands the PPU the display's blank lines at the end of every cycle, after the CPU has run
            // (a PPU reset in that instruction clears them): the video chips are exactly at this cycle now.
            ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
        }
        updateTimers();
        masterCycleCount++;
        if (videoThread.joinable() && masterCycleCount > videoPublished && masterCycleCount - videoPublished >= VIDEO_CHUNK) {
            publishVideo(masterCycleCount);   // video may now run through cycle master-1; the CPU's next access needs master
        }
    }
}

void System::lockstepEvent(uint64_t target) {
    uint64_t displayQuiet = 0;
    const uint64_t q = quietCycles(displayQuiet);
    const uint64_t left = target - masterCycleCount;
    if (q < displayQuiet && q < left) {
        // A pure event cycle (CPU/APU instruction, timer or audio sample) with no display edge on it and DMA
        // idle: the PPU and display tick of that cycle come first in step() and nothing between them and the
        // quiet stretch before it can tell them apart, so tick them together with the stretch (one
        // PPU::tickMany entry instead of two) and run only the rest of the cycle for real. Same order as
        // step(): ppu, display, [idle DMA: nothing], CPU/APU/audio, interrupts, timers, count.
        advanceQuiet(q, true);
        const int phase = static_cast<int>(masterCycleCount % 16);
        bool currentVBlank = false, currentHBlank = false;
        tickRest(phase, currentVBlank, currentHBlank);
        checkInterrupts(currentVBlank, currentHBlank);
        updateTimers();
        masterCycleCount++;
        return;
    }
    const uint64_t quiet = q < displayQuiet ? q : displayQuiet;
    if (quiet > 0) advanceQuiet(quiet < left ? quiet : left);
    if (masterCycleCount >= target) return;
    step();
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

    tickRest(cyclePhase, outVBlank, outHBlank);
}

#if defined(_MSC_VER)
#define ZP_ALWAYS_INLINE __forceinline
#else
#define ZP_ALWAYS_INLINE inline __attribute__((always_inline))
#endif

// What one master cycle does for the CPU, the APU and the audio sampler (not the video chips): shared by tickRest()
// and the run-ahead burst loop, so there is one definition of it.
ZP_ALWAYS_INLINE void System::cpuApuAudioCycle(int cyclePhase) {
    // CPU runs at master/4 (~17 MHz). Accrue one CPU cycle of budget every
    // 4 master cycles, then execute whole instructions while budget remains,
    // debiting each instruction's real cycle cost. This makes a 7-cycle
    // instruction actually take 7 CPU cycles worth of wall-clock pacing
    // instead of running as fast as a 2-cycle one.
    if (cyclePhase % 4 == 3 && cpu.getState() == CPUState::Running) {
        cpuCycleBudget++;
    }
    while (cpuCycleBudget > 0 && cpu.getState() == CPUState::Running) {
        if (idleSkipOK && idleDetect.atSkipPoint(cpu)) [[unlikely]] {
            if (trySkipIdle()) continue;
        }
        uint64_t before = cpu.cycleCount;
        const uint32_t pcBefore = idlePollSkip ? (static_cast<uint32_t>(cpu.PB) << 16) | cpu.PC : 0;
        cpu.step();
        const uint64_t spent = cpu.cycleCount - before;
        cpuCycleBudget -= static_cast<int64_t>(spent);
        if (idlePollSkip) [[unlikely]] idleDetect.observe(cpu, pcBefore, spent, masterCycleCount);
    }

    // APU runs at master/16 (~4.25 MHz), or master/8 (~8.5 MHz) in fast mode. Same cycle-debited pacing; each APU
    // instruction costs 2 APU cycles: ~2.1 MIPS normal, ~4.25 MIPS fast.
    const int apuMask = apu.isFastMode() ? 7 : 15;
    if ((cyclePhase & apuMask) == apuMask && !apu.isHalted()) {
        apuCycleBudget++;
    }
    while (apuCycleBudget > 0 && !apu.isHalted()) {
        uint64_t before = apu.getCycleCount();
        apu.step();
        apuCycleBudget -= static_cast<int64_t>(apu.getCycleCount() - before);
    }

    // MMP mixer runs independently of the APU's CPU (it's a hardware
    // sequencer driven by channel registers, not fetch/decode/execute), so
    // sample it on its own schedule regardless of apu.isHalted().
    audioSampleAccum += AUDIO_SAMPLE_RATE;
    if (audioSampleAccum >= TIMER_SECOND_PERIOD) {
        audioSampleAccum -= TIMER_SECOND_PERIOD;
        nextAudioCycle = masterCycleCount + 1 + audioCyclesUntilSample(audioSampleAccum);   // from the next cycle on
        apu.updateMMP();
        const int16_t left = apu.getMixedSampleLeft(), right = apu.getMixedSampleRight();
        audioBuffer.push_back(left);
        audioBuffer.push_back(right);
        for (int16_t v : {left, right}) {
            audioDigest = (audioDigest ^ (uint8_t)(v & 0xFF)) * 1099511628211ull;
            audioDigest = (audioDigest ^ (uint8_t)((v >> 8) & 0xFF)) * 1099511628211ull;
        }
    }
}

void System::tickRest(int cyclePhase, bool& outVBlank, bool& outHBlank, bool sampleVideo) {
    cpuApuAudioCycle(cyclePhase);

    // Sample the display's blank state once, now that it has ticked. Feed it to
    // the PPU and return it so checkInterrupts() doesn't recompute it.
    if (sampleVideo) {
        outVBlank = display.isVBlank();
        outHBlank = display.isHBlank();
        ppu.setBlankStatus(outVBlank, outHBlank);
    }
}

// Idle-poll skip (docs/idle-poll-skip.md, phase 2). The CPU is at the first instruction of a fresh iteration of a loop
// that is closed, read-only and stable, and everything it loads is a constant or changes only at an instant we know.
// Run k whole iterations as one instruction of k*T CPU cycles at this very cycle: every other component runs as it
// would have, and the CPU is next due exactly when the k-th iteration would have ended (4 master cycles per CPU cycle).
// Exact because (a) the CPU starts an instruction on an accrual cycle (c % 4 == 3) with a budget of exactly 1, and an
// instruction of n cycles then makes the next one due n accruals = 4n cycles later, so iteration j starts at
// c + (j-1) * 4T; (b) nothing the loop loads can change before the first cycle at which a timer fires: only the CPU
// writes RAM/timer registers (the loop has no store) and only the host, between runCycles() calls (which drop the
// detector), changes controller ports; DMA is idle in run-ahead. The timer status is set at the *end* of its event
// cycle, so loads at cycles before it read the old value; we skip only iterations that end at or before it. A timer
// event is also the only thing that can latch an IRQ, so no interrupt can be due inside the skipped stretch.
bool System::trySkipIdle() {
    const uint64_t c = masterCycleCount;
    if ((c & 3) != 3 || cpuCycleBudget != 1 || cpu.interruptDeliverable() || !dma.tickIsNoOp()) return false;
    // The reference loads must postdate every timer event so far: one that fired during the last real iteration, after
    // the loop's load, would have changed what the next iteration reads.
    if (lastTimerFireCycle != UINT64_MAX && lastTimerFireCycle >= idleDetect.validSince()) return false;
    uint64_t limit = idleSkipTarget;                     // never past the end of this run: a stop inside would see the CPU ahead
    if (nextTimerEventCycle < limit) limit = nextTimerEventCycle;
    if (limit <= c) return false;
    const uint64_t T = idleDetect.iterationCycles();
    uint64_t k;
    if (!idleDetect.hasPpuLoads()) {
        k = (limit - c) / (4 * T);
    } else {
        // Phase 3: the loop polls PPU memory, always with one instruction (so at one cycle, `firstRead` in the first
        // iteration and 4T later in each next one). Nothing the CPU does can change that memory, so run the video chips
        // ahead *now* with a watch on the polled bytes: they stop right after the cycle whose instruction changes one
        // (H), or at `lppu` if none does. Iterations whose read is before H (or before lppu) see the old bytes and are
        // skipped; the next one is run for real, its read at or after H, where the PPU is exactly in step (a read at
        // cycle r wants the PPU ticked through r, and it is ticked through H, r >= H). lppu leaves one iteration before
        // `limit`, so every skipped iteration also ends by the timer/run bound. A PPU that stopped short of the first
        // read just means no iteration is skipped this time: the video chips being ahead of the CPU is harmless while
        // the CPU's next PPU access is at or after the cycle they reached, or its bytes cannot have changed.
        const uint64_t period = 4 * T;
        if (!videoLazy || limit < period) return false;
        const uint64_t lppu = limit - period;
        const uint64_t firstRead = c + 4 * idleDetect.ppuReadCycles();
        if (lppu <= firstRead) return false;
        if (videoThread.joinable()) waitVideoIdle();
        ppu.armWatch(idleDetect.ppuAddrs(), idleDetect.ppuAddrCount());
        tickVideoTo(lppu);
        const bool hit = ppu.watchFired();
        ppu.disarmWatch();
        if (hit) idleDetect.markStale();   // the polled byte changed: the loop's reference loads are out of date from here
        const uint64_t vc = videoCycle.load(std::memory_order_relaxed);
        if (vc > videoPublished) videoPublished = vc;
        const uint64_t bound = hit ? vc - 1 : lppu;   // the first cycle whose read may see a new byte (H = vc - 1 on a hit)
        idleDetect.notePpuRun(hit, hit && bound > firstRead && (bound - firstRead) % period == 0);
        if (bound <= firstRead) return false;
        k = (bound - firstRead + period - 1) / period;
    }
    if (k == 0) return false;
    cpu.idleAdvance(k * T, k * idleDetect.iterationInstructions());
    cpuCycleBudget -= static_cast<int64_t>(k * T);
    idleDetect.noteSkipped(k);
    return true;
}

void System::runCpuBurst(uint64_t target) {
    // Run-ahead's common case, made cheap: stretches where the only events are CPU and APU instructions (the APU is
    // the busier of the two when it decodes audio: ~4M instructions a second in fast mode, each of which used to end
    // the burst). The first cycle of a timer or audio-sample event is computed once; CPU/APU events before it then
    // run back to back with just the budget arithmetic between them, through the same cpuApuAudioCycle() a full
    // tickRest() uses (it runs whichever of the two is due). A CPU instruction can change what is due (it can write
    // the timer control register, start or halt the APU, switch its clock, or start a DMA), so those are rechecked
    // after every event and the horizon recomputed (or the burst ended). Neither the APU nor a timer touches the
    // video chips, so only a CPU access sets videoAccessed.
    auto horizon = [this]() -> uint64_t {
        const uint64_t c = masterCycleCount;
        uint64_t h = nextAudioCycle > c ? nextAudioCycle : c;
        if (nextTimerEventCycle != UINT64_MAX) {
            const uint64_t t = nextTimerEventCycle > c ? nextTimerEventCycle : c;
            if (t < h) h = t;
        }
        return h;
    };

    uint64_t h = horizon();
    uint64_t timerSnap = nextTimerEventCycle;
    bool apuHaltedSnap = apu.isHalted();
    bool apuFastSnap = apu.isFastMode();
    while (cpu.getState() == CPUState::Running) {
        const uint64_t c = masterCycleCount;
        uint64_t e = c;                                                // the cycle of the CPU's next instruction
        if (cpuCycleBudget <= 0) {
            const uint64_t need = static_cast<uint64_t>(1 - cpuCycleBudget);
            e = c + ((3 - (c & 3)) & 3) + 4 * (need - 1);
        }
        if (!apuHaltedSnap && e > c) {                                 // ... or the APU's, if that comes first
            uint64_t ea = c;
            if (apuCycleBudget <= 0) {
                const uint64_t need = static_cast<uint64_t>(1 - apuCycleBudget);
                const uint64_t period = apu.isFastMode() ? 8 : 16;     // as quietCyclesNonVideo()
                ea = c + ((period - 1) - (c & (period - 1))) + period * (need - 1);
            }
            if (ea < e) e = ea;
        }
        if (e >= h || e >= target) return;                             // not this burst's: the generic path takes it

        if (e > c) {                                                   // the quiet cycles before it
            cpuCycleBudget += static_cast<int64_t>(e / 4 - c / 4);
            if (!apuHaltedSnap) {
                const unsigned apuShift = apu.isFastMode() ? 3 : 4;
                apuCycleBudget += static_cast<int64_t>((e >> apuShift) - (c >> apuShift));
            }
            audioSampleAccum += AUDIO_SAMPLE_RATE * (e - c);
            masterCycleCount = e;
        }
        videoAccessed = false;
        idleSkipOK = idlePollSkip;
        idleSkipTarget = target;
        cpuApuAudioCycle(static_cast<int>(e & 15));
        idleSkipOK = false;
        if (videoAccessed) {
            // as runAhead(): the video chips are exactly at this cycle now, and step() would have handed the PPU the
            // display's blank lines after the CPU ran
            ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
        }
        updateTimers();
        masterCycleCount++;
        if (videoThread.joinable() && masterCycleCount > videoPublished && masterCycleCount - videoPublished >= VIDEO_CHUNK) {
            publishVideo(masterCycleCount);
        }

        if (cpu.isHalted() || !dma.tickIsNoOp()) return;
        if (nextTimerEventCycle != timerSnap || apu.isHalted() != apuHaltedSnap || apu.isFastMode() != apuFastSnap) {
            h = horizon();
            timerSnap = nextTimerEventCycle;
            apuHaltedSnap = apu.isHalted();
            apuFastSnap = apu.isFastMode();
        }
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

void System::setDevUnit(bool enabled) {
    cpu.setDevUnit(enabled);
    if (enabled) {
        std::cout << "Dev unit ENABLED - debug serial port is a UART, signature checks always pass\n";
    }
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
    lastTimerFireCycle = masterCycleCount;
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

// DMA callback wrappers: the DMAController hands back the context pointer it was given (this System).
uint8_t System::dmaReadCallback(void* sys, uint32_t address, bool privileged) {
    return static_cast<System*>(sys)->cpu.readByteForDMA(address, privileged);
}

void System::dmaWriteCallback(void* sys, uint32_t address, uint8_t value) {
    static_cast<System*>(sys)->cpu.writeByteForDMA(address, value);
}

bool System::dmaBusContendedQuery(void* sys, uint32_t address) {
    System* s = static_cast<System*>(sys);
    return s->apu.isFastMode() && static_cast<int>(address >> 16) == s->cpu.apuWindowBank;
}

bool System::dmaPrivilegeQuery(void* sys) {
    return static_cast<System*>(sys)->cpu.getPB() == 0xE0;
}

void System::dmaCompleteCallback(void* sys, uint8_t /*channel*/) {
    static_cast<System*>(sys)->signalIRQ(IRQSource::DMA);
}


// ---- HLE Boot ROM -----------------------------------------------------------------------------
// What the real ZPbootROM does, natively. docs/boot-hle.md has the contract and what is skipped.

namespace {
// CPU cycles the real Boot ROM spends per service, including COP and RTI (tools/test_hle_cop.cpp
// measures them against the real image; these are its worst cases). The HLE charges them so code
// that busy-waits or is cycle-tuned behaves the same with and without --boot.
constexpr uint32_t COST_SIN = 171;
constexpr uint32_t COST_COS = 175;
constexpr uint32_t COST_SINCOS = 335;
constexpr uint32_t COST_ATAN2 = 1086;
constexpr uint32_t COST_ATAN = 1042;
constexpr uint32_t COST_SQRT = 3902;
constexpr uint32_t COST_HYPOT = 4011;
constexpr uint32_t COST_RESERVED = 17;
constexpr uint32_t COST_PPU_LOAD_BASE = 216;   // fit of the real ROM: 648, 1800, 2375, 6981, 27705, 110613 cycles for 1, 4, 5, 16, 64, 256 pages
constexpr uint32_t COST_PPU_LOAD_PER_PAGE = 432;
// COP #$FF: the real thing hashes 16 KiB (~half a second), the whole point of HLE booting being
// that it doesn't, so a flat token cost.
constexpr uint32_t COST_VERIFY_CHUNK = 64;
}  // namespace

BootHLE::SignedROMView System::signedView() const {
    BootHLE::SignedROMView v;
    if (pendingSignedMetadata.size() > ROM::RAW_HEADER_SIZE) {
        v.header = pendingSignedMetadata.data();
        v.trailer = pendingSignedMetadata.data() + ROM::RAW_HEADER_SIZE;
        v.trailerSize = pendingSignedMetadata.size() - ROM::RAW_HEADER_SIZE;
    }
    v.payload = pendingRomData.data();
    v.payloadSize = pendingRomData.size();
    return v;
}

void System::hleBootCartridge() {
    if (verifySignatures && !cpu.isDevUnit()) {   // a dev unit's signature check always passes
        BootHLE::VerifyResult r = BootHLE::verifyROM(signedView());
        if (r != BootHLE::VerifyResult::Ok) {
            // The real Boot ROM hlts here too (after telling the APU, whose jingle HLE doesn't play).
            std::cerr << "[boot] refusing to run \"" << romTitle << "\": " << BootHLE::describe(r)
                      << " (--no-verify skips the check)\n";
            cpu.halt();
            return;
        }
    }

    // The same side effects the real Boot ROM (and the old 35-byte stub) leave, so a cartridge can't
    // tell them apart: a JMP-long trampoline in WRAM $80:0000-0003 and A/N/Z holding the entry
    // point's bank byte. The PPU is idle and black, as after the real splash's hand-off.
    uint8_t bank = (entryPoint >> 16) & 0xFF;
    cpu.writeByte(0x800000, 0x10);
    cpu.writeByte(0x800001, entryPoint & 0xFF);
    cpu.writeByte(0x800002, (entryPoint >> 8) & 0xFF);
    cpu.writeByte(0x800003, bank);
    CPUFlags p = cpu.getFlags();
    p.N = (bank & 0x80) != 0;
    p.Z = bank == 0;
    cpu.setFlags(p);
    cpu.setA(bank);
    ppu.reset();

    cpu.setPC(entryPoint & 0xFFFF);
    cpu.setPB(bank);
}

uint32_t System::hleService(uint8_t sig) {
    if (cpu.hasBootROM() || sig < 0xF0) return 0;   // a real Boot ROM (or the cartridge) owns it
    switch (sig) {
        case 0xF0: cpu.setA(BootHLE::sinQ15(cpu.getA())); return COST_SIN;
        case 0xF1: cpu.setA(BootHLE::cosQ15(cpu.getA())); return COST_COS;
        case 0xF2: {
            uint16_t angle = cpu.getA();
            cpu.setA(BootHLE::sinQ15(angle));
            cpu.setX(BootHLE::cosQ15(angle));
            return COST_SINCOS;
        }
        case 0xF3: cpu.setA(BootHLE::atan2Angle(cpu.getY(), cpu.getX())); return COST_ATAN2;
        case 0xF4: cpu.setA(BootHLE::atanQ88(cpu.getA())); return COST_ATAN;
        case 0xF5: cpu.setA(BootHLE::sqrt32(cpu.getA(), cpu.getX())); return COST_SQRT;
        case 0xF6: cpu.setA(BootHLE::hypot16(cpu.getX(), cpu.getY())); return COST_HYPOT;
        case 0xF7: return hlePPULoad();
        case 0xFF: return hleVerifyChunk();
        default: return COST_RESERVED;   // $F8-$FE: reserved, registers untouched
    }
}

// COP #$F7 (handoff.def svc_ppu_load): A = source bank, X = source offset, Y = 256-byte pages
// (0 = 256). Resets the PPU, copies to PPU address 0, starts it, returns A = 0. The bytes go through
// the same CPU bus accesses a cartridge's own writes would, so they are gated (an unverified v3 data
// chunk copies as poison, which the real ROM's privileged DMA would not) and a byte landing on a live
// PPU register takes effect mid-copy, as docs/boot-handoff.md warns.
uint32_t System::hlePPULoad() {
    uint32_t pages = cpu.getY() ? cpu.getY() : 256;
    uint32_t src = ((cpu.getA() & 0xFF) << 16) | cpu.getX();
    cpu.writeByte(0xD80000, 0x02);   // PPUCTRL: reset
    for (uint32_t i = 0; i < pages * 256; i++) {
        cpu.writeByte(0xB00000 | (i & 0xFFFF), cpu.readByte((src + i) & 0xFFFFFF));
    }
    cpu.writeByte(0xD80000, 0x01);   // PPUCTRL: start
    cpu.setA(0);
    return COST_PPU_LOAD_BASE + COST_PPU_LOAD_PER_PAGE * pages;
}

// COP #$FF (rsa.def verify_chunk): X = chunk index. Hash it, and only if it matches the signed
// manifest mark it verified; a mismatch just leaves the chunk reading as poison.
uint32_t System::hleVerifyChunk() {
    uint32_t chunk = cpu.getX();
    if (pendingTrailerVersion == 3 && chunk < pendingChunkCount && !cpu.isChunkVerified(chunk) &&
        BootHLE::chunkMatchesManifest(signedView(), pendingCodeSize, chunk)) {
        cpu.markChunkVerified(chunk);
    }
    return COST_VERIFY_CHUNK;
}

std::vector<std::pair<const char*, uint64_t>> System::hashParts(bool presentation) {
    auto part = [](auto fill) {
        uint64_t h = 1469598103934665603ull;
        auto mixv = [&h](auto v) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
            for (size_t i = 0; i < sizeof v; i++) h = (h ^ p[i]) * 1099511628211ull;
        };
        fill(mixv);
        return h;
    };
    std::vector<std::pair<const char*, uint64_t>> out;
    out.push_back({"cpu", cpu.stateHash()});
    out.push_back({"ppu", ppu.stateHash()});
    out.push_back({"display", presentation ? display.stateHash() : display.machineHash()});
    out.push_back({"apu", apu.stateHash()});
    out.push_back({"dma", dma.stateHash()});
    out.push_back({"timers+irq", part([&](auto mixv) {
        mixv(timers.control); mixv(timers.status); mixv(timers.intEnable);
        mixv(timers.vblankCounter); mixv(timers.hblankCounter); mixv(timers.secondCounter);
        mixv(timers.quarterSecCounter); mixv(timers.eighthSecCounter); mixv(timers.k1024Counter);
        mixv(timers.microCounter); mixv(timers.vblank60Counter);
        mixv(intc.pending()); mixv(intc.enable());
        mixv(vblankIRQEnabled); mixv(hblankIRQEnabled); mixv(lastVBlank); mixv(lastHBlank);
    })});
    out.push_back({"clock", part([&](auto mixv) {
        mixv(masterCycleCount); mixv(nextTimerEventCycle); mixv(timersLastUpdateCycle);
        mixv(cpuCycleBudget); mixv(apuCycleBudget); mixv(audioSampleAccum); mixv(nextAudioCycle); mixv(devMode);
    })});
    out.push_back({"cartridge", part([&](auto mixv) {
        mixv(romLoaded); mixv(cartridgeMapped); mixv(entryPoint); mixv(cartridgeDigest);
        mixv(pendingTrailerVersion); mixv(pendingCodeSize); mixv(pendingChunkCount);
        mixv(pendingRomData.size()); mixv(pendingSignedMetadata.size());
    })});
    out.push_back({"audio", part([&](auto mixv) {
        mixv(audioDigest);
        if (presentation) {   // what is waiting to be drained is the host's business, not the machine's
            mixv(audioBuffer.size());
            for (int16_t v : audioBuffer) mixv(v);
        }
    })});
    return out;
}

std::vector<std::pair<const char*, uint64_t>> System::machineHashParts() { return hashParts(false); }

uint64_t System::machineHash() {
    uint64_t h = 1469598103934665603ull;
    for (const auto& p : machineHashParts()) {
        for (int i = 0; i < 8; i++) h = (h ^ ((p.second >> (8 * i)) & 0xFF)) * 1099511628211ull;
    }
    return h;
}

uint64_t System::stateHash() {
    uint64_t h = 1469598103934665603ull;
    for (const auto& p : stateHashParts()) {
        for (int i = 0; i < 8; i++) h = (h ^ ((p.second >> (8 * i)) & 0xFF)) * 1099511628211ull;
    }
    return h;
}

} // namespace ZeroPoint
