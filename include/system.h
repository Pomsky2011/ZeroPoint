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
};

} // namespace ZeroPoint

#endif // ZEROPOINT_SYSTEM_H
