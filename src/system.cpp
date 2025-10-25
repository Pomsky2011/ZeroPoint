#include "system.h"
#include <iostream>
#include <fstream>

namespace ZeroPoint {

System::System()
    : cpu(), ppu(), apu(), display(),
      romLoaded(false), entryPoint(0),
      masterCycleCount(0),
      cpuCycleAccum(0), ppuCycleAccum(0), apuCycleAccum(0), displayCycleAccum(0),
      vblankIRQEnabled(false), hblankIRQEnabled(false),
      lastVBlank(false), lastHBlank(false)
{
    // Connect components
    cpu.setPPU(&ppu);
    cpu.setAPU(&apu);
    cpu.setDisplay(&display);
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

    std::cout << "ZeroPoint System initialized\n";
    std::cout << "  Work RAM:   Banks $80-$9F (2 MB)\n";
    std::cout << "  Shadow RAM: Banks $BE-$BF (128 KB)\n";
    std::cout << "  PPU Window: Bank $B0\n";
    std::cout << "  APU Window: Bank $A0\n";
    std::cout << "  I/O Regs:   Bank $D8\n";
}

System::~System() {
}

void System::reset() {
    cpu.reset();
    ppu.reset();
    apu.reset();

    masterCycleCount = 0;
    cpuCycleAccum = 0;
    ppuCycleAccum = 0;
    apuCycleAccum = 0;
    displayCycleAccum = 0;

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
    // Accumulate fractional cycles for each component
    cpuCycleAccum += CPU_RATIO * 1000;     // *1000 for fixed-point
    ppuCycleAccum += PPU_RATIO * 1000;
    apuCycleAccum += APU_RATIO * 1000;
    displayCycleAccum += DISPLAY_RATIO * 1000;

    // Tick CPU when accumulated >= 1000
    if (cpuCycleAccum >= 1000) {
        cpu.step();
        cpuCycleAccum -= 1000;
    }

    // Tick PPU when accumulated >= 1000
    if (ppuCycleAccum >= 1000) {
        ppu.tick();
        ppuCycleAccum -= 1000;
    }

    // Tick APU when accumulated >= 1000
    if (apuCycleAccum >= 1000) {
        apu.step();
        apuCycleAccum -= 1000;
    }

    // Tick Display when accumulated >= 1000
    if (displayCycleAccum >= 1000) {
        display.tick();
        displayCycleAccum -= 1000;
    }

    // Update PPU blank status from display
    ppu.setBlankStatus(display.isVBlank(), display.isHBlank());
}

void System::checkInterrupts() {
    bool currentVBlank = display.isVBlank();
    bool currentHBlank = display.isHBlank();

    // Detect V-Blank rising edge
    if (vblankIRQEnabled && currentVBlank && !lastVBlank) {
        // TODO: Trigger CPU IRQ when interrupt routing is implemented
        // cpu.triggerIRQ();
    }

    // Detect H-Blank rising edge
    if (hblankIRQEnabled && currentHBlank && !lastHBlank) {
        // TODO: Trigger CPU IRQ when interrupt routing is implemented
        // cpu.triggerIRQ();
    }

    lastVBlank = currentVBlank;
    lastHBlank = currentHBlank;
}

} // namespace ZeroPoint
