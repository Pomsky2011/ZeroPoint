#include "system.h"
#include <iostream>
#include <fstream>

namespace ZeroPoint {

// Static member initialization
System* System::currentSystem = nullptr;

System::System()
    : cpu(), ppu(), apu(), dma(), display(),
      romLoaded(false), entryPoint(0),
      masterCycleCount(0),
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

    // Connect DMA to memory system
    dma.setMemoryCallbacks(dmaReadCallback, dmaWriteCallback);

    std::cout << "ZeroPoint System initialized\n";
    std::cout << "  Work RAM:   Banks $80-$9F (2 MB)\n";
    std::cout << "  Shadow RAM: Banks $BE-$BF (128 KB)\n";
    std::cout << "  PPU Window: Bank $B0\n";
    std::cout << "  APU Window: Bank $A0\n";
    std::cout << "  I/O Regs:   Bank $D8\n";
    std::cout << "  DMA:        16 channels, 32 MHz\n";
}

System::~System() {
}

void System::reset() {
    cpu.reset();
    ppu.reset();
    apu.reset();
    dma.reset();

    masterCycleCount = 0;

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

    // If ROM is loaded, set CPU to entry point
    if (romLoaded) {
        cpu.setPC(entryPoint & 0xFFFF);
        cpu.setPB((entryPoint >> 16) & 0xFF);
        std::cout << "System reset - Entry point: $" << std::hex
                  << (int)cpu.getPB() << ":" << cpu.getPC() << std::dec << "\n";
    }
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

    // Load ROM data into CPU memory (banks $00-$7F)
    cpu.loadROM(rom.getData(), rom.getSize(), 0x00);

    romLoaded = true;

    std::cout << "Loaded ROM: " << romTitle << "\n";
    std::cout << "  Developer: " << romDeveloper << "\n";
    std::cout << "  Size: " << rom.getSize() << " bytes\n";
    std::cout << "  Entry: $" << std::hex << entryPoint << std::dec << "\n";

    // Set CPU to entry point
    cpu.setPC(entryPoint & 0xFFFF);
    cpu.setPB((entryPoint >> 16) & 0xFF);

    return true;
}

