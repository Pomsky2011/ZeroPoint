#ifndef ZEROPOINT_SYSTEM_H
#define ZEROPOINT_SYSTEM_H

#include "cpu.h"
#include "ppu.h"
#include "apu.h"
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
    Display& getDisplay() { return display; }

    const CPU& getCPU() const { return cpu; }
    const PPU& getPPU() const { return ppu; }
    const APU& getAPU() const { return apu; }
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

private:
    // Components
    CPU cpu;
    PPU ppu;
    APU apu;
    Display display;

    // ROM information
    bool romLoaded;
    std::string romTitle;
    std::string romDeveloper;
    uint32_t entryPoint;

    // Cycle synchronization
    uint64_t masterCycleCount;
    uint64_t cpuCycleAccum;
    uint64_t ppuCycleAccum;
    uint64_t apuCycleAccum;
    uint64_t displayCycleAccum;

    // Clock ratios (cycles per master clock)
    // Master clock = 64 MHz (PPU speed)
    static constexpr double CPU_RATIO = 1.0;     // CPU @ 64 MHz (placeholder - adjust as needed)
    static constexpr double PPU_RATIO = 1.0;     // PPU @ 64 MHz
    static constexpr double APU_RATIO = 0.065625; // APU @ 4.2 MHz (64/15.238)
    static constexpr double DISPLAY_RATIO = 1.0; // Display ticked every pixel clock

    // Interrupt state
    bool vblankIRQEnabled;
    bool hblankIRQEnabled;
    bool lastVBlank;
    bool lastHBlank;

    // Helper methods
    void tickComponents();
    void checkInterrupts();
};

} // namespace ZeroPoint

#endif // ZEROPOINT_SYSTEM_H
