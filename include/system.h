#ifndef ZEROPOINT_SYSTEM_H
#define ZEROPOINT_SYSTEM_H

#include "determinism_assumptions.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "dma.h"
#include "display.h"
#include "rom.h"
#include "boot_hle.h"
#include "interrupt_controller.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ZeroPoint {

/**
 * System Integration Class
 *
 * Manages the complete ZeroPoint system with synchronized execution
 * of CPU, PPU, APU, and Display components.
 */
class System {
public:
    System();
    ~System();

    // One display frame = every pixel of every scanline. The display advances
    // one tick per master clock cycle, so this is also the number of master
    // cycles per frame. Shared by every frontend via stepFrame() so they all
    // run the same core at the same pace instead of each re-deriving it.
    static constexpr uint64_t CYCLES_PER_FRAME =
        static_cast<uint64_t>(TOTAL_SCANLINES) * TOTAL_PIXELS_PER_LINE;

    // Sample rate the APU's MMP mixer is polled at (see getAudioBuffer()).
    // A frontend's audio device should be opened at this same rate.
    static constexpr uint32_t AUDIO_SAMPLE_RATE = 48000;

    // System control
    void reset();

    // Power-on-reset sequence: the cartridge interface stays disconnected
    // (unmapped - loadROM() only parses and caches the cartridge, it no
    // longer maps it into CPU memory directly) through a settle delay, then
    // everything is hard-reset (reset(), same as a soft reset - register/
    // execution state only, RAM is left as-is), and only then is the
    // cartridge bus connected. Mirrors real hardware power sequencing: hold
    // reset while rails/clocks stabilize, release reset, only then expose
    // the cartridge/expansion bus. Call this once at startup instead of
    // reset() after loadROM()/loadBootROM().
    void powerOn();

    void step();                    // Execute one master clock cycle
    void run(uint64_t cycles);      // Run for N cycles
    void stepFrame();

    // Advance `cycles` master cycles (stopping early if the CPU halts), state-identically to calling step() that
    // many times - but quiet stretches, where the only thing happening is the PPU ticking and the raster counter
    // moving, are applied in bulk (see quietCycles()) and only the cycles where something else can happen run
    // through step(). test_batched checks the two paths against each other.
    void runCycles(uint64_t cycles);               // Run CYCLES_PER_FRAME cycles, or until the CPU halts

    // Let the PPU/display catch-up (see syncVideoForAccess) run on a worker thread alongside the CPU/APU events
    // whenever the CPU is not touching them: bit-exact (the CPU's own accesses still wait for the worker to reach
    // exactly their cycle), and only worth it with 2+ cores, so it is ignored on a single-core host. Call between
    // runCycles() calls; off by default.
    void setVideoThread(bool on);
    // What a frontend should pass to setVideoThread(): on, unless ZEROPOINT_VIDEO_THREAD=0 is set.
    static bool videoThreadDefault();

    // Idle-poll skip (design: docs/idle-poll-skip.md): fast-forward a guest loop that does nothing but poll a value that
    // cannot change, to the first cycle it can. Bit-exact by contract: the machine at every cycle must equal the one
    // without it (tools/fuzz_idle_skip checks that).
    // On: loops that only poll a timer status, work RAM, a controller port or ROM are fast-forwarded (phase 2); PPU polls
    // are still run (phase 3). idleStats() counts what was found and skipped.
    void setIdlePollSkip(bool on) { if (!on) idleDetect.drop(cpu); idlePollSkip = on; }
    // What a frontend should pass to setIdlePollSkip(): on, unless ZEROPOINT_IDLE_SKIP=0 is set (or it passes --no-idle-skip).
    static bool idlePollSkipDefault();
    const IdleStats& idleStats() const { return idleDetect.stats(); }
    bool idlePollSkipEnabled() const { return idlePollSkip; }

    // Called by the CPU on every access to something that reads or changes PPU/display state (the PPU window and
    // the PPU/display I/O blocks). While runCycles() is running the CPU ahead of the video chips (the catch-up
    // scheduler, see docs/batched-clock.md) this brings them up to date through the current cycle first, exactly
    // where a lockstep step() would already have ticked them. A no-op otherwise.
    void syncVideoForAccess() {
        if (videoLazy) {
            syncVideo(masterCycleCount + 1);
            videoAccessed = true;
        }
    }
    bool loadROM(const std::string& filename);
    // Same from the file's bytes in memory: the cartridge is cached and reaches the bus at powerOn()/reset(), like loadROM().
    bool loadROMImage(const std::vector<uint8_t>& image);

