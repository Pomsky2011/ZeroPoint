#ifndef ZEROPOINT_SYSTEM_H
#define ZEROPOINT_SYSTEM_H

#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "dma.h"
#include "display.h"
#include "rom.h"
#include "interrupt_controller.h"
#include <cstdint>
#include <string>
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
    void stepFrame();               // Run CYCLES_PER_FRAME cycles, or until the CPU halts
    bool loadROM(const std::string& filename);

    // Replace the default Boot ROM stub (installed at construction) with an
    // alternate payload at bank $E0 - e.g. a demo that never hands off to a
    // cartridge at all. Call reset() afterward to actually jump into it.
    bool loadBootROM(const std::string& filename);

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
    } timers;

private:
    // Components
    CPU cpu;
    PPU ppu;
    APU apu;
    DMAController dma;
    Display display;

    // Static wrappers for DMA callbacks (need to access CPU instance)
    static uint8_t dmaReadCallback(uint32_t address);
    static void dmaWriteCallback(uint32_t address, uint8_t value);
    static void dmaCompleteCallback(uint8_t channel);
    static System* currentSystem;  // Pointer to current system for callbacks

    // ROM information
    bool romLoaded;
    bool cartridgeMapped = false;  // has pendingRomData actually been mapped into bank $00-$7F yet?
    std::vector<uint8_t> pendingRomData;
    // Raw 64-byte ZPB header + "ZPSG" trailer for a signed (v2) ROM, mapped
    // read-only at bank $E1 in lockstep with pendingRomData/cartridgeMapped
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
    void checkInterrupts(bool currentVBlank, bool currentHBlank);
    void updateTimers();

    // Route a source through the interrupt controller: latch its pending bit
    // and assert the CPU line if the controller's top-level mask allows it.
    void signalIRQ(IRQSource src);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_SYSTEM_H
