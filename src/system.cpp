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
    // Set current system for DMA callbacks
    currentSystem = this;

    // Connect components
    cpu.setPPU(&ppu);
    cpu.setAPU(&apu);
    cpu.setDisplay(&display);
    cpu.setDMA(&dma);
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