    // The cartridge slot while the machine is on. All three take effect between master cycles (call them outside
    // runCycles()/step()), exactly as a movie event does.
    //   insertCartridge  maps the image into the slot right now. If one is already there this is a Stop 'n' Swop: the
    //                    old one is gone and the new one is mapped in the same instant. No reset, no boot ROM, no
    //                    signature check - the running software has to cope; chunk gating starts all-unverified.
    //   ejectCartridge   a yank: the slot is empty (CPU::EMPTY_SLOT_EVEN/ODD on the bus) and the CART_ENTRY register
    //                    reads 0. The CPU keeps running whatever it was running.
    //   softReset        the reset button: reset() - CPU/chips reset, work RAM kept, boot ROM runs again.
    //   hardReset        a power cycle: every RAM/VRAM/chip register and the cartridge bus go back to the power-on
    //                    state (the cartridge stays in the slot), then boot as powerOn() does. A run that starts with
    //                    a hardReset() is state-identical to a fresh System (tools/test_cart_slot checks this).
    // `image` is the .zpb file's bytes.
    bool insertCartridge(const std::vector<uint8_t>& image);
    void ejectCartridge();
    bool hasCartridge() const { return romLoaded; }
    void softReset() { reset(); }
    void hardReset();
    // Bump when a change to the emulation changes what any program computes (a movie pins this; docs/movies.md).
    static constexpr uint32_t CORE_REVISION = 9;

    // Load a real Boot ROM image (bank $E0) - e.g. ZPbootROM's bootrom.bin, or a demo that never hands
    // off to a cartridge at all. Call reset() afterward to actually jump into it. Without one, System
    // HLEs the boot ROM natively (docs/boot-hle.md): reset() verifies the cartridge's signature
    // and jumps straight to its entry point, and COP #$F0-$FF are answered natively. Loading a real
    // Boot ROM switches all of that off - the real code does the work.
    bool loadBootROM(const std::string& filename);
    bool loadBootROMImage(const std::vector<uint8_t>& image);   // same from memory (a movie pins the exact bytes)

    // HLE boot only: whether reset() checks the cartridge's signature before running it. On by
    // default, and strict: an unsigned or badly signed ROM halts the CPU (as the real Boot ROM does).
    // Dev tools that run unsigned ROMs turn it off (and the frontends' --no-verify does).
    void setVerifySignatures(bool on) { verifySignatures = on; }
    bool getVerifySignatures() const { return verifySignatures; }
    // What a frontend should pass to setVerifySignatures(): on, unless ZEROPOINT_NO_VERIFY=1 is set (the SDL
    // frontend's --no-verify does the same). Tests and tools call setVerifySignatures() themselves, not this.
    static bool verifySignaturesDefault();
    // True while the native boot services are in effect (no real Boot ROM loaded).
    bool isBootHLE() const { return !cpu.hasBootROM(); }

    // Load a custom APU internal BIOS (see APU::loadBIOS()) - unlike the CPU
    // Boot ROM, there is no default stub installed at construction, so the
    // APU runs zero-filled memory (an endless NOP) at $8000 until one is
    // loaded. Survives reset(). Call before or after reset(); the payload
    // takes effect either way since it's reapplied on every reset().
    bool loadAPUBios(const std::string& filename);
    bool loadAPUBiosImage(const std::vector<uint8_t>& image);
    // Back to the native boot ROM / APU BIOS after a real one was loaded (a movie's setup does this). Takes effect at
    // the next reset().
    void useHLEBootROM() { cpu.unloadBootROM(); }
    void useHLEApuBios() { apu.clearBIOS(); apu.enableHLEBios(true); }

    // Boot ROM / hot-swap: (re)load a subchip's program. The sequence is
    // notify -> let it acknowledge -> halt -> upload -> restart, so a subchip
    // that is already running gets interrupted and knows its code is being
    // switched before it is overwritten (rather than executing garbage mid-swap).
    //   entry        - execution pointer / PC to (re)start the new program at.
    //   switchVector - (PPU only) handler the running program is interrupted to
    //                  before the swap; 0 = don't interrupt, just halt.
    void loadPPUProgram(const uint8_t* code, size_t length,
                        uint16_t entry = 0, uint16_t switchVector = 0);
    void loadAPUProgram(const uint8_t* code, size_t length, uint16_t entry = 0);