void System::step() {
    tickComponents();
    checkInterrupts();
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

void System::tickComponents() {
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

    // CPU runs every 4 cycles (on cycles 3, 7, 11, 15)
    if (cyclePhase % 4 == 3) {
        cpu.step();
    }

    // APU runs every 16 cycles (on cycle 15)
    if (cyclePhase == 15) {
        apu.step();
    }

    // Update PPU blank status from display
    ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
}

void System::checkInterrupts() {
    bool currentVBlank = display.isVBlank();
    bool currentHBlank = display.isHBlank();

    // Detect V-Blank rising edge
    if (vblankIRQEnabled && currentVBlank && !lastVBlank) {
        // Trigger CPU IRQ for V-Blank
        cpu.triggerIRQ();
        // Pause DMA during interrupt
        dma.triggerInterrupt();
    }

    // Detect H-Blank rising edge
    if (hblankIRQEnabled && currentHBlank && !lastHBlank) {
        // Trigger CPU IRQ for H-Blank
        cpu.triggerIRQ();
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

void System::updateTimers() {
    // Timer bit definitions: 0bVHSQETAR
    constexpr uint8_t TIMER_VBLANK = 0x80;      // V
    constexpr uint8_t TIMER_HBLANK = 0x40;      // H
    constexpr uint8_t TIMER_SECOND = 0x20;      // S
    constexpr uint8_t TIMER_QUARTER = 0x10;     // Q
    constexpr uint8_t TIMER_EIGHTH = 0x08;      // E
    constexpr uint8_t TIMER_K1024 = 0x04;       // T
    constexpr uint8_t TIMER_MICRO = 0x02;       // A
    constexpr uint8_t TIMER_VBLANK60 = 0x01;    // R

    // V-blank timer
    if (timers.control & TIMER_VBLANK) {
        timers.vblankCounter++;
        if (timers.vblankCounter >= TIMER_VBLANK_PERIOD) {
            timers.vblankCounter = 0;
            timers.status |= TIMER_VBLANK;
            // Trigger interrupt if enabled
            if (timers.intEnable & TIMER_VBLANK) {
                cpu.triggerIRQ();
            }
        }
    }

    // H-blank timer
    if (timers.control & TIMER_HBLANK) {
        timers.hblankCounter++;
        if (timers.hblankCounter >= TIMER_HBLANK_PERIOD) {
            timers.hblankCounter = 0;
            timers.status |= TIMER_HBLANK;
            // Trigger interrupt if enabled
            if (timers.intEnable & TIMER_HBLANK) {
                cpu.triggerIRQ();
            }
        }
    }

    // 1 Second timer
    if (timers.control & TIMER_SECOND) {
        timers.secondCounter++;
        if (timers.secondCounter >= TIMER_SECOND_PERIOD) {
            timers.secondCounter = 0;
            timers.status |= TIMER_SECOND;
            // Trigger interrupt if enabled
            if (timers.intEnable & TIMER_SECOND) {
                cpu.triggerIRQ();
            }
        }
    }

    // 1/4 Second timer
    if (timers.control & TIMER_QUARTER) {
        timers.quarterSecCounter++;
        if (timers.quarterSecCounter >= TIMER_QUARTER_SEC_PERIOD) {
            timers.quarterSecCounter = 0;
            timers.status |= TIMER_QUARTER;
            // Trigger interrupt if enabled
            if (timers.intEnable & TIMER_QUARTER) {
                cpu.triggerIRQ();
            }
        }
    }

    // 1/8 Second timer
    if (timers.control & TIMER_EIGHTH) {
        timers.eighthSecCounter++;
        if (timers.eighthSecCounter >= TIMER_EIGHTH_SEC_PERIOD) {
            timers.eighthSecCounter = 0;
            timers.status |= TIMER_EIGHTH;
            // Trigger interrupt if enabled
            if (timers.intEnable & TIMER_EIGHTH) {
                cpu.triggerIRQ();
            }
        }
    }

    // 1/1024 Second timer
    if (timers.control & TIMER_K1024) {
        timers.k1024Counter++;
        if (timers.k1024Counter >= TIMER_K1024_PERIOD) {
            timers.k1024Counter = 0;
            timers.status |= TIMER_K1024;
            // Trigger interrupt if enabled
            if (timers.intEnable & TIMER_K1024) {
                cpu.triggerIRQ();
            }
        }
    }

    // 16777/16777216 Second timer
    if (timers.control & TIMER_MICRO) {
        timers.microCounter++;
        if (timers.microCounter >= TIMER_MICRO_PERIOD) {
            timers.microCounter = 0;
            timers.status |= TIMER_MICRO;
            // Trigger interrupt if enabled
            if (timers.intEnable & TIMER_MICRO) {
                cpu.triggerIRQ();
            }
        }
    }

    // 60 V-Blank timer
    if (timers.control & TIMER_VBLANK60) {
        timers.vblank60Counter++;
        if (timers.vblank60Counter >= TIMER_VBLANK60_PERIOD) {
            timers.vblank60Counter = 0;
            timers.status |= TIMER_VBLANK60;
            // Trigger interrupt if enabled
            if (timers.intEnable & TIMER_VBLANK60) {
                cpu.triggerIRQ();
            }
        }
    }
}

// Static DMA callback wrappers
uint8_t System::dmaReadCallback(uint32_t address) {
    if (currentSystem) {
        return currentSystem->cpu.readByte(address);
    }
    return 0xFF;
}

void System::dmaWriteCallback(uint32_t address, uint8_t value) {
    if (currentSystem) {
        currentSystem->cpu.writeByte(address, value);
    }
}

} // namespace ZeroPoint
