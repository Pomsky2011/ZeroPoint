#ifndef ZEROPOINT_SYSTEM_H
#define ZEROPOINT_SYSTEM_H

#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "dma.h"
#include "display.h"
#include "rom.h"
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

    // System control
    void reset();
    void step();                    // Execute one master clock cycle
    void run(uint64_t cycles);      // Run for N cycles
    bool loadROM(const std::string& filename);

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

    // Helper methods
    void tickComponents();
    void checkInterrupts();
    void updateTimers();
};

} // namespace ZeroPoint

#endif // ZEROPOINT_SYSTEM_H