    // Code-switch signal delivered to the APU's CPU->APU input port so a running
    // APU program can poll for an imminent code swap and acknowledge it.
    static constexpr uint16_t APU_CODESWITCH_PORT   = 0x00;  // DEF88186 input offset
    static constexpr uint8_t  APU_CODESWITCH_SIGNAL = 0xC5;  // sentinel value

    // Component access
    // FNV-1a over the whole machine: CPU (incl. all RAM), PPU, Display, APU, DMA, timers, interrupt controller,
    // clock/budget/audio-pacing counters and the pending audio samples. Two runs that must be identical (same
    // inputs, any host, any thread count, any uninitialised-memory pattern) must hash equal at every checkpoint.
    uint64_t stateHash();
    // What a movie checkpoint hashes: stateHash() minus everything the host controls rather than the machine - the
    // latched output frame (frame skip) and the pending audio buffer (frontends drain it when they like; the samples
    // are covered by a running digest of every sample produced instead). Invariant under setLatchEnabled() and
    // clearAudioBuffer(), which fuzz_determinism perturbs on purpose.
    uint64_t machineHash();
    std::vector<std::pair<const char*, uint64_t>> machineHashParts();
    // The same hash, one entry per part (cpu, ppu, display, apu, dma, timers, clock, audio), so a mismatch says who.
    std::vector<std::pair<const char*, uint64_t>> stateHashParts() { return hashParts(true); }

    CPU& getCPU() { return cpu; }
    PPU& getPPU() { return ppu; }
    APU& getAPU() { return apu; }
    DMAController& getDMA() { return dma; }
    Display& getDisplay() { return display; }

    const CPU& getCPU() const { return cpu; }
    const PPU& getPPU() const { return ppu; }
    const APU& getAPU() const { return apu; }
    const DMAController& getDMA() const { return dma; }
    const Display& getDisplay() const { return display; }

    // ROM information
    bool isROMLoaded() const { return romLoaded; }
    const std::string& getROMTitle() const { return romTitle; }
    const std::string& getROMDeveloper() const { return romDeveloper; }
    uint32_t getEntryPoint() const { return entryPoint; }

    // Statistics
    uint64_t getMasterCycleCount() const { return masterCycleCount; }

    // Interleaved stereo 16-bit PCM produced by the APU's MMP mixer -
    // getMixedSampleLeft()/Right() polled once per 1/AUDIO_SAMPLE_RATE of
    // emulated time, appended during step()/run()/stepFrame(). Frontends
    // should drain this after each stepFrame() (e.g. queue it to an audio
    // device) and clear it; it is never cleared internally.
    const std::vector<int16_t>& getAudioBuffer() const { return audioBuffer; }
    void clearAudioBuffer() { audioBuffer.clear(); }

    // Configuration
    void setVBlankIRQEnabled(bool enabled) { vblankIRQEnabled = enabled; }
    void setHBlankIRQEnabled(bool enabled) { hblankIRQEnabled = enabled; }

    // Change the timer enable mask. Must go through here (not a raw write to
    // timers.control) so the next-fire schedule is rebuilt: the per-cycle timer
    // path is a single comparison against nextTimerEventCycle, and enabling or
    // disabling a timer changes when that next event is.
    void setTimerControl(uint8_t value);

    // Debug mode
    void setDevMode(bool enabled);
    bool isDevMode() const { return devMode; }

