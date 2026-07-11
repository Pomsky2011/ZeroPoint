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

    // System control
    void reset();
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

    // Timer periods (in master clock cycles at 67.108864 MHz)
    static constexpr uint64_t TIMER_VBLANK_PERIOD = 4194304;      // ~16.67ms (60Hz)
    static constexpr uint64_t TIMER_HBLANK_PERIOD = 16384;        // One scanline
    static constexpr uint64_t TIMER_SECOND_PERIOD = 67108864;     // 1 second
    static constexpr uint64_t TIMER_QUARTER_SEC_PERIOD = 16777216;// 1/4 second
    static constexpr uint64_t TIMER_EIGHTH_SEC_PERIOD = 8388608;  // 1/8 second
    static constexpr uint64_t TIMER_K1024_PERIOD = 65536;         // 1/1024 second
    static constexpr uint64_t TIMER_MICRO_PERIOD = 67109;         // 16777/16777216 second
    static constexpr uint64_t TIMER_VBLANK60_PERIOD = TIMER_VBLANK_PERIOD * 60; // 60 V-blanks

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
    static System* currentSystem;  // Pointer to current system for callbacks

    // ROM information
    bool romLoaded;
    std::string romTitle;
    std::string romDeveloper;
    uint32_t entryPoint;

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