    // Timer periods, in master clock cycles. Master clock is 68,011,355 Hz
    // (68.011355 MHz) - the standard NTSC colorburst crystal, 3,579,545 Hz,
    // times 19 (same style of small-integer colorburst multiplier real
    // hardware used - SNES's video crystal is colorburst x6, Genesis's
    // master is colorburst x15; x19 here lands closest to this system's
    // target speed).
    //
    // Every period below belongs to the general-purpose 8-timer peripheral
    // (TIMER_DEFS in system.cpp) and is wall-clock-scaled: chosen so N master
    // cycles at the master Hz value approximates a real-world duration
    // (1 second, 1/4 second, one NTSC-ish scanline, etc), then rounded to
    // the nearest whole cycle since colorburst-derived Hz isn't a clean
    // divisor of a real second (same tradeoff real NTSC hardware timers had
    // - see the SNES's actual 60.0988Hz "60Hz"). None of these are tied to
    // the *real* display geometry (TOTAL_SCANLINES/TOTAL_PIXELS_PER_LINE in
    // display.h, which drive System::CYCLES_PER_FRAME and the actual
    // VBlank/HBlank IRQ edge detection in checkInterrupts()) - HBLANK and
    // VBLANK here are just periodic timers that happen to be named after
    // the display events they approximate; verify against TIMER_DEFS in
    // system.cpp before assuming otherwise.
    static constexpr uint64_t TIMER_VBLANK_PERIOD = 1133523;      // ~16.667ms / 60Hz (master/60, rounded)
    static constexpr uint64_t TIMER_HBLANK_PERIOD = 16604;        // ~244us, approximating one scanline (master/4096, rounded)
    static constexpr uint64_t TIMER_SECOND_PERIOD = 68011355;     // 1 second (= master Hz)
    static constexpr uint64_t TIMER_QUARTER_SEC_PERIOD = 17002839;// 1/4 second (master/4, rounded)
    static constexpr uint64_t TIMER_EIGHTH_SEC_PERIOD = 8501419;  // 1/8 second (master/8, rounded)
    static constexpr uint64_t TIMER_K1024_PERIOD = 66417;         // 1/1024 second (master/1024, rounded)
    static constexpr uint64_t TIMER_MICRO_PERIOD = 68010;         // ~1ms (master x 16777/16777216, rounded)
    static constexpr uint64_t TIMER_VBLANK60_PERIOD = TIMER_VBLANK_PERIOD * 60; // 60 V-blanks, ~1 second

    // Interrupt controller (public for I/O register access). Aggregates all
    // IRQ sources onto the CPU's single maskable line and lets the ISR read
    // back which source fired.
    InterruptController intc;

    // Timer system state (public for I/O register access)
    struct {
        uint8_t control;        // Timer enable bits (0bVHSQETAR)
        uint8_t status;         // Timer status flags (set when timer expires)
        uint8_t intEnable;      // Timer interrupt enable bits (0bVHSQETAR)

        // Timer counters
        uint64_t vblankCounter;
        uint64_t hblankCounter;
        uint64_t secondCounter;
        uint64_t quarterSecCounter;
        uint64_t eighthSecCounter;
        uint64_t k1024Counter;
        uint64_t microCounter;      // 16777/16777216 second timer
        uint64_t vblank60Counter;
    } timers = {};

private:
    // Components
    CPU cpu;
    PPU ppu;
    APU apu;
    DMAController dma;
    Display display;

    // Static wrappers for DMA callbacks (need to access CPU instance)
    static uint8_t dmaReadCallback(void* sys, uint32_t address, bool privileged);
    static void dmaWriteCallback(void* sys, uint32_t address, uint8_t value);
    // Queried by DMAController::queueDMA() at trigger time - see
    // DMAConfig::privileged's comment.
    static bool dmaBusContendedQuery(void* sys, uint32_t address);
    static bool dmaPrivilegeQuery(void* sys);
    static void dmaCompleteCallback(void* sys, uint8_t channel);

    // HLE Boot ROM (see isBootHLE()). hleService() is CPU::setServiceHook()'s callback: COP #$F0-$FF
    // answered natively, returning the cycles a real Boot ROM would have spent (0 = not ours).
    bool verifySignatures = true;
    bool cacheROM(const ROM& rom);        // record the parsed cartridge as pending (not yet on the bus)
    void mapCartridgeNow();               // put the pending cartridge on the bus
    uint64_t cartridgeDigest = 0;         // FNV-1a over the inserted image: part of stateHash
    uint64_t audioDigest = 1469598103934665603ull;   // FNV-1a over every audio sample produced since reset()
    std::vector<std::pair<const char*, uint64_t>> hashParts(bool presentation);
    uint32_t hleService(uint8_t sig);
    uint32_t hlePPULoad();
    uint32_t hleVerifyChunk();
    void hleBootCartridge();   // reset() with a cartridge mapped: verify, then jump to the entry point
    BootHLE::SignedROMView signedView() const;

    // ROM information
    bool romLoaded;
    bool cartridgeMapped = false;  // has pendingRomData actually been mapped into bank $00-$7F yet?
    std::vector<uint8_t> pendingRomData;
    // Raw 64-byte ZPB header + "ZPSG" trailer for a signed (v2) ROM, mapped
    // read-only at bank $E2 in lockstep with pendingRomData/cartridgeMapped
    // (see ROM::getRawHeader/getTrailer) - empty for an unsigned (v1) ROM.
    std::vector<uint8_t> pendingSignedMetadata;
    // Trailer version 3 gating info (see CPU::configureDataGating), applied
    // in lockstep with pendingSignedMetadata/cartridgeMapped. trailerVersion
    // 0/1/2 mean no chunk gating for this ROM.
    uint8_t pendingTrailerVersion = 0;
    uint32_t pendingCodeSize = 0;
    uint32_t pendingChunkCount = 0;
    std::string romTitle;
    std::string romDeveloper;
    uint32_t entryPoint;

    // ~0.5s @ 60 fps. Not actually stepped/spent (this emulator has no
    // meaningful "thrash" to simulate - it's deterministic and zero-init'd
    // throughout), just documents the delay powerOn() models. Kept as a
    // named constant in case a frontend wants to hold a blank/splash screen
    // for this long before calling powerOn().
    static constexpr uint64_t POWER_ON_DELAY_CYCLES = CYCLES_PER_FRAME * 30;

    // Cycle synchronization
    uint64_t masterCycleCount;

    // Timer next-event scheduling. Rather than incrementing 8 counters every
    // master cycle, we track the master cycle of the next timer fire and skip
    // straight to it. Between fires updateTimers() is a single compare.
    //   nextTimerEventCycle   - masterCycleCount at which some enabled timer
    //                           next reaches its period; UINT64_MAX when no
    //                           timer is enabled (fast path always skips).
    //   timersLastUpdateCycle - the cycle through which the counters have been
    //                           advanced; the next increment is the cycle after.
    uint64_t nextTimerEventCycle;
    uint64_t timersLastUpdateCycle;
    void rescheduleTimers();   // recompute nextTimerEventCycle from counters
    void fireDueTimers();      // advance counters to now, fire, reschedule

    // Per-component cycle budgets. Each component accrues clock cycles at its
    // own rate (CPU = master/4, APU = master/16) and spends them by debiting
    // each executed instruction's *real* cycle cost. This paces the CPU/APU
    // by cycles rather than by "one instruction per slot", so multi-cycle
    // instructions no longer run several times too fast.
    int64_t cpuCycleBudget;
    int64_t apuCycleBudget;

    // Audio sample pacing: a Bresenham-style accumulator (add
    // AUDIO_SAMPLE_RATE each master cycle, fire and subtract
    // TIMER_SECOND_PERIOD - i.e. the master clock rate - once it catches
    // up) so 48000 samples/sec comes out exact on average even though
    // MASTER_CLOCK_HZ / AUDIO_SAMPLE_RATE isn't an integer. No drift over
    // an arbitrarily long run.
    uint64_t audioSampleAccum;
    // The master cycle on which the accumulator next reaches a full second's worth and a sample is emitted. The
    // accumulator advances by AUDIO_SAMPLE_RATE every cycle whatever else happens, so this is fixed between
    // emissions; keeping it (instead of dividing on every scheduler query) is what quietCyclesNonVideo() reads.
    uint64_t nextAudioCycle;
    static uint64_t audioCyclesUntilSample(uint64_t accum) {   // cycles before the one that emits, from `accum`
        return (TIMER_SECOND_PERIOD - accum + AUDIO_SAMPLE_RATE - 1) / AUDIO_SAMPLE_RATE - 1;
    }
    std::vector<int16_t> audioBuffer;

    // Clock cycle pattern (repeats every 16 master cycles)
    // Cycle 0: PPU, Display
    // Cycle 1: PPU, DMA, Display
    // Cycle 2: PPU, Display
    // Cycle 3: PPU, CPU, DMA, Display
    // ... pattern repeats ...
    // Cycle 15: PPU, CPU, DMA, APU, Display

    // Interrupt state
    bool vblankIRQEnabled;
    bool hblankIRQEnabled;
    bool lastVBlank;
    bool lastHBlank;

    // Debug mode
    bool devMode;

    // Helper methods. tickComponents() returns the display's post-tick blank
    // state via out-params so checkInterrupts() reuses it instead of resampling.
    void tickComponents(bool& outVBlank, bool& outHBlank);

    // Catch-up scheduling: while videoLazy, the PPU and display have been ticked through master cycle
    // videoCycle - 1 only, and the CPU/APU/timers/audio run ahead of them (runAhead()). videoLazy is false
    // (and the video chips are exactly at masterCycleCount) whenever control is outside runCycles(), and while
    // it runs the old lockstep path (DMA busy, or a blank IRQ enabled).
    bool videoLazy = false;
    bool videoAccessed = false;     // the CPU touched the video chips during the current event cycle
    std::atomic<uint64_t> videoCycle{0};
    void tickVideoTo(uint64_t targetCycle);   // the catch-up itself, on whichever thread owns the video chips
    void syncVideo(uint64_t targetCycle);     // owner: waits for the worker (if any), then catches up inline

    // Video worker thread. The video chips (PPU, display) have exactly one owner at a time: the worker while
    // vtDone < vtRequest, the thread running runCycles() otherwise. The runner hands work over by raising
    // vtRequest (always <= the cycle it is at, so the worker never passes a cycle the CPU may still touch) and
    // takes the chips back by waiting for vtDone == vtRequest; both directions are release/acquire.
    bool videoThreadWanted = false;
    bool idlePollSkip = false;           // host setting (not machine state, so not hashed): see setIdlePollSkip()
    bool idleSkipOK = false;             // set only around the CPU's turn inside runCpuBurst(): the one place a skip is exact
    uint64_t idleSkipTarget = 0;         // ... and the cycle that run stops at
    bool trySkipIdle();
    uint64_t lastTimerFireCycle = UINT64_MAX;   // host-side (gates the skip, never read by the machine): the last cycle a timer fired
    IdleDetector idleDetect;             // derived, host-side bookkeeping: dropped whenever the machine is reset or swapped
    std::thread videoThread;
    std::mutex vtMutex;
    std::condition_variable vtWake;      // runner -> worker: vtRequest raised, or quit
    std::condition_variable vtIdle;      // worker -> runner: vtDone advanced
    uint64_t vtRequest = 0;              // guarded by vtMutex (only the runner writes it)
    std::atomic<uint64_t> vtDone{0};
    bool vtQuit = false;                 // guarded by vtMutex
    uint64_t videoPublished = 0;         // runner only: the last cycle handed to the worker (or synced inline)
    static constexpr uint64_t VIDEO_CHUNK = 4096;   // hand the worker a stretch only once this much has built up
    void videoThreadMain();
    void startVideoThread();
    void stopVideoThread();
    void waitVideoIdle();
    void publishVideo(uint64_t targetCycle);
    void leaveRunAhead();
    void runAhead(uint64_t target);
    void lockstepEvent(uint64_t target);
    uint64_t quietCyclesNonVideo() const;
    void advanceNonVideo(uint64_t cycles);

    // The part of a master cycle after ppu/display/dma have ticked: CPU and APU budget and instructions, blank
    // status hand-off to the PPU, audio sampler. tickComponents() ends with it; runCycles() calls it directly on
    // an event cycle whose ppu/display ticks it already folded into the batch before.
    void tickRest(int cyclePhase, bool& outVBlank, bool& outHBlank, bool sampleVideo = true);
    inline void cpuApuAudioCycle(int cyclePhase);   // the CPU/APU/audio part of tickRest(), forced inline
    void runCpuBurst(uint64_t target);

    // How many of the next master cycles are "quiet": only ppu.tick() and display.tick()'s pixel increment
    // happen (no CPU/APU instruction or budget accrual, DMA tick, timer, audio sample, H-blank edge or end of
    // line). 0 when the very next cycle needs the full step(). `displayQuiet` gets the display's share of that
    // (how many of the next ticks are a bare pixel increment); the return value is the cycles before the first
    // non-display event, so the cycle after those is an event cycle, and a pure one (nothing the display does
    // on it) when the return value < displayQuiet.
    uint64_t quietCycles(uint64_t& displayQuiet) const;
    // Apply `cycles` quiet cycles. `foldEventTick` also ticks the PPU and display once more, for the pure event
    // cycle that follows (see quietCycles()): its CPU/APU/audio/timer work is left to the caller.
    void advanceQuiet(uint64_t cycles, bool foldEventTick = false);
    void checkInterrupts(bool currentVBlank, bool currentHBlank);
    void updateTimers();

    // Route a source through the interrupt controller: latch its pending bit
    // and assert the CPU line if the controller's top-level mask allows it.
    void signalIRQ(IRQSource src);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_SYSTEM_H
